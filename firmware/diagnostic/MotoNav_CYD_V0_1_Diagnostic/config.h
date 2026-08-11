#pragma once

// MotoNav-CYD V0.1 diagnostic configuration.
//
// The project accepts any UART GNSS receiver that outputs standard NMEA.
// Change these values for the receiver and CYD revision being tested.

constexpr int GNSS_UART_NUMBER = 2;
constexpr int GNSS_RX_PIN = 35;       // GNSS TX -> CYD GPIO35
constexpr int GNSS_TX_PIN = -1;       // Not required for receive-only diagnostics
constexpr uint32_t GNSS_BAUD = 9600;  // Common values: 9600, 38400, 115200

// Built-in microSD slot on the common ESP32-2432S028R CYD.
constexpr int SD_CS_PIN = 5;
constexpr int SD_SCK_PIN = 18;
constexpr int SD_MISO_PIN = 19;
constexpr int SD_MOSI_PIN = 23;

constexpr uint32_t SCREEN_REFRESH_MS = 500;
constexpr uint32_t GNSS_DATA_TIMEOUT_MS = 3000;
constexpr char TEST_TEXT_PATH[] = "/MOTONAV_SD_TEST.TXT";
constexpr char TEST_KML_PATH[] = "/MOTONAV_V01_TEST.KML";
