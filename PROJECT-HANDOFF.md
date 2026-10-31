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
- The Linux CLI reads complete source from a `.bas` file, accepts stdin source/batches, and opens a persistent line-oriented REPL on an interactive terminal. The REPL keeps session and numbered-program state between entries and reports a failed entry without exiting.
- Implemented language coverage includes assignments; numeric and string values; expression and `PRINT` rules; comments; block `IF`; block and inline `FOR` with mixed range lists; named `NEXT` and `EXIT`; block and inline `REPeat`; procedure/function definitions, parameters, invocation locals, return, forward calls, recursion, and array-by-reference parameters; zero-based inclusive arrays; block `SELect ON` with lists, ranges, and `REMAINDER`; keyword abbreviations for the supported keyword set; and canonicalized `LIST` output.
- String slicing is implemented for scalar strings using one-based inclusive `text$(start TO end)` syntax. The numeric `LEN(string$)` built-in returns UTF-8 code-point length and is covered by runtime tests. Slice assignment and slicing dimensioned string arrays are not implemented yet.
- Numbered lines are validated against candidate program state before replacement, while incomplete block prefixes and forward calls are accepted. Calls unresolved in the complete program are diagnosed by `RUN`. Tests cover replacement rollback, incremental block entry, REPL recovery, interpreter isolation, array rank/type errors, stored-program errors, and cooperative cancellation through the C++17 cancellation token.
- Known profile limits: `SELect` singleton comparisons use exact binary64 equality and only its block form is supported; allocation is capped at one million elements per array; nested inline loops are outside the explicit M1 grammar. See the specification for other QL/32 choices and deferrals.
- The suite has 48 CTest cases across CLI and runtime behavior, including recursion depth, allocation limits, storage and channel confinement, editor file commands, async UI lifecycle and BASIC dialog/menu round trips, graphics command dispatch/session cleanup, SVG rendering and escaping, FabGL and CYD graphics/console adapters, FabGL keyboard input, ESP32 clock/storage/channel behavior, Linux clock behavior, capability descriptor validation, VGA32 and CYD board profile validation, and single-pass source-order evaluation of FOR ranges. `cmake -S . -B build`, `cmake --build build`, and `ctest --test-dir build --output-on-failure` pass with GNU C++ 15.2 and CMake 4.2.3.
- M1 BASIC core acceptance is complete. The M2 Linux slice adds portable text storage and session-owned channel contracts, sandboxed directory adapters, interactive `LOAD`/`SAVE`/`DIR`, BASIC `OPEN`/`READ$`/`EOF`/`WRITE`/`CLOSE` calls, and a thread-safe asynchronous UI request queue. BASIC `ALERT`/`CONFIRM`, per-interpreter VM suspend/resume, and the Linux console presenter are implemented and tested. M2 Linux acceptance is complete; embedded UI adapters remain platform work. All current changes remain uncommitted and unpushed.

## Decisions already made

- M1 targets a small, verified SMSQ/E SuperBASIC subset. Claim compatibility only when both a reference and conformance test support the behavior; otherwise mark it QL/32-specific.
- Keywords are case-insensitive. The current resolver uses mandatory-letter subsequences for the supported table and canonicalizes keyword tokens in stored lines; broader table coverage against a live SBASIC interpreter remains useful.
- `ELSE IF ... THEN` is nested `IF`, not a distinct keyword.
- M1 `FOR` supports mixed comma-separated values and inclusive stepped ranges; both block and inline forms are supported.
- String literals escape a quote by doubling it. M1 accepts `REMark`, `REM`, and apostrophe comments.
- Scalar procedure/function arguments pass by value; dimensioned array arguments pass by reference, including forwarding through calls.
- Numeric arrays use zero-based inclusive bounds. QL/32 dynamic UTF-8 string arrays differ from historical fixed-length arrays.
- `RUN` clears session variables. Numbered lines replace a prior line with the same number; a blank numbered line deletes it.
- `SELect` block arms accept values, inclusive ranges, and a final `REMAINDER`; exact singleton matching is currently a QL/32-specific choice.
- The language contract and remaining open questions are in
  [`QL32-LANG-001.md`](docs/specification/QL32-LANG-001.md). References and
  provenance are in [`docs/reference/README.md`](docs/reference/README.md).

## Next milestone

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
adapters using fakes; all 45 CTest cases pass. PlatformIO Core 6.2.0,
Arduino-ESP32 2.0.17, and FabGL 1.0.9 build the current image at 754,773 bytes
flash and 30,392 bytes RAM. The user confirmed the USB serial REPL works on the
board. VGA output, PS/2 input, clock, and SD behavior remain unverified. The
CYD target builds with Adafruit ILI9341/GFX and a scrolling TFT PRINT console
at 690,337 bytes flash and 27,856 bytes RAM. The user verified TFT output, the
serial REPL, and a small BASIC `FOR`/`PRINT` loop on hardware; SD behavior still
needs checking. The next
software gap is a board UI presenter connected to
the dialog/menu service. Assess whether the VGA32 prompt also needs a more
complete line editor. M5 hardware acceptance still needs a board run covering
VGA output, keyboard input, clock status, startup loading, and channel
round-trips. Publish changes only when explicitly requested.

## Pending language features

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
- Slice assignment and slicing dimensioned string arrays remain unimplemented.
- Add palette update and palette cycling commands. Keep `INK` and `PAPER`
  indexed; the initial eight entries use the QL color ordering. Specify palette
  mutation and cycle semantics when this backlog item is designed.

## Starting a new chat

After cloning or pulling the repository, point the new chat at
`PROJECT-HANDOFF.md` and ask it to continue the next work item. The chat
history itself is not required: this handoff and the linked specification,
decisions, references, and `AGENTS.md` carry the project context.
