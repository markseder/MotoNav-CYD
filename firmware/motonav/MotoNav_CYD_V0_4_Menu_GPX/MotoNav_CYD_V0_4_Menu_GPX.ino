#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <SD.h>
#include <FS.h>

#include "config.h"

TFT_eSPI tft;
TFT_eSprite speedSprite(&tft);
TinyGPSPlus gps;
HardwareSerial gnssSerial(GNSS_UART_NUMBER);
SPIClass touchSpi(HSPI);
SPIClass sdSpi(VSPI);
File trackFile;
XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);

bool nightTheme = true;
uint32_t nmeaChars = 0;
uint32_t lastNmeaByteMs = 0;
uint32_t lastScreenMs = 0;
uint32_t touchStartedMs = 0;
bool touchWasDown = false;
bool longPressHandled = false;

enum UiMode { DRIVE_UI, MENU_UI };
UiMode uiMode = DRIVE_UI;
enum TrackState { TRACK_STOPPED, TRACK_WAIT_FIX, TRACK_RECORDING, TRACK_AUTO_PAUSED };
TrackState trackState = TRACK_STOPPED;
bool sdReady = false;
uint32_t lastTrackPointMs = 0;
uint32_t lastTrackFlushMs = 0;
uint32_t lastTrackMotionMs = 0;
String trackFileName;

constexpr int16_t TOUCH_X_MIN = 250;
constexpr int16_t TOUCH_X_MAX = 3850;
constexpr int16_t TOUCH_Y_MIN = 250;
constexpr int16_t TOUCH_Y_MAX = 3850;

enum ScreenMode { TRIP_SCREEN, SPEED_SCREEN };
ScreenMode screenMode = TRIP_SCREEN;
uint32_t speedThresholdStartedMs = 0;
uint32_t stoppedStartedMs = 0;
int previousGaugeTick = -1;

bool tripActive = false;
bool havePreviousPosition = false;
double previousLatitude = 0.0;
double previousLongitude = 0.0;
uint32_t previousPositionMs = 0;
uint32_t tripStartedMs = 0;
uint32_t lastMotionSampleMs = 0;
uint32_t movingTimeMs = 0;
double tripDistanceM = 0.0;
double maximumSpeedKmh = 0.0;

String previousStatus;
String previousSpeed;
String previousTrip;
String previousAverage;
String previousMaximum;
String previousTimes;

constexpr int16_t GAUGE_CX = 160;
constexpr int16_t GAUGE_CY = 125;
constexpr int16_t GAUGE_OUTER_R = 108;
constexpr int16_t GAUGE_INNER_R = 90;
constexpr int16_t GAUGE_LABEL_R = 72;
constexpr float GAUGE_SEGMENT_HALF_ANGLE_DEG = 3.6f;
constexpr int GAUGE_TICK_COUNT = 32;

uint16_t backgroundColor() { return nightTheme ? TFT_BLACK : TFT_WHITE; }
uint16_t primaryColor() { return nightTheme ? TFT_WHITE : TFT_BLACK; }
uint16_t secondaryColor() { return nightTheme ? TFT_LIGHTGREY : TFT_DARKGREY; }
uint16_t accentColor() { return nightTheme ? TFT_CYAN : TFT_BLUE; }

const char *trackStateText() {
  switch (trackState) {
    case TRACK_WAIT_FIX: return "WAIT FIX";
    case TRACK_RECORDING: return "REC";
    case TRACK_AUTO_PAUSED: return "PAUSE";
    default: return "STOP";
  }
}

void drawTrackBadge() {
  if (uiMode != DRIVE_UI) return;
  const uint16_t bg = backgroundColor();
  const uint16_t color = trackState == TRACK_RECORDING ? TFT_RED :
                         trackState == TRACK_AUTO_PAUSED ? TFT_ORANGE :
                         trackState == TRACK_WAIT_FIX ? TFT_YELLOW : secondaryColor();
  tft.fillRect(112, 4, 76, 21, bg);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(color, bg);
  tft.drawString(trackStateText(), 150, 6, 2);
}

bool initializeSd() {
  if (sdReady) return true;
  sdSpi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  sdReady = SD.begin(SD_CS_PIN, sdSpi);
  Serial.println(sdReady ? "microSD ready" : "ERROR: microSD initialization failed");
  return sdReady;
}

String makeTrackFileName() {
  char name[32];
  if (gps.date.isValid() && gps.time.isValid()) {
    snprintf(name, sizeof(name), "/TRK_%04d%02d%02d_%02d%02d%02d.GPX",
             gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second());
  } else {
    snprintf(name, sizeof(name), "/TRK_%08lu.GPX",
             static_cast<unsigned long>(millis()));
  }
  return String(name);
}

bool openTrackFile() {
  if (!initializeSd()) return false;
  trackFileName = makeTrackFileName();
  trackFile = SD.open(trackFileName, FILE_WRITE);
  if (!trackFile) return false;
  trackFile.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
  trackFile.println("<gpx version=\"1.1\" creator=\"MotoNav-CYD\" xmlns=\"http://www.topografix.com/GPX/1/1\">");
  trackFile.println("  <trk><name>MotoNav ride</name><trkseg>");
  trackFile.flush();
  lastTrackFlushMs = millis();
  return true;
}

void startTrack() {
  if (trackState != TRACK_STOPPED) return;
  if (!validFix()) {
    trackState = TRACK_WAIT_FIX;
  } else {
    trackState = openTrackFile() ? TRACK_RECORDING : TRACK_STOPPED;
    lastTrackMotionMs = millis();
  }
  drawTrackBadge();
}

void finishTrack() {
  if (trackFile) {
    trackFile.println("  </trkseg></trk>");
    trackFile.println("</gpx>");
    trackFile.flush();
    trackFile.close();
  }
  trackState = TRACK_STOPPED;
  lastTrackPointMs = 0;
  drawTrackBadge();
}

void writeTrackPoint() {
  if (trackState == TRACK_WAIT_FIX && validFix()) {
    trackState = openTrackFile() ? TRACK_RECORDING : TRACK_STOPPED;
    lastTrackMotionMs = millis();
    drawTrackBadge();
  }
  if ((trackState != TRACK_RECORDING && trackState != TRACK_AUTO_PAUSED) ||
      !validFix()) return;

  const uint32_t now = millis();
  const double speed = filteredSpeedKmh(true);
  if (speed >= TRACK_MOVING_THRESHOLD_KMH) {
    lastTrackMotionMs = now;
    if (trackState == TRACK_AUTO_PAUSED) {
      trackState = TRACK_RECORDING;
      drawTrackBadge();
    }
  } else if (trackState == TRACK_RECORDING &&
             now - lastTrackMotionMs >= TRACK_AUTO_PAUSE_MS) {
    trackState = TRACK_AUTO_PAUSED;
    drawTrackBadge();
  }

  if (trackState != TRACK_RECORDING || !gps.location.isUpdated() ||
      now - lastTrackPointMs < TRACK_POINT_INTERVAL_MS) return;

  trackFile.printf("    <trkpt lat=\"%.7f\" lon=\"%.7f\">\n",
                   gps.location.lat(), gps.location.lng());
  if (gps.altitude.isValid()) {
    trackFile.printf("      <ele>%.2f</ele>\n", gps.altitude.meters());
  }
  if (gps.date.isValid() && gps.time.isValid()) {
    trackFile.printf("      <time>%04d-%02d-%02dT%02d:%02d:%02dZ</time>\n",
                     gps.date.year(), gps.date.month(), gps.date.day(),
                     gps.time.hour(), gps.time.minute(), gps.time.second());
  }
  trackFile.println("    </trkpt>");
  lastTrackPointMs = now;
  if (now - lastTrackFlushMs >= TRACK_FLUSH_INTERVAL_MS) {
    trackFile.flush();
    lastTrackFlushMs = now;
  }
}

void drawMenuButton(int16_t x, int16_t y, const String &title,
                    const String &subtitle, uint16_t color) {
  const uint16_t bg = backgroundColor();
  tft.fillRoundRect(x, y, 146, 78, 8, nightTheme ? tft.color565(18, 24, 30)
                                                  : tft.color565(232, 238, 242));
  tft.drawRoundRect(x, y, 146, 78, 8, color);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(color, bg);
  tft.drawString(title, x + 73, y + 13, 2);
  tft.setTextColor(secondaryColor(), bg);
  tft.drawString(subtitle, x + 73, y + 43, 1);
}

void drawMenu() {
  uiMode = MENU_UI;
  const uint16_t bg = backgroundColor();
  tft.fillScreen(bg);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(accentColor(), bg);
  tft.drawString("MOTONAV MENU", 160, 7, 2);
  drawMenuButton(8, 34, "TRACK", trackStateText(),
                 trackState == TRACK_RECORDING ? TFT_RED : accentColor());
  drawMenuButton(166, 34, "TRIP", "VIEW / RESET", accentColor());
  drawMenuButton(8, 122, "DISPLAY", nightTheme ? "NIGHT" : "DAY", accentColor());
  drawMenuButton(166, 122, "SETTINGS", "COMING NEXT", secondaryColor());
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(secondaryColor(), bg);
  tft.drawString("TAP TILE   HOLD: BACK", 160, 235, 1);
}

void closeMenu() {
  uiMode = DRIVE_UI;
  redrawTheme();
  drawTrackBadge();
}

void handleMenuTap(int16_t x, int16_t y) {
  if (y >= 34 && y <= 112 && x < 160) {
    if (trackState == TRACK_STOPPED) startTrack();
    else finishTrack();
    drawMenu();
  } else if (y >= 34 && y <= 112) {
    closeMenu();
  } else if (y >= 122 && y <= 200 && x < 160) {
    nightTheme = !nightTheme;
    drawMenu();
  }
}

bool nmeaActive() {
  return nmeaChars > 0 && millis() - lastNmeaByteMs < GNSS_DATA_TIMEOUT_MS;
}

bool validFix() {
  return gps.location.isValid() && gps.location.age() < GNSS_DATA_TIMEOUT_MS;
}

double filteredSpeedKmh(bool fix) {
  if (!fix || !gps.speed.isValid()) return 0.0;
  const double speed = gps.speed.kmph();
  return speed < SPEED_NOISE_FLOOR_KMH ? 0.0 : speed;
}

String gnssStatus(bool fix) {
  if (fix) return "GNSS FIX";
  return nmeaActive() ? "SEARCHING" : "NO DATA";
}

String speedText(bool fix) {
  const double speed = filteredSpeedKmh(fix);
  return speed < 100.0 ? String(speed, 1) : String(speed, 0);
}

String distanceText() {
  if (tripDistanceM < 10000.0) return String(tripDistanceM / 1000.0, 2);
  if (tripDistanceM < 100000.0) return String(tripDistanceM / 1000.0, 1);
  return String(tripDistanceM / 1000.0, 0);
}

String statisticSpeedText(double speed) {
  return speed < 100.0 ? String(speed, 1) : String(speed, 0);
}

String durationText(uint32_t durationMs) {
  const uint32_t totalMinutes = durationMs / 60000UL;
  const uint32_t hours = totalMinutes / 60UL;
  const uint32_t minutes = totalMinutes % 60UL;
  char value[12];
  if (hours < 100) {
    snprintf(value, sizeof(value), "%02lu:%02lu",
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes));
  } else {
    snprintf(value, sizeof(value), "%luh",
             static_cast<unsigned long>(hours));
  }
  return String(value);
}

double averageSpeedKmh() {
  if (movingTimeMs == 0) return 0.0;
  return (tripDistanceM / 1000.0) / (movingTimeMs / 3600000.0);
}

uint32_t totalTimeMs() {
  return tripActive ? millis() - tripStartedMs : 0;
}

void invalidateDynamicValues() {
  previousStatus = "";
  previousSpeed = "";
  previousTrip = "";
  previousAverage = "";
  previousMaximum = "";
  previousTimes = "";
}

void drawGaugeTick(int tick, bool active) {
  const float centerDeg = 135.0f + (270.0f * tick / GAUGE_TICK_COUNT);
  const float startAngle = (centerDeg - GAUGE_SEGMENT_HALF_ANGLE_DEG) * DEG_TO_RAD;
  const float endAngle = (centerDeg + GAUGE_SEGMENT_HALF_ANGLE_DEG) * DEG_TO_RAD;

  const int16_t outerStartX = GAUGE_CX + cosf(startAngle) * GAUGE_OUTER_R;
  const int16_t outerStartY = GAUGE_CY + sinf(startAngle) * GAUGE_OUTER_R;
  const int16_t outerEndX = GAUGE_CX + cosf(endAngle) * GAUGE_OUTER_R;
  const int16_t outerEndY = GAUGE_CY + sinf(endAngle) * GAUGE_OUTER_R;
  const int16_t innerStartX = GAUGE_CX + cosf(startAngle) * GAUGE_INNER_R;
  const int16_t innerStartY = GAUGE_CY + sinf(startAngle) * GAUGE_INNER_R;
  const int16_t innerEndX = GAUGE_CX + cosf(endAngle) * GAUGE_INNER_R;
  const int16_t innerEndY = GAUGE_CY + sinf(endAngle) * GAUGE_INNER_R;

  const uint16_t inactive = nightTheme ? tft.color565(30, 40, 48)
                                        : tft.color565(190, 200, 205);
  const uint16_t color = active ? accentColor() : inactive;

  // Two triangles form one thick annular segment.
  tft.fillTriangle(outerStartX, outerStartY, outerEndX, outerEndY,
                   innerStartX, innerStartY, color);
  tft.fillTriangle(innerStartX, innerStartY, outerEndX, outerEndY,
                   innerEndX, innerEndY, color);

  if ((tick % 8) == 0) {
    const float centerAngle = centerDeg * DEG_TO_RAD;
    const int speedMark = tick * 5;
    const int16_t lx = GAUGE_CX + cosf(centerAngle) * GAUGE_LABEL_R;
    const int16_t ly = GAUGE_CY + sinf(centerAngle) * GAUGE_LABEL_R;
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(active ? accentColor() : secondaryColor(), backgroundColor());
    tft.drawString(String(speedMark), lx, ly, 1);
  }
}

int gaugeTickForSpeed(double speed) {
  const double limited = constrain(speed, 0.0, 160.0);
  return static_cast<int>(round(limited / 5.0));
}

void drawGaugeScale(bool force = false) {
  const int currentTick = gaugeTickForSpeed(filteredSpeedKmh(validFix()));
  if (!force && currentTick == previousGaugeTick) return;
  for (int tick = 0; tick <= GAUGE_TICK_COUNT; ++tick) {
    drawGaugeTick(tick, tick <= currentTick);
  }
  previousGaugeTick = currentTick;
}

void drawSpeedometerStatic() {
  const uint16_t bg = backgroundColor();
  tft.fillScreen(bg);
  tft.drawRoundRect(3, 3, 314, 234, 8, nightTheme ? TFT_DARKGREY : TFT_LIGHTGREY);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(secondaryColor(), bg);
  tft.drawString("SPEED", 160, 8, 2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("KM/h", 160, 181, 2);
  previousGaugeTick = -1;
  previousSpeed = "";
  previousStatus = "";
  drawGaugeScale(true);
}

void drawStaticScreen() {
  const uint16_t bg = backgroundColor();
  const uint16_t divider = nightTheme ? TFT_DARKGREY : TFT_LIGHTGREY;

  tft.fillScreen(bg);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(accentColor(), bg);
  tft.drawString("MotoNav", 8, 6, 2);
  tft.drawFastHLine(8, 27, 304, divider);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(secondaryColor(), bg);
  tft.drawString("km/h", 160, 116, 2);

  tft.drawFastHLine(8, 128, 304, divider);
  tft.drawFastVLine(106, 134, 50, divider);
  tft.drawFastVLine(213, 134, 50, divider);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(secondaryColor(), bg);
  tft.drawString("TRIP km", 54, 132, 1);
  tft.drawString("AVG km/h", 160, 132, 1);
  tft.drawString("MAX km/h", 267, 132, 1);

  tft.drawFastHLine(8, 185, 304, divider);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("TOTAL", 12, 193, 1);
  tft.drawString("MOVING", 166, 193, 1);

  tft.setTextColor(accentColor(), bg);
  tft.setTextDatum(BL_DATUM);
  tft.drawString(nightTheme ? "TAP: DAY" : "TAP: NIGHT", 12, 237, 1);
  tft.setTextDatum(BR_DATUM);
  tft.drawString("HOLD: RESET", 308, 237, 1);

  invalidateDynamicValues();
}

void drawStatus(bool fix, bool force) {
  const String status = gnssStatus(fix);
  if (!force && status == previousStatus) return;
  const uint16_t bg = backgroundColor();
  tft.fillRect(190, 4, 122, 21, bg);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(fix ? TFT_GREEN : TFT_ORANGE, bg);
  tft.drawString(status, 312, 6, 2);
  previousStatus = status;
}

void drawSpeed(bool fix, bool force) {
  const String value = speedText(fix);
  if (!force && value == previousSpeed) return;
  speedSprite.fillSprite(backgroundColor());
  speedSprite.setTextDatum(MC_DATUM);
  speedSprite.setTextColor(primaryColor(), backgroundColor());
  speedSprite.drawString(value, 95, 39, 8);
  speedSprite.pushSprite(65, 29);
  previousSpeed = value;
}

void drawStatistic(const String &value, String &previous,
                   int16_t centerX, bool force) {
  if (!force && value == previous) return;
  const uint16_t bg = backgroundColor();
  tft.fillRect(centerX - 48, 145, 96, 36, bg);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(primaryColor(), bg);
  tft.drawString(value, centerX, 148, 4);
  previous = value;
}

void drawTimes(bool force) {
  const String value = durationText(totalTimeMs()) + "|" +
                       durationText(movingTimeMs);
  if (!force && value == previousTimes) return;
  const uint16_t bg = backgroundColor();
  // Keep the time refresh area clear of the footer hints at y=229.
  tft.fillRect(8, 204, 304, 23, bg);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(primaryColor(), bg);
  tft.drawString(durationText(totalTimeMs()), 76, 205, 2);
  tft.drawString(durationText(movingTimeMs), 236, 205, 2);
  previousTimes = value;
}

void drawGaugeSpeed(bool fix, bool force) {
  const String value = speedText(fix);
  if (!force && value == previousSpeed) return;
  speedSprite.fillSprite(backgroundColor());
  speedSprite.setTextDatum(MC_DATUM);
  speedSprite.setTextColor(primaryColor(), backgroundColor());
  speedSprite.drawString(value, 95, 39, 8);
  speedSprite.pushSprite(65, 83);
  previousSpeed = value;

  // The speed sprite overlaps the gauge edges; restore the thick arc afterward.
  drawGaugeScale(true);
}

void updateDynamicScreen(bool force = false) {
  if (uiMode != DRIVE_UI) return;
  const bool fix = validFix();
  if (screenMode == SPEED_SCREEN) {
    drawGaugeScale(force);
    drawGaugeSpeed(fix, force);
    return;
  }
  drawStatus(fix, force);
  drawSpeed(fix, force);
  drawStatistic(distanceText(), previousTrip, 54, force);
  drawStatistic(statisticSpeedText(averageSpeedKmh()), previousAverage, 160, force);
  drawStatistic(statisticSpeedText(maximumSpeedKmh), previousMaximum, 267, force);
  drawTimes(force);
  drawTrackBadge();
}

void startTripIfNeeded() {
  if (tripActive || !validFix()) return;
  tripActive = true;
  tripStartedMs = millis();
  lastMotionSampleMs = tripStartedMs;
}

void updateTripStatistics() {
  const uint32_t now = millis();
  startTripIfNeeded();

  if (tripActive) {
    const uint32_t sampleMs = now - lastMotionSampleMs;
    if (sampleMs <= MAX_MOTION_SAMPLE_MS &&
        filteredSpeedKmh(validFix()) >= MOVING_SPEED_THRESHOLD_KMH) {
      movingTimeMs += sampleMs;
    }
    lastMotionSampleMs = now;
  }

  const bool fix = validFix();
  const double speed = filteredSpeedKmh(fix);
  if (fix && speed >= MOVING_SPEED_THRESHOLD_KMH &&
      speed <= MAX_VALID_SPEED_KMH) {
    maximumSpeedKmh = max(maximumSpeedKmh, speed);
  }

  if (!gps.location.isUpdated() || !fix) return;

  const double latitude = gps.location.lat();
  const double longitude = gps.location.lng();

  if (havePreviousPosition) {
    const uint32_t gapMs = now - previousPositionMs;
    const double segmentM = TinyGPSPlus::distanceBetween(
        previousLatitude, previousLongitude, latitude, longitude);
    const double impliedSpeedKmh =
        gapMs > 0 ? (segmentM * 3600.0 / gapMs) : 0.0;
    const bool hdopOk = !gps.hdop.isValid() ||
                        gps.hdop.hdop() <= MAX_HDOP_FOR_DISTANCE;

    if (speed >= MOVING_SPEED_THRESHOLD_KMH &&
        gapMs <= MAX_POSITION_GAP_MS &&
        segmentM <= MAX_SEGMENT_DISTANCE_M &&
        impliedSpeedKmh <= MAX_VALID_SPEED_KMH &&
        hdopOk) {
      tripDistanceM += segmentM;
    }
  }

  previousLatitude = latitude;
  previousLongitude = longitude;
  previousPositionMs = now;
  havePreviousPosition = true;
}

void resetTrip() {
  tripActive = false;
  havePreviousPosition = false;
  tripDistanceM = 0.0;
  movingTimeMs = 0;
  maximumSpeedKmh = 0.0;
  tripStartedMs = 0;
  lastMotionSampleMs = millis();
  invalidateDynamicValues();

  const uint16_t bg = backgroundColor();
  tft.fillRect(70, 84, 180, 28, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(accentColor(), bg);
  tft.drawString("TRIP RESET", 160, 98, 2);
  delay(350);
  updateDynamicScreen(true);
}

void showTripScreen() {
  screenMode = TRIP_SCREEN;
  drawStaticScreen();
  updateDynamicScreen(true);
}

void showSpeedScreen() {
  screenMode = SPEED_SCREEN;
  drawSpeedometerStatic();
  updateDynamicScreen(true);
}

void redrawTheme() {
  if (uiMode == MENU_UI) {
    drawMenu();
  } else if (screenMode == SPEED_SCREEN) {
    showSpeedScreen();
  } else {
    showTripScreen();
  }
}

void updateAutomaticScreenMode() {
  if (uiMode != DRIVE_UI) return;
  const uint32_t now = millis();
  const double speed = filteredSpeedKmh(validFix());

  if (screenMode == TRIP_SCREEN) {
    stoppedStartedMs = 0;
    if (speed >= SPEED_SCREEN_ENTER_KMH) {
      if (speedThresholdStartedMs == 0) speedThresholdStartedMs = now;
      if (now - speedThresholdStartedMs >= SPEED_SCREEN_ENTER_HOLD_MS) {
        speedThresholdStartedMs = 0;
        showSpeedScreen();
      }
    } else {
      speedThresholdStartedMs = 0;
    }
    return;
  }

  speedThresholdStartedMs = 0;
  if (speed <= SPEED_SCREEN_EXIT_KMH) {
    if (stoppedStartedMs == 0) stoppedStartedMs = now;
    if (now - stoppedStartedMs >= SPEED_SCREEN_EXIT_HOLD_MS) {
      stoppedStartedMs = 0;
      showTripScreen();
    }
  } else {
    stoppedStartedMs = 0;
  }
}

void handleTouch() {
  const bool down = touch.touched();
  const uint32_t now = millis();
  static int16_t tapX = 0;
  static int16_t tapY = 0;

  if (down && !touchWasDown) {
    TS_Point p = touch.getPoint();
    tapX = constrain(map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, 319), 0, 319);
    tapY = constrain(map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 239), 0, 239);
    touchWasDown = true;
    longPressHandled = false;
    touchStartedMs = now;
  }

  if (down && touchWasDown && !longPressHandled &&
      now - touchStartedMs >= MENU_HOLD_MS) {
    longPressHandled = true;
    if (uiMode == MENU_UI) closeMenu();
    else if (filteredSpeedKmh(validFix()) < MENU_MAX_SPEED_KMH) drawMenu();
  }

  if (!down && touchWasDown) {
    const uint32_t heldMs = now - touchStartedMs;
    touchWasDown = false;
    if (!longPressHandled && heldMs >= TOUCH_MIN_PRESS_MS) {
      if (uiMode == MENU_UI) handleMenuTap(tapX, tapY);
      else {
        nightTheme = !nightTheme;
        redrawTheme();
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("MotoNav-CYD V0.4 menu and GPX recording");

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(true);

  speedSprite.setColorDepth(16);
  if (speedSprite.createSprite(190, 82) == nullptr) {
    Serial.println("ERROR: speed sprite allocation failed");
  }

  touchSpi.begin(TOUCH_CLK_PIN, TOUCH_MISO_PIN, TOUCH_MOSI_PIN, TOUCH_CS_PIN);
  touch.begin(touchSpi);
  touch.setRotation(1);

  gnssSerial.begin(GNSS_BAUD, SERIAL_8N1, GNSS_RX_PIN, GNSS_TX_PIN);
  initializeSd();
  showTripScreen();
  drawTrackBadge();
}

void loop() {
  while (gnssSerial.available()) {
    const char c = static_cast<char>(gnssSerial.read());
    gps.encode(c);
    nmeaChars++;
    lastNmeaByteMs = millis();
  }

  handleTouch();
  updateTripStatistics();
  writeTrackPoint();
  updateAutomaticScreenMode();

  if (millis() - lastScreenMs >= SCREEN_REFRESH_MS) {
    lastScreenMs = millis();
    updateDynamicScreen();
  }
}

