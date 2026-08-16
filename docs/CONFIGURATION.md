# MotoNav-CYD V1.1.2 configuration guide

[English](#english) · [Русский](#русский)

The firmware-specific settings are stored in:

```text
firmware/motonav/MotoNav_CYD_V1_1_2_Stable/config.h
```

Keep `config.h` in the same directory as `MotoNav_CYD_V1_1_2_Stable.ino`. After changing it, compile and upload the sketch again.

## English

### QUESCAN G10A-F30 quick setup

The QUESCAN G10A-F30 default UART setting is **38400 baud, 8N1**. Change this line:

```cpp
constexpr uint32_t GNSS_BAUD = 9600;
```

to:

```cpp
constexpr uint32_t GNSS_BAUD = 38400;
```

No change is required in the INO file. MotoNav already starts the GNSS port in 8N1 mode:

```cpp
gnssSerial.begin(GNSS_BAUD, SERIAL_8N1, GNSS_RX_PIN, GNSS_TX_PIN);
```

Use this wiring:

| QUESCAN G10A-F30 | CYD |
|---|---|
| TX | GPIO35 |
| GND | GND |
| VCC | Supply voltage specified for the exact module |
| RX | Not connected |

The resulting GNSS block in `config.h` is:

```cpp
constexpr int GNSS_UART_NUMBER = 2;
constexpr int GNSS_RX_PIN = 35;
constexpr int GNSS_TX_PIN = -1;
constexpr uint32_t GNSS_BAUD = 38400;
```

`GNSS_RX_PIN` is the ESP32 receive pin, so the GNSS module **TX** wire connects to it. `GNSS_TX_PIN = -1` disables transmission from MotoNav because V1.1.2 only reads NMEA messages.

### Configuration reference

#### GNSS and serial port

| Setting | Default | Meaning |
|---|---:|---|
| `GNSS_UART_NUMBER` | `2` | ESP32 hardware UART; normally leave unchanged |
| `GNSS_RX_PIN` | `35` | CYD input receiving NMEA from GNSS TX |
| `GNSS_TX_PIN` | `-1` | ESP32 transmission disabled |
| `GNSS_BAUD` | `9600` | Must exactly match the GNSS module UART speed |
| `GNSS_DATA_TIMEOUT_MS` | `3000` | GNSS data is considered lost after 3 seconds without bytes |

Common baud-rate examples:

```cpp
constexpr uint32_t GNSS_BAUD = 9600;   // GT-U12, ATGM336H and many NMEA modules
constexpr uint32_t GNSS_BAUD = 38400;  // QUESCAN G10A-F30 default
constexpr uint32_t GNSS_BAUD = 115200; // only when the module is configured for it
```

Only one `GNSS_BAUD` line may remain active.

#### Speed filtering

| Setting | Default | Meaning |
|---|---:|---|
| `SPEED_NOISE_FLOOR_KMH` | `0.8` | Lower speeds are displayed as zero |
| `MOVING_SPEED_THRESHOLD_KMH` | `2.0` | General movement threshold |
| `MAX_VALID_SPEED_KMH` | `220.0` | Higher readings are rejected as invalid |
| `MAX_SPEED_JUMP_KMH_PER_S` | `45.0` | Rejects implausibly fast speed changes |
| `SPEED_FILTER_ALPHA_LOW` | `0.22` | Smoothing strength at low speed |
| `SPEED_FILTER_ALPHA_NORMAL` | `0.38` | Smoothing strength at normal speed |

Lower alpha values produce smoother but slower response. Increase them only in small steps.

#### Automatic speedometer screen

| Setting | Default | Meaning |
|---|---:|---|
| `SPEED_SCREEN_ENTER_KMH` | `5.0` | Open the large speedometer above this speed |
| `SPEED_SCREEN_EXIT_KMH` | `1.0` | Begin returning after speed drops below this value |
| `SPEED_SCREEN_ENTER_HOLD_MS` | `800` | Entry threshold must remain valid for 0.8 seconds |
| `SPEED_SCREEN_EXIT_HOLD_MS` | `3000` | Exit threshold must remain valid for 3 seconds |

#### GPX track recording

| Setting | Default | Meaning |
|---|---:|---|
| `TRACK_POINT_INTERVAL_MS` | `1000` | Try to record one point per second |
| `MIN_TRACK_POINT_DISTANCE_M` | `1.5` | Minimum movement before recording another point |
| `MAX_HDOP_FOR_TRACK` | `5.0` | Reject track points with worse HDOP |
| `MIN_SATELLITES_FOR_TRACK` | `4` | Minimum satellites required for a track point |
| `TRACK_FLUSH_INTERVAL_MS` | `2000` | Flush buffered track data to microSD every 2 seconds |

#### Interface timing

| Setting | Default | Meaning |
|---|---:|---|
| `SCREEN_REFRESH_MS` | `200` | Main refresh period, approximately 5 Hz |
| `TOUCH_MIN_PRESS_MS` | `60` | Minimum valid touch duration |
| `MENU_HOLD_MS` | `1500` | Hold time required to open the menu |

### Verification after flashing

1. Power the GNSS module and MotoNav with a common ground.
2. Open **MENU → GNSS**.
3. Confirm that the received-character counter increases.
4. Move the antenna outdoors and wait for satellites and **GNSS FIX**.
5. Start a short test track and verify that a GPX file appears on the microSD card.

If the character counter remains at zero, check the baud rate first, then verify **GNSS TX → GPIO35**, common ground and module supply voltage. If characters arrive but there is no fix, the serial connection works; move the antenna outdoors and check its installation.

## Русский

### Быстрая настройка QUESCAN G10A-F30

Заводская настройка UART QUESCAN G10A-F30 — **38400 бод, 8N1**. В `config.h` замените:

```cpp
constexpr uint32_t GNSS_BAUD = 9600;
```

на:

```cpp
constexpr uint32_t GNSS_BAUD = 38400;
```

Основной INO-файл менять не нужно: режим `8N1` уже задан через `SERIAL_8N1`.

Подключение:

| QUESCAN G10A-F30 | CYD |
|---|---|
| TX | GPIO35 |
| GND | GND |
| VCC | По паспорту конкретной версии модуля |
| RX | Не подключать |

Готовый блок GNSS:

```cpp
constexpr int GNSS_UART_NUMBER = 2;
constexpr int GNSS_RX_PIN = 35;
constexpr int GNSS_TX_PIN = -1;
constexpr uint32_t GNSS_BAUD = 38400;
```

Название `GNSS_RX_PIN` относится к входу ESP32, поэтому к GPIO35 подключается провод **TX модуля**. Значение `GNSS_TX_PIN = -1` отключает передачу из MotoNav: прошивка V1.1.2 только принимает NMEA.

### Справочник параметров

#### GNSS и UART

| Параметр | По умолчанию | Назначение |
|---|---:|---|
| `GNSS_UART_NUMBER` | `2` | аппаратный UART ESP32; обычно не менять |
| `GNSS_RX_PIN` | `35` | вход CYD для NMEA с выхода TX приёмника |
| `GNSS_TX_PIN` | `-1` | передача из ESP32 отключена |
| `GNSS_BAUD` | `9600` | скорость должна точно совпадать с модулем |
| `GNSS_DATA_TIMEOUT_MS` | `3000` | потеря связи после 3 секунд без данных |

Примеры скорости:

```cpp
constexpr uint32_t GNSS_BAUD = 9600;   // GT-U12, ATGM336H и многие NMEA-модули
constexpr uint32_t GNSS_BAUD = 38400;  // QUESCAN G10A-F30 по умолчанию
constexpr uint32_t GNSS_BAUD = 115200; // только если модуль настроен на эту скорость
```

Активной должна оставаться только одна строка `GNSS_BAUD`.

#### Фильтрация скорости

| Параметр | По умолчанию | Назначение |
|---|---:|---|
| `SPEED_NOISE_FLOOR_KMH` | `0.8` | меньшая скорость отображается как нулевая |
| `MOVING_SPEED_THRESHOLD_KMH` | `2.0` | общий порог определения движения |
| `MAX_VALID_SPEED_KMH` | `220.0` | более высокие значения отбрасываются |
| `MAX_SPEED_JUMP_KMH_PER_S` | `45.0` | отбрасывание резких ошибочных скачков |
| `SPEED_FILTER_ALPHA_LOW` | `0.22` | сглаживание на малой скорости |
| `SPEED_FILTER_ALPHA_NORMAL` | `0.38` | сглаживание на обычной скорости |

Чем меньше коэффициент `alpha`, тем плавнее, но медленнее реакция показаний. Меняйте его небольшими шагами.

#### Автоматический экран спидометра

| Параметр | По умолчанию | Назначение |
|---|---:|---|
| `SPEED_SCREEN_ENTER_KMH` | `5.0` | открыть большой спидометр выше этой скорости |
| `SPEED_SCREEN_EXIT_KMH` | `1.0` | начать возврат после снижения скорости |
| `SPEED_SCREEN_ENTER_HOLD_MS` | `800` | скорость входа должна держаться 0,8 секунды |
| `SPEED_SCREEN_EXIT_HOLD_MS` | `3000` | скорость выхода должна держаться 3 секунды |

#### Запись GPX

| Параметр | По умолчанию | Назначение |
|---|---:|---|
| `TRACK_POINT_INTERVAL_MS` | `1000` | попытка записать одну точку в секунду |
| `MIN_TRACK_POINT_DISTANCE_M` | `1.5` | минимальное перемещение до следующей точки |
| `MAX_HDOP_FOR_TRACK` | `5.0` | точки с худшим HDOP отбрасываются |
| `MIN_SATELLITES_FOR_TRACK` | `4` | минимум спутников для записи точки |
| `TRACK_FLUSH_INTERVAL_MS` | `2000` | сброс буфера на microSD каждые 2 секунды |

#### Интерфейс

| Параметр | По умолчанию | Назначение |
|---|---:|---|
| `SCREEN_REFRESH_MS` | `200` | обновление примерно 5 раз в секунду |
| `TOUCH_MIN_PRESS_MS` | `60` | минимальная длительность касания |
| `MENU_HOLD_MS` | `1500` | удержание для открытия меню |

### Проверка после прошивки

1. Подайте питание на GNSS и CYD с общей землёй.
2. Откройте **MENU → GNSS**.
3. Убедитесь, что счётчик принятых символов увеличивается.
4. Вынесите антенну на открытое место и дождитесь спутников и **GNSS FIX**.
5. Запишите короткий тестовый трек и проверьте появление GPX на microSD.

Если счётчик символов остаётся нулевым, сначала проверьте скорость UART, затем соединение **GNSS TX → GPIO35**, общую землю и напряжение питания. Если символы идут, но фикса нет, UART уже работает — модулю нужен открытый обзор неба или проверка установки антенны.
