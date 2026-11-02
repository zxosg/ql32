# Olimex ESP32-SBC-FabGL

This target supports Olimex ESP32-SBC-FabGL, schematic revision B. The board
PDF supplied for this project is kept locally at
`docs/datasheets/ESP32-SBC-FabGL.pdf`; it is not part of the public source
distribution. Its VGA and PS/2 pins used by QL/32 match the LILYGO adapter,
but its SD MISO pin is GPIO 35, not GPIO 2.

Build, flash, and monitor from the project root:

```sh
tools/build-target.sh olimex
pio run -e olimex_esp32_sbc_fabgl -t upload
pio device monitor -b 115200
```

The SD SPI pins are MOSI GPIO 12, MISO GPIO 35, clock GPIO 14, and chip select
GPIO 13. PS/2 keyboard port 0 uses data GPIO 32 and clock GPIO 33; mouse port 1
uses data GPIO 27 and clock GPIO 26. The selected VGA color pins are red GPIO
21, green GPIO 18, blue GPIO 5, horizontal sync GPIO 23, and vertical sync
GPIO 15. These connections are shown on schematic page 6 of the board PDF.

The Olimex target mounts SD through ESP-IDF's native SPI2/VFS SD-SPI driver,
using the same approach as ESPectrum. Startup prints either `SD storage ready`
or an ESP-IDF diagnostic naming the failed step. The configured FatFs build has
exFAT disabled, so use FAT12/16/32 media; test with a spare FAT32 card before
reformatting a card containing data.

The PS/2 keyboard provides execution shortcuts: `Pause` pauses/resumes the
focused task, `Shift+Pause` pauses/resumes the BASIC VM, `Ctrl+Pause` (also
`Ctrl+Space`) breaks the current program, and `Ctrl+Shift+Pause` stops workers
and returns to the supervisor. `Ctrl+Alt+Delete` restarts the board. At the
prompt, `REBOOT` performs the same software restart.

The VGA screen reserves its bottom two 8-pixel text rows for console channel
`#0`, including echoed interactive input. The default `#1` graphics window
occupies the area above those rows; output continues to mirror to USB serial.
