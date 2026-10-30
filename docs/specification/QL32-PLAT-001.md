# QL/32 — Platform Portability Requirements

**Document ID:** QL32-PLAT-001  
**Status:** M0 baseline

Supported build families are independent firmware targets:

| Profile | Display / input | Status |
| --- | --- | --- |
| `lilygo_vga32` | FabGL VGA, PS/2 | Primary target |
| `esp32s3_tft` | SPI/RGB TFT, USB HID/buttons/touch | Future target |
| `esp32p4_tft` | RGB/MIPI display, USB HID/touch | Evaluation target |
| `linux` | Host adapter | Development and conformance host |

Platform capabilities provide resolution, pixel format, orientation, scaling,
available input, and enabled services. The shared core must compile without
FabGL, ESP-IDF LCD, LVGL, or board-header dependencies.

