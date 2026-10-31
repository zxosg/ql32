# LILYGO FabGL VGA32 profile

M5's first integration target is the LILYGO FabGL VGA32 V1.4, using Arduino-
ESP32 2.0.17 through PlatformIO `espressif32@6.12.0` and FabGL v1.0.9. The
firmware links the shared BASIC VM, VGA graphics adapter, PS/2 keyboard input,
ESP32 clock, SD text storage, and SD-backed text channels. The profile reports
a 320x200 landscape display with three-bit indexed color and PS/2 keyboard
input through `capabilities.hpp`.

Build and flash from the project root:

```sh
pio run -e lilygo_vga32
pio run -e lilygo_vga32 -t upload
pio device monitor -b 115200
```

The verified PlatformIO build uses Core 6.2.0, Arduino-ESP32 2.0.17, and FabGL
1.0.9; it uses 754,773 bytes flash and 30,392 bytes RAM. The USB serial REPL
has been verified on the physical board. VGA output, PS/2 input, clock status,
and SD behavior still need hardware checks. The serial monitor reports VGA32 readiness, uptime,
wall-clock synchronization, and SD mount status. BASIC output and runtime
errors go to serial; the VGA canvas is available to BASIC graphics commands.
The BASIC prompt accepts input from both the USB serial terminal and PS/2
keyboard. Connect at 115200 baud, type an immediate statement or numbered
program line, and press Enter. Backspace edits the current line. Enter `RUN`
to execute a numbered program. If input appears twice, disable local echo in
the serial terminal.

If the SD card contains `startup.bas` in its root, the firmware runs that file
at boot; otherwise it runs a built-in VM demo. `examples/vga32_startup.bas`
shows startup output and graphics. BASIC channel commands can read and write
files on the SD card using paths relative to its root. For example, enter these
commands at the prompt to test a channel round-trip:

```basic
c = OPEN("test.txt", "WRITE")
WRITE(c, "hello from ql32")
CLOSE(c)
c = OPEN("test.txt", "READ")
PRINT READ$(c)
PRINT EOF(c)
CLOSE(c)
```

The VGA mapping follows FabGL's documented VGA32 assignment. For the QL eight
color mode, it drives one bit per RGB channel on GPIO 21 (red), 18 (green), and
5 (blue), with sync on GPIO 23 and 15. PS/2 keyboard data/clock use GPIO 32/33;
mouse data/clock use GPIO 27/26. The board also exposes audio on GPIO 25. SD
uses HSPI with MOSI 12, MISO 2, clock 14, and chip select 13.

The graphics adapter supports one active graphics session on the VGA surface;
multi-session window composition and a managed dialog/menu presenter remain
future work. The exact physical unit revision, PSRAM behavior, and monitor
timing remain to be checked. FabGL is GPL-3.0; retain its license and review
distribution obligations before bundling it into a release image.
