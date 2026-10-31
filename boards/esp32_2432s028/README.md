# ESP32-2432S028 yellow TFT profile

This target supports the common yellow-PCB ESP32-2432S028R / CYD variant:
ESP32-WROOM-32, ILI9341 320x240 TFT, and serial terminal input. The display
implements the same QL/32 graphics service as the VGA32 target. The USB serial
terminal at 115200 baud acts as the BASIC keyboard. BASIC `PRINT` output goes
to a scrolling console strip at the bottom of the TFT; graphics commands draw
on the same panel using their own coordinates. Touch input is not enabled.

Build and flash from the repository root:

```sh
pio run -e esp32_2432s028
pio run -e esp32_2432s028 -t upload --upload-port /dev/ttyUSB0
pio device monitor -p /dev/ttyUSB0 -b 115200
```

At the serial `>` prompt, enter BASIC statements or numbered program lines. Use
`RUN` to execute a numbered program. Backspace edits the current line. If the
SD card contains `startup.bas` in its root, it runs at boot; otherwise the
firmware runs a built-in demo. `examples/cyd_startup.bas` demonstrates text,
lines, rectangles, and circles on the TFT. BASIC channel paths are relative to
the SD root.

To check that text and graphics use separate output paths, enter
`PRINT "hello on LCD"`, then `TEXT(8,8,"positioned graphics text")`. The first
line appears in the scrolling strip at the bottom; the second is drawn at the
requested coordinates. BASIC output is not echoed to serial unless the optional
mirror flag is enabled.

The profile uses the common CYD wiring: TFT SPI on GPIO 14/12/13 with CS 15,
DC 2, backlight 21; SD uses a separate SPI bus on GPIO 18/19/23 with CS 5.
Boards sold under the same family name can differ. Check that the module is the
2.8-inch ESP32-WROOM / ILI9341 variant before flashing; touch-controller wiring
and alternate revisions are not used by this target.

The ESP32 image builds with PlatformIO `espressif32@6.12.0`
(Arduino-ESP32 2.0.17), Adafruit ILI9341, and Adafruit GFX. The current image
uses 690,337 bytes flash and 27,856 bytes RAM. The user has verified TFT
output, the serial REPL, and a small BASIC `FOR` loop printing to the LCD on
the board. SD-card behavior remains to be checked. Define
`QL32_TFT_CONSOLE_SERIAL_MIRROR` in this environment's `build_flags` to mirror
BASIC console output to serial.
