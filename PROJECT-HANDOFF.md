# QL/32 project handoff

Use this file to continue work across Codex chats. Read it together with
[`AGENTS.md`](AGENTS.md) before changing the project. GitHub `main` and the
specifications are the project record; refresh this handoff after meaningful
implementation or hardware-acceptance changes.

## Project and architecture

QL/32 is a portable, native BASIC computer inspired by SMSQ/E SuperBASIC and
Psion-style application interfaces. Linux and macOS are host targets. ESP32
targets include LILYGO FabGL VGA32, Olimex ESP32-SBC-FabGL, ESP32-2432S028
(CYD), and the TENSTAR TS-ESP32-S3. Keep the language,
VM, runtime, system, and UI layers independent of board libraries. Session
resources belong to their BASIC session, and UI waits suspend only that
session.

## Repository state reviewed

Reviewed 2026-11-06: `main` is at [`f1fadad`](https://github.com/zxosg/ql32/commit/f1fadad)
and matches `origin/main`. The working tree contains uncommitted TENSTAR target,
graphics/console adapter, test, SPIFFS, monitor-transfer, README, and
specification changes. Inspect `git status` before staging or committing; do
not discard these changes. Math precision and ESP32 CPU frequency options are
committed.

The latest implementation includes:

- M1 BASIC core: expressions, variables, structured `IF`, flexible block and
  inline `FOR`, `REPeat`, procedures/functions/locals, arrays, block
  `SELect ON`, abbreviations and canonical `LIST`; numbered programs and a
  persistent host REPL.
- Host storage and session-owned channels, `LOAD`/`SAVE`/`DIR`, `OPEN`,
  line and exact-byte file I/O, and asynchronous UI requests. `ALERT`,
  `CONFIRM`, dialogs and menus suspend/resume only their calling session.
  Optional `PAGER ON` pagination covers generated listings; graphics adapters
  show the input prompt on screen. Submit an empty line, `L`, or `Q` to advance,
  step, or stop the listing.
- Retained indexed graphics windows, `PLOT`, filled/outline primitives,
  window movement/resizing, CYD rotation
  and palette queries, Linux PPM output, and CYD/VGA32 graphics adapters.
  `WINDOW #channel,x,y,w,h` now selects independent per-session channel
  surfaces, including a graphics surface for console channel `#0`; `PRINT #0`
  remains console output. CYD and FabGL console overlays follow `#0` bounds.
  `CLS` accepts no arguments, `#channel`, `ink,paper`, or
  `#channel,ink,paper`; color arguments persist on that graphics surface.
  Linux and all maintained ESP32 profiles build. The classic glyph set covers
  printable ASCII `0x20`–`0x7E`, verified by VGA capture. CYD users have
  confirmed compositor movement, resizing/clipping and rotations; the four
  `CLS` forms ran without syntax or VM errors, though overlapping windows
  prevented visual isolation of clear colors. Visual confirmation of the latest
  `PLOT` attractor remains open.
- TENSTAR TS-ESP32-S3 target with ST7789 240×135 landscape graphics, USB CDC
  REPL, 4 MiB flash layout, QSPI PSRAM, and 512 KiB SPIFFS. `startup.bas`,
  graphics/memory tests, three continuous worker programs, and a Mandelbrot
  workload are uploaded from `data/boards/ql32_tenstar_esp32s3/`. Sensors are
  intentionally deferred.
- Cooperative weighted session scheduling and an embedded persistent REPL.
  Current supervisor/BASIC task APIs use `TASK`, `KILL`, `TASKRESULT`,
  `TASKCHANNELS`, `LOGON`, `LOGOFF`, `SUSPEND`, `RESUME`, `SLICE`,
  and `PRIORITY`. `TASK` has command and function forms, optional
  `KTaskWait`, and task status/error queries `ERR`, `ERR$(code)`,
  `ERRX$`.
- M6 Wi-Fi station calls: `GETNET`, `DISNET`, `NETSTAT$()`,
  `NETADDR$()`, and `NETSIGNAL()`; recursive source `INCLUDE` with path,
  cycle, depth, size and module-preamble checks.
- Recent language helpers: UTF-8 `LEFT$`, `RIGHT$`, `MID$`; `SIN`,
  `COS`, `INT`; unsigned 32-bit `BAND`, `BOR`, `BXOR`, `BNOT`,
  `ROL`, `ROR`; `&H`/`&B` literals and `HEX$`/`BIN$`. Graphics
  refresh batching uses `GUPDATE`.
On 2026-11-06 the native Linux CMake build and all 64 CTest cases passed. The
Tenstar PlatformIO environment also built successfully. The ESP32-S3 board was
flashed with the firmware and a 512 KiB SPIFFS image; esptool detected 4 MiB
embedded flash and 2 MiB PSRAM. The USB REPL returned `GWIDTH()=240`,
`GHEIGHT()=135`, and `GDEPTH()=4`; `/startup.bas`, the graphics smoke test, and
the memory test ran without VM errors.

On 2026-11-05, the Olimex firmware was rebuilt and flashed. Board checks
verified the full printable ASCII glyph set in a 320x200 VGA capture, paged
HELP resumption after `Q`, and rendered Mandelbrot output. The Mandelbrot demo
now uses an 80x56 streaming renderer on the default graphics surface. Its
`WINDOW` resize statement raised `bad_alloc` on this board, even at 80x64, so
the demo avoids resizing. The runtime, scheduler, graphics, and graphics UI
host suites passed when compiled directly with `g++`; CMake was unavailable, so
this was not a full CTest run at that time; the full native Linux suite later
passed 64/64 on 2026-11-06.

## Decisions to preserve

- SMSQ/E SuperBASIC is the core language reference; compatibility claims need
  a cited reference and conformance test. Label other behavior QL/32-specific.
- SuperBASIC mixed-case spellings show mandatory uppercase and optional
  lowercase letters. Keywords are case-insensitive and recognized
  abbreviations list in canonical form.
- `ELSE IF` is a nested `IF`, not an `ELSEIF` token.
- `FOR` accepts mixed comma-separated values and inclusive stepped ranges,
  in block and inline forms.
- String quotes are doubled. Comments accept `REMark`, `REM`, and apostrophe.
- Scalars pass by value; array parameters pass by reference. Numeric arrays
  have zero-based inclusive bounds; dynamic UTF-8 string arrays are QL/32
  behavior. `RUN` clears session variables.
- Commands/procedures are statement calls without parentheses; functions
  require parentheses in expressions. See
  [ADR-002](docs/decisions/ADR-002-language-profile.md) and
  [QL32-LANG-001](docs/specification/QL32-LANG-001.md).

## Milestone and hardware status

- Use the target-by-target matrix and ranked open work in
  [QL32-SRS-001](docs/specification/QL32-SRS-001.md) as the current milestone
  record. Earlier hardware measurements and detailed profile notes remain in
  [QL32-PLAT-001](docs/specification/QL32-PLAT-001.md).
- CYD acceptance already includes the TFT/serial REPL, BASIC execution, SD and
  SPIFFS storage checks, rotation/compositor behavior, 1/4/8-bit rotation heap
  stability, Wi-Fi station calls, multi-window workers, scheduling weights,
  slice changes, suspend/resume, and worker cancellation. The latest attractor
  using `PLOT` was uploaded/launched in window `#1` at `0,0,320,192`, but visual
  confirmation of the plot is still pending. CYD serial `SCREEN` capture now
  streams a reconstructed compositor and console snapshot as an 8-bit indexed
  BMP over YMODEM without a full-frame allocation; a 320×240 capture was
  received from hardware. The CYD SPIFFS firmware also completed an automated
  25-run worker suite with cleanup and reboot recovery; cumulative free-heap
  drift before reboot was 5,796 bytes, with task-result retention not isolated.
- Olimex Rev B has 512 KiB internal FFS alongside external SDC. Both mounts,
  BASIC REPL arithmetic/heap queries, dual-drive YMODEM, remote debugger file
  listing, worker launch, breakpoint/EVAL/watch/step/diagnostics, and worker
  cancellation were verified on 2026-11-05. VGA screen capture, ASCII font
  output, pager interaction, and Mandelbrot output were also verified. Physical
  PS/2 key input/shortcuts, clock, line UI, and Wi-Fi still need board-level
  verification.
- Tenstar stress run: with `DEBUGBAR ON` and `SLICE 64`, three continuous
  workers and Mandelbrot all remained `RUNNABLE` together. After closing them,
  the task list contained only the supervisor and free heap recovered by about
  65 KiB; the largest free block stayed at 2,064,372 bytes. The user observed
  a few blinking pixels under concurrent drawing and confirmed they stopped
  after workers closed. Mandelbrot itself remained runnable after a further
  28 seconds alone and was cancelled; its runtime is the current Tenstar
  performance gap. Debug bar was left enabled.
- A CYD `WINDOW #0` resize returned out-of-memory under the tested workload.
- CYD memory constraint: three concurrent workers plus Mandelbrot can fail
  during compiler preparation. After CYD worker cycles, free heap was 5–10 KiB
  below the reset baseline while the largest block recovered; retained task
  results and canvas-arena high-water storage were not isolated from a leak.

See [QL32-SRS-001](docs/specification/QL32-SRS-001.md),
[QL32-PLAT-001](docs/specification/QL32-PLAT-001.md), and
[QL32-NET-001](docs/specification/QL32-NET-001.md) for acceptance details.

## Next work

1. **Tenstar performance:** characterize why the 80×56 Mandelbrot program was
   still runnable after 28 seconds alone at `SLICE 64`. Compare other slices,
   then decide whether to optimize the BASIC workload, interpreter execution,
   or ST7789 update path. Keep the debug bar enabled for heap/task observation.
2. **Tenstar display/network acceptance:** visually verify ST7789 color order
   and rotation, then test Wi-Fi. The concurrent-draw blinking disappeared
   when the workers stopped; it was not persistent corruption. QMI8658C,
   BMP280, and RGB LED support are deferred by user decision.
3. **CYD graphics acceptance:** latest `PLOT` attractor visual confirmation is
   still open. On 2026-11-07, the attractor and a static point/text fixture ran
   without VM errors, but `SCREEN` produced an all-zero compositor image.
   Reproduce this discrepancy, compare the physical TFT to the capture, then
   confirm attractor animation/clipping and console readability.
4. **CYD task and memory acceptance:** CYD verified `TASK` command/function
   forms, `KTaskWait`, child error propagation, idle-worker `LOGON`/`LOGOFF`,
   and direct `PRIORITY id,0` baseline reset on 2026-11-07. The worker memory
   suite passed after it was changed to read cancelled `TASKRESULT` data and
   issue a second `KILL` to reap the hidden terminal record; pre-reboot total
   heap drift fell from 8,420 B to 1,872 B, and reboot returned within 60 B of
   baseline. The 100-cycle solo resize soak remained stable. A two-worker
   320x240 canvas replacement succeeded, but the 8-iteration render had not
   finished after five minutes. Characterize the residual heap variation and
   render/concurrency boundary. Details: [CYD test results](docs/test-results/CYD-open-points-2026-11-07.md).
5. **Olimex/LILYGO acceptance:** verify physical PS/2 input/shortcuts, `REBOOT`,
   line UI, task/window controls, clock, and Wi-Fi on Olimex; keep LILYGO
   physical acceptance separate. Add an Olimex GPIO/FFS host profile test.
6. **Performance check:** time the 80×56 attractor and measure the selected CPU
   clock for SINGLE/DOUBLE and 80/160/240 MHz.
7. **Backlog:** specify bounded bitmap-font assets and `FONT`/`LOAD FONT`;
   implement REPL `EDIT linenumber`; later scope palette changes, task-window
   tiling and shared display buffers, BASIC system/build/platform and capability
   introspection, string splitting, indirect procedure calls, and network
   channels. BLE and new hardware families remain deferred. Shared-buffer work
   must define drawing/visibility semantics and memory ownership; a full
   320×240 4-bit retained canvas uses 38,400 bytes. Introspection should build
   on the portable board capability descriptors while defining product
   version/build ID, host/board identity, supported BASIC queries (for example
   SPI, I2C, and serial), and how support differs from runtime availability.

## Starting a new chat

After pulling the repository, ask the new chat to read `PROJECT-HANDOFF.md`
and `AGENTS.md`, inspect the current GitHub/local status, and continue the
first unfinished item under “Next work”. Update this file when that item or
milestone state changes.
