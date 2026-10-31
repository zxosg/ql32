# QL/32 — Platform Portability Requirements

**Document ID:** QL32-PLAT-001  
**Status:** Portable capability descriptor and LILYGO VGA32 V1.4 profile implemented; VGA32 firmware links the BASIC VM, graphics, PS/2, clock, SD storage, and SD channels; physical hardware verification pending

Supported build families are independent firmware targets:

| Profile | Display / input | Status |
| --- | --- | --- |
| `lilygo_vga32` | FabGL VGA, PS/2 | First integration target; bring-up image compiles successfully |
| `esp32_2432s028` | ILI9341 TFT console/graphics, USB serial terminal input | Firmware and host tests pass; TFT console, serial REPL, and BASIC loop verified on hardware; SD check pending |
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
permits one active display session. The current build succeeds at 754,773 bytes
flash and 30,392 bytes RAM. Host tests cover the adapters using fakes; the USB
serial REPL has been verified on the physical board. VGA output, PS/2 behavior,
clock status, and SD behavior still require verification. The dialog/menu presenter and ESP32-S3 TFT
integration remain platform work.

The `esp32_2432s028` target applies the same BASIC graphics contract to the
common ESP32-2432S028R yellow-board variant using an ILI9341 TFT. Its USB serial
terminal is the line-oriented BASIC input device; BASIC `PRINT` uses a scrolling
TFT console strip, with optional serial mirroring for diagnostics. Coordinate-
based graphics draw independently on the same panel. Its current firmware
build succeeds at 690,337 bytes flash and 27,856 bytes RAM. It has a
separate SPI bus configuration for the TFT and SD card. Hardware testing has
confirmed TFT output, serial input, and BASIC `FOR`/`PRINT`; SD behavior remains
to be checked. The board
profile excludes touch until a specific unit's controller and calibration are
qualified; alternate CYD revisions may require different wiring.

