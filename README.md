# SEN69C Air Monitor

Firmware for a small indoor air-quality monitor built from an
[Adafruit ESP32-S3 Reverse TFT Feather](https://learn.adafruit.com/esp32-s3-reverse-tft-feather),
a [Sensirion SEN69C](https://sensirion.com/products/catalog/SEN69C) environmental sensor and the
[Adafruit SEN6x breakout](https://learn.adafruit.com/adafruit-sen6x-breakout).

> **Status:** early development (milestone M0, hardware bring-up). Nothing below "Planned" works yet.

## Planned features

- Shows PM1/2.5/4/10, CO₂, formaldehyde (HCHO), VOC and NOx indices, temperature and humidity on the built-in 240×135 screen, with warnings when a value leaves the healthy range.
- Three-button interface: several screens, trend graphs and a settings menu.
- Wi-Fi setup from a phone (QR code on screen → setup page), no reflashing; remembers several networks.
- Home Assistant integration through MQTT auto-discovery.
- Works fully offline: display and alarms never depend on the network.

## Hardware

| Part | Notes |
|---|---|
| Adafruit ESP32-S3 Reverse TFT Feather | 4 MB flash, 2 MB PSRAM, ST7789 240×135 TFT, 3 buttons |
| Sensirion SEN69C | PM, RH/T, VOC, NOx, HCHO, CO₂ — I²C address `0x6B` |
| Adafruit SEN6x breakout | 3.3 V regulator + level shifter, STEMMA QT |
| JST-GH 6-pin cable (#5754) | SEN69C → breakout |
| STEMMA QT / Qwiic cable | Breakout → Feather |

### Wiring

1. SEN69C → breakout with the 6-pin JST-GH cable.
2. Breakout → Feather with a STEMMA QT cable. This powers the breakout from the Feather's 3.3 V.

> ⚠️ Do **not** power the breakout's `VIN` from 5 V: its I²C pull-ups go to `VIN`, and the ESP32-S3 pins are not 5 V tolerant.

Mount the sensor with its inlets/outlet facing sideways (not up or down), away from or below the Feather (its heat skews temperature readings), and out of direct sunlight.

## Building and flashing

Uses [pioarduino](https://github.com/pioarduino/pioarduino-vscode-ide), the community fork of PlatformIO
that supports Arduino-ESP32 3.x. In VS Code install the **pioarduino IDE** extension (not the classic
PlatformIO IDE extension — they conflict), open this folder and use the Build / Upload buttons, or from
a terminal:

```sh
pio run                 # build
pio run -t upload       # build and flash
pio device monitor      # serial output (115200 baud)
```

- No driver is needed on Windows 10/11, macOS or Linux; the board appears as a USB serial port.
- If no port appears, put the board in ROM bootloader mode: hold **D0**, tap **Reset**, release **D0**.
- After flashing, press **Reset** if the new firmware doesn't start on its own.
- Flashing replaces the board's factory UF2 bootloader/CircuitPython. Adafruit's guide explains how to restore it.

### Optional local settings

`platformio.ini` loads `platformio_override.ini` if it exists (git-ignored). Use it for machine-specific
settings, for example to keep the ~1 GB of build output out of a cloud-synced folder:

```ini
[platformio]
workspace_dir = C:/path/outside/the/synced/folder
```

## License

Copyright © 2026 Francisco de los Ríos.

This program is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version. See [LICENSE](LICENSE).

Third-party components and their licenses are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
