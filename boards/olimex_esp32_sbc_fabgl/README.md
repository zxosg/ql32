# Olimex ESP32-SBC-FabGL

This target supports Olimex ESP32-SBC-FabGL, schematic revision B. The board
PDF supplied for this project is kept locally at
`docs/datasheets/ESP32-SBC-FabGL.pdf`; it is not part of the public source
distribution. Its VGA and PS/2 pins used by QL/32 match the LILYGO adapter,
but its SD MISO pin is GPIO 35, not GPIO 2. Olimex mounts the external SD card
as `SDC:/` and a 512 KiB internal SPIFFS partition as `FFS:/`.

Build, flash, and monitor from the project root:

```sh
tools/build-target.sh olimex
pio run -e olimex_esp32_sbc_fabgl -t upload
pio run -e olimex_esp32_sbc_fabgl -t uploadfs
pio device monitor -b 115200
```

The board's SD SPI pins are MOSI GPIO 12, MISO GPIO 35, clock GPIO 14, and
chip select GPIO 13. PS/2 keyboard port 0 uses data GPIO 32 and clock GPIO 33; mouse port 1
uses data GPIO 27 and clock GPIO 26. The selected VGA color pins are red GPIO
21, green GPIO 18, blue GPIO 5, horizontal sync GPIO 23, and vertical sync
GPIO 15. These connections are shown on schematic page 6 of the board PDF.

The Olimex target mounts the external card and internal SPIFFS independently.
Use `SDC:/` for removable storage and `FFS:/` for internal flash. Startup
reports each mount separately. The 512 KiB SPIFFS partition is not
automatically reformatted if it fails to mount; provision its initial contents
with `uploadfs`.

The PS/2 keyboard provides execution shortcuts: `Pause` pauses/resumes the
focused task, `Shift+Pause` pauses/resumes the BASIC VM, `Ctrl+Pause` (also
`Ctrl+Space`) breaks the current program, and `Ctrl+Shift+Pause` stops workers
and returns to the supervisor. `Ctrl+Alt+Delete` restarts the board. At the
prompt, `REBOOT` performs the same software restart.

The VGA screen reserves its bottom three 8-pixel text rows for console channel
`#0`, including echoed interactive input. Its 6x8 font matches the default
character-cell size used by graphics channel `#1`; console text uses QL ink 4
(green). The default `#1` graphics window occupies the area above those rows;
output continues to mirror to USB serial.

## Hardware checks (2026-11-05)

The firmware was rebuilt and flashed to an Olimex Rev B board. A 320x200 VGA
capture received over YMODEM confirmed printable ASCII `0x20` through `0x7E`,
including the punctuation glyphs. With `PAGER ON`, `HELP` suspended for input;
`Q` returned the supervisor to its REPL prompt. Graphics UI input is shown on
screen and accepts keyboard or serial text. The 80x56 Mandelbrot demo completed
and was captured from the VGA output.

The Mandelbrot demo draws on the default graphics surface. On this board its
`WINDOW` resize statement raised `bad_alloc`, even with an 80x64 surface; the
demo avoids that resize. Physical PS/2 key interaction and shortcuts, clock,
and Wi-Fi still need board-level checks.
