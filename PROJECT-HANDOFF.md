# QL/32 project handoff

Use this file to continue work across Codex chats. Read it together with
[`AGENTS.md`](AGENTS.md) before changing the project. The repository is the
source of truth; update this handoff when the milestone state or next task
changes.

## Project

QL/32 is a portable, native BASIC computer inspired by SMSQ/E SuperBASIC and
Psion-style application interfaces. Linux is the initial development host; the
first hardware target is LILYGO FabGL VGA32. Keep the language, VM, runtime,
system, and UI layers independent of board libraries.

## Current state

- The current committed baseline is `d1831f8` (`Add exact-byte file channel I/O and document endpoint bindings`). Current local changes are uncommitted and include target/build fixes, host memory portability, CYD compositor memory fixes, embedded scheduler integration and supervisor commands, and an opened-endpoint registry with serial/null output.
- The Linux CLI reads complete source from a `.bas` file, accepts stdin source/batches, and opens a persistent line-oriented REPL on an interactive terminal. The REPL keeps session and numbered-program state between entries and reports a failed entry without exiting. The portable VM also exposes cooperative `executeSlice`/`resumeSlice` operations; `ql32::vm::SessionScheduler` round-robins interpreter sessions using a configurable bytecode instruction budget.
- `FREE()` is a zero-argument BASIC function returning free-memory bytes through the portable `runtime::MemoryService`; ESP32 reports free 8-bit-capable heap, Linux reports host `MemAvailable`, and macOS uses free plus reclaimable inactive pages. The retained window canvas packs pixels at 1, 4, or 8 bits and enforces a total canvas-byte budget. BASIC graphics now render through the retained compositor on Linux, CYD, and VGA32; windows default to 4-bit depth.
- Implemented language coverage includes module-level immutable `CONST` declarations for numeric and string literals, assignments; numeric and string values; expression and `PRINT` rules; comments; block `IF`; block and inline `FOR` with mixed range lists; named `NEXT` and `EXIT`; block and inline `REPeat`; procedure/function definitions, parameters, invocation locals, return, forward calls, recursion, and array-by-reference parameters; zero-based inclusive arrays; block `SELect ON` with lists, ranges, and `REMAINDER`; numeric `GO TO`/`GOTO` line targets and QL/32 named labels; UTF-8 string slices and `LEN`, `LEFT$`, `RIGHT$`, and `MID$`; trigonometric `SIN`/`COS` and flooring `INT`; graphics refresh control with `GUPDATE`; keyword abbreviations for the supported keyword set; and canonicalized `LIST` output.
- QL/32-specific numeric helpers also include unsigned 32-bit bit operations (`BAND`, `BOR`, `BXOR`, `BNOT`, `ROL`, `ROR`), `&H`/`&B` literals, and `HEX$`/`BIN$` formatting. The Linux regression suite covers their valid and invalid boundaries.
- String slicing and extraction functions use one-based UTF-8 code-point indexes. Slice assignment and slicing dimensioned string arrays are not implemented yet.
- Numbered lines are validated against candidate program state before replacement, while incomplete block prefixes and forward calls are accepted. Calls unresolved in the complete program are diagnosed by `RUN`. Tests cover replacement rollback, incremental block entry, REPL recovery, interpreter isolation, array rank/type errors, stored-program errors, and cooperative cancellation through the C++17 cancellation token.
- Known profile limits: `SELect` singleton comparisons use exact binary64 equality and only its block form is supported; allocation is capped at one million elements per array; nested inline loops are outside the explicit M1 grammar. See the specification for other QL/32 choices and deferrals.
- The suite has 53 CTest cases across CLI and runtime behavior, including recursion depth, allocation limits, GOTO line/label behavior, storage and channel confinement, editor file commands, async UI lifecycle and BASIC dialog/menu round trips, cooperative scheduler slicing and UI suspension, graphics command dispatch/session cleanup, SVG rendering and escaping, FabGL and CYD graphics/console adapters, FabGL keyboard input, ESP32 clock/storage/channel/network behavior, Linux clock behavior, capability descriptor validation, VGA32 and CYD board profile validation, embedded line UI presentation and VM resumption, Wi-Fi BASIC calls, recursive source includes, and single-pass source-order evaluation of FOR ranges. The Linux suite passes with GNU C++ 15.2 and CMake 4.2.3. `tools/build-target.sh mac` builds with AppleClang, and all 53 CTest cases pass on macOS.
- M1 BASIC core acceptance is complete. The M2 Linux slice adds portable text storage and session-owned channel contracts, sandboxed directory adapters, interactive `LOAD`/`SAVE`/`DIR`, BASIC `OPEN`/`READ$`/`EOF`/`WRITE`/`CLOSE` calls, and a thread-safe asynchronous UI request queue. BASIC `ALERT`/`CONFIRM`, per-interpreter VM suspend/resume, and Linux and embedded line presenters are implemented and tested. M2 Linux acceptance is complete.

## Decisions already made

- M1 targets a small, verified SMSQ/E SuperBASIC subset. Claim compatibility only when both a reference and conformance test support the behavior; otherwise mark it QL/32-specific.
- Keywords are case-insensitive. The current resolver uses mandatory-letter subsequences for the supported table and canonicalizes keyword tokens in stored lines; broader table coverage against a live SBASIC interpreter remains useful.
- `ELSE IF ... THEN` is nested `IF`, not a distinct keyword.
- Native commands and user-defined procedures are statement calls with no call
  parentheses; functions require parentheses in expressions. Built-in commands
  and functions have separate lexer token kinds. Parentheses remain for
  function arguments, procedure/function declarations, array indexes, slices,
  and grouping.
- M1 `FOR` supports mixed comma-separated values and inclusive stepped ranges; both block and inline forms are supported.
- String literals escape a quote by doubling it. M1 accepts `REMark`, `REM`, and apostrophe comments.
- Scalar procedure/function arguments pass by value; dimensioned array arguments pass by reference, including forwarding through calls.
- Numeric arrays use zero-based inclusive bounds. QL/32 dynamic UTF-8 string arrays differ from historical fixed-length arrays.
- `RUN` clears session variables. Numbered lines replace a prior line with the same number; a blank numbered line deletes it.
- Task management uses `TASK` in command and function forms; `KTaskWait` makes it wait synchronously while the child owns focus. `KILL` stops a task, `TASKRESULT` reports task properties, and `ERR`/`ERR$`/`ERRX$` expose QL/32 errors. The task-result memory property is deferred. Linux build and tests pass; ESP32 build and CYD verification remain.
- Bit functions use an unsigned 32-bit range; `BNOT` inverts 32 bits, and `ROL`/`ROR` wrap integer counts modulo 32. `HEX$`/`BIN$` emit unprefixed uppercase hex/binary digits without leading zeroes. Logical operators retain their Boolean meaning.
- `SELect` block arms accept values, inclusive ranges, and a final `REMAINDER`; exact singleton matching is currently a QL/32-specific choice.
- The language contract and remaining open questions are in
  [`QL32-LANG-001.md`](docs/specification/QL32-LANG-001.md). References and
  provenance are in [`docs/reference/README.md`](docs/reference/README.md).

## Current milestone status and open points

M2 Linux acceptance is complete. M3 now has a portable session-owned graphics
service contract and VM dispatch for `INK`, `PAPER`, `LINE`, `RECT`, `CIRCLE`,
`TEXT`, `CLS`, and `GUPDATE`. Decisions: one lazy default graphics window per session;
OPL-style integer pixel coordinates with `(0,0)` at the top-left; indexed ink
and paper colors initialized to the QL eight-color order. SuperBASIC remains
the core loop syntax; use OPL selectively for language refinements and as the
managed UI reference, not as a source-compatibility target. Palette updates and
cycling are backlog items. Linux uses a dependency-free SVG debug presenter
that writes one 640x480 `ql32-graphics-session-<id>.svg` snapshot on session
close; `examples/graphics.bas` exercises all implemented commands. A FabGL
VGA graphics adapter maps the portable service to the board canvas. The pinned
`lilygo_vga32` image also includes a portable keyboard contract and FabGL PS/2
adapter, ESP32 clock adapter, SD storage adapter, and session-owned SD-backed
text/binary file channels. A second `esp32_2432s028` profile now targets the common yellow
ESP32-2432S028R board with ILI9341 TFT graphics and serial terminal input. Its
TFT adapter maps the QL eight-color indexed palette to RGB565, while SD uses
the board's separate SPI bus. The shared BASIC VM is linked into firmware, with serial output
and a prompt accepting USB serial input. On startup it runs `startup.bas` from
the SD root when present; otherwise, the TFT remains clear for readable text
output. The built-in graphics demo was removed. The channel adapter supports line and exact-byte file
I/O with root-confined paths. Host coverage exercises the hardware adapters
using fakes; all 53 CTest cases pass. PlatformIO Core 6.2.0, Arduino-ESP32
2.0.17, and FabGL 1.0.9 build both profiles. Current size and RAM measurements
are maintained in [`QL32-PLAT-001.md`](docs/specification/QL32-PLAT-001.md).
The user confirmed the USB serial REPL works on the
board. VGA output, PS/2 input, clock, and SD behavior remain unverified. The
CYD target builds with Adafruit ILI9341/GFX and a scrolling TFT PRINT console.
The
user verified TFT output, the
serial REPL, a small BASIC `FOR`/`PRINT` loop, and an SD channel round-trip
with successful text readback and EOF detection on hardware. VGA32 SD behavior
still needs checking. Both profiles now use a 4 MiB single-app partition with
NVS and coredump space, no OTA slot, and no internal filesystem. Both firmware profiles now integrate the asynchronous line
presenter for alerts, confirmations, typed dialogs, and menus. Linux tests
cover presenter input, validation, cancellation, menu IDs, and VM resumption.
M5 software integration is complete; hardware acceptance remains open for the
VGA32 display/PS2/clock and SD behavior, including startup loading and channel
round-trips. M6 has begun with a portable asynchronous Wi-Fi service and BASIC
calls (`GETNET`, `DISNET`, `NETSTAT$`, `NETADDR$`, `NETSIGNAL`) plus an ESP32
station adapter on both board profiles. Linux fake-service and adapter tests
cover state and call behavior; both firmware profiles compile. The user has
verified CYD association, status, address, signal, and disconnect on hardware;
VGA32 Wi-Fi verification remains pending with the board unavailable. `INCLUDE` now
expands recursively included BASIC source from the storage service, with cycle,
root-confinement, depth, size, and module-preamble checks. Linux tests cover
nested libraries and numbered programs. CYD physical Wi-Fi acceptance has
passed; VGA32 hardware acceptance remains pending. The
current M6 acceptance spec is [`QL32-NET-001.md`](docs/specification/QL32-NET-001.md).
HTTP/socket APIs, BLE, enhanced UI, and broader platform support remain later
slices. Publish changes only when explicitly requested.

The graphics API also now provides CYD command `ROTATE degrees` and window queries:
`GWIDTH()`, `GHEIGHT()`, `GORIGINX()`, `GORIGINY()`, `GDEPTH()`,
`GROTATION()`, `GPALETTESIZE()`, and `GPALETTE$(index)`. Host tests and both
ESP32 builds pass. The CYD now starts
explicitly in 320x240 landscape, and rotation recalculates the TFT console
layout. The user has confirmed on hardware that all four CYD rotations work,
rotation 0 reports the intended landscape geometry, and the QL/32 red palette
entry renders red after clearing the panel's BGR-order flag. The user has also
confirmed the scrolling TFT text window occupies the bottom quarter of the
screen and scrolls correctly.

Current open points before calling the hardware milestone accepted:

- **CYD display:** basic TFT, serial REPL, SD round-trip, palette, and scrolling
  console are hardware-verified. The retained-compositor regression has now
  confirmed window movement, resize/clipping with retained drawing, and all
  four rotations. The user confirmed that 90°/270° are portrait, 0°/180° are
  landscape, and the 200x140 window remains at the top-left through rotation.
  Initial allocation failures were fixed by skipping no-op canvas resizes,
  reusing canvas storage for orientation changes, and dropping the cached
  composed frame before size-changing allocations. On the patched firmware,
  first-command `ROTATE 0`, resize from 200x140 to 240x320, full-screen 180° and
  270° transitions, retained drawing, and return to 0° all succeeded without
  serial errors. The user visually confirmed each orientation and retained
  drawing. The repeatable memory check is `examples/cyd-rotation-memory.bas`.
  `FREE()` values for one cycle were 100456, 100176, 100172, 100172, and
  100128 bytes; all five values in a second cycle were 100140 bytes, showing
  stable heap use after the initial warm-up. A direct CYD adapter test also
  exercised 1-, 4-, and 8-bit canvases across every rotation; free heap stayed
  constant within each depth and matched the expected canvas storage sizes.
  The macOS build command is:

  ```sh
  PLATFORMIO_CORE_DIR=/private/tmp/ql32-platformio-core tools/build-target.sh cyd
  ```

  The serial monitor used `/dev/cu.usbserial-1110` at 115200 baud.
- **VGA32 hardware:** verify VGA drawing, PS/2 input, clock display, SD mount,
  `startup.bas`, and SD channel read/write round-trip. Its embedded line UI
  presenter also needs physical interaction checks.
- **Wi-Fi on hardware:** CYD association, status, address, signal, and
  disconnect behavior passed. Repeat the same acceptance on VGA32 when that
  board is available, per [`QL32-NET-001.md`](docs/specification/QL32-NET-001.md).
- **Multi-session UI/display:** a portable cooperative smooth weighted
  round-robin scheduler runs independent interpreter sessions in bounded
  bytecode slices. The supervisor can set the global slice with `SLICE n`
  (1..65536, default 128) and each active task's weight with
  `PRIORITY task-id,weight` (non-negative 32-bit integer, default 4).
  `SUSPEND id` saves the current weight and sets it to 0; `RESUME id` restores
  it. `PRIORITY id,0` sets zero directly and clears the saved resume weight.
  `TASKS` reports the slice and priorities to the current print channel
  (initially `#1`); `TASKS #channel` redirects one listing and suspended tasks
  show their saved resume weight.
  Host tests verify the reference sequence and
  weighted frequencies. On CYD, the user confirmed the 8:4:1 demo rates ran in
  the expected order and verified `SUSPEND`, `RESUME`, and `CANCEL` on hardware.
  Direct `PRIORITY id,0` baseline behavior and VGA32 scheduler acceptance
  remain unverified on hardware. UI waits suspend only their owner. Both ESP32 event loops now
  drive a persistent interactive REPL session with a default 128-instruction
  scheduler slice, adjustable through `SLICE`;
  startup, immediate commands, and `RUN` preserve interpreter state, with
  serial/keyboard input and UI replies handled between ticks. Host tests cover
  interactive-session continuity, weighted scheduling, error recovery, and
  UI resumption; both ESP32 profiles build. The CYD scheduler was flashed and
  checked on hardware: persistent state produced `42`, a numbered `RUN` showed
  all three expected lines, the alert waited for Enter, and `AFTER_UI_RESUME`
  appeared afterward. A syntax error was reported over serial and the REPL
  accepted the next command. The embedded REPL provides task controls and
  separate per-session state. The latest working tree changes replace
  `SPAWN`/`CANCEL` with `TASK`/`KILL`, add optional `KTaskWait`, `TASKRESULT`,
  and the QL/32 error functions `ERR`, `ERR$`, and `ERRX$`. Those latest API
  changes have not yet been built, tested, or verified on CYD hardware.
  Spawned workers remain
  interactive after their startup source completes. Each session owns its
  BASIC state and channel handles; sessions
  share board adapters and physical devices. Worker `#0` output and diagnostics
  use the shared console; `#1` goes to its own graphics window. Both ESP32
  profiles build with this supervisor. CYD hardware ran the flow: sessions
  2–4 were started and cancelled in sequence; sessions 5–7 were started again;
  `TASKCHANNELS` completed, and serial prompts confirmed `LOGON`, per-session
  `WINDOW` changes, and `LOGOFF` for sessions 5, 6, and 7. No allocation errors
  occurred after the TFT row-streaming and initial-bounds fixes. The serial
  monitor cannot show the TFT channel-list text, so visual inspection of that
  listing remains open. VGA32 acceptance remains pending because that board is
  unavailable. The user visually confirmed the CYD's three
  persistent columns side by side (white/blue/magenta, left to right), with
  worker output advancing in roughly five-second turns. The portable
  first CYD three-worker run hit `std::bad_alloc` while creating the third
  session. The TFT presenter now streams composed rows, and first-time
  `WINDOW` creation allocates the requested bounds directly instead of keeping
  a full-screen canvas allocation. CYD text updates now compose and transfer
  dirty regions, and restore the console overlay only when an update overlaps
  it. The optimized CYD firmware was flashed and the priority demo restarted
  at slice 32 with task weights 8:4:1. The user confirmed the expected relative
  rates and verified suspend, resume, and cancel on hardware. Linux, CYD, and
  VGA32 builds pass after these fixes.
  `graphics::WindowManager`
  now tracks owner, bounds, visibility, stacking order, clipping, and tiled
  layouts. Each window has a retained indexed canvas for pixels and geometric
  primitives at 1, 4, or 8 bits per pixel, defaulting to 4-bit; `compose()`
  produces a row-major palette-index frame and `compositionOrder()` lists
  visible windows bottom-to-top.
  Linux BASIC drawing now uses `WindowedService`; it retains primitive and
  text pixels and presents the composed indexed frame to `ql32-screen.ppm`.
  The CYD and VGA32 adapters now use the same retained canvases and compositor.
  Each session gets a 4-bit default canvas; board budgets permit two full-screen
  canvases, while the composed frame uses one byte per pixel. VGA palette colors
  are quantized to its physical 8-color output. The portable manager supports
  movement, sizing, visibility, raising, tiling, and depth changes through its
  C++ API. BASIC `WINDOW x,y,width,height` now moves/resizes the current
  session's window while keeping drawing coordinates canvas-relative; a BASIC
  `CLOSEWINDOW` releases that session's canvas, and later drawing recreates it;
  `FREE()` can inspect available heap. The embedded BASIC supervisor can start,
  list, and cancel worker sessions; BASIC commands to arrange other sessions'
  windows remain open. Resize preserves
  upper-left pixels and fills newly exposed space with paper color. Linux
  integration verifies that moving an overlapping window restores the
  underlying retained canvas. The user manually confirmed `WINDOW` position
  and size changes work. `GORIGINX()` and `GORIGINY()` expose absolute screen
  coordinates. The manual demo is `examples/window-manager.bas`.
  The CYD composed-output regression is hardware-verified for window
  move/resize, clipping, retained drawing, full-screen allocation, and all four
  orientations. VGA32 composed output remains unverified because that board is
  not physically available.
- **Universal channel model:** reserve `#0` for the CON-like command/error
  console and keyboard input, and `#1` for user/program output and graphics.
  Default `PRINT` and graphics target `#1`; command input, BASIC `INPUT`, and
  system/error messages use `#0`. Allocate other channel IDs and endpoint
  resources on demand. Every opened endpoint receives a unique per-session
  handle in one channel namespace through generic `OPEN(deviceKind,
  parameters...)`; device kinds are typed values, and window geometry uses
  numeric arguments rather than an encoded string. `OPEN` assigns channel IDs
  automatically; explicit channel-number opens are not part of the design.
  I/O and graphics commands accept
  explicit channel selectors, for example `PRINT #out,...`, `LIST #out`, and
  `CIRCLE #window,...`. `CHANNEL #channel` changes the default for unqualified
  output. `CHANNEL #source TO #destination` directly binds the source to the
  destination's current endpoint for all operations. The one-step binding
  shares endpoint state, retains the source's original endpoint, and resets
  with `CHANNEL #source TO #source`; channel-to-channel chains are not kept.
  Closing a file endpoint resets aliases that point to it; reserved-channel
  close/reopen is not implemented.
  `GETCHANNEL(KChannelCount)` returns open-channel count,
  `GETCHANNEL(KChannelHandle, position)` returns a handle by zero-based
  position in ascending-handle order, and
  `GETCHANNEL(KChannelType, #handle)` returns a symbolic endpoint type.
  Enumeration includes reserved `#0`/`#1` and opened file, serial, and null
  channels. `CONST` module declarations now support numeric and string literals;
  `ql32.inc` defines the channel selectors and device kinds. Prefer general operations parameterized by included
  constants over a growing set of operation-specific keywords.
  `CHANNEL` default and route commands, plus output routing for `PRINT`,
  `WRITE`, `LIST`, and `DIR`, are implemented per session. Graphics commands
  accept explicit channel selectors and follow the same default/routes; the
  resolved endpoint must currently be graphics window `#1`, except null
  graphics output is discarded. Text output can target `#0`, `#1`, or an open
  file, serial, or null channel. `GETCHANNEL` enumerates all these handles.
  `READ$` and `EOF` work on file channels.
  `INPUT` reads a line from keyboard channel `#0` asynchronously or an explicit
  file channel; prompts default to `#1` and may select an independent channel.
  Keyboard input suspends only its session. `#channel AT offset`, `IN count,
  buffer$`, and `OUT count, buffer$` implement file seeking and exact-byte
  transfers (maximum 8192 bytes) on Linux and ESP32 SD adapters. The portable
  `ChannelRegistry` allocates opened file, serial-output, and null-output
  handles in one namespace; `OPEN(KDeviceFile, path, mode)`,
  `OPEN(KDeviceSerial)`, and `OPEN(KDeviceNull)` are available, while the legacy
  file form remains supported. Registry metadata and opened-handle lifecycle
  are shared across these endpoints. Serial/null input and registry ownership
  of reserved `#0`/`#1` remain open work. On CYD, serial text and byte output,
  null text/byte/graphics discard, endpoint type/count enumeration, and
  close/reopen were checked after flashing; the serial port is released.
  `CLOSEWINDOW` remains interim graphics-only behavior.
- **Test state:** current source changes are local, uncommitted, and unpushed.

Channel-based networking is deferred to the backlog; it is not part of the
current M6 Wi-Fi station acceptance. File channels support text-line I/O,
exact-byte `IN`/`OUT`, and `AT`; serial is output-only and null is a sink.

## Pending language features

- Implemented QL/32-specific unsigned 32-bit bit functions (`BAND`, `BOR`,
  `BXOR`, `BNOT`, `ROL`, `ROR`), hexadecimal/binary literals (`&H`, `&B`),
  and formatting (`HEX$`, `BIN$`). All 53 Linux tests pass both normally and
  under ASan/UBSan. CYD and VGA32 firmware builds pass; hardware checks remain
  pending.
- Add a string-parsing facility that splits text using a caller-supplied
  delimiter string. Include an option to remove non-printable characters from
  the beginning and end of each parsed word, so input such as
  `"ahoj, nazdar , cau"` can yield `"ahoj"`, `"nazdar"`, and `"cau"`.
  Decide the public function/command name, result representation, and behavior
  for empty delimiters and empty words during syntax design.
- Decide whether to add OPL-inspired indirect procedure calls using a string
  variable (the proposed `@name$` form). Define lookup, argument passing, and
  behavior when the variable is empty or names no procedure before adding it.
- Slice assignment and slicing dimensioned string arrays remain unimplemented.
- Add palette update and palette cycling commands. Keep `INK` and `PAPER`
  indexed; the initial eight entries use the QL color ordering. Specify palette
  mutation and cycle semantics when this backlog item is designed.
- Add network connections as channels, so BASIC can open a network endpoint
  and use channel I/O on it. Design endpoint naming, client/server roles,
  transport, timeouts, buffering, EOF, and error behavior before
  implementation. This is deferred beyond M6 Wi-Fi association/status.

## Starting a new chat

After cloning or pulling the repository, point the new chat at
`PROJECT-HANDOFF.md` and ask it to continue the next work item. The chat
history itself is not required: this handoff and the linked specification,
decisions, references, and `AGENTS.md` carry the project context.
