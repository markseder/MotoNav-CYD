# MotoNav-CYD V1.1.2 — Quick Start

## Русский

### 1. Что потребуется

- ESP32-2432S028R (CYD);
- UART GNSS-приёмник с NMEA, настроенный на 9600 baud;
- microSD FAT32;
- Arduino IDE;
- библиотеки TFT_eSPI, TinyGPSPlus и XPT2046_Touchscreen.

### 2. Подключение GNSS

| GNSS | CYD |
|---|---|
| TX | GPIO35 |
| GND | GND |
| VCC | согласно паспорту GNSS |

RX GNSS для V1.1.2 не требуется.

### 3. Прошивка

1. Скачайте папку `firmware/motonav/MotoNav_CYD_V1_1_2_Stable` целиком.
2. Убедитесь, что INO и `config.h` лежат в одной папке.
3. Откройте INO в Arduino IDE.
4. Выберите совместимую ESP32-плату и правильный COM-порт.
5. Скомпилируйте и загрузите.
6. После теста шкалы и анимации дождитесь трёхсекундной заставки **READY TO RIDE**.

### 4. Первая поездка

1. Дождитесь **GNSS FIX**.
2. Удерживайте экран около 1,5 секунды.
3. Откройте **TRACK → START TRACK**.
4. После поездки снова откройте меню и выберите **FINISH**.
5. Проверьте **RIDE SAVED**, затем откройте **RIDES**.

## English

### 1. Required hardware and software

- ESP32-2432S028R (CYD);
- UART NMEA GNSS receiver configured for 9600 baud;
- FAT32 microSD card;
- Arduino IDE;
- TFT_eSPI, TinyGPSPlus and XPT2046_Touchscreen libraries.

### 2. GNSS wiring

| GNSS | CYD |
|---|---|
| TX | GPIO35 |
| GND | GND |
| VCC | Follow the GNSS module specification |

GNSS RX is not required for V1.1.2.

### 3. Flashing

1. Download the complete `firmware/motonav/MotoNav_CYD_V1_1_2_Stable` folder.
2. Keep the INO file and `config.h` together.
3. Open the INO file in Arduino IDE.
4. Select a compatible ESP32 board and the correct serial port.
5. Compile and upload.
6. Wait for the gauge test, motorcycle animation and the three-second **READY TO RIDE** screen.

### 4. First ride

1. Wait for **GNSS FIX**.
2. Hold the screen for about 1.5 seconds.
3. Select **TRACK → START TRACK**.
4. After the ride, open the menu and select **FINISH**.
5. Check **RIDE SAVED**, then open **RIDES**.
