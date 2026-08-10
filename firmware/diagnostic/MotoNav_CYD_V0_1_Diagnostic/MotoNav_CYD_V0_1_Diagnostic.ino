#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include <SPI.h>
#include <SD.h>

#include "config.h"

TFT_eSPI tft = TFT_eSPI();
TinyGPSPlus gps;
HardwareSerial gnssSerial(GNSS_UART_NUMBER);
SPIClass sdSpi(VSPI);

bool sdReady = false;
bool sdWritePassed = false;
bool kmlWritten = false;
uint32_t nmeaChars = 0;
uint32_t lastNmeaByteMs = 0;
uint32_t lastScreenMs = 0;

void drawLabel(int y, const char* label, const String& value, uint16_t color = TFT_WHITE) {
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(label, 8, y, 2);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawRightString(value, 312, y, 2);
}

bool testSdWrite() {
  SD.remove(TEST_TEXT_PATH);
  File file = SD.open(TEST_TEXT_PATH, FILE_WRITE);
  if (!file) return false;

  file.println("MotoNav-CYD V0.1 microSD diagnostic: PASS");
  file.flush();
  const size_t bytesWritten = file.size();
  file.close();
  return bytesWritten > 0;
}

bool writeDiagnosticKml() {
  if (!sdReady || !gps.location.isValid()) return false;

  SD.remove(TEST_KML_PATH);
  File file = SD.open(TEST_KML_PATH, FILE_WRITE);
  if (!file) return false;

  file.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
  file.println("<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Document>");
  file.println("<name>MotoNav-CYD V0.1 diagnostic</name>");
  file.println("<Placemark><name>First valid GNSS fix</name>");
  file.print("<description>Satellites: ");
  file.print(gps.satellites.isValid() ? gps.satellites.value() : 0);
  file.print("; HDOP: ");
  file.print(gps.hdop.isValid() ? gps.hdop.hdop() : 0.0, 1);
  file.println("</description>");
  file.println("<Point><coordinates>");
  file.print(gps.location.lng(), 7);
  file.print(",");
  file.print(gps.location.lat(), 7);
  file.print(",");
  file.print(gps.altitude.isValid() ? gps.altitude.meters() : 0.0, 2);
  file.println("</coordinates></Point></Placemark>");
  file.println("</Document></kml>");
  file.flush();

  const size_t bytesWritten = file.size();
  file.close();
  return bytesWritten > 0;
}

void drawScreen() {
  const bool nmeaActive = nmeaChars > 0 && (millis() - lastNmeaByteMs) < GNSS_DATA_TIMEOUT_MS;
  const bool fix = gps.location.isValid() && gps.location.age() < GNSS_DATA_TIMEOUT_MS;

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("MotoNav-CYD  V0.1 DIAGNOSTIC", 8, 5, 2);
  tft.drawFastHLine(8, 25, 304, TFT_DARKGREY);

  drawLabel(34, "DISPLAY", "PASS", TFT_GREEN);
  drawLabel(55, "microSD", !sdReady ? "INIT FAIL" : (sdWritePassed ? "WRITE PASS" : "WRITE FAIL"),
            sdReady && sdWritePassed ? TFT_GREEN : TFT_RED);
  drawLabel(76, "NMEA UART", nmeaActive ? "DATA" : "WAIT", nmeaActive ? TFT_GREEN : TFT_YELLOW);
  drawLabel(97, "GNSS FIX", fix ? "VALID" : "NO FIX", fix ? TFT_GREEN : TFT_YELLOW);
  drawLabel(118, "SAT / HDOP",
            String(gps.satellites.isValid() ? gps.satellites.value() : 0) + " / " +
            String(gps.hdop.isValid() ? gps.hdop.hdop() : 0.0, 1));
  drawLabel(139, "SPEED", String(gps.speed.isValid() ? gps.speed.kmph() : 0.0, 1) + " km/h", TFT_CYAN);

  if (fix) {
    drawLabel(160, "LAT", String(gps.location.lat(), 6));
    drawLabel(181, "LON", String(gps.location.lng(), 6));
  } else {
    drawLabel(160, "NMEA bytes", String(nmeaChars));
    drawLabel(181, "UART", String(GNSS_BAUD) + " baud");
  }

  drawLabel(202, "TEST KML", kmlWritten ? "CREATED" : "WAIT FOR FIX",
            kmlWritten ? TFT_GREEN : TFT_YELLOW);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("Serial Monitor: 115200 baud", 160, 224, 1);
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("MotoNav-CYD V0.1 diagnostic");
  Serial.printf("GNSS UART: RX=%d TX=%d baud=%lu\n", GNSS_RX_PIN, GNSS_TX_PIN,
                static_cast<unsigned long>(GNSS_BAUD));

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(true);
  tft.fillScreen(TFT_BLACK);

  gnssSerial.begin(GNSS_BAUD, SERIAL_8N1, GNSS_RX_PIN, GNSS_TX_PIN);

  sdSpi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  sdReady = SD.begin(SD_CS_PIN, sdSpi);
  if (sdReady) {
    sdWritePassed = testSdWrite();
    Serial.printf("microSD initialized, write test: %s\n", sdWritePassed ? "PASS" : "FAIL");
  } else {
    Serial.println("microSD initialization: FAIL");
  }

  drawScreen();
}

void loop() {
  while (gnssSerial.available()) {
    const char c = static_cast<char>(gnssSerial.read());
    gps.encode(c);
    nmeaChars++;
    lastNmeaByteMs = millis();

    // Uncomment for raw NMEA troubleshooting.
    // Serial.write(c);
  }

  if (!kmlWritten && gps.location.isUpdated() && gps.location.isValid()) {
    kmlWritten = writeDiagnosticKml();
    Serial.printf("Diagnostic KML: %s\n", kmlWritten ? "CREATED" : "WRITE FAIL");
  }

  if (millis() - lastScreenMs >= SCREEN_REFRESH_MS) {
    lastScreenMs = millis();
    drawScreen();

    if (millis() > 10000 && nmeaChars < 10) {
      Serial.println("No NMEA data. Check GNSS TX -> CYD RX, GND and GNSS_BAUD.");
    }
  }
}
