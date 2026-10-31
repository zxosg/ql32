# LILYGO FabGL VGA32 profile

M5 first integration target: LILYGO FabGL VGA32 V1.4, using Arduino-ESP32
2.0.17 through PlatformIO `espressif32@6.12.0` and FabGL v1.0.9. The
PlatformIO environment builds a VGA/PS/2 hardware image from
`src/platform/fabgl_vga/main.cpp`; the bring-up screen uses the portable
graphics service through `FabglGraphics`, but the image does not yet build the
portable BASIC VM.
The profile reports a 320x200 landscape display with three-bit indexed color
and PS/2 keyboard input through `capabilities.hpp`.

Build and flash from the project root:

```sh
pio run -e lilygo_vga32
pio run -e lilygo_vga32 -t upload
pio device monitor -b 115200
```

The pinned firmware build has been verified with PlatformIO Core 6.2.0,
Arduino-ESP32 2.0.17, and FabGL 1.0.9 (381,737 bytes flash and 24,040 bytes
RAM). The monitor should print `QL/32 VGA32 V1.4 bring-up ready`. The VGA
display should show the title, a diagonal line, an outlined rectangle, and a
circle; those physical outputs still need to be checked on the board.

The VGA mapping follows FabGL's documented VGA32 assignment. For the QL eight
color mode, it drives one bit per RGB channel on GPIO 21 (red), 18 (green), and
5 (blue), with sync on GPIO 23 and 15. The VGA32 V1.4's remaining DAC pins are
not used. PS/2 keyboard data/clock use GPIO 32/33; mouse data/clock use GPIO
27/26. The board also exposes audio on GPIO 25 and SD over HSPI (MOSI 12,
MISO 2, clock 14, chip select 13); those services are not initialized by this
bring-up image.

The adapter supports one active graphics session on the VGA surface; multi-
session window composition and managed UI presenters remain future work. The
exact physical unit revision, PSRAM behavior, and monitor timing still require
a run on the user's board. FabGL is GPL-3.0; retain its license and review
distribution obligations before bundling it into a release image.

