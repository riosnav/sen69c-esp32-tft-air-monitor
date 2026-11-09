# Third-party notices

The firmware is built from the following third-party components. Each is used under its own
license, all of which are compatible with this project's GPL-3.0-or-later license. The full license
texts ship with each component's source, which the build downloads. Releases come as source only.

| Component | Version | License | Source |
|---|---|---|---|
| Arduino-ESP32 core | 3.3.11 | LGPL-2.1 | <https://github.com/espressif/arduino-esp32> |
| ESP-IDF (inside the Arduino core) | 5.5.5 | Apache-2.0, with closed-source binary radio/PHY libraries under Espressif's license | <https://github.com/espressif/esp-idf> |
| &nbsp;&nbsp;of which: HTTP server, MQTT client, mDNS, OTA | 5.5.5 | Apache-2.0 | (part of ESP-IDF; MQTT client from <https://github.com/espressif/esp-mqtt>, mDNS from <https://github.com/espressif/esp-protocols>) |
| &nbsp;&nbsp;lwIP TCP/IP stack | | BSD-3-Clause | (part of ESP-IDF) |
| &nbsp;&nbsp;Mbed TLS | | Apache-2.0 | (part of ESP-IDF) |
| &nbsp;&nbsp;wpa_supplicant | | BSD-3-Clause | (part of ESP-IDF) |
| &nbsp;&nbsp;cJSON | 1.7 | MIT, © Dave Gamble and cJSON contributors | (part of ESP-IDF) |
| Sensirion I2C SEN69C | 1.1.0 | BSD-3-Clause, © 2026 Sensirion AG | <https://github.com/Sensirion/arduino-i2c-sen69c> |
| Sensirion Core | 0.7.3 | BSD-3-Clause, © 2020 Sensirion AG | <https://github.com/Sensirion/arduino-core> |
| LovyanGFX | 1.2.33 | FreeBSD (BSD-2-Clause), including code derived from Adafruit_GFX / Adafruit_ILI9341 (BSD / MIT), TFT_eSPI (FreeBSD) and Richard Moore's QR code generator (MIT) | <https://github.com/lovyan03/LovyanGFX> |

Build tooling (pioarduino / PlatformIO, the GCC toolchain, esptool) is used to build the firmware but
is not part of it.

The screen renderer in [tools/screen-renderer](tools/screen-renderer) is built from the firmware's UI
code and the same LovyanGFX 1.2.33, which its build downloads. LovyanGFX includes miniz (MIT), which the
renderer uses to write PNG files.
