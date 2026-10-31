# SEN69C Air Monitor

Firmware for a small indoor air-quality monitor built from an **Adafruit ESP32-S3 Reverse TFT Feather** (an
ESP32-S3 microcontroller board with a built-in screen and three buttons) and a **Sensirion SEN69C** sensor module,
connected through Adafruit's SEN6x breakout board. The three boards plug together with cables, with no soldering
(see [What you need](#what-you-need)). It measures particles (PM1, PM2.5, PM4, PM10), carbon dioxide (CO₂), formaldehyde, volatile organic
compounds and nitrogen oxides (as VOC and NOₓ indices), temperature and humidity. It shows them on its built-in
screen with green, amber and red levels, and warns you when the air gets unhealthy. Over Wi-Fi it adds a web page
and can send everything to Home Assistant; without Wi-Fi it works just the same. Its warnings are on the screen
only, without sound, and it is not a smoke or carbon monoxide alarm.

![The monitor's main screen: CO₂ and PM2.5 in large digits, the other gases below them, then temperature and
humidity, all in green](docs/screen.png)

> **Status:** version 1.0.0.
>
> **Made with AI:** the code and documentation were written by an AI, Anthropic's Claude, working in Claude Code.
> The author set the requirements, made the design decisions and tested the monitor on the real hardware.

## Features

- Every value on the built-in 240×135 colour screen, with green / amber / red levels.
- A pop-up alert (which also wakes the screen, and closes by itself after a minute) when a value leaves the
  healthy range; the board's small colour LED shows the overall air quality.
- Trend graphs of the last hour and the last 24 hours for every value.
- A settings menu on the device, worked with its three buttons. Settings survive restarts and power cuts.
- Wi-Fi setup from a phone, no reprogramming: scan the QR code on the screen and pick your network. Up to five
  networks are saved; the monitor joins the strongest one in range.
- A web page on your network with live values (optionally with the sensor's tolerance), trend charts, settings,
  maintenance and firmware updates over Wi-Fi (with automatic rollback if an update fails).
- Home Assistant integration: the monitor appears as a device with a sensor for every value.
- Works fully offline; Wi-Fi is optional and everything stays on your network (no cloud).

## What you need

| Qty | Part | Notes |
|---|---|---|
| 1 | [Adafruit ESP32-S3 Reverse TFT Feather](https://www.adafruit.com/product/5691), product **5691** | The microcontroller board with the screen and buttons. Not product 6303, the version for an external antenna. |
| 1 | [Sensirion SEN69C](https://sensirion.com/products/catalog/SEN69C) sensor module, order code **SEN69C-SIN-T** | Sold by electronics distributors such as Digi-Key or Mouser. Not the SEK-SEN69C evaluation kit. |
| 1 | [Adafruit SEN6x Breakout](https://www.adafruit.com/product/6331), product **6331** | Sold "for Sensirion SEN66"; it fits the whole SEN6x family, including the SEN69C. Comes **without** cables or sensor. |
| 1 | [Adafruit JST GH 6-pin cable, 100 mm](https://www.adafruit.com/product/5754), product **5754** | Sensor → breakout. |
| 1 | [STEMMA QT / Qwiic cable, 100 mm](https://www.adafruit.com/product/4210), product **4210** | Breakout → Feather. STEMMA QT is Adafruit's 4-pin plug-in connector, the same as SparkFun's Qwiic; any length works. |
| 1 | USB-C cable that carries **data** | To program the board: USB-A to C or C to C, to fit your computer. Some cables only charge; they won't work for that. |
| 1 | USB charger (5 V) | To power the monitor afterwards; a phone charger is fine. |
| opt. | LiPo battery (3.7 V, 250 mAh or more) with a JST-PH 2-pin plug | Only as a backup for power cuts or moving it around; the monitor is meant to run from USB. Adafruit's own LiPo batteries fit; some from other sellers are wired the other way round, which can destroy the board. |

No soldering or tools are needed: the Feather comes with loose header pins, which you don't use. This repository
doesn't include an enclosure.

## Assembly

1. Connect the SEN69C to the breakout with the 6-pin JST GH cable.
2. Connect the breakout to the Feather with the STEMMA QT cable. Either of the breakout's two STEMMA QT
   connectors works. This cable also powers the breakout and the sensor from the Feather.

That's all. The connectors are keyed and only fit one way round; push them in fully. Leave the breakout's row of
pin holes empty.

> ⚠️ If you ever wire the breakout with jumper wires instead, connect its `VIN` to the Feather's **3V** pin, never
> to 5 V or USB: its data lines are pulled up to `VIN`, and the ESP32-S3 can be damaged by 5 V.

Where to put it: mount the sensor with its openings facing sideways (not up or down), away from or below the
Feather (whose heat skews the temperature reading), and out of direct sunlight. Without an enclosure, double-sided
foam tape on a piece of card or plastic holds the three boards in place.

## Installing the firmware

You need a computer with Windows 10/11, macOS or Linux, an internet connection and about 5 GB of free disk space;
no drivers are needed.

The Feather's three buttons beside the screen are **D0**, **D1** and **D2**: holding the board with them on the
left, D0 is the top one. Its **Reset** button is a separate small button; the board has one on each side.

1. **Get the code.** On [this project's GitHub page](https://github.com/riosnav/sen69c-esp32-tft-air-monitor)
   click **Code → Download ZIP** and unzip it, or clone it with git. You get a folder called
   `sen69c-esp32-tft-air-monitor` (from the ZIP: `sen69c-esp32-tft-air-monitor-main`; if you find a folder inside a
   folder, use the inner one, which contains `platformio.ini`). Put it somewhere that isn't synced by
   OneDrive, iCloud or Dropbox (for example `C:\Projects` on Windows): the build writes hundreds of MB of files,
   and syncing them can make builds fail.
2. **Install [Visual Studio Code](https://code.visualstudio.com/)** (free).
3. **Open the project.** In VS Code choose *File → Open Folder…* and pick that folder. If asked whether you trust
   the authors, answer yes. VS Code then offers, at the bottom right, to install the recommended extensions: accept,
   which installs **pioarduino IDE** (the community version of PlatformIO that supports this board's software). If
   you missed the offer, open the Extensions view (the four-squares icon on the left), search for
   *pioarduino IDE* and click *Install*. If you already have the classic *PlatformIO IDE* extension, disable it,
   as the two conflict.
4. **Wait for the extension.** The first time, it installs its tools (progress at the bottom right) and may ask
   you to reload the window. It's ready when a pioarduino icon appears in the left sidebar and **✓** and **→**
   buttons appear in the status bar at the bottom.
5. **Connect the Feather** to the computer with the USB-C data cable.
6. **Build and upload.** Click the **→** (Upload) button in the status bar, or open the pioarduino icon in the
   left sidebar and choose *Project Tasks → feather_s3_reversetft → General → Upload*. The very first time, this
   downloads about 3–4 GB of tools and can take 10–30 minutes; later builds take about a minute. The terminal at the
   bottom shows the progress and ends with **SUCCESS**.
7. The monitor restarts and shows its *Air quality* page. If the screen stays dark, press **Reset**.

If the upload can't find the board, put it in its bootloader mode: hold **D0**, press and release **Reset**,
release **D0**, then click Upload again and press **Reset** when it has finished. On Linux the upload may also fail
with "permission denied" on the serial port; install
[PlatformIO's udev rules](https://docs.platformio.org/en/latest/core/installation/udev-rules.html) and log out and
back in.

Uploading this firmware replaces the demo that Adafruit ships on the board and its UF2 bootloader. Adafruit's
[factory reset page](https://learn.adafruit.com/esp32-s3-reverse-tft-feather/factory-reset) explains how to put
them back.

## First start

- The screen shows the *Air quality* page. The level at the top right reads "..." and the colour LED glows
  blue-green until every value has a reading; then both show the overall level: **GOOD** (green), **FAIR** (amber)
  or **POOR** (red).
- Most values appear within a minute. Formaldehyde needs about 10 minutes to be accurate, and CO₂ about an hour.
  On the very first use, leave it running for 12 hours and air the room at least once, so the CO₂ reading can
  settle. The VOC and NOₓ indices learn your room's usual air over the first hours.
- The *Device* page should say **Sensor OK** (press **D0** once to get there: the pages wrap around). If the
  screen says **No sensor** or **Sensor not found**, check that both cables are pushed in fully.

Then, in the settings (hold **D1**; see [Settings](#settings)):

1. Set your **Altitude** in metres; the CO₂ reading depends on air pressure.
2. After an hour, compare the temperature with a thermometer and set the **Temp. offset** to the thermometer's
   reading minus the monitor's (usually negative: the board warms the sensor a little).
3. Optionally, set up Wi-Fi (see [Wi-Fi and the web page](#wi-fi-and-the-web-page)).

## Using it

Hold the monitor with the buttons on the left: **D0** at the top, **D1** in the middle, **D2** at the bottom.
Small symbols next to each button show what it does on the current screen.

| | D0 | D1 | D2 |
|---|---|---|---|
| Pages | previous page | page action (see below) · **hold**: settings | next page |
| Settings | up | select · **hold**: close | down |
| Changing a value | increase (hold to repeat) | done | decrease (hold to repeat) |
| Confirmation (e.g. factory reset) | cancel | **hold**: confirm | cancel |
| Alert | dismiss | dismiss | dismiss |
| Wi-Fi setup screen | – | cancel, or done once connected | – |

Pages: *Air quality* (overview), *Particles*, *Gases*, *Comfort*, the trend of the last hour, the trend of the
last 24 hours, *Wi-Fi* and *Device*.
D1 on a trend page shows the next value. On the Wi-Fi page it starts Wi-Fi setup (or turns Wi-Fi back on), or,
once connected, shows a QR code that opens the web page on a phone.
The screen dims after 2 minutes without use (changeable); when it is dimmed, the first press only wakes it.

### Levels and alerts

| Value | Fair (amber) | Poor (red) | Basis |
|---|---|---|---|
| CO₂ | 1000 ppm | 1500 ppm | Health Canada indoor guideline (24-hour average) and German Federal Environment Agency / UK school ventilation guidance BB101 (daily average) |
| PM2.5 | 15 µg/m³ | 35 µg/m³ | WHO 2021 24-hour guideline / US EPA 24-hour standard |
| PM10 | 45 µg/m³ | 150 µg/m³ | WHO 2021 24-hour guideline / US EPA 24-hour standard |
| Formaldehyde | 40 ppb | 80 ppb | ~WELL building standard / WHO 30-minute guideline (0.1 mg/m³) |
| VOC index | 150 | 250 | Sensirion index: 100 is the sensor's learned typical air |
| NOₓ index | 20 | 150 | Sensirion index: 1 is the sensor's learned typical air |
| Temperature | below 18 or above 26 °C | below 16 or above 30 °C | Comfort range |
| Humidity | below 30 or above 60 % | below 20 or above 70 % | Comfort range; above 70 % mould risk |

PM1 and PM4 have no limits and are shown for information. A level only drops back once the value is a little
inside the limit again, so readings near a limit don't flicker. The overall level is the worst of them.

When a value turns Fair or Poor, a pop-up names it. It closes with any button, or by itself after a minute, and the
same value only alerts again if it gets worse. While a Poor alert is showing, the LED blinks red.

### Accuracy

Typical tolerances from Sensirion's SEN6x datasheet. The web page can show them as `±` on each value and as a
shaded band on the charts.

| Value | Tolerance |
|---|---|
| CO₂ | ±(100 ppm + 10 % of the reading) |
| PM1, PM2.5 | ±(5 µg/m³ + 5 %) up to 100 µg/m³; ±10 % above |
| PM4, PM10 | ±25 µg/m³ up to 100 µg/m³; ±25 % above |
| Formaldehyde | ±20 ppb or ±20 %, whichever is larger |
| Temperature | ±0.45 °C (the sensor alone, 15–30 °C) |
| Humidity | ±4.5 % (30–70 %) |

Much of this is a steady error of the individual sensor, so changes over time are more reliable than absolute
values. The VOC and NOₓ indices have no tolerance: every sensor reads 100 and 1 in a room's usual air.

### Settings

Hold **D1** to open them. They are kept through restarts and power cuts. The trend graphs are not: they are kept
in memory and start over after a restart or update.

| Setting | What it does |
|---|---|
| Brightness, Dim after, Units, Status LED | Screen brightness, when the screen dims, °C or °F, and how bright the colour LED is (or off). |
| Wi-Fi | Turns Wi-Fi on or off. |
| Set up Wi-Fi | Adds a network from a phone (see below). |
| Forget Wi-Fi | Deletes every saved network. |
| Temp. offset | Added to the measured temperature, to correct the warmth of the board. |
| Altitude | Your height above sea level, for the CO₂ reading. |
| CO₂ self-calibration | Keeps the CO₂ reading accurate on its own. Leave it on if the room gets fresh air at least once a week. |
| Calibrate CO₂ | Sets the current reading to 420 ppm, the CO₂ of outdoor air. **Only outdoors** (power it from a power bank or the battery), after 3 minutes in fresh air; indoors it makes the readings wrong. |
| Clean fan | Runs the sensor's fan at full speed for 10 s to blow out dust. The sensor doesn't do this by itself. |
| Restart | Restarts the monitor. |
| Factory reset | Erases all settings, saved Wi-Fi networks, the web password and the Home Assistant settings. |

If a LiPo battery is connected, tick *A battery is connected* on the web page (*Settings → Display and sensor*;
this needs Wi-Fi set up once) to show its charge on the *Device* page. Without a battery the board's battery
gauge reads the charger, so this is off by default.

## Wi-Fi and the web page

1. On the Wi-Fi page press **D1** (or choose *Set up Wi-Fi* in the settings). The monitor opens a temporary
   hotspot and shows a QR code, with the hotspot's name and password for typing them in by hand.
2. Scan the QR code with your phone's camera and join the hotspot. If the phone warns that the network has no
   internet, choose to stay connected. A setup page opens; if it doesn't, the screen shows a second QR code and the
   address to open.
3. Pick your home network and enter its password. After a few seconds the phone and the monitor show that it
   worked, with the monitor's address, e.g. `http://airmonitor-a1b2.local/`, and its IP address.
4. Tap **Done** on the phone (or press **D1** on the monitor). The hotspot closes and the phone goes back to its
   usual Wi-Fi, from where the address opens the monitor's web page.

Notes:

- Only 2.4 GHz networks work (an ESP32-S3 limitation).
- Up to five networks are saved (saving a sixth drops the oldest). The monitor joins the strongest one in range
  and keeps looking, slowly, when none is. More networks can be added from the web page or by running the setup
  again.
- Addresses ending in `.local` don't open on every device (some Android phones); use the QR code on the Wi-Fi page
  (D1) or the IP address.
- The setup hotspot has a random password, shown on the screen. It closes 2 minutes after a successful setup, after
  10 minutes without use, or after 30 minutes at most.

The web page shows the readings, trend charts and device status to anyone on your network. Changing settings,
maintenance and firmware updates need the **web password**, shown on the monitor's Wi-Fi page (the page asks for
it once per browser tab). A random one is created on first start; it can be changed or removed on the web page,
and a factory reset creates a new one. The connection is plain HTTP, as is usual for devices on a home network, so
don't reuse a valuable password.

![The web page: a card per value with its accuracy, and a trend chart of the last hour with the sensor's
tolerance as a shaded band](docs/web-page.png)

The web page is served by the monitor itself, so it also works on networks without Home Assistant, e.g. when you
take the monitor elsewhere.

## Home Assistant

The monitor can send its readings to [Home Assistant](https://www.home-assistant.io/) through an MQTT broker (a
small message server). Home Assistant then lists it as a device with a sensor for every value, a *status* sensor
(Good, Fair or Poor) for every value that has limits, the overall air quality and the Wi-Fi signal, without any
YAML (it uses Home Assistant's MQTT discovery).

![The monitor's device page in Home Assistant: device info with a link to the web page, and the sensors, each value
followed by its status](docs/home-assistant.png)

1. In Home Assistant, install and start the **Mosquitto broker** app from *Settings → Apps* (called *Add-ons*
   before Home Assistant 2026.2). If your Home Assistant runs in Docker ("Container") it has no apps: run a
   Mosquitto broker yourself and use its address below.
2. Under *Settings → Devices & services*, add the **MQTT** integration it discovers.
3. Create a user for the monitor: turn on *Advanced mode* in your profile, then *Settings → People → Users →
   Add user* (not an administrator). Mosquitto accepts Home Assistant users.
4. On the monitor's web page, open *Home Assistant*, tick *Send readings to Home Assistant* and enter the broker's
   address (your Home Assistant's, e.g. `homeassistant.local` or its IP address), port 1883, and that user name and
   password.

The monitor's Wi-Fi page and the web page show whether it is connected, or why not (e.g. "Wrong user name or
password"). Readings are sent every 30 s; if the monitor loses power, Home Assistant shows its sensors as
unavailable after about 90 s. Without the broker (e.g. on another network) the monitor works as usual and keeps
retrying in the background.

## Updating the firmware

Get the new version of the code (download it again and open the new folder in VS Code, or `git pull`) and build
it with the **✓** (Build) button. Settings, saved Wi-Fi networks and the web password are kept; the trend graphs
start over. Then either upload it over USB as in [Installing the firmware](#installing-the-firmware), or over
Wi-Fi: on the web page open *Firmware update* and choose

```text
sen69c-esp32-tft-air-monitor/.pio/build/feather_s3_reversetft/firmware.bin
```

(not `firmware.factory.bin`, which is only for USB). `.pio` is a hidden folder on macOS and Linux: in the file
chooser press Cmd+Shift+. (macOS) or Ctrl+H (Linux) to see it. Or, from a command line in the project folder
(in VS Code: *Terminal → New Terminal*), with the web password from the monitor's Wi-Fi page and your monitor's
address (any user name works; on Windows type `curl.exe`):

```sh
curl -u any:WEBPASSWORD --data-binary @.pio/build/feather_s3_reversetft/firmware.bin http://airmonitor-a1b2.local/api/ota
```

The monitor restarts into the new firmware, which confirms itself once it has rejoined Wi-Fi. If it crashes
first, or can't rejoin within 5 minutes, the monitor goes back to the previous firmware.

## Troubleshooting

| Problem | What to do |
|---|---|
| No ✓ / → buttons or pioarduino icon in VS Code | Install the *pioarduino IDE* extension (step 3 of [Installing the firmware](#installing-the-firmware)) and reload the window. |
| The upload can't find the board | Use a USB-C cable that carries data. Put the board in bootloader mode: hold **D0**, press and release **Reset**, release **D0**, upload again, then press **Reset**. On Linux, see the udev rules above. |
| Dark screen after uploading | Press **Reset**. |
| **No sensor** / **Sensor not found** | Push both cables in fully, at the sensor, the breakout and the Feather. |
| Values show `--` | The sensor is still starting; wait a minute. |
| Can't join Wi-Fi | Only 2.4 GHz networks work. The Wi-Fi page says why, e.g. "Couldn't join … Wrong password?"; run the setup again with the right password. |
| The `.local` address doesn't open | Use the IP address shown on the Wi-Fi page, or its QR code (D1). |
| Home Assistant doesn't connect | The Wi-Fi page shows the reason, e.g. "HA: Wrong user name or password" or "HA: Can't reach the broker"; check the settings on the web page. |
| Lost the web password | It is shown on the monitor's Wi-Fi page. |
| Start over | *Factory reset* in the settings. |

## For developers

### Command line

The extension's command-line tool `pio` is not on the system path. Open a terminal that has it from the pioarduino
sidebar (*Quick Access → Miscellaneous → PlatformIO Core CLI*), or run it directly (`~/.platformio/penv/bin/pio`; on Windows
`%USERPROFILE%\.platformio\penv\Scripts\pio.exe`):

```sh
pio run                 # build
pio run -t upload       # build and flash
pio device monitor      # log output over USB (115200 baud)
```

The web pages live in [web/](web/); a build step (`scripts/embed_web.py`) compresses them into the firmware. The
sensor is on I²C address `0x6B`.

### Running the tests

The hardware-independent logic (`lib/aqcore`: limits, history, buttons, comfort maths; `lib/netcore`: choosing
which Wi-Fi network to join) has Unity tests that run on the board. With native USB the serial port disappears
while the board restarts, so upload and test in two steps:

```sh
pio test --without-testing      # build and flash the test firmware
pio test --without-uploading    # read the results (within 15 s)
pio run -t upload               # put the normal firmware back
```

### JSON API and MQTT topics

The web page uses a small JSON API (`/api/state`, `/api/history?metric=co2&range=hour|day`, `/api/settings`,
...), listed at the top of [src/net/Web.h](src/net/Web.h). Metric names: `pm1 pm25 pm4 pm10 co2 hcho voc nox
temp rh`.

Over MQTT, with `<host>` the monitor's host name (e.g. `airmonitor-a1b2`): `<host>/state` carries all the values
as JSON, `<host>/status` is `online` or `offline`, and the discovery message is
`homeassistant/device/<host>/config`. See [src/net/MqttService.h](src/net/MqttService.h).

### Optional local settings

`platformio.ini` loads `platformio_override.ini` if it exists (git-ignored). Use it for machine-specific
settings, for example to keep the build output (about 250 MB) out of a cloud-synced folder:

```ini
[platformio]
workspace_dir = C:/path/outside/the/synced/folder
```

The firmware files (`firmware.bin`) are then under that folder instead of `.pio`. The tools themselves (about
3.6 GB) live in `~/.platformio` and are not affected.

## Disclaimer

This project is provided "as is", without warranty of any kind, express or implied, including the warranties of
merchantability and fitness for a particular purpose. You build and use it at your own risk. To the extent the law
allows, the author accepts no liability for any damage, injury or loss arising from it, including from the
hardware, a battery, or decisions based on its readings.

The monitor is not a medical, safety or certified measuring device. Its readings can be wrong (for example while
the sensor warms up, or if it fails), so don't rely on them where health or safety depends on it.

## License

Copyright © 2026 Francisco de los Ríos.

This program is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version. See [LICENSE](LICENSE).

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
more details.

Third-party components and their licenses are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
