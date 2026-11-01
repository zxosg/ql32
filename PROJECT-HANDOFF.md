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

- The current committed baseline is `a0f1f49` (`Refactor BASIC editor and session UI handling`); the M2 work below is local and uncommitted.
- The Linux CLI reads complete source from a `.bas` file, accepts stdin source/batches, and opens a persistent line-oriented REPL on an interactive terminal. The REPL keeps session and numbered-program state between entries and reports a failed entry without exiting. The portable VM also exposes cooperative `executeSlice`/`resumeSlice` operations; `ql32::vm::SessionScheduler` round-robins interpreter sessions using a configurable bytecode instruction budget.
- `FREE()` is a zero-argument BASIC function returning free-memory bytes through the portable `runtime::MemoryService`; ESP32 reports free 8-bit-capable heap and Linux reports host `MemAvailable`. The retained window canvas packs pixels at 1, 4, or 8 bits and enforces a total canvas-byte budget. BASIC graphics now render through the retained compositor on Linux, CYD, and VGA32; windows default to 4-bit depth.
- Implemented language coverage includes module-level immutable `CONST` declarations for numeric and string literals, assignments; numeric and string values; expression and `PRINT` rules; comments; block `IF`; block and inline `FOR` with mixed range lists; named `NEXT` and `EXIT`; block and inline `REPeat`; procedure/function definitions, parameters, invocation locals, return, forward calls, recursion, and array-by-reference parameters; zero-based inclusive arrays; block `SELect ON` with lists, ranges, and `REMAINDER`; numeric `GO TO`/`GOTO` line targets and QL/32 named labels; keyword abbreviations for the supported keyword set; and canonicalized `LIST` output.
- String slicing is implemented for scalar strings using one-based inclusive `text$(start TO end)` syntax. The numeric `LEN(string$)` built-in returns UTF-8 code-point length and is covered by runtime tests. Slice assignment and slicing dimensioned string arrays are not implemented yet.
- Numbered lines are validated against candidate program state before replacement, while incomplete block prefixes and forward calls are accepted. Calls unresolved in the complete program are diagnosed by `RUN`. Tests cover replacement rollback, incremental block entry, REPL recovery, interpreter isolation, array rank/type errors, stored-program errors, and cooperative cancellation through the C++17 cancellation token.
- Known profile limits: `SELect` singleton comparisons use exact binary64 equality and only its block form is supported; allocation is capped at one million elements per array; nested inline loops are outside the explicit M1 grammar. See the specification for other QL/32 choices and deferrals.
- The suite has 53 CTest cases across CLI and runtime behavior, including recursion depth, allocation limits, GOTO line/label behavior, storage and channel confinement, editor file commands, async UI lifecycle and BASIC dialog/menu round trips, cooperative scheduler slicing and UI suspension, graphics command dispatch/session cleanup, SVG rendering and escaping, FabGL and CYD graphics/console adapters, FabGL keyboard input, ESP32 clock/storage/channel/network behavior, Linux clock behavior, capability descriptor validation, VGA32 and CYD board profile validation, embedded line UI presentation and VM resumption, Wi-Fi BASIC calls, recursive source includes, and single-pass source-order evaluation of FOR ranges. `cmake -S . -B build`, `cmake --build build`, and `ctest --test-dir build --output-on-failure` pass with GNU C++ 15.2 and CMake 4.2.3.
- M1 BASIC core acceptance is complete. The M2 Linux slice adds portable text storage and session-owned channel contracts, sandboxed directory adapters, interactive `LOAD`/`SAVE`/`DIR`, BASIC `OPEN`/`READ$`/`EOF`/`WRITE`/`CLOSE` calls, and a thread-safe asynchronous UI request queue. BASIC `ALERT`/`CONFIRM`, per-interpreter VM suspend/resume, and Linux and embedded line presenters are implemented and tested. M2 Linux acceptance is complete. All current changes remain uncommitted and unpushed.

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
- `SELect` block arms accept values, inclusive ranges, and a final `REMAINDER`; exact singleton matching is currently a QL/32-specific choice.
- The language contract and remaining open questions are in
  [`QL32-LANG-001.md`](docs/specification/QL32-LANG-001.md). References and
  provenance are in [`docs/reference/README.md`](docs/reference/README.md).

## Current milestone status and open points

M2 Linux acceptance is complete. M3 now has a portable session-owned graphics
service contract and VM dispatch for `INK`, `PAPER`, `LINE`, `RECT`, `CIRCLE`,
`TEXT`, and `CLS`. Decisions: one lazy default graphics window per session;
OPL-style integer pixel coordinates with `(0,0)` at the top-left; indexed ink
and paper colors initialized to the QL eight-color order. SuperBASIC remains
the core loop syntax; use OPL selectively for language refinements and as the
managed UI reference, not as a source-compatibility target. Palette updates and
cycling are backlog items. Linux uses a dependency-free SVG debug presenter
that writes one 640x480 `ql32-graphics-session-<id>.svg` snapshot on session
close; `examples/graphics.bas` exercises all implemented commands. A FabGL
VGA graphics adapter maps the portable service to the board canvas. The pinned
`lilygo_vga32` image also includes a portable keyboard contract and FabGL PS/2
adapter, ESP32 clock adapter, SD storage adapter, and SD-backed session-owned
text channels. A second `esp32_2432s028` profile now targets the common yellow
ESP32-2432S028R board with ILI9341 TFT graphics and serial terminal input. Its
TFT adapter maps the QL eight-color indexed palette to RGB565, while SD uses
the board's separate SPI bus. The shared BASIC VM is linked into firmware, with serial output
and a prompt accepting input from both USB serial and PS/2. On
startup it runs `startup.bas` from the SD root when present and falls back to a
built-in demo otherwise. The channel adapter supports `OPEN`/`READ$`/`EOF`/
`WRITE`/`CLOSE` with root-confined paths. Host coverage exercises the hardware
adapters using fakes; all 53 CTest cases pass. PlatformIO Core 6.2.0,
  Arduino-ESP32 2.0.17, and FabGL 1.0.9 build the current image using 1,286,905
bytes of the custom 4,063,232-byte app partition (31.7%) and 52,416 bytes static RAM.
The user confirmed the USB serial REPL works on the
board. VGA output, PS/2 input, clock, and SD behavior remain unverified. The
CYD target builds with Adafruit ILI9341/GFX and a scrolling TFT PRINT console
using 1,225,913 bytes of the custom 4,063,232-byte app partition (30.2%) and
49,864 bytes static RAM. The
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
cover state and call behavior; both firmware profiles compile. `INCLUDE` now
expands recursively included BASIC source from the storage service, with cycle,
root-confinement, depth, size, and module-preamble checks. Linux tests cover
nested libraries and numbered programs. Physical Wi-Fi
association remains pending. The
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

- **CYD display:** hardware-verified. All four rotations, rotation 0's 320x240
  query, red palette output, scrolling text strip, and edge graphics pass.
- **VGA32 hardware:** verify VGA drawing, PS/2 input, clock display, SD mount,
  `startup.bas`, and SD channel read/write round-trip. Its embedded line UI
  presenter also needs physical interaction checks.
- **Wi-Fi on hardware:** on either ESP32 board, connect to a local access point
  and verify status, address, signal, and disconnect behavior per
  [`QL32-NET-001.md`](docs/specification/QL32-NET-001.md).
- **Multi-session UI/display:** a portable cooperative round-robin scheduler
  now runs independent interpreter sessions in bounded bytecode slices, and
  UI waits suspend only their owner. The portable `graphics::WindowManager`
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
  `FREE()` can inspect available heap. A BASIC supervisor to control other
  sessions remains open. Resize preserves
  upper-left pixels and fills newly exposed space with paper color. Linux
  integration verifies that moving an overlapping window restores the
  underlying retained canvas. The user manually confirmed `WINDOW` position
  and size changes work. `GORIGINX()` and `GORIGINY()` expose absolute screen
  coordinates. The manual demo is `examples/window-manager.bas`.
  Physical regression checks for the new
  composed output remain open on both boards; prior CYD rotation, palette,
  console scrolling, and direct graphics checks were done before this path.
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
  output. `CHANNEL #source TO #destination` is approved to directly rebind the
  source handle to the destination's current endpoint for all operations; the
  source retains its original endpoint, restored with
  `CHANNEL #source TO #source`. The current implementation still resolves
  per-session routes at operation time and must be refactored to the
  channel-table binding model.
  `GETCHANNEL(KChannelCount)` returns open-channel count,
  `GETCHANNEL(KChannelHandle, position)` returns a handle by zero-based
  position in ascending-handle order, and
  `GETCHANNEL(KChannelType, #handle)` returns a symbolic endpoint type.
  Enumeration currently includes reserved `#0`/`#1` and open file channels.
  `CONST` module declarations now support numeric and string literals;
  `ql32.inc` defines the named channel selectors. Prefer general operations parameterized by included
  constants over a growing set of operation-specific keywords.
  `CHANNEL` default and route commands, plus output routing for `PRINT`,
  `WRITE`, `LIST`, and `DIR`, are implemented per session. Graphics commands
  accept explicit channel selectors and follow the same default/routes; the
  resolved endpoint must currently be graphics window `#1`. Text output can
  target `#0`, `#1`, or an open file channel. `GETCHANNEL` enumerates reserved
  channels and open file channels. `READ$` and `EOF` work on file channels.
  `INPUT` reads a line from keyboard channel `#0` asynchronously or an explicit
  file channel; prompts default to `#1` and may select an independent channel.
  Keyboard input suspends only its session. `#channel AT offset`, `IN count,
  buffer$`, and `OUT count, buffer$` implement file seeking and exact-byte
  transfers (maximum 8192 bytes) on Linux and ESP32 SD adapters. Serial and
  null input endpoints, generic typed `OPEN`, and a unified endpoint registry remain open
  work. The null endpoint is planned, not yet
  available. Closing an opened channel releases its endpoint resources;
  `CLOSEWINDOW` remains interim graphics-only behavior.
- **Test state:** current source changes are local, uncommitted, and unpushed.

Channel-based networking is deferred to the backlog; it is not part of the
current M6 Wi-Fi station acceptance. Existing `OPEN`/`READ$`/`WRITE` calls
handle file-backed text channels only.

## Pending language features

- Add standard string functions `LEFT$`, `RIGHT$`, and `MID$`; specify whether
  their indexing and out-of-range behavior follow SuperBASIC or another
  documented QL/32 rule, and cover the chosen behavior with conformance tests.
- Add bitwise integer operations: bitwise AND, OR, XOR, invert, and bit
  rotations. Keep these distinct from the existing logical `AND`, `OR`, and
  `XOR` operators. Define the integer width, signedness, rotation behavior,
  conversion APIs, and source syntax before implementation.
- Add hexadecimal and binary number support, including conversions to and
  from numeric values and literals with explicit, documented prefixes. Choose
  prefixes and define overflow, signed-value, and formatting behavior as part
  of the language contract.
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
