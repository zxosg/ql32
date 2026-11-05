# QL/32

QL/32 is a portable, standalone BASIC computer inspired by SMSQ/E SuperBASIC
and Psion-style application interfaces. It is a native C++ implementation,
not a QL emulator.

ESP32 hardware targets include the LILYGO FabGL VGA32, Olimex ESP32-SBC-FabGL,
and ESP32-2432S028 TFT board. Linux and macOS are host development and test
targets.

## Repository map

- `PROJECT-HANDOFF.md` — durable milestone context and next steps for new chats.
- `docs/specification/` — requirements and evolving public contracts.
- `docs/reference/` — language manual links and provenance.
- `src/language/` — lexer, parser, semantic analysis, and bytecode compiler.
- `src/vm/` — bytecode execution and per-session VM state.
- `include/ql32/runtime/`, `include/ql32/ui/` — portable service contracts.
- `src/runtime/`, `src/graphics/`, `src/vm/` — portable service and VM
  implementations.
- `src/platform/` — host, FabGL VGA, and ESP LCD/TFT adapters.
- `boards/` — board-specific build profiles and pin/resource definitions.
- `tests/` — host-side language, VM, and integration tests.

## Implemented software

The host CLI includes expressions, structured blocks, procedures, functions,
locals, arrays, `SELect ON`, numbered-line commands (`LIST`, `RUN`, `NEW`,
`LOAD`, `SAVE`, `DIR`, `CD`, `DELETE`, `RENAME`, and `FORMAT`), and a persistent
terminal REPL. `CD` maintains a per-session current directory; file commands
accept relative paths, including `.` and `..`. `FORMAT` recursively clears a
selected directory, including a drive root. The M2 host slice
also defines portable storage, session-owned text/binary file channels, line
input, and asynchronous UI
request services. BASIC `ALERT`/`CONFIRM` calls suspend and resume individual
interpreter sessions through the async service. Linux interactive sessions
render graphical controls to `ql32-screen.ppm` and accept terminal responses;
scripted runs retain the console presenter. BASIC exposes
session-owned dialog and menu builders through `DINIT`/`DIALOG` and
`MINIT`/`MENU`; [examples/ui.bas](examples/ui.bas) exercises typed results and
stable menu IDs. CYD and VGA render graphical controls over the console strip,
using serial and PS/2 input respectively. The panels share the console bitmap
font, with bold white titles and a two-tone frame visible over dark or light
backgrounds. The embedded REPL can also launch and manage worker sessions with
`TASK`,
`TASKS`, `TASKRESULT`, `TASKCHANNELS`, `LOGON`, `LOGOFF`, `KILL`, `SUSPEND`,
and `RESUME`. Use `TASK "worker.bas"` to start asynchronously, or add
`KTaskWait` to wait for completion. `TASKRESULT id` prints the available task
properties; `TASKRESULT(id, selector)` returns one selected property. `ERR`,
`ERR$(code)`, and `ERRX$` expose the current QL/32 error code, message, and
contextual detail.
The supervisor can set a global bytecode slice with `SLICE n` and per-task
scheduling weights with `PRIORITY id,weight`; smooth weighted scheduling gives
higher weights more turns. `SUSPEND id` saves the current weight, while
`RESUME id` restores it. `PRIORITY id,0` directly sets a zero baseline and
clears the saved resume weight. Each session owns
its BASIC state and channel handles while sharing the board's physical services.
The [embedded supervisor demo](examples/supervisor/README.md) shows three
workers in side-by-side display windows. See
[PROJECT-HANDOFF.md](PROJECT-HANDOFF.md).

### Debugger

Use the supervisor REPL to attach to a running task and control it:

```text
DEBUG 2
BREAK 100
BREAKPOINTS
CLEARBREAK 100
CONTINUE
WHERE
STEP
NEXT
LOCALS
GLOBALS
STACK
EVAL A+B*2
```

`DEBUG id` attaches to and pauses the selected task immediately. Set breakpoints
while paused, then use `CONTINUE` to run. Breakpoints use BASIC line numbers in
numbered programs and physical source lines in unnumbered programs;
`BREAKPOINTS` lists them and `CLEARBREAK line` removes one. `STEP` advances one
BASIC statement and enters procedures; `NEXT` runs through calls made by the
current statement. `WATCH expr`
adds a safe expression watch, `WATCHES` displays its value while stopped, and
`UNWATCH expr` removes it. `BREAKPOINTS` lists active breakpoints. `LOCALS` and
`GLOBALS` also show array upper bounds, while `STACK` shows each call's source
location. `DIAGNOSTICS ON` enables per-task bytecode, user-call, and evaluated IF
counters; `DIAGNOSTICS RESET` clears the counters and trace without disabling
diagnostics. Enable diagnostics before resuming if you want counts from that
point onward. `TRACE ON` also keeps a bounded recent instruction trace. `STATS`
displays counters and available free-memory information. Debug
EVAL and watches allow literals, variable and array reads, and pure operators;
function calls and writes are rejected. A debug-paused task keeps its VM state
and does not consume scheduler instruction budget.

### Math precision and ESP32 CPU frequency

Programs default to DOUBLE precision. Include `ql32.inc` and put module
options before definitions and executable statements to select math precision
or, on supported ESP32 boards, the device-wide CPU frequency:

```basic
INCLUDE "ql32.inc"
OPTION KOptMathSingle
OPTION KEspCpuFreq240
```

Use `KOptMathDouble` to select the default binary64 calculations, or
`KEspCpuFreq80` / `KEspCpuFreq160` for a lower ESP32 clock. Each option
category may appear once per module. The CPU setting applies to the whole board
when the module starts; the latest started module with a CPU option controls
the clock for all tasks. Linux and macOS accept the ESP32 option and ignore it.
SINGLE changes rounding and arithmetic precision, not the VM value storage
size. See the [language specification](docs/specification/QL32-LANG-001.md)
and the [attractor example](examples/attractor-README.md).

`INCLUDE "relative/path.bas"` at the start of a source module expands a BASIC
library/header file before compilation. Included procedures and functions
become part of that module. For example, run
`./build/linux/ql32 examples/include_demo.bas` (or
`./build/macos/ql32 examples/include_demo.bas`); it includes
`examples/lib/arithmetic.bas` and prints `42`. This is BASIC source inclusion,
not C/C++ header or binary-library linking.

### `PRINT` and `WRITE`

Both commands can write a newline-terminated text line to an open channel.
`WRITE` takes one channel handle and one string. `PRINT #channel` is more
flexible: it can format multiple items with separators and normally adds a
final line feed. A trailing backslash (`\`) emits a line feed explicitly; a
trailing semicolon suppresses the automatic final line feed.

```basic
file = OPEN("messages.txt", "WRITE")
WRITE file, "Written with WRITE"
PRINT #file, "Written with PRINT"\
CLOSE file
```

The file contains:

```text
Written with WRITE
Written with PRINT
```

## Build

Build exactly one named target with the helper:

```bash
tools/build-target.sh linux
tools/build-target.sh mac
tools/build-target.sh cyd
tools/build-target.sh vga32
tools/build-target.sh olimex
```

The host executables are `build/linux/ql32` and `build/macos/ql32`. Run the
matching host test suite with `ctest --test-dir build/linux --output-on-failure`
or `ctest --test-dir build/macos --output-on-failure`.

PlatformIO's plain `pio run` builds all supported ESP32 profiles. Select one
environment explicitly when uploading:

```bash
pio run -e lilygo_vga32 -t upload
pio run -e olimex_esp32_sbc_fabgl -t upload
pio run -e esp32_2432s028 -t upload
```

### Serial file transfer

The monitor accepts `RECV "SDC:/path"` and `SEND "SDC:/path"` (use
`FFS:/path` on the SPIFFS profile). Install the utility dependency with
`python3 -m pip install pyserial`. The utility can enter the monitor command
for you and uses the same serial port and baud rate as the REPL:

```bash
python3 tools/ql32-ymodem.py send /dev/cu.usbserial-XXXX 115200 local.bin --device-path "SDC:/remote.bin"
python3 tools/ql32-ymodem.py receive /dev/cu.usbserial-XXXX 115200 local.bin --device-path "SDC:/remote.bin"
python3 tools/ql32-ymodem.py capture /dev/cu.usbserial-XXXX 115200 screen.bmp
```

`send` uploads the local file; `receive` downloads the device file. Without
`--device-path`, start `RECV` or `SEND` from an already-open monitor first.
Transfers use CRC-16 and 1 KiB YMODEM data packets. A failed receive removes
its partial file and restores any previous destination from `.BAK`.
On CYD, `capture` sends an indexed BMP snapshot of the retained graphics compositor and
text console without writing it to board storage. `SCREEN` is also available as
a monitor command. Capture pauses BASIC scheduling while the image transfers.
The image is reconstructed a row at a time, so it does not allocate a full
screen buffer.

### PC remote debugger

The Python desktop client supports the ESP32-2432S028 CYD and Olimex
ESP32-SBC-FabGL. Install its dependencies and launch it from the repository root:

```bash
python3 -m pip install -r tools/ql32_remote_debugger/requirements.txt
python3 -m tools.ql32_remote_debugger
```

Connect to the QL/32 USB serial port at 115200 baud. The Files tab browses the
device roots reported by the board, including `FFS:/` on SPIFFS builds. Upload,
download, and screen capture share one serial owner and use the monitor's
YMODEM flow; BASIC scheduling pauses for the duration of each transfer. The
Debugger tab can download a selected source file and launch it paused, or attach
to an existing task after selecting the source file to display. Click the
editor gutter to toggle a breakpoint, then use Continue, Step, Next, and Pause.
The panels show source position, locals, globals, arrays, watches, stack, and
opt-in execution counters. EVAL and watches accept only side-effect-free
expressions. Screen captures are on demand and can be saved as BMP files.

The client deliberately reports other boards, including LILYGO VGA32, as
unsupported. Firmware protocol requests use the versioned `@QL32DBG/1` serial
frame; the existing monitor REPL and YMODEM commands remain available.

On embedded targets, enter `REBOOT` at the interactive supervisor prompt to
perform a software restart. The Olimex FabGL target also supports PS/2 execution
shortcuts; see its [board notes](boards/olimex_esp32_sbc_fabgl/README.md).

The `lilygo_vga32` and `olimex_esp32_sbc_fabgl` images include the shared
portable BASIC VM and FabGL adapters. Their VGA display, PS/2, clock, SD, and
line-presenter behavior still needs physical-board verification. The Olimex
profile uses SD MISO GPIO 35; the LILYGO profile uses GPIO 2. The CYD's TFT,
serial REPL, BASIC loop, SD text round-trip, retained-compositor
movement/resize/clipping, full-screen canvas,
and all four rotations have been checked on hardware. CYD Wi-Fi association,
status, address, signal, and disconnect are also verified; VGA32 Wi-Fi still
needs a physical check. [`examples/cyd-rotation-memory.bas`](examples/cyd-rotation-memory.bas)
prints free heap after each rotation. The pinned
VGA32 build uses PlatformIO Core 6.2.0, Arduino-ESP32 2.0.17, and FabGL v1.0.9.
The ESP32-S3 TFT profile remains backlog work.

For host graphics testing, run `./build/linux/ql32 examples/graphics.bas` or
`./build/macos/ql32 examples/graphics.bas`. The CLI
composites drawing into retained 4-bit windows and writes the latest screen to
`ql32-screen.ppm` in its current directory. Open the PPM image to inspect the
composed frame.
Unqualified `PRINT` and `PRINT #1` write text into the user window; `PRINT #0`
writes to the terminal. Use `./build/linux/ql32 --console program.bas` or
`./build/macos/ql32 --console program.bas` to run a program without graphics
and send the default output to the terminal.

To see a retained canvas move after it has been drawn, run
`./build/linux/ql32 examples/window-manager.bas` or
`./build/macos/ql32 examples/window-manager.bas`. `WINDOW x,y,width,height`
changes the current default window (initially `#1`).
`WINDOW #channel,x,y,width,height` selects a window by channel, including the
console-associated `#0` graphics surface.
Drawing coordinates remain relative to the selected window. `PRINT #0` still
writes to the console stream; drawing commands such as `LINE #0,...` target its
graphics surface. `CLS` clears the default graphics channel; `CLS #channel`
clears a selected channel. `CLS ink,paper` sets both colors and clears the
default channel, while `CLS #channel,ink,paper` does the same on a selected
channel. The chosen colors remain active for later drawing. `GORIGINX()` and
`GORIGINY()` report the default `#1` window's
absolute screen coordinates. The resulting composed frame is written to
`ql32-screen.ppm`.

Start the interactive REPL with `./build/linux/ql32` or
`./build/macos/ql32`. Enter `CHANNEL #0` to send unqualified output to the
terminal, then run `PRINT FREE()`, `CLOSEWINDOW`, and `PRINT FREE()` to compare
available memory before and after releasing the session's graphics canvas. A
later graphics command recreates its default window. Press Ctrl-D to exit.

For a manual dialog and menu test, run
`./build/linux/ql32 examples/ui.bas` or `./build/macos/ql32 examples/ui.bas`. Enter
values at the console prompts, accept or cancel the dialog, then select a menu
item by number or shortcut. The example prints the submitted values and stable
command ID.

For an automated CYD serial-REPL memory acceptance run, build and flash the
`esp32_2432s028_spiffs` profile, install `pyserial`, then run
`python3 tools/ql32-memory-test.py /dev/cu.usbserial-XXXX`. The runner uploads
its BASIC workers over YMODEM and writes relative heap measurements to
`memory-test-report.json`. See [examples/memory-tests](examples/memory-tests/README.md)
for the cases and pass criteria.

