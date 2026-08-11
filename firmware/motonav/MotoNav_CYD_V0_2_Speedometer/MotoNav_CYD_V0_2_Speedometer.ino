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
uint32_t lastTouchMs = 0;

String previousStatus;
String previousSpeed;
String previousSatellites;
String previousHdop;
String previousTime;

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

String gnssStatus(bool fix) {
  if (fix) return "GNSS FIX";
  return nmeaActive() ? "SEARCHING" : "NO DATA";
}

String gnssTime() {
  if (!gps.time.isValid()) return "--:--:--";
  char value[9];
  snprintf(value, sizeof(value), "%02d:%02d:%02d",
           gps.time.hour(), gps.time.minute(), gps.time.second());
  return String(value);
}

String speedText(bool fix) {
  double speed = 0.0;
  if (fix && gps.speed.isValid()) speed = gps.speed.kmph();
  if (speed < SPEED_NOISE_FLOOR_KMH) speed = 0.0;
  return speed < 100.0 ? String(speed, 1) : String(speed, 0);
}

void invalidateDynamicValues() {
  previousStatus = "";
  previousSpeed = "";
  previousSatellites = "";
  previousHdop = "";
  previousTime = "";
}

void drawStaticScreen() {
  const uint16_t bg = backgroundColor();
  const uint16_t divider = nightTheme ? TFT_DARKGREY : TFT_LIGHTGREY;

  tft.fillScreen(bg);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(accentColor(), bg);
  tft.drawString("MotoNav", 8, 6, 2);
  tft.drawFastHLine(8, 27, 304, divider);

  // Compact unit label keeps the speed area visually dominant.
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(secondaryColor(), bg);
  tft.drawString("km/h", 160, 132, 2);

  // Compact footer: labels, values and theme hint each have their own row.
  tft.drawFastHLine(8, 148, 304, divider);
  tft.drawFastVLine(106, 155, 65, divider);
  tft.drawFastVLine(213, 155, 65, divider);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(secondaryColor(), bg);
  tft.drawString("SAT", 54, 154, 2);
  tft.drawString("HDOP", 160, 154, 2);
  tft.drawString("UTC", 267, 154, 2);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(accentColor(), bg);
  tft.drawString(nightTheme ? "TAP: DAY" : "TAP: NIGHT", 160, 237, 1);

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
  speedSprite.drawString(value, 152, 49, 8);
  speedSprite.pushSprite(8, 29);
  previousSpeed = value;
}

void drawFooterValue(const String &value, String &previous,
                     int16_t centerX, int16_t y, int16_t width,
                     uint8_t font, bool force) {
  if (!force && value == previous) return;

  const uint16_t bg = backgroundColor();
  tft.fillRect(centerX - width / 2, y, width, 30, bg);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(primaryColor(), bg);
  tft.drawString(value, centerX, y, font);
  previous = value;
}

void updateDynamicScreen(bool force = false) {
  const bool fix = validFix();
  drawStatus(fix, force);
  drawSpeed(fix, force);

  const String satellites =
      gps.satellites.isValid() ? String(gps.satellites.value()) : "--";
  const String hdop =
      gps.hdop.isValid() ? String(gps.hdop.hdop(), 1) : "--";
  const String time = gnssTime();

  drawFooterValue(satellites, previousSatellites, 54, 174, 84, 4, force);
  drawFooterValue(hdop, previousHdop, 160, 174, 84, 4, force);
  drawFooterValue(time, previousTime, 267, 180, 96, 2, force);
}

void redrawTheme() {
  drawStaticScreen();
  updateDynamicScreen(true);
}

void handleTouch() {
  if (!touch.touched() || millis() - lastTouchMs < TOUCH_DEBOUNCE_MS) return;
  touch.getPoint();
  lastTouchMs = millis();
  nightTheme = !nightTheme;
  redrawTheme();
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("MotoNav-CYD V0.2 speedometer");

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(true);

  speedSprite.setColorDepth(16);
  if (speedSprite.createSprite(304, 118) == nullptr) {
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

  if (millis() - lastScreenMs >= SCREEN_REFRESH_MS) {
    lastScreenMs = millis();
    updateDynamicScreen();
  }
}
