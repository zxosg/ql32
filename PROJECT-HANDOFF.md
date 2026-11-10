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

Reviewed 2026-11-08: `main` is at [`f1fadad`](https://github.com/zxosg/ql32/commit/f1fadad)
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
  Linux and all maintained ESP32 profiles build. Graphics channels support
  Spleen 5×8, 6×12, 8×16, and 12×24 via shared session font handles, separate
  `FONT`/`GFONT` styles, `UNLOADFONT`, and `GETFONT`; QFNT v2 reads glyphs from
  storage on demand. The CYD received and visually rendered all four files over
  YMODEM on 2026-11-10; cross-board visual checks and heap measurements remain
  open. Classic console text remains unchanged. The classic glyph set covers printable
  ASCII `0x20`–`0x7E`, verified by VGA capture. CYD users have
  confirmed compositor movement, resizing/clipping and rotations; the four
  `CLS` forms ran without syntax or VM errors, though overlapping windows
  prevented visual isolation of clear colors. The attractor's `PLOT` output was
  captured and visually confirmed on 2026-11-07.
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
  `ROL`, `ROR`, `SRA`, `SRL`, `SLA`, `SLL`; `&H`/`&B` literals and
  `HEX$`/`BIN$`. Graphics refresh batching uses `GUPDATE`.
On 2026-11-08 the native Linux CMake build and full CTest suite passed (65/65).
The scheduler pager assertion was stale: its expected text used lowercase
`Enter=page`, while the current prompt is `Enter=Page, L=Line, Q=Quit`. The test
was updated to the prompt contract, preserving the assertion that only the
pager's session suspends while another task continues. CYD and Tenstar firmware
builds and Tenstar graphics regression coverage also passed; detailed board
results are in [the 2026-11-08 acceptance report](docs/test-results/board-acceptance-performance-2026-11-08.md).

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
  slice changes, suspend/resume, and worker cancellation. The attractor's
  `PLOT` output was captured and visually confirmed on 2026-11-07. CYD serial
  `SCREEN` capture now
  streams a reconstructed compositor and console snapshot as an 8-bit indexed
  BMP over YMODEM without a full-frame allocation; a 320×240 capture was
  received from hardware. `TASKVIEW` offset cropping is complete and visually
  confirmed on the CYD: a 160×120 sine/cosine crop bounced across the task
  canvas with `PAUSE 100`. The demo restores automatic graphics updates after
  its batched draw; syntax and semantics are documented in
  [QL32-LANG-001](docs/specification/QL32-LANG-001.md). The CYD SPIFFS firmware
  also completed an automated
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
  28 seconds alone and was cancelled. The bounded Mandelbrot workload
  completed once in 33.929 seconds; later attempts did not complete within the
  observation window, so that workload has no reliable median. The finite
  attractor benchmark now runs as a task and completed once in 26.681 seconds
  at its requested 80 MHz/SINGLE setting; actual CPU frequency is not read back
  yet. Display
  color blocks and all four rotations were visually confirmed after fixing the
  rotation callback ordering. The 2026-11-08 YMODEM investigation found that
  the 1,029-byte STX frame exceeded the default 256-byte USB CDC RX queue;
  Tenstar now configures a 2,048-byte queue. An unpaced benchmark upload passed
  after the fix, and the temporary file was deleted. Debug bar was left enabled.
- A CYD `WINDOW #0` resize returned out-of-memory under the tested workload.
- CYD memory constraint: three concurrent workers plus Mandelbrot can fail
  during compiler preparation. CYD saturation is now characterized: two
  full-screen 4-bit canvases fit the arena, ten animated 64×64 workers ran,
  and the next worker failed safely while the supervisor and `SAVE` remained
  usable. Repeated cleanup showed no monotonic heap loss, though the largest
  block can remain reduced after reaping a ten-worker OOM pass. The separate
  25-run suite returned total free to within 60 B of baseline after reboot;
  the saturation pass itself was not measured across reboot. The 320×240
  eight-iteration render exceeded five minutes with array and channel
  workers active, so that combination is outside the interactive envelope.
  Root cause of the variable post-saturation fragmentation/high-water cost is
  not isolated. See [CYD test results](docs/test-results/CYD-open-points-2026-11-07.md).
  A separate v2 cleanup suite passed 25 worker runs: pre-reboot total-free drift
  was 1,816 B within the 2,048 B tolerance, the largest block stayed at
  110,580 B, and rebooted baseline was within 60 B. The flashed image revision
  was unverified, and task-record checkpoint deltas do not establish an exact
  retained-record cost; see [the v2 report](docs/test-results/CYD-memory-2026-11-07.md).

See [QL32-SRS-001](docs/specification/QL32-SRS-001.md),
[QL32-PLAT-001](docs/specification/QL32-PLAT-001.md), and
[QL32-NET-001](docs/specification/QL32-NET-001.md) for acceptance details.

## Next work

1. **Performance characterization:** complete the SINGLE/DOUBLE × 80/160/240
   MHz timing matrix on each ESP32 board. A bounded benchmark source is at
   `examples/attractor-benchmark.bas`; the attractor has one 26.681-second
   sample at requested 80 MHz/SINGLE, but no actual-frequency readback or
   median. The separate Mandelbrot workload completed once in 33.929 seconds
   and later timed out. CPU readback diagnostics were removed after the
   attempt. The YMODEM root cause is now identified and
   fixed on Tenstar: its USB CDC queue was 256 bytes for a 1,029-byte data
   frame. Firmware with a 2,048-byte queue passed an unpaced 1,027-byte upload.
   A first attractor timing is 26.681 s at requested 80 MHz/SINGLE. Repeat
   with actual-frequency readback and three samples per configuration; also
   repeat transfer with a larger file and consecutive batches.
2. **Board acceptance left open by user choice:** Tenstar Wi-Fi was skipped;
   Olimex physical PS/2 shortcuts, clock, Wi-Fi and related checks were skipped;
   LILYGO physical acceptance was skipped. Current results and profiles are in
   the board acceptance report. Do not treat these checks as passed.
3. **Tenstar performance follow-up:** determine whether the variable Mandelbrot
   duration comes from BASIC math, interpreter execution, or display transfer.
   Keep the debug bar available for heap/task observation. QMI8658C, BMP280,
   and RGB LED support remain deferred by user decision.
4. **Other backlog:** scope palette changes, task-window management, BASIC
   system/build/platform and capability introspection,
   enhanced `DIR` output with optional file sizes and `<subdir>` directory
   labels, `DATA`/`READ`/`RESTORE` statements, string splitting, indirect
   procedure calls, and network channels.
   BLE and new hardware families remain deferred. Introspection should build
   on the portable board capability descriptors while defining product
   version/build ID, host/board identity, supported BASIC queries (for example
   SPI, I2C, and serial), and how support differs from runtime availability.

## Recently completed

- **REPL program editing and line deletion (2026-11-08):** The numbered-line
  REPL editor has been implemented and fixed. `DELETE lineNumber1,...` is also
  implemented and validates the remaining program after deletion. These are
  no longer backlog items; see [QL32-LANG-001](docs/specification/QL32-LANG-001.md)
  and [QL32-API-001](docs/specification/QL32-API-001.md).

- **32-bit shifts (2026-11-08):** Added `SRA`, `SRL`, `SLA`, and `SLL` with
  32-bit fill semantics, saturation for counts of 32 or greater, and errors
  for negative/fractional counts. The host runtime tests passed. CYD firmware
  built and flashed; on-device examples yielded `C0000000`, `40000000`, `2`,
  and `1`, large-count results matched the specified fill behavior, and
  invalid counts raised VM errors. See
  [QL32-LANG-001](docs/specification/QL32-LANG-001.md) and
  [QL32-API-001](docs/specification/QL32-API-001.md).

- **Supervisor diagnostics and task window modes (2026-11-08):** Added the
  CYD `DEBUGBAR ON|OFF` overlay for heap totals, largest free block, allocation
  count, active tasks, and wall-clock time. Added supervisor-only
  `WINDOWMODE [ISOLATED|SHARED]`; isolated mode tiles active task viewports and
  shared mode restores full-display viewports. Linux tests pass, and the CYD
  was flashed and checked with three live sin/cos workers, the visible debug
  bar, mode switching, and cleanup. See
  [QL32-API-001](docs/specification/QL32-API-001.md) for command behavior.

## Starting a new chat

After pulling the repository, ask the new chat to read `PROJECT-HANDOFF.md`
and `AGENTS.md`, inspect the current GitHub/local status, and continue the
first unfinished item under “Next work”. Update this file when that item or
milestone state changes.
