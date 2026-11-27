# Startup splash

`startup.bas` is the single, shared boot splash for every platform. It queries the window size,
font metrics and `SYSINFO$` at run time, scales the block-letter QL32 title to the width and
centres every line horizontally and the block vertically. A host test
(`ql32_graphics_tests`) runs it at 320x216, 320x200, 320x240, 400x300, 640x400 and 640x480.

## Install

| Board | Auto-run location |
| --- | --- |
| CYD ESP32-2432S028 (`esp32_2432s028_spiffs`) | `FFS:/startup.bas` (hardware-verified) |
| Olimex ESP32-SBC-FabGL | `SDC:/startup.bas` if the card is ready, else `FFS:/startup.bas` |
| LILYGO TTGO VGA32 | `SDC:/startup.bas` if the card is ready, else `FFS:/startup.bas` |
| Tenstar ESP32-S3 | `FFS:/startup.bas` |

Olimex, LILYGO and Tenstar are **not hardware-verified**; only the host layout test covers them.
Upload the same file on every board (for example `YMODEM` upload to the path above).
