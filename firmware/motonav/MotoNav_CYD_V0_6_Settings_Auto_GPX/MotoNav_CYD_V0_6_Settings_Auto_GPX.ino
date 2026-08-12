#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <SD.h>
#include <FS.h>
#include <Preferences.h>

#include "config.h"

TFT_eSPI tft;
TFT_eSprite speedSprite(&tft);
TinyGPSPlus gps;
HardwareSerial gnssSerial(GNSS_UART_NUMBER);
SPIClass touchSpi(HSPI);
SPIClass sdSpi(VSPI);
File trackFile;
XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);
Preferences preferences;

bool nightTheme = true;
bool useMph = false;
bool autoTrackStart = false;
bool autoStartArmed = true;
uint32_t autoStartThresholdMs = 0;
uint32_t nmeaChars = 0;
uint32_t lastNmeaByteMs = 0;
uint32_t lastScreenMs = 0;
uint32_t touchStartedMs = 0;
bool touchWasDown = false;
bool longPressHandled = false;

enum UiMode { DRIVE_UI, MENU_UI, SETTINGS_UI };
UiMode uiMode = DRIVE_UI;
enum TrackState { TRACK_STOPPED, TRACK_WAIT_FIX, TRACK_RECORDING, TRACK_AUTO_PAUSED };
TrackState trackState = TRACK_STOPPED;
bool sdReady = false;
uint32_t lastTrackPointMs = 0;
uint32_t lastTrackFlushMs = 0;
uint32_t lastTrackMotionMs = 0;
uint32_t trackPointCount = 0;
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
constexpr int16_t GAUGE_CY = 126;
constexpr int16_t GAUGE_OUTER_RX = 154;
constexpr int16_t GAUGE_OUTER_RY = 116;
constexpr int16_t GAUGE_INNER_RX = 136;
constexpr int16_t GAUGE_INNER_RY = 94;
constexpr float GAUGE_START_DEG = 160.0f;
constexpr float GAUGE_SWEEP_DEG = 220.0f;
constexpr int GAUGE_TICK_COUNT = 40;

uint16_t backgroundColor() { return nightTheme ? TFT_BLACK : TFT_WHITE; }
uint16_t primaryColor() { return nightTheme ? TFT_WHITE : TFT_BLACK; }
uint16_t secondaryColor() { return nightTheme ? TFT_LIGHTGREY : TFT_DARKGREY; }
uint16_t accentColor() { return nightTheme ? TFT_CYAN : TFT_BLUE; }

double displaySpeed(double kmh) { return useMph ? kmh * 0.621371 : kmh; }
double displayDistance(double km) { return useMph ? km * 0.621371 : km; }
const char *speedUnitText() { return useMph ? "mph" : "km/h"; }
const char *distanceUnitText() { return useMph ? "mi" : "km"; }

void saveSettings() {
  preferences.putBool("night", nightTheme);
  preferences.putBool("mph", useMph);
  preferences.putBool("autoTrack", autoTrackStart);
}

void loadSettings() {
  preferences.begin("motonav", false);
  nightTheme = preferences.getBool("night", true);
  useMph = preferences.getBool("mph", false);
  autoTrackStart = preferences.getBool("autoTrack", false);
}

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
  const String badge = trackState == TRACK_RECORDING
                           ? String("REC ") + String(trackPointCount)
                           : String(trackStateText());
  const int16_t badgeX = screenMode == SPEED_SCREEN ? 4 : 104;
  const int16_t badgeCenter = screenMode == SPEED_SCREEN ? 45 : 150;
  tft.fillRect(badgeX, 4, 92, 21, bg);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(color, bg);
  tft.drawString(badge, badgeCenter, 6, 2);
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
  lastTrackPointMs = 0;
  trackPointCount = 0;
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
  if (autoTrackStart && filteredSpeedKmh(validFix()) >= SPEED_SCREEN_ENTER_KMH) {
    autoStartArmed = false;
  }
  drawTrackBadge();
}

void writeTrackPoint(bool locationUpdated, double latitude, double longitude) {
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

  if (trackState != TRACK_RECORDING || !locationUpdated ||
      now - lastTrackPointMs + 100UL < TRACK_POINT_INTERVAL_MS) return;

  trackFile.printf("    <trkpt lat=\"%.7f\" lon=\"%.7f\">\n",
                   latitude, longitude);
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
  trackPointCount++;
  Serial.printf("GPX point %lu written: %.7f, %.7f\n",
                static_cast<unsigned long>(trackPointCount),
                latitude, longitude);
  drawTrackBadge();
  if (now - lastTrackFlushMs >= TRACK_FLUSH_INTERVAL_MS) {
    trackFile.flush();
    lastTrackFlushMs = now;
  }
}

void drawMenuButton(int16_t x, int16_t y, const String &title,
                    const String &subtitle, uint16_t color) {
  const uint16_t tile = nightTheme ? tft.color565(20, 27, 35) : TFT_WHITE;
  const uint16_t titleColor = nightTheme ? TFT_WHITE : tft.color565(18, 28, 38);
  const uint16_t subtitleColor = nightTheme ? tft.color565(150, 164, 178)
                                            : tft.color565(82, 96, 110);

  if (!nightTheme) {
    tft.fillRoundRect(x + 2, y + 3, 146, 78, 10,
                      tft.color565(205, 214, 222));
  }
  tft.fillRoundRect(x, y, 146, 78, 10, tile);
  tft.fillRoundRect(x, y, 6, 78, 3, color);

  // Match the glyph background to the tile. The old screen background here
  // caused visible rectangular boxes around menu text in daylight mode.
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(titleColor, tile);
  tft.drawString(title, x + 18, y + 13, 4);
  tft.setTextColor(subtitleColor, tile);
  tft.drawString(subtitle, x + 18, y + 49, 2);
}

void drawSettings() {
  uiMode = SETTINGS_UI;
  const uint16_t bg = nightTheme ? TFT_BLACK : tft.color565(235, 240, 244);
  tft.fillScreen(bg);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(nightTheme ? TFT_WHITE : tft.color565(18, 28, 38), bg);
  tft.drawString("SETTINGS", 10, 7, 4);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(accentColor(), bg);
  tft.drawString("MOTONAV", 310, 11, 2);

  drawMenuButton(8, 38, "THEME", nightTheme ? "NIGHT" : "DAY", TFT_ORANGE);
  drawMenuButton(166, 38, "UNITS", useMph ? "MPH / MI" : "KM/H / KM", TFT_GREEN);
  drawMenuButton(8, 124, "GPX",
                 autoTrackStart ? "AUTO START: 5 KM/H" : "START: MANUAL", TFT_CYAN);
  drawMenuButton(166, 124, "BACK", "RETURN TO MENU", secondaryColor());

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(secondaryColor(), bg);
  tft.drawString("SETTINGS ARE SAVED", 160, 237, 2);
}

void drawMenu() {
  uiMode = MENU_UI;
  const uint16_t bg = nightTheme ? TFT_BLACK : tft.color565(235, 240, 244);
  tft.fillScreen(bg);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(nightTheme ? TFT_WHITE : tft.color565(18, 28, 38), bg);
  tft.drawString("MENU", 10, 7, 4);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(accentColor(), bg);
  tft.drawString("MOTONAV", 310, 11, 2);

  const String trackAction = trackState == TRACK_STOPPED ? "START TRACK" :
                             trackState == TRACK_WAIT_FIX ? "WAITING FOR FIX" :
                             trackState == TRACK_AUTO_PAUSED ? "AUTO PAUSED" :
                             "FINISH TRACK";
  drawMenuButton(8, 38, "TRACK", trackAction,
                 trackState == TRACK_RECORDING ? TFT_RED : accentColor());
  drawMenuButton(166, 38, "TRIP", "RETURN TO TRIP", TFT_GREEN);
  drawMenuButton(8, 124, "DISPLAY", nightTheme ? "NIGHT MODE" : "DAY MODE",
                 TFT_ORANGE);
  drawMenuButton(166, 124, "SETTINGS", "THEME / UNITS / TRACK", secondaryColor());
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(secondaryColor(), bg);
  tft.drawString("HOLD TO RETURN", 160, 237, 2);
}

void closeMenu() {
  uiMode = DRIVE_UI;
  redrawTheme();
  drawTrackBadge();
}

void handleMenuTap(int16_t x, int16_t y) {
  if (y >= 38 && y <= 116 && x < 160) {
    if (trackState == TRACK_STOPPED) startTrack();
    else finishTrack();
    drawMenu();
  } else if (y >= 38 && y <= 116) {
    closeMenu();
  } else if (y >= 124 && y <= 202 && x < 160) {
    nightTheme = !nightTheme;
    saveSettings();
    drawMenu();
  } else if (y >= 124 && y <= 202) {
    drawSettings();
  }
}

void handleSettingsTap(int16_t x, int16_t y) {
  if (y >= 38 && y <= 116 && x < 160) {
    nightTheme = !nightTheme;
    saveSettings();
    drawSettings();
  } else if (y >= 38 && y <= 116) {
    useMph = !useMph;
    saveSettings();
    drawSettings();
  } else if (y >= 124 && y <= 202 && x < 160) {
    autoTrackStart = !autoTrackStart;
    autoStartThresholdMs = 0;
    autoStartArmed = true;
    saveSettings();
    drawSettings();
  } else if (y >= 124 && y <= 202) {
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
  const double speed = displaySpeed(filteredSpeedKmh(fix));
  return speed < 100.0 ? String(speed, 1) : String(speed, 0);
}

String distanceText() {
  const double distance = displayDistance(tripDistanceM / 1000.0);
  if (distance < 10.0) return String(distance, 2);
  if (distance < 100.0) return String(distance, 1);
  return String(distance, 0);
}

String statisticSpeedText(double speedKmh) {
  const double speed = displaySpeed(speedKmh);
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
  const float stepDeg = GAUGE_SWEEP_DEG / GAUGE_TICK_COUNT;
  const float centerDeg = GAUGE_START_DEG + (stepDeg * tick);
  const float halfSegmentDeg = stepDeg * 0.39f;
  const float startAngle = (centerDeg - halfSegmentDeg) * DEG_TO_RAD;
  const float endAngle = (centerDeg + halfSegmentDeg) * DEG_TO_RAD;

  const int16_t outerStartX = GAUGE_CX + cosf(startAngle) * GAUGE_OUTER_RX;
  const int16_t outerStartY = GAUGE_CY + sinf(startAngle) * GAUGE_OUTER_RY;
  const int16_t outerEndX = GAUGE_CX + cosf(endAngle) * GAUGE_OUTER_RX;
  const int16_t outerEndY = GAUGE_CY + sinf(endAngle) * GAUGE_OUTER_RY;
  const int16_t innerStartX = GAUGE_CX + cosf(startAngle) * GAUGE_INNER_RX;
  const int16_t innerStartY = GAUGE_CY + sinf(startAngle) * GAUGE_INNER_RY;
  const int16_t innerEndX = GAUGE_CX + cosf(endAngle) * GAUGE_INNER_RX;
  const int16_t innerEndY = GAUGE_CY + sinf(endAngle) * GAUGE_INNER_RY;

  const uint16_t inactive = nightTheme ? tft.color565(30, 40, 48)
                                        : tft.color565(190, 200, 205);
  const bool redZone = tick > (GAUGE_TICK_COUNT / 2);
  const uint16_t liveColor = redZone ? TFT_RED : accentColor();
  const uint16_t color = active ? liveColor : inactive;

  // Two triangles form one thick annular segment.
  tft.fillTriangle(outerStartX, outerStartY, outerEndX, outerEndY,
                   innerStartX, innerStartY, color);
  tft.fillTriangle(innerStartX, innerStartY, outerEndX, outerEndY,
                   innerEndX, innerEndY, color);

  if ((tick % 10) == 0) {
    const float centerAngle = centerDeg * DEG_TO_RAD;
    const int speedMark = useMph ? static_cast<int>(round(tick * 2.5)) : tick * 4;
    const int16_t labelRx = tick == 20 ? 121 : 125;
    const int16_t labelRy = tick == 20 ? 72 : 75;
    const int16_t lx = GAUGE_CX + cosf(centerAngle) * labelRx;
    const int16_t ly = GAUGE_CY + sinf(centerAngle) * labelRy;
    tft.setTextDatum(MC_DATUM);
    const bool markInRedZone = useMph ? speedMark > 50 : speedMark > 80;
    tft.setTextColor(markInRedZone ? TFT_RED : accentColor(), backgroundColor());
    tft.drawString(String(speedMark), lx, ly, 2);
  }
}

int gaugeTickForSpeed(double speedKmh) {
  const double speed = displaySpeed(speedKmh);
  const double gaugeMaximum = useMph ? 100.0 : 160.0;
  const double limited = constrain(speed, 0.0, gaugeMaximum);
  return static_cast<int>(round(limited * GAUGE_TICK_COUNT / gaugeMaximum));
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
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(accentColor(), bg);
  tft.drawString(useMph ? "MPH" : "KM/h", 160, 194, 4);
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
  tft.drawString(speedUnitText(), 160, 116, 2);

  tft.drawFastHLine(8, 128, 304, divider);
  tft.drawFastVLine(106, 134, 50, divider);
  tft.drawFastVLine(213, 134, 50, divider);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(secondaryColor(), bg);
  tft.drawString(String("TRIP ") + distanceUnitText(), 54, 132, 1);
  tft.drawString(String("AVG ") + speedUnitText(), 160, 132, 1);
  tft.drawString(String("MAX ") + speedUnitText(), 267, 132, 1);

  tft.drawFastHLine(8, 185, 304, divider);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("TOTAL", 12, 193, 1);
  tft.drawString("MOVING", 166, 193, 1);

  tft.setTextColor(accentColor(), bg);
  tft.setTextDatum(BL_DATUM);
  tft.drawString(nightTheme ? "TAP: DAY" : "TAP: NIGHT", 12, 237, 1);
  tft.setTextDatum(BR_DATUM);
  tft.drawString("HOLD: MENU", 308, 237, 1);

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
  speedSprite.pushSprite(65, 91);
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

void updateTripStatistics(bool locationUpdated, double latitude, double longitude) {
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

  if (!locationUpdated || !fix) return;

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

void updateAutomaticTrackStart() {
  if (!autoTrackStart) {
    autoStartThresholdMs = 0;
    return;
  }

  const double speed = filteredSpeedKmh(validFix());
  if (speed < SPEED_SCREEN_ENTER_KMH) {
    autoStartThresholdMs = 0;
    autoStartArmed = true;
    return;
  }

  if (trackState != TRACK_STOPPED || !autoStartArmed || !validFix()) return;
  const uint32_t now = millis();
  if (autoStartThresholdMs == 0) autoStartThresholdMs = now;
  if (now - autoStartThresholdMs >= SPEED_SCREEN_ENTER_HOLD_MS) {
    autoStartArmed = false;
    autoStartThresholdMs = 0;
    startTrack();
    Serial.println("GPX auto-start triggered");
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
    if (uiMode == MENU_UI || uiMode == SETTINGS_UI) closeMenu();
    else if (filteredSpeedKmh(validFix()) < MENU_MAX_SPEED_KMH) drawMenu();
  }

  if (!down && touchWasDown) {
    const uint32_t heldMs = now - touchStartedMs;
    touchWasDown = false;
    if (!longPressHandled && heldMs >= TOUCH_MIN_PRESS_MS) {
      if (uiMode == MENU_UI) handleMenuTap(tapX, tapY);
      else if (uiMode == SETTINGS_UI) handleSettingsTap(tapX, tapY);
      else {
        nightTheme = !nightTheme;
        saveSettings();
        redrawTheme();
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("MotoNav-CYD V0.6 persistent settings and GPX auto-start");

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
  loadSettings();
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

  const bool locationUpdated = gps.location.isUpdated();
  const double latitude = gps.location.lat();
  const double longitude = gps.location.lng();

  handleTouch();
  updateTripStatistics(locationUpdated, latitude, longitude);
  updateAutomaticTrackStart();
  writeTrackPoint(locationUpdated, latitude, longitude);
  updateAutomaticScreenMode();

  if (millis() - lastScreenMs >= SCREEN_REFRESH_MS) {
    lastScreenMs = millis();
    updateDynamicScreen();
  }
}
