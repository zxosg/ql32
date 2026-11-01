# QL/32

QL/32 is a portable, standalone BASIC computer inspired by SMSQ/E SuperBASIC
and Psion-style application interfaces. It is a native C++ implementation,
not a QL emulator.

The first hardware target is the LILYGO FabGL VGA32 (ESP32, VGA, PS/2), while
the core is designed to support independent TFT firmware builds on newer ESP32
chips. A Linux build is the primary development and test host.

## Repository map

- `PROJECT-HANDOFF.md` — durable milestone context and next steps for new chats.
- `docs/specification/` — requirements and evolving public contracts.
- `docs/reference/` — language manual links and provenance.
- `src/language/` — lexer, parser, semantic analysis, and bytecode compiler.
- `src/vm/` — bytecode execution and per-session VM state.
- `src/runtime/`, `src/system/`, `src/ui/` — portable services.
- `src/platform/` — Linux, FabGL VGA, and ESP LCD/TFT adapters.
- `boards/` — board-specific build profiles and pin/resource definitions.
- `tests/` — host-side language, VM, and integration tests.

## M1 status

The Linux CLI includes expressions, structured blocks, procedures, functions,
locals, arrays, `SELect ON`, numbered-line commands (`LIST`, `RUN`, `NEW`,
`LOAD`, `SAVE`, and `DIR`), and a persistent terminal REPL. The M2 host slice
also defines portable storage, session-owned text channels, and asynchronous UI
request services. BASIC `ALERT`/`CONFIRM` calls suspend and resume individual
interpreter sessions through the async service. The Linux console presenter
supports alerts, confirmations, typed dialogs, and menus. BASIC exposes
session-owned dialog and menu builders through `DINIT`/`DIALOG` and
`MINIT`/`MENU`; [examples/ui.bas](examples/ui.bas) exercises typed results and
stable menu IDs. Embedded ESP32 line presenters are integrated. See
[PROJECT-HANDOFF.md](PROJECT-HANDOFF.md).

`INCLUDE "relative/path.bas"` at the start of a source module expands a BASIC
library/header file before compilation. Included procedures and functions
become part of that module. For example, run
`./build/ql32 examples/include_demo.bas`; it includes
`examples/lib/arithmetic.bas` and prints `42`. This is BASIC source inclusion,
not C/C++ header or binary-library linking.

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The LILYGO VGA32 V1.4 PlatformIO environment pins Arduino-ESP32 2.0.17 and
FabGL v1.0.9, and builds a hardware bring-up image:

```bash
pio run -e lilygo_vga32
pio run -e lilygo_vga32 -t upload
```

This image initializes VGA/PS/2 and exercises the portable graphics service
through the FabGL adapter with text and primitive shapes. The pinned build has
been verified with PlatformIO Core 6.2.0, Arduino-ESP32 2.0.17, and FabGL
1.0.9. It still builds separately from the BASIC VM; physical-board testing is
required. The ESP32-S3 TFT environment remains a backlog placeholder.

For Linux graphics testing, run `./build/ql32 examples/graphics.bas`. The CLI
composites drawing into retained 4-bit windows and writes the latest screen to
`ql32-screen.ppm` in its current directory. Open the PPM image to inspect the
composed frame.
Unqualified `PRINT` and `PRINT #1` write text into the user window; `PRINT #0`
writes to the terminal. Use `./build/ql32 --console program.bas` to run a
program without graphics and send the default output to the terminal.

To see a retained canvas move after it has been drawn, run
`./build/ql32 examples/window-manager.bas`. The `WINDOW x,y,width,height`
command changes the current window's position and size; its drawing coordinates
remain relative to the window. `GORIGINX()` and `GORIGINY()` return its absolute
screen coordinates. The resulting composed frame is written to
`ql32-screen.ppm`.

In the interactive REPL, run `PRINT FREE()`, `CLOSEWINDOW`, then
`PRINT FREE()` to compare available memory before and after releasing the
current session's graphics canvas. A later graphics command recreates its
default window.

For a manual dialog and menu test, run `./build/ql32 examples/ui.bas`. Enter
values at the console prompts, accept or cancel the dialog, then select a menu
item by number or shortcut. The example prints the submitted values and stable
command ID.

