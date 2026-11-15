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

Reviewed 2026-11-15: branch `main` is at `5997aff` (2026-11-14). The working
tree contains uncommitted QIMG, palette, profile-introspection, `TILE` and
`MENABLE` implementation, tests, examples, and documentation changes. Inspect
`git status` before staging or committing; do not discard these changes. The
latest Olimex WROVER/PSRAM build succeeded. CYD and Tenstar builds were not
revalidated in this session because required PlatformIO packages were not
cached and network access was unavailable. No claim is made that local `main`
matches GitHub; check the remote before syncing.

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
  Graphics channels support
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
  `HEX$`/`BIN$`; session-owned `RND()` overloads and `RANDOMIZE seed`.
  Graphics refresh batching uses `GUPDATE`.

The 2026-11-14/15 functionality milestone added QL/32-specific QIMG v2
indexed-4 images (`LOADIMAGE`, transparent `BLIT`, `UNLOADIMAGE`) with an
embedded 16-color RGB palette and nearest-color mapping to the active display
palette. The Python remote debugger now includes image load, resize, preview,
QIMG export, and YMODEM upload. Earlier physical image checks used v1 fixtures;
the Olimex PSRAM build was flashed on 2026-11-15 and passed the QIMG v2
YMODEM/LOAD/BLIT/UNLOAD fixture with a VGA capture. Host checks and the desktop
GUI dependency limitation are recorded in
`docs/test-results/qimg-v2-implementation-2026-11-15.md`.
The milestone also includes program-controlled
`PALETTE`/`RGB`/`PALROT`/`PALSHIFT`/`PALFADE`, portable
`SYSINFO$`/`CAPABILITY` profile introspection, supervisor `TILE`, and menu item
enablement via `MENABLE`. Olimex serial checks covered the profile APIs and
palette transforms. The three-window TILE layout and visibly dimmed disabled
menu item were verified on VGA. See the SRS backlog and dated test reports.
The Olimex debugger serial receiver now detects raw `@QL32DBG/...` frames
before the graphical terminal/keyboard decoding path; a direct `tasks.list`
request returned valid JSON after flashing. The PS/2 Break event is already
queued, but a scheduler tick and synchronous BLIT can delay handling it. Fast
Break within long drawing operations remains open; see
`docs/test-results/debugger-serial-input-2026-11-15.md`.
The VGA console also now refreshes its cached screen dimensions when the
window manager resizes for a display mode change. This fixes clipping the
console to the original 320×200 geometry on larger/taller modes. Olimex PSRAM
firmware builds with the fix; physical mode cycling still needs device
verification.
Olimex startup graphics now persist after `FFS:/startup.bas` returns to its
supervisor REPL. The VM/scheduler were closing the program's `OPEN(4)` window
channel and tearing down the supervisor graphics session; both now follow the
interactive session lifetime. The startup source uses the returned handle
instead of assuming `#2`. Host scheduler regression and on-device serial
checks passed; VGA visual confirmation is pending. See
`docs/test-results/startup-window-lifetime-2026-11-15.md`.
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

1. **Attractor and Mandelbrot performance:** `attractor.bas` hoists a
   row-invariant calculation, but needs a board A/B retest and separate
   draw-versus-presentation measurements. The 80/160/240 MHz × SINGLE/DOUBLE
   matrix showed 2.12× improvement from 80 to 240 MHz, with no consistent
   precision-mode winner. The bounded Mandelbrot run has only one reliable
   completion; repeatable timing is still needed. See
   `docs/test-results/attractor-performance-2026-11-12.md`.
2. **Olimex configuration test:** add a host test for Olimex GPIO and FFS
   configuration; recent board checks do not replace this profile-specific
   CTest coverage.
3. **Board acceptance:** repeat broader Olimex/LILYGO acceptance. The latest
   Olimex QIMG, TILE, and MENABLE checks passed on 2026-11-15; LILYGO was not
   tested in this round. Tenstar Wi-Fi remains unverified. Other not-run items
   are recorded in the board reports.
4. **Language, UI, and image candidates:** define scope before implementing
   string splitting, indirect procedure calls, network channels, or cascading
   menus. `LOADIMAGE` now reads QIMG v2 with an embedded 16-color RGB palette;
   v1 compatibility is not required before release. The Python debugger has an
   image converter tab for load, preview, resize, export, and YMODEM upload.
   QIMG v2 passed on Olimex; launch the desktop GUI with its dependencies
   installed and exercise image conversion. See
   `docs/plans/bitmap-format-study.md`.
5. **System profile accuracy:** build ID currently reports `unknown`; validate
   capability flags against exact board builds, including LilyGo.
6. **Shared display-buffer design:** define task ownership, overlap, visibility,
   and mode-change semantics before implementation. Retained per-session
   windows remain the default.
7. **Parked graphics work:** the custom PSRAM-DMA scanout loses sync on some
   high-transition patterns; keep FabGL as the qualified path. The optimized
   framebuffer primitives are not yet the backing store for BASIC drawing,
   and the full Phase 6 benchmark matrix remains open. The `GUPDATE SKIP,1`
   missing-prompt report stays in the issue backlog. PS/2 Break is queued by
   FabGL but is only handled between scheduler ticks; long BLIT operations need
   cooperative Break polling to improve response time without slowing every
   VM slice.

## Recently completed

- **BASIC random numbers (2026-11-10):** Implemented per-session `RND()`,
  `RND(value)`, `RND(low,high)`, and `RANDOMIZE seed`. Seeds are finite
  nonnegative 32-bit integers; unequal bounds form a half-open range, while
  equal bounds return that value. Added runtime coverage for range validation,
  repeatable reseeding, and session isolation. Linux runtime and language
  conformance tests pass; see [QL32-LANG-001](docs/specification/QL32-LANG-001.md).

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
