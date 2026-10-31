# QL/32 — Platform Portability Requirements

**Document ID:** QL32-PLAT-001  
**Status:** Portable capability descriptor and LILYGO VGA32 V1.4 profile implemented; pinned FabGL graphics adapter added and firmware build verified; physical hardware verification pending

Supported build families are independent firmware targets:

| Profile | Display / input | Status |
| --- | --- | --- |
| `lilygo_vga32` | FabGL VGA, PS/2 | First integration target; bring-up image compiles successfully |
| `esp32s3_tft` | SPI/RGB TFT, USB HID/buttons/touch | Backlog |
| `esp32p4_tft` | RGB/MIPI display, USB HID/touch | Evaluation target |
| `linux` | Host adapter | Development and conformance host |

Platform capabilities provide resolution, pixel format (including three-bit
indexed color for the VGA32's eight-color mode), orientation, scaling,
available input, and enabled services. The shared core must compile without
FabGL, ESP-IDF LCD, LVGL, or board-header dependencies.

`ql32::platform::Capabilities` reports the board profile, optional display
geometry and format, available input kinds, and supported service kinds. A
profile must validate before the core uses it. The descriptor does not assign
pin numbers or assume a specific resolution; those values belong to a
qualified board adapter. Linux remains the development and conformance host,
not a target requirement for firmware.

M5 targets LILYGO FabGL VGA32 V1.4. The bring-up image pins PlatformIO's
`espressif32@6.12.0` (Arduino-ESP32 2.0.17) and FabGL v1.0.9, and uses the
board's documented eight-color VGA and PS/2 keyboard pin mapping. The bring-up
screen uses the portable graphics service through a FabGL canvas adapter; the
shared BASIC VM and input/storage services are not linked into this firmware
image yet. The graphics adapter currently permits one active display session.
The pinned firmware build succeeds with PlatformIO Core 6.2.0, Arduino-ESP32
2.0.17, and FabGL 1.0.9. Physical VGA output and keyboard behavior still
require verification on the board. ESP32-S3 TFT integration remains in the
platform backlog.

