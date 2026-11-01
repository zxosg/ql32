# QL/32 — Platform Portability Requirements

**Document ID:** QL32-PLAT-001  
**Status (2026-11-01):** Both ESP32 firmware profiles and retained compositor adapters build with the custom 4 MiB single-app partition. CYD TFT, serial REPL, BASIC loop, SD text round-trip, compositor move/resize/clipping, full-screen allocation, all rotation behavior, and Wi-Fi station acceptance are hardware-verified. VGA32 display, PS/2, clock, SD, and Wi-Fi acceptance remains pending.

Supported build families are independent firmware targets:

| Profile | Display / input | Status |
| --- | --- | --- |
| `lilygo_vga32` | FabGL VGA, PS/2 | Firmware and host tests pass; VGA32 display, PS/2, clock, SD, and Wi-Fi checks pending |
| `esp32_2432s028` | ILI9341 TFT console/graphics, USB serial terminal input | Firmware and host tests pass; TFT, serial REPL, BASIC loop, SD text round-trip, compositor movement/clipping, full-screen resizing, all rotations, and Wi-Fi station behavior verified |
| `esp32s3_tft` | SPI/RGB TFT, USB HID/buttons/touch | Backlog |
| `esp32p4_tft` | RGB/MIPI display, USB HID/touch | Evaluation target |
| `linux` | Host adapter | Development and conformance host |

Platform capabilities provide resolution, pixel format (including three-bit
indexed color for the VGA32's eight-color mode), orientation, scaling,
available input, and enabled services. The shared core must compile without
FabGL, ESP-IDF LCD, LVGL, or board-header dependencies.

`ql32::platform::Capabilities` reports the board profile, optional display
geometry and format, available input kinds (including serial terminal input),
and supported service kinds. A profile must validate before the core uses it.
The descriptor does not assign
pin numbers or assume a specific resolution; those values belong to a
qualified board adapter. Linux remains the development and conformance host,
not a target requirement for firmware.

M5 targets LILYGO FabGL VGA32 V1.4. The image pins PlatformIO's
`espressif32@6.12.0` (Arduino-ESP32 2.0.17) and FabGL v1.0.9, and uses the
board's documented eight-color VGA, PS/2 keyboard, and SD-over-HSPI pin mapping.
The shared BASIC VM is linked with the portable graphics, storage, and channel
services. A PS/2 prompt accepts immediate statements and numbered program
lines; serial is the BASIC output stream. The firmware runs `startup.bas` from
the card root when present, otherwise a built-in BASIC demo. SD channel handles
are session-owned and paths are confined to the card root. Channel and storage
availability are included in the board capability descriptor. An absent card
reports `Unavailable` for service operations. The graphics adapter currently
permits one active display session. The custom 4 MiB partition reserves a
3.875 MiB single factory app and a 64 KiB coredump, with NVS and PHY init
partitions retained. The current VGA32 build uses 1,298,053 bytes of that app
(31.9%) and 52,424 bytes static RAM; `firmware.bin` is 1,304,624 bytes. Host
builds cover the adapters using fakes; the USB
serial REPL has been verified on the physical board. VGA output, PS/2 behavior,
clock status, and SD behavior still require verification. The async line
presenter for alert, confirmation, dialog, and menu requests is integrated. It
accepts replies via serial or PS/2 and is covered by Linux state-machine tests;
physical interaction still requires board verification. ESP32-S3 TFT
integration remains platform work.

The `esp32_2432s028` target applies the same BASIC graphics contract to the
common ESP32-2432S028R yellow-board variant using an ILI9341 TFT. Its USB serial
terminal is the line-oriented BASIC input device; BASIC `PRINT` uses a scrolling
TFT console strip, with optional serial mirroring for diagnostics. Coordinate-
based graphics draw independently on the same panel. The macOS PlatformIO
build on 2026-11-01 uses 1,236,497 bytes of the 3.875 MiB app partition (30.4%)
and 49,872 bytes static RAM; `firmware.bin` is 1,243,072 bytes. Both ESP32 profiles require 4 MiB flash, have no OTA or internal
SPIFFS/LittleFS partition, and store BASIC files on SD. The CYD profile has a
separate SPI bus configuration for the TFT and SD card. Hardware testing has
confirmed TFT output, serial input, BASIC `FOR`/`PRINT`, and an SD channel
round-trip with successful readback and EOF detection. The board
profile excludes touch until a specific unit's controller and calibration are
qualified; alternate CYD revisions may require different wiring. The CYD
rotation memory check is `examples/cyd-rotation-memory.bas`: the first cycle
reported 100456, 100176, 100172, 100172, and 100128 free bytes after 0°, 90°,
180°, 270°, and return to 0°. The next full cycle reported 100140 bytes at
every sample, indicating stable free heap after warm-up. A board-side
`WindowedService` diagnostic selected each supported canvas depth and sampled
the same `MemoryService` used by BASIC `FREE()` after each rotation:

| Canvas depth | 0° baseline | 90° | 180° | 270° | 0° return |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 bit | 154,332 | 154,332 | 154,332 | 154,332 | 154,332 |
| 4 bit | 125,532 | 125,532 | 125,532 | 125,532 | 125,532 |
| 8 bit | 87,132 | 87,132 | 87,132 | 87,132 | 87,132 |

All values are free heap bytes. The 28,800-byte drop from 1-bit to 4-bit and
38,400-byte drop from 4-bit to 8-bit match the additional storage for 76,800
pixels. The depth was selected through the platform-neutral C++ adapter API;
the BASIC language currently has no pixel-depth setter command. No rotation
produced a further heap change at any tested depth. The current CYD firmware's
opened-endpoint registry was also exercised: serial text and byte output,
null text/byte/graphics discard, type/count enumeration, and close/reopen all
worked.

