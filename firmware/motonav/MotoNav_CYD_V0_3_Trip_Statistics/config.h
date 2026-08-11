#pragma once

constexpr int GNSS_UART_NUMBER = 2;
constexpr int GNSS_RX_PIN = 35;
constexpr int GNSS_TX_PIN = -1;
constexpr uint32_t GNSS_BAUD = 9600;

constexpr int TOUCH_CS_PIN = 33;
constexpr int TOUCH_IRQ_PIN = 36;
constexpr int TOUCH_MOSI_PIN = 32;
constexpr int TOUCH_MISO_PIN = 39;
constexpr int TOUCH_CLK_PIN = 25;

constexpr uint32_t GNSS_DATA_TIMEOUT_MS = 3000;
constexpr uint32_t SCREEN_REFRESH_MS = 200;
constexpr uint32_t TOUCH_MIN_PRESS_MS = 60;
constexpr uint32_t RESET_HOLD_MS = 1500;

constexpr double SPEED_NOISE_FLOOR_KMH = 0.8;
constexpr double MOVING_SPEED_THRESHOLD_KMH = 2.0;
constexpr double MAX_VALID_SPEED_KMH = 220.0;
constexpr double MAX_HDOP_FOR_DISTANCE = 4.0;
constexpr double MAX_SEGMENT_DISTANCE_M = 80.0;
constexpr uint32_t MAX_POSITION_GAP_MS = 5000;
constexpr uint32_t MAX_MOTION_SAMPLE_MS = 1000;
