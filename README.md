# SEN69C Air Monitor

Firmware for a small indoor air-quality monitor built from an
[Adafruit ESP32-S3 Reverse TFT Feather](https://learn.adafruit.com/esp32-s3-reverse-tft-feather),
a [Sensirion SEN69C](https://sensirion.com/products/catalog/SEN69C) environmental sensor and the
[Adafruit SEN6x breakout](https://learn.adafruit.com/adafruit-sen6x-breakout).

> **Status:** early development (milestone M2: Wi-Fi and web page). Home Assistant integration is next.

## Features

- Shows PM1/2.5/4/10, CO₂, formaldehyde (HCHO), VOC and NOx indices, temperature and humidity on the built-in 240×135 screen.
- Colour-coded levels and a pop-up alert (which also wakes the screen) when a value leaves the healthy range; the NeoPixel shows the overall air quality.
- Trend graphs of the last hour and the last 24 hours for every value.
- Settings menu: brightness, auto-dim, °C/°F, status LED, Wi-Fi, temperature offset, altitude, CO₂ self-calibration, fan cleaning, CO₂ recalibration, restart, factory reset. Settings survive restarts and power cuts. (The sensor itself forgets the altitude, temperature offset and self-calibration setting when it loses power, so the firmware sends them again every time it starts.)
- Battery charge (Feather's MAX17048 gauge) when a LiPo is connected and *A battery is connected* is ticked on the web page; without a battery the gauge reads the charger and is ignored.
- Wi-Fi setup from a phone, no reflashing: scan the QR code on the screen and pick your network. Up to five networks are saved; the monitor joins whichever is in range.
- Local web page with live values, trend charts, settings, maintenance and firmware updates over Wi-Fi (with automatic rollback if an update fails).
- Works fully offline; Wi-Fi is optional and everything stays on your network (no cloud).

Planned: Home Assistant integration through MQTT auto-discovery.

## Using it

Hold the monitor with the buttons on the left: **D0** at the top, **D1** in the middle, **D2** at the bottom.
Small symbols next to each button show what it does on the current screen.

| | D0 | D1 | D2 |
|---|---|---|---|
| Pages | previous page | page action (see below) · **hold**: settings | next page |
| Settings | up | select · **hold**: close | down |
| Changing a value | increase (hold to repeat) | done | decrease (hold to repeat) |
| Alert | dismiss | dismiss | dismiss |
| Wi-Fi setup screen | – | cancel, or done once connected | – |

Pages: overview, particles, gases, comfort, trend (1 h), trend (24 h), Wi-Fi, device information.
D1 on a trend page shows the next value; on the Wi-Fi page it starts Wi-Fi setup, or, once connected,
shows a QR code that opens the web page on a phone.
When the screen is dimmed, the first press only wakes it.

### Default limits

| Value | Warning | Alarm | Basis |
|---|---|---|---|
| CO₂ | 1000 ppm | 1500 ppm | Health Canada indoor guideline (24-hour average) and German Federal Environment Agency / UK school ventilation guidance BB101 (daily average) |
| PM2.5 | 15 µg/m³ | 35 µg/m³ | WHO 2021 24-hour guideline / US EPA 24-hour standard |
| PM10 | 45 µg/m³ | 150 µg/m³ | WHO 2021 24-hour guideline / US EPA 24-hour standard |
| Formaldehyde | 40 ppb | 80 ppb | ~WELL building standard / WHO 30-minute guideline (0.1 mg/m³) |
| VOC index | 150 | 250 | Sensirion index: 100 is the sensor's learned typical air |
| NOx index | 20 | 150 | Sensirion index: 1 is the sensor's learned typical air |
| Temperature | below 18 or above 26 °C | below 16 or above 30 °C | Comfort range |
| Humidity | below 30 or above 60 % | below 20 or above 70 % | Comfort range; above 70 % mould risk |

A level only drops back once the value is a little inside the limit again, so readings near a limit don't flicker.
The sensor's readings are only meaningful once warmed up: formaldehyde needs about 10 minutes, and the VOC and NOx
indices learn the room's typical air over the first hours.

### Wi-Fi and the web page

1. On the Wi-Fi page press **D1** (or choose *Set up Wi-Fi* in the settings). The monitor opens a temporary
   hotspot and shows a QR code.
2. Scan it with your phone's camera and join the hotspot. A setup page opens; if it doesn't, the screen shows a
   second QR code and the address to open.
3. Pick your network and enter its password. The monitor connects, saves it, and shows its address, e.g.
   `http://airmonitor-a1b2.local/` and its IP address.

Notes:

- Only 2.4 GHz networks work (an ESP32-S3 limitation).
- Up to five networks are saved; the monitor joins the strongest one in range and keeps looking, slowly, when
  none is. More networks can be added from the web page or by running the setup again.
- `.local` names don't resolve on every Android phone; use the QR code on the Wi-Fi page (D1) or the IP address.
- The setup hotspot has a random password, shown on the screen, and closes after use or after 10 minutes.

The web page shows the readings, trend charts and device status to anyone on your network. Changing settings,
maintenance and firmware updates need the **web password**, shown on the monitor's Wi-Fi page (the browser asks
for it; any user name works). A random one is created on first start; it can be changed or removed on the web
page, and a factory reset creates a new one. The connection is plain HTTP, as is usual for devices on a home
network, so don't reuse a valuable password.

The web page also serves a small JSON API (`/api/state`, `/api/history?metric=co2&range=hour|day`,
`/api/settings`, ...), documented at the top of [src/net/Web.h](src/net/Web.h).

### Firmware updates over Wi-Fi

On the web page open *Firmware update* and upload a `firmware.bin` (not `firmware.factory.bin`), or from a
computer:

```sh
curl -u any:WEBPASSWORD --data-binary @firmware.bin http://airmonitor-a1b2.local/api/ota
```

The monitor restarts into the new firmware, which confirms itself once it has rejoined Wi-Fi. If it crashes
first, or can't rejoin within 5 minutes, the monitor goes back to the previous firmware.

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
- The web pages live in [web/](web/); a build step (`scripts/embed_web.py`) compresses them into the firmware.

### Running the tests

The hardware-independent logic (`lib/aqcore`: limits, history, buttons, comfort maths; `lib/netcore`: choosing
which Wi-Fi network to join) has Unity tests that run on the board. With native USB the serial port disappears
while the board restarts, so upload and test in two steps:

```sh
pio test --without-testing      # build and flash the test firmware
pio test --without-uploading    # read the results (within 15 s)
pio run -t upload               # put the normal firmware back
```

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
