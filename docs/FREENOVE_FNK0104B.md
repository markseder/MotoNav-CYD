# Freenove ESP32-S3 Display FNK0104B

The source in `firmware/motonav/MotoNav_CYD_V1_1_2_Stable` now targets the
**FNK0104B, 2.8-inch ILI9341 with FT6336U capacitive touch**. Historical
sketches and the prebuilt ZIP in `dist/` still target the original CYD;
do not use that ZIP for the Freenove board.

## Hardware configuration

The pinout and TFT initialization follow [Freenove's official repository](https://github.com/Freenove/Freenove_ESP32_S3_Display/tree/90004f874ed567b3c09d534d021172cb716e61a1):
the 2.8-inch schematic, FNK0104AB TFT setup, and touch/SDMMC/audio/LED examples.

| Peripheral | FNK0104B GPIOs |
|---|---|
| ILI9341 SPI | MOSI 11, MISO 13, SCLK 12, CS 10, DC 46 |
| Display backlight | 45, active high; reset tied to board reset |
| FT6336U I²C | SDA 16, SCL 15, address `0x38` |
| Touch reset / interrupt | 18 / 17; firmware polls at 20 ms |
| microSD, four-bit SDMMC | CLK 38, CMD 40, D0 39, D1 41, D2 48, D3 47 |
| ES8311 | Shares I²C 16/15; I²S MCLK 4, BCLK 5, WS 7, input 6, output 8 |
| Audio amplifier enable | 1; held low (muted) |
| WS2812 | 42; initialized off |
| External GNSS TX → board RX | **21**, on the Expanded IO connector |

Connect GNSS ground to board ground and supply the receiver according to its
specification. Its TX signal must be 3.3 V compatible. Leave GNSS RX disconnected.
UART1 receives at `9600` baud by default; change `GNSS_BAUD` in `config.h` for
your receiver (`38400` for a factory-configured QUESCAN G10A-F30).
GPIO35 from the CYD configuration must not be used: the S3R8's memory wiring
occupies that pin. GPIO21 avoids onboard display, touch, SDMMC, and audio pins.

MotoNav uses the display, touch, GNSS, and microSD. Audio playback and RGB status
effects are not implemented. ES8311 and WS2812 libraries are not needed.

## Build with Arduino CLI

Install Arduino CLI, Python 3 (used by ESP32 build tools), and these dependencies:

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.10 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install 'TFT_eSPI@2.5.43' 'TinyGPSPlus@1.1.0'
bash scripts/build-fnk0104b.sh
```

Run from the repository root. The build script selects `ESP32S3 Dev Module`,
16 MB flash, the 16 MB / 3 MB app partition layout, OPI PSRAM (8 MB on the S3R8),
and USB CDC on boot. It injects `fnk0104b_tft_setup.h` into **both the sketch
and TFT_eSPI's compiled sources**. Including the setup in the sketch alone
cannot configure the library. Set `ARDUINO_CLI` to an absolute CLI path if needed;
`MOTONAV_BUILD_DIR` overrides the default `/tmp/motonav-fnk0104b-build` output.

After building, upload with the same board options and your actual serial port:

```sh
arduino-cli upload --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,CDCOnBoot=cdc' --port YOUR_PORT --input-dir /tmp/motonav-fnk0104b-build firmware/motonav/MotoNav_CYD_V1_1_2_Stable
```

## Arduino IDE alternative

Select **ESP32S3 Dev Module**, 16 MB flash, the **16M Flash (3MB APP/9.9MB FATFS)** partition scheme,
**OPI PSRAM**, and **USB CDC On Boot: Enabled**. Install the same libraries.
Copy `fnk0104b_tft_setup.h` into the installed TFT_eSPI library, then replace
the active setup selection in its `User_Setup_Select.h` with:

```cpp
#include "fnk0104b_tft_setup.h"
```

Disable other setup includes. This changes the installed library's configuration
for all sketches using it; library updates may require reapplying it. Open the
V1.1.2 INO with all adjacent headers present. The CLI build script avoids editing
the installed library and is the reproducible build path.

## Validation

Run the host touch decoder checks:

```sh
g++ -std=c++11 -Wall -Wextra -Werror tests/fnk0104b/touch_packet_test.cpp -o /tmp/motonav-touch-test
/tmp/motonav-touch-test
```

These verify coordinate rotation, touch ID masking, contact/lift events,
two-touch packets, and rejection of invalid coordinates/counts. Bus failures
cancel the active gesture instead of triggering a release action.

On the board, verify the startup animation is landscape 320×240 with correct
colors, short taps change theme, and a 1.5-second hold opens the menu. Test all
menu buttons to confirm touch alignment (rotation 1 maps `x = rawY`,
`y = 239 - rawX`). Confirm the amplifier is silent and RGB LED off. Insert a
FAT32 card and check `microSD ready` on the 115200-baud serial console. Connect
GNSS TX to GPIO21, verify the GNSS character counter and fix, then record,
finish, and reopen a ride. Check GPX/CSV output and interrupted-track recovery.

Cloud validation currently cannot compile the firmware: the environment proxy
denies `downloads.arduino.cc` and `espressif.github.io`, preventing core/library
installation. Hardware behavior and full ESP32-S3 compilation remain unverified.
