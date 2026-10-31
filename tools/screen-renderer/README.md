# Screen renderer

Draws the air monitor's 240×135 screen on a PC and saves it as PNG files, so
you can see every screen without the hardware, check a change to the UI, or
redo the screenshots in the main README.

It runs the firmware's own UI code (`src/ui/Ui.cpp`, `UiDraw.cpp`,
`Text.cpp` and `lib/aqcore`) and the same LovyanGFX library. Layouts, fonts,
colours, levels, alerts and menus therefore come out as the device draws
them. A script sets what the rest of the device would report (readings,
Wi-Fi, Home Assistant, updates) and presses the buttons.

It's a tool for working on the firmware; you don't need it to build or use
the monitor.

## Building

The first build downloads LovyanGFX (about 50 MB, checked against a
checksum) and takes a minute or two. Run the build again after changing the
firmware's UI code.

**Linux** (and probably macOS, which hasn't been tried): you need a C++20
compiler (GCC 11 or later, or Clang 14 or later), CMake 3.20 or later and,
optionally, Ninja. On Debian or Ubuntu:

```sh
sudo apt install g++ cmake ninja-build
tools/screen-renderer/build.sh
```

It builds into `~/.cache/sen69c-air-monitor/screen-renderer`, or into the
folder given as an argument.

**Windows**: you need Visual Studio 2022 or later, with the **Desktop
development with C++** workload (MSVC, CMake and Ninja). Then, in PowerShell:

```powershell
tools\screen-renderer\build.ps1
```

It builds into `%LOCALAPPDATA%\sen69c-air-monitor\screen-renderer`, outside
the project folder.

Either way, to use a LovyanGFX already on disk instead of downloading it
(such as the one PlatformIO downloaded for the firmware), configure with
`-DLOVYANGFX_DIR=<path to LovyanGFX>`.

## Running

From `tools/screen-renderer`, with `r` the program you built:

```sh
r=~/.cache/sen69c-air-monitor/screen-renderer/screen-renderer
$r --scale 3 --out shots scripts/gallery.txt     # every screen
$r --scale 3 -e "air poor; page gases; shot gases.png"
$r --help                                        # all commands
```

On Windows, `$r = "$env:LOCALAPPDATA\sen69c-air-monitor\screen-renderer\screen-renderer.exe"`
and `& $r ...`.

Each script starts from a monitor that has been running for 30 hours, in good
air, on a home network, with Home Assistant off. Commands are one per line
or separated by `;`, for example:

```
air fair              # or good, poor, warm-up
set co2 1720          # one reading
page wifi             # presses D0/D2 until the Wi-Fi page shows
click d1              # the Wi-Fi page's action: the QR code
shot wifi-qr.png
menu altitude         # holds D1, then moves to Altitude
click d1
click d0              # +10 m
shot editing.png
```

The `page`, `menu`, `trend` and `dismiss` commands press the buttons like a
person would, and the firmware's own button logic sees every press. Time is
simulated, so `wait 10m` takes no real time. The gallery's 70 screens take
under a second, and come out the same on every system.

## How it works

- `shim/` replaces the Arduino and ESP-IDF headers the UI includes: `String`
  (formatting numbers exactly as Arduino-ESP32 does), the clock, the pins,
  and empty FreeRTOS, Wi-Fi, MQTT and OTA types.
- `shim/LovyanGFX.hpp` builds the real LovyanGFX with its desktop back end.
  `shim/SDL.h` defines just the few SDL names that back end's headers
  mention, so SDL isn't needed. The ESP32 SPI bus and backlight become
  stand-ins that discard what they receive.
- `src/World.h` holds the simulated device's state. `src/Fakes.cpp` hands it
  to the UI through the firmware's own classes (`WifiService`, `Ota`,
  `MqttService` and so on), so it needs updating when the UI starts using
  something new from them.
- `src/Simulator.cpp` runs the main loop every 10 ms of simulated time, and
  reads the frame from the UI's sprite. To know which page or menu item is
  showing, it reads private members of `Ui`. Only the explicit template
  instantiation at the end of that file names them, so if the firmware
  renames one, that is the one place to update.

What it doesn't show: the backlight level, the status LED, and anything the
readings don't decide. The trend graphs use made-up history that ends at the
current readings.
