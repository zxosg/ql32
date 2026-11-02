# QL/32 project handoff

Use this file to continue work across Codex chats. Read it together with
[`AGENTS.md`](AGENTS.md) before changing the project. GitHub `main` and the
specifications are the project record; refresh this handoff after meaningful
implementation or hardware-acceptance changes.

## Project and architecture

QL/32 is a portable, native BASIC computer inspired by SMSQ/E SuperBASIC and
Psion-style application interfaces. Linux and macOS are host targets. ESP32
targets are LILYGO FabGL VGA32 and ESP32-2432S028 (CYD). Keep the language,
VM, runtime, system, and UI layers independent of board libraries. Session
resources belong to their BASIC session, and UI waits suspend only that
session.

## Repository state reviewed

GitHub `main` was reviewed at
[`4bfa26e`](https://github.com/zxosg/ql32/commit/4bfa26e60e49774b8b104e5aba77638f6476fd4b),
“Add session-safe UI commands and expand runtime services” (2 November 2026).
This supersedes the old `d1831f8` baseline and old notes that described the
implementation as unpushed local work. Check the actual checkout status before
editing; this handoff describes the published GitHub state.

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
  produces no input. VGA display, clock, SD/startup, line UI and supervisor
  behavior remain unverified.
- M6 Wi-Fi works in host tests and both ESP32 profiles. CYD association,
  status, address, signal and disconnect are verified; repeat on VGA32 when
  available.
- M7 BLE, enhanced UI and broader platform support are backlog.

See [QL32-SRS-001](docs/specification/QL32-SRS-001.md),
[QL32-PLAT-001](docs/specification/QL32-PLAT-001.md), and
[QL32-NET-001](docs/specification/QL32-NET-001.md) for acceptance details.

## Next work

1. Diagnose VGA32 PS/2 input. FabGL port 0 defaults to CLK GPIO 33 and
   DATA GPIO 32, matching the board pin map; startup calls
   `ps2.begin(PS2Preset::KeyboardPort0)` and the input adapter polls FabGL's
   virtual-key queue. The board firmware boots, but physical key events are
   not reaching the REPL. Check the board/keyboard connection and add temporary
   serial diagnostics for keyboard initialization and queued key events.
2. Run the current Linux and macOS build/test workflows and both ESP32 profile
   builds after the pull. Do not assume the previous test count describes the
   current tree.
3. Continue VGA32 display, clock, SD/startup, line UI, supervisor and Wi-Fi
   acceptance. Run targeted CYD checks for the current task command and
   wait/result/error forms when practical.
4. Keep deferred ideas as design backlog until specified: string splitting,
   indirect procedure calls via a string name, slice assignment/string-array
   slicing, palette updates/cycling, and network connections as channels.
   Network channels are separate from the accepted Wi-Fi station service.

## Starting a new chat

After pulling the repository, ask the new chat to read `PROJECT-HANDOFF.md`
and `AGENTS.md`, inspect the current GitHub/local status, and continue the
first unfinished item under “Next work”. Update this file when that item or
milestone state changes.
