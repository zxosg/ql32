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
`PRINT "hello on LCD"`, then `TEXT 8,8,"positioned graphics text"`. The first
line appears in the scrolling strip at the bottom; the second is drawn at the
requested coordinates. BASIC output is not echoed to serial unless the optional
mirror flag is enabled.

The CYD adapter presents a board-oriented 320x240 landscape window at rotation
0, even though Adafruit's ILI9341 driver reports its controller-native
240x320 dimensions at that index. QL/32 overrides the driver's drawing bounds
to match the CYD glass, so geometry queries, drawing clipping, and the scrolling
console use the same dimensions. Programs can query the active window:

```basic
ROTATE 0
PRINT GWIDTH(), GHEIGHT(), GDEPTH()
PRINT GPALETTESIZE(), GPALETTE$(2)
```

`ROTATE` accepts 0, 90, 180, or 270 degrees clockwise. On the CYD, 0 and 180
are landscape (320x240), while 90 and 270 are portrait (240x320). A rotation
clears the display and recalculates the scrolling console layout. This
board-specific geometry follows the [CYD setup reference](https://blog.decryption.net.au/posts/cyd-for-beginners.html),
which configures the ILI9341 as 320x240 at rotation 0. Rotation 0 and its
320x240 window query have been confirmed on hardware. The CYD adapter clears
the ILI9341 BGR-order flag; the user confirmed the red palette entry now
renders red on this panel.

The async alert, confirmation, dialog, and menu presenter prints prompts to the
TFT console and accepts each response through serial input. Use
`examples/embedded_ui.bas` on the card as `startup.bas` to exercise the full
flow. Replies are entered one line at a time; menus accept item numbers or
shortcuts, choices accept their numbered position, and dialogs accept blank
input to keep a field's current value.

M6 BASIC network commands are `GETNET ssid$, password$` and `DISNET`; status
functions are `NETSTAT$()`, `NETADDR$()`, and `NETSIGNAL()`. The connection starts
asynchronously. `examples/network.bas` shows the calls with placeholders; enter
real credentials only in a local, unsaved session because the serial REPL echoes
the command line. Wi-Fi hardware verification is pending.

`INCLUDE "library.bas"` at the top of a program expands BASIC library source
from the SD card before compilation. Nested includes resolve relative to the
including file. The include and all paths it references must stay on the SD
root; C/C++ headers and compiled object libraries are not loaded by this BASIC
directive.

The profile uses the common CYD wiring: TFT SPI on GPIO 14/12/13 with CS 15,
DC 2, backlight 21; SD uses a separate SPI bus on GPIO 18/19/23 with CS 5.
Boards sold under the same family name can differ. Check that the module is the
2.8-inch ESP32-WROOM / ILI9341 variant before flashing; touch-controller wiring
and alternate revisions are not used by this target.

Graphics use retained indexed canvases and a compositor. New windows default
to 4-bit pixels, and the CYD allows two full-screen canvases within its configured
canvas budget. The composed frame occupies one byte per screen pixel in addition
to canvas storage.

The ESP32 image builds with PlatformIO `espressif32@6.12.0`
(Arduino-ESP32 2.0.17), Adafruit ILI9341, and Adafruit GFX. It uses the shared
[4 MiB single-app partition table](../esp32_4mb_single_app.csv): 3.875 MiB for
firmware, NVS, and a 64 KiB coredump area. It requires a 4 MiB flash chip.
There is no OTA slot or internal SPIFFS/LittleFS partition; BASIC files use SD.
Current build-size and RAM measurements are kept in
[QL32-PLAT-001](../../docs/specification/QL32-PLAT-001.md). The user has verified TFT
output, the serial REPL, and a small BASIC `FOR` loop printing to the LCD on
the board. The async line presenter for alerts, confirmations, dialogs, and
menus is compiled into the image. The SD channel round-trip is verified on
hardware: written text read back successfully and `EOF` returned `1`. Define
`QL32_TFT_CONSOLE_SERIAL_MIRROR` in this environment's `build_flags` to mirror
BASIC console output to serial.
