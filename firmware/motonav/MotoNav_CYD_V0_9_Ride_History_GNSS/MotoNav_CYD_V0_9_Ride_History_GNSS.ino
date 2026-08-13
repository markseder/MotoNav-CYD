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
bool sdError = false;
bool savedNotice = false;
uint32_t savedNoticeMs = 0;
uint32_t savedPointCount = 0;
uint32_t nmeaChars = 0;
uint32_t lastNmeaByteMs = 0;
uint32_t lastScreenMs = 0;
uint32_t touchStartedMs = 0;
bool touchWasDown = false;
bool longPressHandled = false;

enum UiMode { DRIVE_UI, MENU_UI, SETTINGS_UI, SUMMARY_UI, GNSS_UI, RIDES_UI };
UiMode uiMode = DRIVE_UI;
enum TrackState { TRACK_STOPPED, TRACK_WAIT_FIX, TRACK_RECORDING };
TrackState trackState = TRACK_STOPPED;
bool sdReady = false;
uint32_t lastTrackPointMs = 0;
uint32_t lastTrackFlushMs = 0;
uint32_t trackPointCount = 0;
String trackFileName;

constexpr const char *RIDES_INDEX_FILE = "/RIDES_INDEX.CSV";
constexpr uint8_t MAX_RIDE_HISTORY = 20;

struct RideRecord {
  String date;
  String time;
  String gpxFile;
  double distanceKm = 0.0;
  uint32_t totalSeconds = 0;
  uint32_t movingSeconds = 0;
  uint32_t stoppedSeconds = 0;
  double averageKmh = 0.0;
  double maximumKmh = 0.0;
  uint32_t points = 0;
};

RideRecord rideHistory[MAX_RIDE_HISTORY];
uint8_t rideHistoryCount = 0;
uint8_t selectedRideIndex = 0;

double smoothedSpeedKmh = 0.0;
double lastAcceptedRawSpeedKmh = 0.0;
uint32_t lastSpeedFilterMs = 0;
bool speedFilterReady = false;

bool havePreviousTrackPosition = false;
double previousTrackLatitude = 0.0;
double previousTrackLongitude = 0.0;
uint32_t previousTrackPositionMs = 0;

constexpr double STOP_REMINDER_SPEED_KMH = 1.0;
constexpr double STOP_REMINDER_CLEAR_KMH = 3.0;
constexpr double MENU_AUTO_CLOSE_SPEED_KMH = 5.0;
constexpr uint32_t STOP_REMINDER_DELAY_MS = 15000UL;
uint32_t trackStoppedStartedMs = 0;
bool stopRecordReminderVisible = false;

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
uint32_t tripFinishedMs = 0;
uint32_t lastMotionSampleMs = 0;

double summaryDistanceM = 0.0;
double summaryAverageSpeedKmh = 0.0;
double summaryMaximumSpeedKmh = 0.0;
uint32_t summaryTotalTimeMs = 0;
uint32_t summaryMovingTimeMs = 0;
uint32_t summaryStoppedTimeMs = 0;
uint32_t summaryPointCount = 0;
bool summarySavedOk = false;
String summaryTrackFileName;

constexpr const char *ACTIVE_TRACK_MARKER = "/ACTIVE_TRACK.TXT";
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

// Forward declarations required by the Arduino sketch preprocessor.
void resetTrip(bool showNotice);
void drawRideSummary();
void showSpeedScreen();
void drawGnssDiagnostics();
void drawRideHistory();
void drawMenu();
bool validFix();
String durationText(uint32_t durationMs);
String statisticSpeedText(double speedKmh);

uint16_t backgroundColor() { return nightTheme ? TFT_BLACK : TFT_WHITE; }
uint16_t primaryColor() { return nightTheme ? TFT_WHITE : TFT_BLACK; }
uint16_t secondaryColor() { return nightTheme ? TFT_LIGHTGREY : TFT_DARKGREY; }
uint16_t accentColor() { return nightTheme ? TFT_CYAN : TFT_BLUE; }

uint16_t labelColor() { return nightTheme ? TFT_WHITE : TFT_BLACK; }

void drawBoldText(const String &text, int16_t x, int16_t y, uint8_t font,
                  uint16_t foreground, uint16_t background) {
  // Real bold GFX fonts replace the old doubled thin bitmap text.
  const GFXfont *boldFont = font >= 4 ? &FreeSansBold12pt7b
                                     : &FreeSansBold9pt7b;
  tft.setFreeFont(boldFont);
  tft.setTextColor(foreground, background);
  tft.drawString(text, x, y);
  tft.setFreeFont(nullptr);
}

double displaySpeed(double kmh) { return useMph ? kmh * 0.621371 : kmh; }
double displayDistance(double km) { return useMph ? km * 0.621371 : km; }
const char *speedUnitText() { return useMph ? "mph" : "km/h"; }
const char *distanceUnitText() { return useMph ? "mi" : "km"; }

void saveSettings() {
  preferences.putBool("night", nightTheme);
  preferences.putBool("mph", useMph);
}

void loadSettings() {
  preferences.begin("motonav", false);
  nightTheme = preferences.getBool("night", true);
  useMph = preferences.getBool("mph", false);
}

const char *trackStateText() {
  switch (trackState) {
    case TRACK_WAIT_FIX: return "WAIT FIX";
    case TRACK_RECORDING: return "REC";
    default: return "STOP";
  }
}

void drawTrackBadge() {
  if (uiMode != DRIVE_UI || stopRecordReminderVisible) return;
  String badge = "STOP"; uint16_t color = secondaryColor();
  if (sdError) { badge = "SD ERROR"; color = TFT_RED; }
  else if (trackState == TRACK_RECORDING) { badge = String("REC ") + String(trackPointCount); color = TFT_RED; }
  else if (trackState == TRACK_WAIT_FIX) { badge = "WAIT FIX"; color = TFT_YELLOW; }
  else if (savedNotice && millis() - savedNoticeMs < 5000UL) { badge = String("SAVED ") + String(savedPointCount); color = TFT_GREEN; }

  const bool tripLayout = screenMode == TRIP_SCREEN;
  const uint16_t cellBg = tripLayout ? (nightTheme ? tft.color565(18,28,38) : tft.color565(222,231,238)) : backgroundColor();
  const int16_t cellX = tripLayout ? 105 : 4;
  const int16_t centerX = tripLayout ? 151 : 45;
  tft.fillRect(cellX, 6, 92, 20, cellBg);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(color, cellBg);
  tft.drawString(badge, centerX, 16, 2);
}

bool initializeSd() {
  if (sdReady) return true;
  sdSpi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  sdReady = SD.begin(SD_CS_PIN, sdSpi) && SD.cardType() != CARD_NONE;
  sdError = !sdReady;
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

String statisticsFileName(const String &gpxName) {
  String name = gpxName;
  if (name.endsWith(".GPX")) name.remove(name.length() - 4);
  return name + ".CSV";
}

bool writeActiveTrackMarker(const String &gpxName) {
  SD.remove(ACTIVE_TRACK_MARKER);
  File marker = SD.open(ACTIVE_TRACK_MARKER, FILE_WRITE);
  if (!marker) return false;
  marker.println(gpxName);
  marker.flush();
  const bool ok = !marker.getWriteError();
  marker.close();
  return ok;
}

void clearActiveTrackMarker() {
  if (SD.exists(ACTIVE_TRACK_MARKER)) SD.remove(ACTIVE_TRACK_MARKER);
}

bool gpxAlreadyClosed(File &file) {
  if (!file || file.size() == 0) return false;
  const size_t tailSize = min(static_cast<size_t>(file.size()), static_cast<size_t>(96));
  if (!file.seek(file.size() - tailSize)) return false;
  String tail;
  while (file.available()) tail += static_cast<char>(file.read());
  return tail.indexOf("</gpx>") >= 0;
}

void recoverInterruptedTrack() {
  if (!sdReady || !SD.exists(ACTIVE_TRACK_MARKER)) return;

  File marker = SD.open(ACTIVE_TRACK_MARKER, FILE_READ);
  if (!marker) return;
  String interruptedName = marker.readStringUntil('\n');
  marker.close();
  interruptedName.trim();
  if (interruptedName.length() == 0 || !SD.exists(interruptedName)) {
    clearActiveTrackMarker();
    return;
  }

  File interrupted = SD.open(interruptedName, FILE_READ);
  const bool alreadyClosed = gpxAlreadyClosed(interrupted);
  interrupted.close();

  bool recovered = alreadyClosed;
  if (!alreadyClosed) {
    interrupted = SD.open(interruptedName, FILE_APPEND);
    if (interrupted) {
      interrupted.println("  </trkseg></trk>");
      interrupted.println("</gpx>");
      interrupted.flush();
      recovered = !interrupted.getWriteError();
      interrupted.close();
    }
  }

  if (recovered) {
    clearActiveTrackMarker();
    Serial.printf("Recovered interrupted GPX: %s\n", interruptedName.c_str());
  }
}

String csvField(const String &line, uint8_t index) {
  int start = 0;
  for (uint8_t field = 0; field < index; ++field) {
    start = line.indexOf(',', start);
    if (start < 0) return "";
    start++;
  }
  int end = line.indexOf(',', start);
  if (end < 0) end = line.length();
  return line.substring(start, end);
}

bool appendRideIndex(const String &dateText, const String &timeText) {
  const bool newFile = !SD.exists(RIDES_INDEX_FILE);
  File index = SD.open(RIDES_INDEX_FILE, FILE_APPEND);
  if (!index) return false;
  if (newFile) {
    index.println("version,date,time,gpx_file,distance_km,total_seconds,moving_seconds,stopped_seconds,average_kmh,maximum_kmh,gpx_points");
  }
  index.printf("1,%s,%s,%s,%.3f,%lu,%lu,%lu,%.2f,%.2f,%lu\n",
               dateText.c_str(), timeText.c_str(), summaryTrackFileName.c_str(),
               summaryDistanceM / 1000.0,
               static_cast<unsigned long>(summaryTotalTimeMs / 1000UL),
               static_cast<unsigned long>(summaryMovingTimeMs / 1000UL),
               static_cast<unsigned long>(summaryStoppedTimeMs / 1000UL),
               summaryAverageSpeedKmh, summaryMaximumSpeedKmh,
               static_cast<unsigned long>(summaryPointCount));
  index.flush();
  const bool ok = !index.getWriteError();
  index.close();
  return ok;
}

bool saveRideStatistics() {
  if (!sdReady || summaryTrackFileName.length() == 0) return false;

  char dateBuffer[11] = "0000-00-00";
  char timeBuffer[9] = "00:00:00";
  if (gps.date.isValid()) {
    snprintf(dateBuffer, sizeof(dateBuffer), "%04d-%02d-%02d",
             gps.date.year(), gps.date.month(), gps.date.day());
  }
  if (gps.time.isValid()) {
    snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d",
             gps.time.hour(), gps.time.minute(), gps.time.second());
  }
  const String dateText(dateBuffer);
  const String timeText(timeBuffer);
  const String csvName = statisticsFileName(summaryTrackFileName);

  SD.remove(csvName);
  File stats = SD.open(csvName, FILE_WRITE);
  if (!stats) return false;
  stats.println("version,date,time,gpx_file,distance_km,total_seconds,moving_seconds,stopped_seconds,average_kmh,maximum_kmh,gpx_points,completed");
  stats.printf("1,%s,%s,%s,%.3f,%lu,%lu,%lu,%.2f,%.2f,%lu,1\n",
               dateText.c_str(), timeText.c_str(), summaryTrackFileName.c_str(),
               summaryDistanceM / 1000.0,
               static_cast<unsigned long>(summaryTotalTimeMs / 1000UL),
               static_cast<unsigned long>(summaryMovingTimeMs / 1000UL),
               static_cast<unsigned long>(summaryStoppedTimeMs / 1000UL),
               summaryAverageSpeedKmh, summaryMaximumSpeedKmh,
               static_cast<unsigned long>(summaryPointCount));
  stats.flush();
  const bool statsOk = !stats.getWriteError();
  stats.close();
  return statsOk && appendRideIndex(dateText, timeText);
}

bool parseRideIndexLine(const String &line, RideRecord &ride) {
  if (line.length() < 10 || line.startsWith("version")) return false;
  ride.date = csvField(line, 1);
  ride.time = csvField(line, 2);
  ride.gpxFile = csvField(line, 3);
  ride.distanceKm = csvField(line, 4).toDouble();
  ride.totalSeconds = static_cast<uint32_t>(csvField(line, 5).toInt());
  ride.movingSeconds = static_cast<uint32_t>(csvField(line, 6).toInt());
  ride.stoppedSeconds = static_cast<uint32_t>(csvField(line, 7).toInt());
  ride.averageKmh = csvField(line, 8).toDouble();
  ride.maximumKmh = csvField(line, 9).toDouble();
  ride.points = static_cast<uint32_t>(csvField(line, 10).toInt());
  return ride.gpxFile.length() > 0;
}

void loadRideHistory() {
  rideHistoryCount = 0;
  selectedRideIndex = 0;
  if (!initializeSd() || !SD.exists(RIDES_INDEX_FILE)) return;
  File index = SD.open(RIDES_INDEX_FILE, FILE_READ);
  if (!index) return;
  while (index.available()) {
    String line = index.readStringUntil('\n');
    line.trim();
    RideRecord ride;
    if (!parseRideIndexLine(line, ride)) continue;
    if (rideHistoryCount < MAX_RIDE_HISTORY) {
      rideHistory[rideHistoryCount++] = ride;
    } else {
      for (uint8_t i = 1; i < MAX_RIDE_HISTORY; ++i) {
        rideHistory[i - 1] = rideHistory[i];
      }
      rideHistory[MAX_RIDE_HISTORY - 1] = ride;
    }
  }
  index.close();
  if (rideHistoryCount > 0) selectedRideIndex = rideHistoryCount - 1;
}

bool openTrackFile() {
  sdError = false;
  if (!initializeSd()) {
    drawTrackBadge();
    return false;
  }
  trackFileName = makeTrackFileName();
  trackFile = SD.open(trackFileName, FILE_WRITE);
  if (!trackFile) {
    sdError = true;
    drawTrackBadge();
    return false;
  }
  trackFile.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
  trackFile.println("<gpx version=\"1.1\" creator=\"MotoNav-CYD\" xmlns=\"http://www.topografix.com/GPX/1/1\">");
  trackFile.println("  <trk><name>MotoNav ride</name><trkseg>");
  trackFile.flush();
  if (trackFile.getWriteError() || !writeActiveTrackMarker(trackFileName)) {
    trackFile.close();
    SD.remove(trackFileName);
    clearActiveTrackMarker();
    sdError = true;
    drawTrackBadge();
    return false;
  }
  lastTrackFlushMs = millis();
  lastTrackPointMs = 0;
  havePreviousTrackPosition = false;
  trackPointCount = 0;
  savedNotice = false;
  return true;
}

void startTrack() {
  if (trackState != TRACK_STOPPED) return;
  sdError = false;
  savedNotice = false;
  resetTrip(false);
  if (!validFix()) {
    trackState = TRACK_WAIT_FIX;
  } else {
    trackState = openTrackFile() ? TRACK_RECORDING : TRACK_STOPPED;
  }
  drawTrackBadge();
}

void finishTrack() {
  const uint32_t finishMs = millis();
  summaryTrackFileName = trackFileName;
  summaryDistanceM = tripDistanceM;
  summaryTotalTimeMs = tripStartedMs > 0 ? finishMs - tripStartedMs : 0;
  summaryMovingTimeMs = min(movingTimeMs, summaryTotalTimeMs);
  summaryStoppedTimeMs = summaryTotalTimeMs - summaryMovingTimeMs;
  summaryAverageSpeedKmh = summaryMovingTimeMs > 0
      ? (summaryDistanceM / 1000.0) / (summaryMovingTimeMs / 3600000.0)
      : 0.0;
  summaryMaximumSpeedKmh = maximumSpeedKmh;
  summaryPointCount = trackPointCount;

  bool gpxSavedOk = false;
  if (trackFile) {
    trackFile.println("  </trkseg></trk>");
    trackFile.println("</gpx>");
    trackFile.flush();
    gpxSavedOk = !trackFile.getWriteError();
    trackFile.close();
  }
  if (gpxSavedOk) clearActiveTrackMarker();

  const bool statisticsSavedOk = gpxSavedOk && saveRideStatistics();
  summarySavedOk = gpxSavedOk && statisticsSavedOk;
  savedPointCount = trackPointCount;
  savedNotice = summarySavedOk;
  savedNoticeMs = finishMs;
  sdError = !summarySavedOk && trackPointCount > 0;
  trackState = TRACK_STOPPED;
  tripActive = false;
  tripFinishedMs = finishMs;
  havePreviousPosition = false;
  lastTrackPointMs = 0;
  drawTrackBadge();
}

bool trackPointQualityOk(uint32_t now, double latitude, double longitude) {
  if (gps.hdop.isValid() && gps.hdop.hdop() > MAX_HDOP_FOR_TRACK) return false;
  if (gps.satellites.isValid() &&
      gps.satellites.value() < MIN_SATELLITES_FOR_TRACK) return false;
  if (!havePreviousTrackPosition) return true;

  const uint32_t gapMs = now - previousTrackPositionMs;
  const double segmentM = TinyGPSPlus::distanceBetween(
      previousTrackLatitude, previousTrackLongitude, latitude, longitude);
  const double impliedKmh = gapMs > 0 ? segmentM * 3600.0 / gapMs : 9999.0;
  if (segmentM < MIN_TRACK_POINT_DISTANCE_M) return false;
  if (gapMs > MAX_TRACK_POINT_GAP_MS) return false;
  if (segmentM > MAX_TRACK_SEGMENT_DISTANCE_M) return false;
  return impliedKmh <= MAX_VALID_SPEED_KMH;
}

void writeTrackPoint(bool locationUpdated, double latitude, double longitude) {
  if (trackState == TRACK_WAIT_FIX && validFix()) {
    trackState = openTrackFile() ? TRACK_RECORDING : TRACK_STOPPED;
    havePreviousTrackPosition = false;
    drawTrackBadge();
  }
  if (trackState != TRACK_RECORDING || !validFix()) return;

  const uint32_t now = millis();
  if (!locationUpdated ||
      now - lastTrackPointMs + 100UL < TRACK_POINT_INTERVAL_MS ||
      !trackPointQualityOk(now, latitude, longitude)) return;

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
  if (gps.hdop.isValid()) {
    trackFile.printf("      <hdop>%.2f</hdop>\n", gps.hdop.hdop());
  }
  if (gps.satellites.isValid()) {
    trackFile.printf("      <sat>%lu</sat>\n",
                     static_cast<unsigned long>(gps.satellites.value()));
  }
  trackFile.println("    </trkpt>");
  if (trackFile.getWriteError()) {
    trackFile.close(); sdReady = false; sdError = true;
    trackState = TRACK_STOPPED; drawTrackBadge(); return;
  }

  previousTrackLatitude = latitude;
  previousTrackLongitude = longitude;
  previousTrackPositionMs = now;
  havePreviousTrackPosition = true;
  lastTrackPointMs = now;
  trackPointCount++;
  drawTrackBadge();

  if (now - lastTrackFlushMs >= TRACK_FLUSH_INTERVAL_MS) {
    trackFile.flush();
    if (trackFile.getWriteError()) {
      trackFile.close(); sdReady = false; sdError = true;
      trackState = TRACK_STOPPED; drawTrackBadge(); return;
    }
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
  drawBoldText(title, x + 18, y + 18, 2, titleColor, tile);
  tft.setTextColor(subtitleColor, tile);
  tft.drawString(subtitle, x + 18, y + 51, 2);
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
  drawMenuButton(8, 124, "GPX", "START: MANUAL", TFT_CYAN);
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
  drawBoldText("MENU", 10, 17, 4, labelColor(), bg);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(accentColor(), bg);
  tft.drawString("V0.9", 310, 11, 2);

  const String trackAction = trackState == TRACK_STOPPED ? "START TRACK" :
                             trackState == TRACK_WAIT_FIX ? "CANCEL / WAIT FIX" :
                             String("FINISH / ") + String(trackPointCount) + " PTS";
  drawMenuButton(8, 38, "TRACK", trackAction,
                 trackState == TRACK_RECORDING ? TFT_RED : accentColor());
  drawMenuButton(166, 38, "RIDES", "SAVED TRIPS", TFT_GREEN);
  drawMenuButton(8, 124, "GNSS", "LIVE DIAGNOSTICS", TFT_ORANGE);
  drawMenuButton(166, 124, "SETTINGS", "THEME / UNITS", secondaryColor());

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(secondaryColor(), bg);
  tft.drawString("HOLD TO RETURN", 160, 239, 1);
}

String gnssQualityText() {
  if (!validFix()) return "NO FIX";
  const bool satellitesOk = !gps.satellites.isValid() ||
                            gps.satellites.value() >= MIN_SATELLITES_FOR_TRACK;
  const bool hdopOk = !gps.hdop.isValid() ||
                      gps.hdop.hdop() <= MAX_HDOP_FOR_DISTANCE;
  return satellitesOk && hdopOk ? "GOOD" : "WEAK";
}

void drawDiagnosticCard(int16_t x, int16_t y, int16_t w,
                        const String &label, const String &value,
                        uint16_t accent) {
  const uint16_t panel = nightTheme ? tft.color565(18,28,38)
                                    : tft.color565(222,231,238);
  const uint16_t border = nightTheme ? tft.color565(55,78,94)
                                     : tft.color565(165,181,192);
  tft.fillRoundRect(x,y,w,48,7,panel);
  tft.drawRoundRect(x,y,w,48,7,border);
  tft.fillRoundRect(x+4,y+4,5,40,2,accent);
  tft.setTextDatum(TL_DATUM);
  drawBoldText(label,x+15,y+13,2,labelColor(),panel);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(primaryColor(),panel);
  tft.drawString(value,x+w-8,y+24,2);
}

void drawGnssDiagnostics() {
  uiMode = GNSS_UI;
  const uint16_t bg = backgroundColor();
  const String quality = gnssQualityText();
  const uint16_t qualityColor = quality=="GOOD" ? TFT_GREEN :
                                quality=="WEAK" ? TFT_ORANGE : TFT_RED;
  tft.fillScreen(bg);
  tft.fillRoundRect(6,4,308,30,7,qualityColor);
  tft.setTextDatum(ML_DATUM);
  drawBoldText("GNSS",14,19,4,quality=="GOOD"?TFT_BLACK:TFT_WHITE,qualityColor);
  tft.setTextDatum(MR_DATUM);
  drawBoldText(quality,306,19,2,quality=="GOOD"?TFT_BLACK:TFT_WHITE,qualityColor);

  const String sats = gps.satellites.isValid() ? String(gps.satellites.value()) : "--";
  const String hdop = gps.hdop.isValid() ? String(gps.hdop.hdop(),1) : "--";
  const String age = gps.location.isValid() ? String(gps.location.age())+" ms" : "--";
  const String altitude = gps.altitude.isValid() ? String(gps.altitude.meters(),1)+" m" : "--";
  drawDiagnosticCard(6,40,151,"SATELLITES",sats,TFT_GREEN);
  drawDiagnosticCard(163,40,151,"HDOP",hdop,TFT_ORANGE);
  drawDiagnosticCard(6,93,151,"FIX AGE",age,TFT_CYAN);
  drawDiagnosticCard(163,93,151,"ALTITUDE",altitude,TFT_BLUE);

  const uint16_t panel=nightTheme?tft.color565(18,28,38):tft.color565(222,231,238);
  tft.fillRoundRect(6,146,308,66,7,panel);
  tft.setTextDatum(TL_DATUM);tft.setTextColor(secondaryColor(),panel);
  tft.drawString("LAT",16,154,2);tft.drawString("LON",16,178,2);
  tft.setTextDatum(TR_DATUM);tft.setTextColor(primaryColor(),panel);
  tft.drawString(gps.location.isValid()?String(gps.location.lat(),6):"--",304,154,2);
  tft.drawString(gps.location.isValid()?String(gps.location.lng(),6):"--",304,178,2);
  tft.setTextDatum(BC_DATUM);tft.setTextColor(secondaryColor(),bg);
  tft.drawString("TAP: MENU",160,238,1);
}

void drawRideHistory() {
  uiMode = RIDES_UI;
  const uint16_t bg=backgroundColor(),panel=nightTheme?tft.color565(18,28,38):tft.color565(222,231,238);
  tft.fillScreen(bg);
  tft.setTextDatum(ML_DATUM);
  drawBoldText("RIDES",10,18,4,labelColor(),bg);

  if(rideHistoryCount==0){
    tft.setTextDatum(MC_DATUM);
    drawBoldText("NO SAVED RIDES",160,112,2,labelColor(),bg);
    tft.setTextColor(secondaryColor(),bg);tft.drawString("Finish a GPX track first",160,144,2);
  } else {
    const RideRecord &ride=rideHistory[selectedRideIndex];
    tft.setTextDatum(MR_DATUM);tft.setTextColor(accentColor(),bg);
    tft.drawString(String(selectedRideIndex+1)+"/"+String(rideHistoryCount),310,12,2);
    tft.fillRoundRect(6,38,308,48,7,panel);
    tft.setTextDatum(ML_DATUM);drawBoldText(ride.date,16,61,2,labelColor(),panel);
    tft.setTextDatum(MR_DATUM);drawBoldText(ride.time,304,61,2,labelColor(),panel);

    drawDiagnosticCard(6,92,151,"DISTANCE",String(displayDistance(ride.distanceKm),2)+" "+distanceUnitText(),TFT_GREEN);
    drawDiagnosticCard(163,92,151,"TOTAL",durationText(ride.totalSeconds*1000UL),TFT_CYAN);
    drawDiagnosticCard(6,145,98,"AVG",statisticSpeedText(ride.averageKmh),TFT_CYAN);
    drawDiagnosticCard(111,145,98,"MAX",statisticSpeedText(ride.maximumKmh),TFT_ORANGE);
    drawDiagnosticCard(216,145,98,"POINTS",String(ride.points),TFT_GREEN);
  }
  tft.setTextDatum(BC_DATUM);tft.setTextColor(secondaryColor(),bg);
  tft.drawString("LEFT: PREV   RIGHT: NEXT   HOLD: MENU",160,239,1);
}


void closeMenu() {
  uiMode = DRIVE_UI;
  redrawTheme();
  drawTrackBadge();
}

void drawStopRecordReminder() {
  stopRecordReminderVisible = true;
  tft.fillScreen(TFT_BLACK);

  // The whole red panel is one large, unmistakable STOP button.
  tft.fillRoundRect(12, 20, 296, 194, 18, TFT_RED);
  tft.drawRoundRect(12, 20, 296, 194, 18, TFT_WHITE);
  tft.drawRoundRect(14, 22, 292, 190, 16, TFT_WHITE);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.drawString("STOP", 160, 73, 4);
  tft.drawString("RECORD?", 160, 125, 4);
  tft.drawString("TAP TO FINISH", 160, 180, 2);
}

void hideStopRecordReminder() {
  if (!stopRecordReminderVisible) return;
  stopRecordReminderVisible = false;
  redrawTheme();
  drawTrackBadge();
}

void updateSafetyUi() {
  const uint32_t now = millis();
  const double speed = filteredSpeedKmh(validFix());

  if ((uiMode == MENU_UI || uiMode == SETTINGS_UI ||
       uiMode == GNSS_UI || uiMode == RIDES_UI) &&
      speed >= MENU_AUTO_CLOSE_SPEED_KMH) {
    closeMenu();
  }

  if (uiMode == SUMMARY_UI && speed >= MENU_AUTO_CLOSE_SPEED_KMH) {
    showSpeedScreen();
  }

  if (trackState != TRACK_RECORDING) {
    trackStoppedStartedMs = 0;
    hideStopRecordReminder();
    return;
  }

  if (speed > STOP_REMINDER_CLEAR_KMH) {
    trackStoppedStartedMs = 0;
    hideStopRecordReminder();
    return;
  }

  if (speed <= STOP_REMINDER_SPEED_KMH) {
    if (trackStoppedStartedMs == 0) trackStoppedStartedMs = now;
    if (!stopRecordReminderVisible &&
        uiMode == DRIVE_UI &&
        now - trackStoppedStartedMs >= STOP_REMINDER_DELAY_MS) {
      drawStopRecordReminder();
    }
  } else {
    trackStoppedStartedMs = 0;
  }
}

void handleMenuTap(int16_t x, int16_t y) {
  if (y >= 38 && y <= 116 && x < 160) {
    if (trackState == TRACK_STOPPED) { startTrack(); drawMenu(); }
    else if (trackState == TRACK_WAIT_FIX) {
      trackState = TRACK_STOPPED; resetTrip(false); drawMenu();
    } else { finishTrack(); drawRideSummary(); }
  } else if (y >= 38 && y <= 116) {
    loadRideHistory(); drawRideHistory();
  } else if (y >= 124 && y <= 202 && x < 160) {
    drawGnssDiagnostics();
  } else if (y >= 124 && y <= 202) {
    drawSettings();
  }
}

void handleRideHistoryTap(int16_t x) {
  if (rideHistoryCount == 0) { drawMenu(); return; }
  if (x < 160) {
    selectedRideIndex = selectedRideIndex == 0
        ? rideHistoryCount - 1 : selectedRideIndex - 1;
  } else {
    selectedRideIndex = (selectedRideIndex + 1) % rideHistoryCount;
  }
  drawRideHistory();
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
    // GPX recording is intentionally manual-only.
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

void updateSpeedFilter() {
  if (!gps.speed.isUpdated() || !gps.speed.isValid()) return;
  const uint32_t now = millis();
  const double raw = gps.speed.kmph();
  if (raw > MAX_VALID_SPEED_KMH) return;

  if (!speedFilterReady) {
    smoothedSpeedKmh = raw;
    lastAcceptedRawSpeedKmh = raw;
    lastSpeedFilterMs = now;
    speedFilterReady = true;
    return;
  }

  const double measuredElapsedS = (now - lastSpeedFilterMs) / 1000.0;
  const double elapsedS = measuredElapsedS < 0.2 ? 0.2 : measuredElapsedS;
  const double allowedJump = MAX_SPEED_JUMP_KMH_PER_S * elapsedS + 5.0;
  if (fabs(raw - lastAcceptedRawSpeedKmh) > allowedJump) return;

  const double alpha = raw < 8.0 ? SPEED_FILTER_ALPHA_LOW
                                 : SPEED_FILTER_ALPHA_NORMAL;
  smoothedSpeedKmh += alpha * (raw - smoothedSpeedKmh);
  lastAcceptedRawSpeedKmh = raw;
  lastSpeedFilterMs = now;
}

double filteredSpeedKmh(bool fix) {
  if (!fix || !speedFilterReady) return 0.0;
  return smoothedSpeedKmh < SPEED_NOISE_FLOOR_KMH ? 0.0 : smoothedSpeedKmh;
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
  if (tripActive) return millis() - tripStartedMs;
  if (tripStartedMs > 0 && tripFinishedMs >= tripStartedMs) {
    return tripFinishedMs - tripStartedMs;
  }
  return 0;
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
  const uint16_t bg=backgroundColor(), panel=nightTheme?tft.color565(18,28,38):tft.color565(222,231,238), border=nightTheme?tft.color565(55,78,94):tft.color565(165,181,192);
  tft.fillScreen(bg);
  tft.fillRoundRect(6,4,308,26,6,panel); tft.drawRoundRect(6,4,308,26,6,border);
  tft.drawFastVLine(104,7,20,border); tft.drawFastVLine(198,7,20,border);
  tft.setTextDatum(MC_DATUM); drawBoldText("MOTONAV",55,16,2,labelColor(),panel);
  tft.setTextColor(secondaryColor(),bg); tft.drawString(speedUnitText(),160,117,2);

  const int16_t x[3]={6,109,212}; const uint16_t accent[3]={TFT_GREEN,accentColor(),TFT_ORANGE}; const char* label[3]={"TRIP","AVG","MAX"};
  for(int i=0;i<3;++i){tft.fillRoundRect(x[i],128,98,50,7,panel);tft.drawRoundRect(x[i],128,98,50,7,border);tft.fillRoundRect(x[i]+4,132,5,42,2,accent[i]);tft.setTextDatum(TC_DATUM);drawBoldText(label[i],x[i]+53,132,2,labelColor(),panel);}

  tft.fillRoundRect(6,182,308,43,7,panel);tft.drawRoundRect(6,182,308,43,7,border);tft.drawFastVLine(160,187,33,border);
  tft.setTextDatum(TL_DATUM);drawBoldText("TOTAL",16,186,2,labelColor(),panel);drawBoldText("MOVING",170,186,2,labelColor(),panel);
  tft.setTextDatum(BC_DATUM);tft.setTextColor(secondaryColor(),bg);tft.drawString("HOLD FOR MENU",160,239,1);
  invalidateDynamicValues();
}

void drawStatus(bool fix, bool force) {
  const String status=gnssStatus(fix); if(!force&&status==previousStatus)return;
  const uint16_t panel=nightTheme?tft.color565(18,28,38):tft.color565(222,231,238);
  tft.fillRect(200,6,111,20,panel);tft.setTextDatum(MC_DATUM);tft.setTextColor(fix?TFT_GREEN:TFT_ORANGE,panel);tft.drawString(status,255,16,2);previousStatus=status;
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

void drawStatistic(const String &value,String &previous,int16_t centerX,bool force){
  if(!force&&value==previous)return;const uint16_t panel=nightTheme?tft.color565(18,28,38):tft.color565(222,231,238);
  tft.fillRect(centerX-39,149,78,26,panel);tft.setTextDatum(MC_DATUM);tft.setTextColor(primaryColor(),panel);tft.drawString(value,centerX,162,4);previous=value;
}

void drawTimes(bool force){
  const String value=durationText(totalTimeMs())+"|"+durationText(movingTimeMs);if(!force&&value==previousTimes)return;
  const uint16_t panel=nightTheme?tft.color565(18,28,38):tft.color565(222,231,238);
  tft.fillRect(12,203,142,19,panel);tft.fillRect(166,203,142,19,panel);tft.setTextDatum(MC_DATUM);tft.setTextColor(primaryColor(),panel);
  tft.drawString(durationText(totalTimeMs()),83,212,4);tft.drawString(durationText(movingTimeMs),237,212,4);previousTimes=value;
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
  if (uiMode != DRIVE_UI || stopRecordReminderVisible) return;
  const bool fix = validFix();
  if (screenMode == SPEED_SCREEN) {
    drawGaugeScale(force);
    drawGaugeSpeed(fix, force);
    return;
  }
  drawStatus(fix, force);
  drawSpeed(fix, force);
  drawStatistic(distanceText(), previousTrip, 59, force);
  drawStatistic(statisticSpeedText(averageSpeedKmh()), previousAverage, 162, force);
  drawStatistic(statisticSpeedText(maximumSpeedKmh), previousMaximum, 265, force);
  drawTimes(force);
  drawTrackBadge();
}

void startTripIfNeeded() {
  if (tripActive || trackState == TRACK_STOPPED || !validFix()) return;
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
  const bool hdopGood = !gps.hdop.isValid() ||
                        gps.hdop.hdop() <= MAX_HDOP_FOR_DISTANCE;
  const bool satellitesGood = !gps.satellites.isValid() ||
                              gps.satellites.value() >= MIN_SATELLITES_FOR_TRACK;
  if (fix && hdopGood && satellitesGood &&
      speed >= MOVING_SPEED_THRESHOLD_KMH &&
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
    const bool satellitesOk = !gps.satellites.isValid() ||
                              gps.satellites.value() >= MIN_SATELLITES_FOR_TRACK;

    if (speed >= MOVING_SPEED_THRESHOLD_KMH &&
        satellitesOk &&
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

void resetTrip(bool showNotice = true) {
  tripActive = false;
  havePreviousPosition = false;
  tripDistanceM = 0.0;
  movingTimeMs = 0;
  maximumSpeedKmh = 0.0;
  tripStartedMs = 0;
  tripFinishedMs = 0;
  lastMotionSampleMs = millis();
  invalidateDynamicValues();

  if (showNotice) {
    const uint16_t bg = backgroundColor();
    tft.fillRect(70, 84, 180, 28, bg);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(accentColor(), bg);
    tft.drawString("TRIP RESET", 160, 98, 2);
    delay(350);
    updateDynamicScreen(true);
  }
}


void drawRideSummary() {
  uiMode=SUMMARY_UI;
  const uint16_t bg=backgroundColor(),panel=nightTheme?tft.color565(18,28,38):tft.color565(222,231,238),border=nightTheme?tft.color565(55,78,94):tft.color565(165,181,192),state=summarySavedOk?TFT_GREEN:TFT_RED;
  tft.fillScreen(bg);
  tft.fillRoundRect(6,4,308,28,7,state);tft.setTextDatum(MC_DATUM);drawBoldText(summarySavedOk?"RIDE SAVED":"SAVE ERROR",160,18,2,summarySavedOk?TFT_BLACK:TFT_WHITE,state);

  tft.fillRoundRect(6,36,308,51,8,panel);tft.drawRoundRect(6,36,308,51,8,border);
  tft.setTextDatum(ML_DATUM);drawBoldText("DISTANCE",18,61,2,labelColor(),panel);
  tft.setTextDatum(MC_DATUM);tft.setTextColor(primaryColor(),panel);tft.drawString(String(displayDistance(summaryDistanceM/1000.0),2),201,59,6);
  tft.setTextDatum(MR_DATUM);drawBoldText(distanceUnitText(),304,61,2,labelColor(),panel);

  const int16_t x[3]={6,109,212};const char* tl[3]={"TOTAL","MOVING","STOPPED"};const String tv[3]={durationText(summaryTotalTimeMs),durationText(summaryMovingTimeMs),durationText(summaryStoppedTimeMs)};
  for(int i=0;i<3;++i){tft.fillRoundRect(x[i],92,98,54,7,panel);tft.drawRoundRect(x[i],92,98,54,7,border);tft.setTextDatum(TC_DATUM);drawBoldText(tl[i],x[i]+49,97,2,labelColor(),panel);tft.setTextDatum(MC_DATUM);tft.setTextColor(primaryColor(),panel);tft.drawString(tv[i],x[i]+49,126,4);}

  const char* sl[3]={"AVG","MAX","POINTS"};const String sv[3]={statisticSpeedText(summaryAverageSpeedKmh),statisticSpeedText(summaryMaximumSpeedKmh),String(summaryPointCount)};const uint16_t sa[3]={TFT_CYAN,TFT_ORANGE,TFT_GREEN};
  for(int i=0;i<3;++i){tft.fillRoundRect(x[i],151,98,61,7,panel);tft.drawRoundRect(x[i],151,98,61,7,border);tft.fillRoundRect(x[i]+4,155,5,53,2,sa[i]);tft.setTextDatum(TC_DATUM);drawBoldText(sl[i],x[i]+53,155,2,labelColor(),panel);tft.setTextDatum(MC_DATUM);tft.setTextColor(primaryColor(),panel);tft.drawString(sv[i],x[i]+53,188,4);}

  tft.setTextDatum(BC_DATUM);tft.setTextColor(secondaryColor(),bg);tft.drawString("TAP TO CONTINUE",160,239,1);
}


void showTripScreen() {
  uiMode = DRIVE_UI;
  screenMode = TRIP_SCREEN;
  drawStaticScreen();
  updateDynamicScreen(true);
}

void showSpeedScreen() {
  uiMode = DRIVE_UI;
  screenMode = SPEED_SCREEN;
  drawSpeedometerStatic();
  updateDynamicScreen(true);
}

void redrawTheme() {
  if (uiMode == MENU_UI) {
    drawMenu();
  } else if (uiMode == SETTINGS_UI) {
    drawSettings();
  } else if (uiMode == SUMMARY_UI) {
    drawRideSummary();
  } else if (uiMode == GNSS_UI) {
    drawGnssDiagnostics();
  } else if (uiMode == RIDES_UI) {
    drawRideHistory();
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
      !stopRecordReminderVisible &&
      now - touchStartedMs >= MENU_HOLD_MS) {
    longPressHandled = true;
    if (uiMode == MENU_UI || uiMode == SETTINGS_UI ||
        uiMode == GNSS_UI || uiMode == RIDES_UI) drawMenu();
    else if (filteredSpeedKmh(validFix()) < MENU_MAX_SPEED_KMH) drawMenu();
  }

  if (!down && touchWasDown) {
    const uint32_t heldMs = now - touchStartedMs;
    touchWasDown = false;
    if (!longPressHandled && heldMs >= TOUCH_MIN_PRESS_MS) {
      if (stopRecordReminderVisible) {
        // A tap on the large reminder button finishes and closes the GPX.
        stopRecordReminderVisible = false;
        trackStoppedStartedMs = 0;
        finishTrack();
        drawRideSummary();
      } else if (uiMode == MENU_UI) handleMenuTap(tapX, tapY);
      else if (uiMode == SETTINGS_UI) handleSettingsTap(tapX, tapY);
      else if (uiMode == GNSS_UI) drawMenu();
      else if (uiMode == RIDES_UI) handleRideHistoryTap(tapX);
      else if (uiMode == SUMMARY_UI) showTripScreen();
      else {
        nightTheme = !nightTheme;
        saveSettings();
        redrawTheme();
      }
    }
  }
}

void drawStartupTestValue(int value) {
  const uint16_t bg = backgroundColor();
  tft.fillRect(88, 83, 144, 76, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(primaryColor(), bg);
  tft.drawString(String(value), 160, 119, 7);
}



void drawPixelWheel(TFT_eSprite &sprite, int16_t cx, int16_t cy,
                    uint16_t tire, uint16_t rim, bool phase) {
  // Chunky 16-bit pixel-art wheel: octagonal tire, hub and animated spokes.
  sprite.fillRect(cx - 7, cy - 10, 14, 3, tire);
  sprite.fillRect(cx - 7, cy + 8, 14, 3, tire);
  sprite.fillRect(cx - 10, cy - 7, 3, 14, tire);
  sprite.fillRect(cx + 8, cy - 7, 3, 14, tire);
  sprite.fillRect(cx - 8, cy - 8, 4, 4, tire);
  sprite.fillRect(cx + 5, cy - 8, 4, 4, tire);
  sprite.fillRect(cx - 8, cy + 5, 4, 4, tire);
  sprite.fillRect(cx + 5, cy + 5, 4, 4, tire);
  sprite.fillRect(cx - 2, cy - 2, 5, 5, rim);
  if (phase) {
    sprite.drawLine(cx, cy, cx - 6, cy - 6, rim);
    sprite.drawLine(cx, cy, cx + 6, cy + 6, rim);
  } else {
    sprite.drawLine(cx, cy, cx + 6, cy - 6, rim);
    sprite.drawLine(cx, cy, cx - 6, cy + 6, rim);
  }
}

void drawPixelEnduro(TFT_eSprite &sprite, uint8_t frame) {
  const uint16_t transparent = TFT_MAGENTA;
  const uint16_t outline = nightTheme ? TFT_WHITE : TFT_BLACK;
  const uint16_t body = nightTheme ? TFT_CYAN : tft.color565(0, 105, 70);
  const uint16_t highlight = nightTheme ? TFT_YELLOW : tft.color565(245, 155, 20);
  const uint16_t metal = nightTheme ? TFT_LIGHTGREY : TFT_DARKGREY;
  const uint16_t helmet = nightTheme ? TFT_ORANGE : tft.color565(190, 35, 25);
  sprite.fillSprite(transparent);

  const int16_t bob = (frame == 1) ? 1 : 0;
  const int16_t rearX = 21, frontX = 70, wheelY = 45;

  drawPixelWheel(sprite, rearX, wheelY, outline, metal, frame & 1);
  drawPixelWheel(sprite, frontX, wheelY - bob, outline, metal, frame & 1);

  // High-clearance enduro chassis and swingarm.
  sprite.drawLine(rearX, wheelY, 39, 31 + bob, metal);
  sprite.drawLine(39, 31 + bob, 53, 43, metal);
  sprite.drawLine(53, 43, rearX, wheelY, metal);
  sprite.fillRect(35, 38 + bob, 18, 4, outline);
  sprite.fillRect(39, 34 + bob, 11, 4, metal);

  // Tall front fork and characteristic raised front mudguard.
  sprite.drawLine(54, 23 + bob, frontX - 2, wheelY - bob, outline);
  sprite.drawLine(58, 23 + bob, frontX + 2, wheelY - bob, metal);
  sprite.fillRect(59, 30 + bob, 20, 3, body);
  sprite.fillRect(70, 32 + bob, 12, 3, body);

  // Tank, narrow seat, side panel and exhaust.
  sprite.fillRect(35, 19 + bob, 22, 5, outline);
  sprite.fillRect(38, 16 + bob, 17, 8, body);
  sprite.fillRect(42, 18 + bob, 13, 3, highlight);
  sprite.fillRect(25, 18 + bob, 15, 5, outline);
  sprite.fillRect(28, 23 + bob, 20, 9, body);
  sprite.fillTriangle(29, 31 + bob, 47, 31 + bob, 38, 39 + bob, body);
  sprite.fillRect(18, 26 + bob, 18, 4, metal);
  sprite.fillRect(12, 24 + bob, 12, 4, outline);

  // Headlight mask, handlebar and hand guard.
  sprite.fillRect(56, 14 + bob, 8, 12, body);
  sprite.fillRect(63, 17 + bob, 5, 5, highlight);
  sprite.drawLine(56, 15 + bob, 62, 10 + bob, outline);
  sprite.drawLine(60, 10 + bob, 69, 10 + bob, outline);
  sprite.fillRect(67, 8 + bob, 8, 4, body);

  // Pixel rider leaning naturally over the bars.
  sprite.fillRect(37, 7 + bob, 10, 10, helmet);
  sprite.fillRect(40, 5 + bob, 8, 4, helmet);
  sprite.fillRect(46, 9 + bob, 5, 4, outline);
  sprite.fillRect(33, 14 + bob, 13, 11, outline);
  sprite.drawLine(43, 17 + bob, 61, 11 + bob, outline);
  sprite.drawLine(34, 23 + bob, 27, 34 + bob, outline);
  sprite.drawLine(27, 34 + bob, 37, 37 + bob, outline);
}

void runMotorcycleAnimation() {
  const uint16_t bg = backgroundColor();
  TFT_eSprite motoSprite(&tft);
  motoSprite.setColorDepth(16);
  if (motoSprite.createSprite(88, 58) == nullptr) return;

  tft.fillScreen(bg);
  tft.setTextDatum(TC_DATUM);
  drawBoldText("MOTONAV", 160, 16, 4, labelColor(), bg);

  const uint16_t road = nightTheme ? tft.color565(35, 45, 52)
                                   : tft.color565(185, 190, 194);
  for (int16_t x = -90, frame = 0; x <= 326; x += 7, ++frame) {
    tft.fillRect(0, 62, 320, 136, bg);
    tft.drawFastHLine(0, 174, 320, road);
    for (int16_t mark = (frame * 9) % 50 - 50; mark < 320; mark += 50) {
      tft.fillRect(mark, 184, 25, 3, road);
    }
    drawPixelEnduro(motoSprite, frame % 3);
    motoSprite.pushSprite(x, 112 + ((frame % 8 == 3) ? -1 : 0), TFT_MAGENTA);
    delay(20);
  }
  motoSprite.deleteSprite();
}

void runStartupAnimation() {
  const uint16_t bg = backgroundColor();

  tft.fillScreen(bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(accentColor(), bg);
  tft.drawString("MOTONAV", 160, 88, 4);
  tft.setTextColor(secondaryColor(), bg);
  tft.drawString("IGNITION ON", 160, 132, 2);
  delay(400);

  drawSpeedometerStatic();
  for (int tick = 0; tick <= GAUGE_TICK_COUNT; ++tick) {
    drawGaugeTick(tick, true);
    drawStartupTestValue(tick * 4);
    delay(16);
  }
  for (int tick = GAUGE_TICK_COUNT; tick >= 0; --tick) {
    drawGaugeTick(tick, false);
    drawStartupTestValue(tick * 4);
    delay(11);
  }

  runMotorcycleAnimation();

  tft.fillScreen(bg);
  tft.setTextDatum(MC_DATUM);
  drawBoldText("MOTONAV", 160, 91, 4, TFT_GREEN, bg);
  drawBoldText("READY TO RIDE", 160, 137, 4, labelColor(), bg);
  delay(3000);
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("MotoNav-CYD V0.9 Ride History & GNSS");

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
  recoverInterruptedTrack();
  loadSettings();
  runStartupAnimation();
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

  updateSpeedFilter();
  handleTouch();
  updateTripStatistics(locationUpdated, latitude, longitude);
  writeTrackPoint(locationUpdated, latitude, longitude);
  updateSafetyUi();
  updateAutomaticScreenMode();

  if (savedNotice && millis() - savedNoticeMs >= 5000UL) {
    savedNotice = false;
    drawTrackBadge();
  }

  if (millis() - lastScreenMs >= SCREEN_REFRESH_MS) {
    lastScreenMs = millis();
    if (uiMode == GNSS_UI) drawGnssDiagnostics();
    else updateDynamicScreen();
  }
}
