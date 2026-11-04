# QL/32 project handoff

Use this file to continue work across Codex chats. Read it together with
[`AGENTS.md`](AGENTS.md) before changing the project. GitHub `main` and the
specifications are the project record; refresh this handoff after meaningful
implementation or hardware-acceptance changes.

## Project and architecture

QL/32 is a portable, native BASIC computer inspired by SMSQ/E SuperBASIC and
Psion-style application interfaces. Linux and macOS are host targets. ESP32
targets include LILYGO FabGL VGA32, Olimex ESP32-SBC-FabGL, and
ESP32-2432S028 (CYD). Keep the language,
VM, runtime, system, and UI layers independent of board libraries. Session
resources belong to their BASIC session, and UI waits suspend only that
session.

## Repository state reviewed

Reviewed 2026-11-04: the source baseline was
[`de88fe5`](https://github.com/zxosg/ql32/commit/de88fe5) (“Add QL/32 language
and runtime features”), matching `origin/main`. This review updates project
status documents in the working tree. Math precision and ESP32 CPU frequency
options are committed, not local-only changes.

The latest implementation includes:

- M1 BASIC core: expressions, variables, structured `IF`, flexible block and
  inline `FOR`, `REPeat`, procedures/functions/locals, arrays, block
  `SELect ON`, abbreviations and canonical `LIST`; numbered programs and a
  persistent host REPL.
- Host storage and session-owned channels, `LOAD`/`SAVE`/`DIR`, `OPEN`,
  line and exact-byte file I/O, and asynchronous UI requests. `ALERT`,
  `CONFIRM`, dialogs and menus suspend/resume only their calling session.
- Retained indexed graphics windows, `PLOT`, filled/outline primitives,
  window movement/resizing, CYD rotation
  and palette queries, Linux PPM output, and CYD/VGA32 graphics adapters.
  `WINDOW #channel,x,y,w,h` now selects independent per-session channel
  surfaces, including a graphics surface for console channel `#0`; `PRINT #0`
  remains console output. CYD and FabGL console overlays follow `#0` bounds.
  Linux and all maintained ESP32 profiles build. CYD users have confirmed
  compositor movement, resizing/clipping and rotations; visual confirmation of
  the latest `PLOT` attractor remains open.
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
The 2026-11-04 macOS review rebuilt the host target and passed all 57 CTest
cases. It also built the four maintained PlatformIO environments:
`lilygo_vga32`, `olimex_esp32_sbc_fabgl`, `esp32_2432s028`, and
`esp32_2432s028_spiffs`. The machine is macOS, so the `build/linux` preset
compiled and tested here is not native Linux evidence. Native Linux validation
remains open.

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
  received from hardware.
- Olimex Rev B has earlier partial acceptance for USB serial REPL, internal
  FFS/YMODEM, PS/2 detection, worker launch/cancellation, heap recovery, and
  Mandelbrot after worker cancellation. Do not claim full VGA acceptance: the
  display, PS/2 interaction/shortcuts, clock, line UI, task interaction, SD and
  Wi-Fi still require board-level verification. VGA hardware is currently
  unavailable, so defer those checks.
- Known memory constraint: three concurrent workers plus Mandelbrot can fail
  during compiler preparation. After worker cycles, free heap was 5–10 KiB
  below the reset baseline while the largest block recovered; retained task
  results and canvas-arena high-water storage were not isolated from a leak.

See [QL32-SRS-001](docs/specification/QL32-SRS-001.md),
[QL32-PLAT-001](docs/specification/QL32-PLAT-001.md), and
[QL32-NET-001](docs/specification/QL32-NET-001.md) for acceptance details.

## Next work

1. **CYD regression:** visually confirm the `PLOT` attractor in `#1` resized to
   `0,0,320,192`; note animation/clipping and console readability. `SCREEN`
   currently snapshots retained QL/32 output and the console model, not pixels
   drawn outside the compositor.
2. **CYD task acceptance:** exercise `TASK` command/function with `KTaskWait`,
   child failure/error propagation, and direct `PRIORITY id,0` baseline reset.
3. **CYD memory characterization:** repeat worker/graphics launch-kill cycles
   and isolate source buffers, task-result records, and canvas-arena high-water
   storage using `FREE()`, `FREE(1)`, `FREE(2)`. Record the supported workload
   boundary where Mandelbrot compilation exhausts memory.
4. **Native Linux:** run `tools/build-target.sh linux` and all 57 CTest cases on
   a Linux machine or CI runner; the two build directories in this Mac checkout
   both ran on Darwin.
5. **Deferred VGA acceptance:** when the physical board returns, use the current
   `olimex_esp32_sbc_fabgl` profile and verify VGA/console layout, PS/2 key
   input/shortcuts, `REBOOT`, startup/storage, line UI, task controls, clock,
   and Wi-Fi. Keep LILYGO-specific physical acceptance separate.
6. **Performance check:** time attractor frames and measure the selected CPU
   clock for SINGLE/DOUBLE and 80/160/240 MHz.
7. **Backlog:** specify bounded font assets and `FONT`/`LOAD FONT`; implement
   REPL `EDIT linenumber`; later scope palette changes, task window tiling,
   string splitting/slicing, indirect procedure calls, network channels, BLE,
   and new hardware families.

## Starting a new chat

After pulling the repository, ask the new chat to read `PROJECT-HANDOFF.md`
and `AGENTS.md`, inspect the current GitHub/local status, and continue the
first unfinished item under “Next work”. Update this file when that item or
milestone state changes.
