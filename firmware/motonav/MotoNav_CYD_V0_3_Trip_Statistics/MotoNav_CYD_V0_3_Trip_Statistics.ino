#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

#include "config.h"

TFT_eSPI tft;
TFT_eSprite speedSprite(&tft);
TinyGPSPlus gps;
HardwareSerial gnssSerial(GNSS_UART_NUMBER);
SPIClass touchSpi(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);

bool nightTheme = true;
uint32_t nmeaChars = 0;
uint32_t lastNmeaByteMs = 0;
uint32_t lastScreenMs = 0;
uint32_t touchStartedMs = 0;
bool touchWasDown = false;
bool longPressHandled = false;

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

uint16_t backgroundColor() { return nightTheme ? TFT_BLACK : TFT_WHITE; }
uint16_t primaryColor() { return nightTheme ? TFT_WHITE : TFT_BLACK; }
uint16_t secondaryColor() { return nightTheme ? TFT_LIGHTGREY : TFT_DARKGREY; }
uint16_t accentColor() { return nightTheme ? TFT_CYAN : TFT_BLUE; }

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
  speedSprite.drawString(value, 152, 39, 8);
  speedSprite.pushSprite(8, 29);
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
  tft.fillRect(8, 204, 304, 26, bg);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(primaryColor(), bg);
  tft.drawString(durationText(totalTimeMs()), 76, 205, 2);
  tft.drawString(durationText(movingTimeMs), 236, 205, 2);
  previousTimes = value;
}

void updateDynamicScreen(bool force = false) {
  const bool fix = validFix();
  drawStatus(fix, force);
  drawSpeed(fix, force);
  drawStatistic(distanceText(), previousTrip, 54, force);
  drawStatistic(statisticSpeedText(averageSpeedKmh()), previousAverage, 160, force);
  drawStatistic(statisticSpeedText(maximumSpeedKmh), previousMaximum, 267, force);
  drawTimes(force);
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

void redrawTheme() {
  drawStaticScreen();
  updateDynamicScreen(true);
}

void handleTouch() {
  const bool down = touch.touched();
  const uint32_t now = millis();

  if (down && !touchWasDown) {
    touch.getPoint();
    touchWasDown = true;
    longPressHandled = false;
    touchStartedMs = now;
  }

  if (down && touchWasDown && !longPressHandled &&
      now - touchStartedMs >= RESET_HOLD_MS) {
    longPressHandled = true;
    resetTrip();
  }

  if (!down && touchWasDown) {
    const uint32_t heldMs = now - touchStartedMs;
    touchWasDown = false;
    if (!longPressHandled && heldMs >= TOUCH_MIN_PRESS_MS) {
      nightTheme = !nightTheme;
      redrawTheme();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("MotoNav-CYD V0.3 trip statistics");

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(true);

  speedSprite.setColorDepth(16);
  if (speedSprite.createSprite(304, 82) == nullptr) {
    Serial.println("ERROR: speed sprite allocation failed");
  }

  touchSpi.begin(TOUCH_CLK_PIN, TOUCH_MISO_PIN, TOUCH_MOSI_PIN, TOUCH_CS_PIN);
  touch.begin(touchSpi);
  touch.setRotation(1);

  gnssSerial.begin(GNSS_BAUD, SERIAL_8N1, GNSS_RX_PIN, GNSS_TX_PIN);
  redrawTheme();
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

  if (millis() - lastScreenMs >= SCREEN_REFRESH_MS) {
    lastScreenMs = millis();
    updateDynamicScreen();
  }
}
