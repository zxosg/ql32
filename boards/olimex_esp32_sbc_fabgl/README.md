# Olimex ESP32-SBC-FabGL

This target supports Olimex ESP32-SBC-FabGL, schematic revision B. The board
PDF supplied for this project is kept locally at
`docs/datasheets/ESP32-SBC-FabGL.pdf`; it is not part of the public source
distribution. Its VGA and PS/2 pins used by QL/32 match the LILYGO adapter,
but its SD MISO pin is GPIO 35, not GPIO 2. The Olimex profile uses the
internal 1 MiB SPIFFS partition as `FFS`; no SD card is required.

Build, flash, and monitor from the project root:

```sh
tools/build-target.sh olimex
pio run -e olimex_esp32_sbc_fabgl -t upload
pio device monitor -b 115200
```

The board's SD SPI pins are MOSI GPIO 12, MISO GPIO 35, clock GPIO 14, and
chip select GPIO 13. PS/2 keyboard port 0 uses data GPIO 32 and clock GPIO 33; mouse port 1
uses data GPIO 27 and clock GPIO 26. The selected VGA color pins are red GPIO
21, green GPIO 18, blue GPIO 5, horizontal sync GPIO 23, and vertical sync
GPIO 15. These connections are shown on schematic page 6 of the board PDF.

The Olimex target mounts internal SPIFFS as `FFS`. Startup prints `FFS storage
ready` when the partition is available. The partition is 1 MiB and is not
automatically reformatted if it fails to mount.

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
