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
`LOAD`, `SAVE`, and `DIR`), and a persistent terminal REPL. The M2 host slice
also defines portable storage, session-owned text/binary file channels, line
input, and asynchronous UI
request services. BASIC `ALERT`/`CONFIRM` calls suspend and resume individual
interpreter sessions through the async service. The Linux console presenter
supports alerts, confirmations, typed dialogs, and menus. BASIC exposes
session-owned dialog and menu builders through `DINIT`/`DIALOG` and
`MINIT`/`MENU`; [examples/ui.bas](examples/ui.bas) exercises typed results and
stable menu IDs. Embedded ESP32 line presenters are integrated. The embedded
REPL can also launch and manage worker sessions with `TASK`,
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
graphics surface. `GORIGINX()` and `GORIGINY()` report the default `#1` window's
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

