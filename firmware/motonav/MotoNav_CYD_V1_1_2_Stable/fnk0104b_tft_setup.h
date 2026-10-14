#pragma once

// Apply to ALL TFT_eSPI translation units, using the build script's -include.
// Matches Freenove's FNK0104AB_2.8_240x320_ILI9341 setup.
#define USER_SETUP_LOADED
#define USER_SETUP_INFO "MotoNav Freenove FNK0104B"
#define ILI9341_2_DRIVER
#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_ON
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_MISO 13
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS 10
#define TFT_DC 46
#define TFT_RST -1
#define TFT_BL 45
#define TFT_BACKLIGHT_ON HIGH
#define USE_HSPI_PORT
#define SPI_FREQUENCY 40000000
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
