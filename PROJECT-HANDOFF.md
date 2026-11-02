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

The current upstream baseline is `main` commit
[`4a4586c`](https://github.com/zxosg/ql32/commit/4a4586c) (“Adjust VGA console
size and ink”, 2 November 2026), following `8bdb180` and the earlier
`4bfa26e`/`d1831f8` baselines. Local uncommitted math precision and ESP32 CPU
frequency changes have been reapplied on top of this update. Check the actual
checkout status before editing; those changes have not been revalidated
against the latest upstream code yet.

The latest implementation includes:

- M1 BASIC core: expressions, variables, structured `IF`, flexible block and
  inline `FOR`, `REPeat`, procedures/functions/locals, arrays, block
  `SELect ON`, abbreviations and canonical `LIST`; numbered programs and a
  persistent host REPL.
- Host storage and session-owned channels, `LOAD`/`SAVE`/`DIR`, `OPEN`,
  line and exact-byte file I/O, and asynchronous UI requests. `ALERT`,
  `CONFIRM`, dialogs and menus suspend/resume only their calling session.
- Retained indexed graphics windows, window movement/resizing, CYD rotation
  and palette queries, Linux PPM output, and CYD/VGA32 graphics adapters.
  `WINDOW #channel,x,y,w,h` now selects independent per-session channel
  surfaces, including a graphics surface for console channel `#0`; `PRINT #0`
  remains console output. CYD and FabGL console overlays follow `#0` bounds.
  Linux, CYD, SPIFFS-CYD, and VGA32 builds pass; the new display behavior still
  needs a board-level visual check.
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
- Local uncommitted `OPTION` additions select `KOptMathSingle` or
  `KOptMathDouble` (DOUBLE default) and ESP32-wide `KEspCpuFreq80`,
  `KEspCpuFreq160`, or `KEspCpuFreq240`. Host builds accept and ignore the
  device clock setting. A CYD attractor run with SINGLE and 240 MHz was
  animated but reported slow; that run preceded the latest upstream pull, and
  frame duration/clock readback were not measured. Rebuild and retest after
  integration.

The handoff at the previous revision reported 53 CTest cases passing on Linux
and macOS and both ESP32 profiles building. The latest commit adds more tests;
rerun the current checkout's build, CTest suite and relevant target builds
before relying on those results.

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

- M1 BASIC core acceptance is complete.
- M2 Linux storage, channel and asynchronous UI acceptance is complete.
- M3/M4 software includes retained graphics, dialogs/menus and session
  scheduling. CYD graphics, rotations, compositor behavior and multi-session
  display have been exercised on hardware. The latest `TASK`/`KTaskWait`
  API forms should receive a targeted CYD check before marking those exact
  interactions hardware-verified.
- M5 host and ESP32 software integration is present. CYD display, serial REPL,
  BASIC loop, SD text round-trip, graphics behavior and memory behavior have
  been verified on hardware. The VGA32 firmware has been flashed and boots;
  USB serial REPL was verified earlier, but the PS/2 keyboard currently
  produces no input. With an SD card inserted, startup reports
  `SD storage unavailable`; its capacity and filesystem are not yet known.
  VGA display, clock, SD/startup, line UI and supervisor behavior remain
  unverified. The user clarified that the tested board is Olimex
  ESP32-SBC-FabGL Rev B, not LILYGO VGA32. The LILYGO image had been flashed on
  the Olimex board. A separate Olimex profile now selects its SD MISO GPIO 35;
  the LILYGO profile retains MISO GPIO 2. The supplied board PDF is local and
  not included in the public repository.
- Olimex task/heap stress check (2026-11-02): `FREE()` reports total free
  bytes, `FREE(1)` the largest contiguous block, and `FREE(2)` the free-block
  count; `ql32.inc` defines `KFreeTotal`, `KFreeContiguous`, and
  `KFreeFragments`. Earlier testing found a 104,808 / 69,620 / 15 baseline and
  showed that Mandelbrot compilation fails below its memory peak. The
  240x176 four-bit canvas itself needs 21,120 bytes.
- Canvas arena and task-message hardware retest (2026-11-02): Olimex firmware
  flashed successfully. Baseline was 106,224 / 69,620 / 12. `KILL` and
  statement-form `TASK` acknowledgements now write to console channel `#0`;
  this avoids allocating a full-screen supervisor graphics canvas. With the
  fix, `TASK "worker-1.bas"` through worker 3 started as IDs 2–4 without the
  prior worker line-2 OOM. All three active: 47,168 / 18,420 / 20. Mandelbrot
  started as task 5 but failed during compilation at that pressure. FIFO kills
  restored 102,880 / 69,620 / 26. Mandelbrot then completed and printed
  `Mandelbrot complete`. A second three-worker cycle also started and killed
  successfully; its largest block returned to 69,620. After task cycles, free
  memory remained 5–10 KB below the reset baseline with a higher block count;
  a reboot restored 106,244 / 69,620 / 16. Mandelbrot plus one worker started
  and remained runnable without OOM, but did not complete during the test
  window before the board was reset.
- The window manager uses a bounded, lazily-growing canvas arena that compacts
  blocks on close/resize and releases storage when the last canvas closes.
  Focused host graphics tests pass; Olimex and CYD firmware builds pass. The
  scheduler test reaches and passes the task-control channel tests, then fails
  at the interactive BREAK variable-preservation assertion; that failure is
  not yet isolated. The USB serial monitor was closed after testing, leaving
  the board rebooted at the supervisor prompt.
- CYD memory availability: CYD uses the same portable `WindowManager` arena,
  VM/compiler, scheduler resource cleanup, task acknowledgements, and ESP32
  `MemoryService` as Olimex. Its `WindowedService` cap is 76,800 bytes. The
  CYD firmware builds (50,020 bytes static RAM, 1,298,805 bytes flash), but
  the new multi-task heap/arena behavior has not yet been exercised on CYD
  hardware.
- M6 Wi-Fi works in host tests and both ESP32 profiles. CYD association,
  status, address, signal and disconnect are verified; repeat on VGA32 when
  available.
- M7 BLE, enhanced UI and broader platform support are backlog.

See [QL32-SRS-001](docs/specification/QL32-SRS-001.md),
[QL32-PLAT-001](docs/specification/QL32-PLAT-001.md), and
[QL32-NET-001](docs/specification/QL32-NET-001.md) for acceptance details.

## Next work

1. Build and flash the Olimex profile: `tools/build-target.sh olimex`, then
   `pio run -e olimex_esp32_sbc_fabgl -t upload`. The board's SD wiring is
   MOSI 12, MISO 35, SCK 14, CS 13; using the LILYGO profile (MISO 2) explains
   the original mount failure. With a FAT32 card still failing after the
   Olimex-profile flash, the target now uses ESP-IDF's SPI2/VFS SD-SPI mount
   path from ESPectrum instead of Arduino `SD.begin()`. Report the startup
   `SD diagnostic` line if it does not mount. The FatFs build has
   `FF_FS_EXFAT=0`; do not use exFAT media.
2. Verify the Olimex VGA layout: `#0` text and PS/2 input occupy the bottom
   three 8-pixel rows using the 6x8 font and QL ink 4 (green), while the
   default `#1` graphics area stops above them. Then
   exercise the execution controls on Olimex PS/2: bare `Pause`,
   `Shift+Pause`, `Ctrl+Pause`/`Ctrl+Space`, `Ctrl+Shift+Pause`, and
   `Ctrl+Alt+Delete`; verify `REBOOT` from the prompt on embedded targets.
   Both Olimex and CYD firmware profiles compile with the controls; physical
   shortcut and reboot verification remains pending.
3. Check native PS/2 keyboard detection on the Olimex profile. Port 0 uses
   DATA GPIO 32 and CLK GPIO 33, matching the board schematic. The next firmware
   build reports whether FabGL received a keyboard reset response. If detected
   but keys still do nothing, add temporary serial output for queued key events
   and trace the REPL input gate.
4. Run the current Linux and macOS build/test workflows and all ESP32 profile
   builds after the pull, including module math precision and CPU-frequency
   option coverage. Do not assume the previous test count describes the
   current tree.
5. Continue VGA32 display, clock, SD/startup, line UI, supervisor and Wi-Fi
   acceptance. Run targeted CYD checks for the current task command and
   wait/result/error forms when practical.
6. Keep deferred ideas as design backlog until specified: string splitting,
   indirect procedure calls via a string name, slice assignment/string-array
   slicing, palette updates/cycling, and network connections as channels.
   Network channels are separate from the accepted Wi-Fi station service.
7. Investigate retained heap after repeated worker cycles (about 5–10 KB below
   reset baseline, with free-block count rising), and measure Mandelbrot with
   exactly one worker active to completion. Three concurrent workers plus
   Mandelbrot currently exceed compiler memory; after FIFO kills it completes.
   Current Olimex firmware was built and flashed on 2026-11-02 (53,012 bytes
   static RAM, 1,391,773 bytes flash). Serial monitor has been closed and the
   board rebooted after testing.
8. CYD storage and task-buffer check: the SD profile booted with `Storage
   unavailable` because no card was mounted. The `esp32_2432s028_spiffs` profile
   was flashed instead; internal `FFS` mounted and boot reported
   `Running Storage:/startup.bas`. Five worker launch/kill cycles completed.
   A no-graphics worker probe changed `FREE(0..2)` from 225,304 / 110,580 / 18
   to 224,880 / 110,580 / 22; this includes retained task-result records. The
   graphics-worker run reached 163,816 free bytes from 203,556 during the test,
   while `FREE(1)` stayed 110,580; after reboot it returned to 225,464 / 110,580
   / 15. Isolate the shared canvas arena's retained high-water capacity from
   source-buffer and task-record memory in the next CYD memory stress run.
9. Repeat the attractor check on CYD after rebuilding the pulled source. The
   earlier run with `OPTION KOptMathSingle` and `OPTION KEspCpuFreq240` animated
   but remained slow; frame duration and actual CPU clock were not measured.
10. Improve host-to-board and board-to-host file transfer over USB serial.
    Prefer YMODEM with CRC and a small Mac-friendly host utility over line-by-line
    REPL entry. Add monitor transfer commands such as `RECV "path"` and
    `SEND "path"`; use the storage service, bounded buffers, acknowledgements,
    and clear success/error reporting. Confirm baud-rate limits and support the
    available SD and flash filesystems.
11. Add selectable portable bitmap fonts. `FONT fontID` should select a font
    for the current graphics channel. `LOAD FONT filename$` should validate and
    load a font file, select it, and report its assigned ID. Define a bounded,
    platform-neutral font file format and decide later whether console `PRINT`
    uses the same selected font or retains a separate console font.
12. Add the REPL command `EDIT linenumber`. It should place the selected stored
    line, including its line number, in the current input editor so the user can
    revise it and press Enter to replace the stored line. An empty edited line
    should follow the existing numbered-line rule and delete that line.

## Starting a new chat

After pulling the repository, ask the new chat to read `PROJECT-HANDOFF.md`
and `AGENTS.md`, inspect the current GitHub/local status, and continue the
first unfinished item under “Next work”. Update this file when that item or
milestone state changes.
