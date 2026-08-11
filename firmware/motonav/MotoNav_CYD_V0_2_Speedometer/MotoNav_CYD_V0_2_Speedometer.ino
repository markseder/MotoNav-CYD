#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

#include "config.h"

TFT_eSPI tft;
TinyGPSPlus gps;
HardwareSerial gnssSerial(GNSS_UART_NUMBER);
SPIClass touchSpi(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);

bool nightTheme = true;
uint32_t nmeaChars = 0;
uint32_t lastNmeaByteMs = 0;
uint32_t lastScreenMs = 0;
uint32_t lastTouchMs = 0;

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

String gnssTime() {
  if (!gps.time.isValid()) return "--:--:--";
  char value[9];
  snprintf(value, sizeof(value), "%02d:%02d:%02d",
           gps.time.hour(), gps.time.minute(), gps.time.second());
  return String(value);
}

void drawHeader(bool fix) {
  const uint16_t bg = backgroundColor();
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(accentColor(), bg);
  tft.drawString("MotoNav", 8, 6, 2);

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(fix ? TFT_GREEN : TFT_ORANGE, bg);
  tft.drawString(fix ? "GNSS FIX" : (nmeaActive() ? "SEARCHING" : "NO DATA"), 312, 6, 2);
  tft.drawFastHLine(8, 27, 304, nightTheme ? TFT_DARKGREY : TFT_LIGHTGREY);
}

void drawSpeed(bool fix) {
  const uint16_t bg = backgroundColor();
  double speed = 0.0;
  if (fix && gps.speed.isValid()) speed = gps.speed.kmph();
  if (speed < SPEED_NOISE_FLOOR_KMH) speed = 0.0;

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(primaryColor(), bg);
  tft.drawFloat(speed, speed < 100.0 ? 1 : 0, 160, 92, 7);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(secondaryColor(), bg);
  tft.drawString("km/h", 160, 143, 4);
}

void drawFooter(bool fix) {
  const uint16_t bg = backgroundColor();
  const uint16_t divider = nightTheme ? TFT_DARKGREY : TFT_LIGHTGREY;
  tft.drawFastHLine(8, 171, 304, divider);
  tft.drawFastVLine(106, 178, 37, divider);
  tft.drawFastVLine(213, 178, 37, divider);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(secondaryColor(), bg);
  tft.drawString("SAT", 54, 178, 2);
  tft.drawString("HDOP", 160, 178, 2);
  tft.drawString("UTC", 267, 178, 2);

  tft.setTextColor(primaryColor(), bg);
  tft.drawString(gps.satellites.isValid() ? String(gps.satellites.value()) : "--", 54, 198, 4);
  tft.drawString(gps.hdop.isValid() ? String(gps.hdop.hdop(), 1) : "--", 160, 198, 4);
  tft.drawString(gnssTime(), 267, 201, 2);

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(accentColor(), bg);
  tft.drawString(nightTheme ? "Tap: DAY" : "Tap: NIGHT", 160, 237, 2);
}

void drawScreen() {
  const bool fix = validFix();
  tft.fillScreen(backgroundColor());
  drawHeader(fix);
  drawSpeed(fix);
  drawFooter(fix);
}

void handleTouch() {
  if (!touch.touched() || millis() - lastTouchMs < TOUCH_DEBOUNCE_MS) return;
  touch.getPoint();
  lastTouchMs = millis();
  nightTheme = !nightTheme;
  drawScreen();
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("MotoNav-CYD V0.2 speedometer");

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(true);
  tft.fillScreen(TFT_BLACK);

  touchSpi.begin(TOUCH_CLK_PIN, TOUCH_MISO_PIN, TOUCH_MOSI_PIN, TOUCH_CS_PIN);
  touch.begin(touchSpi);
  touch.setRotation(1);

  gnssSerial.begin(GNSS_BAUD, SERIAL_8N1, GNSS_RX_PIN, GNSS_TX_PIN);
  drawScreen();
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
    drawScreen();
  }
}
