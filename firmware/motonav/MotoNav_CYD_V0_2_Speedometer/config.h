#pragma once

// Universal UART NMEA GNSS configuration.
constexpr int GNSS_UART_NUMBER = 2;
constexpr int GNSS_RX_PIN = 35;       // GNSS TX -> CYD GPIO35
constexpr int GNSS_TX_PIN = -1;       // Receive-only
constexpr uint32_t GNSS_BAUD = 9600;  // Typical: 9600, 38400, 115200

// XPT2046 touch controller pins used by the tested CYD revision.
constexpr int TOUCH_CS_PIN = 33;
constexpr int TOUCH_IRQ_PIN = 36;
constexpr int TOUCH_MOSI_PIN = 32;
constexpr int TOUCH_MISO_PIN = 39;
constexpr int TOUCH_CLK_PIN = 25;

constexpr uint32_t GNSS_DATA_TIMEOUT_MS = 3000;
constexpr uint32_t SCREEN_REFRESH_MS = 200;
constexpr uint32_t TOUCH_DEBOUNCE_MS = 500;
constexpr double SPEED_NOISE_FLOOR_KMH = 0.8;
