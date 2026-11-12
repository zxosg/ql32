# QL/32 BASIC Language Reference

This book describes the implemented QL/32 language and interactive shell. It is
based on the compiler/runtime and the approved language profile in
[QL32-LANG-001](../specification/QL32-LANG-001.md). Unimplemented or planned
features are not listed as available language features. QL/32 is inspired by
SMSQ/E SuperBASIC; compatibility is stated per feature where it has been
verified. OPL-inspired UI facilities and new hardware APIs are QL/32 APIs.

## Contents

- [How to read syntax and target tags](#how-to-read-syntax-and-target-tags)
- [Language concepts](#language-concepts)
- [Supported targets and drivers](#supported-targets-and-drivers)
- [Keywords and declarations](#keywords-and-declarations)
- [Statements and built-in commands](#statements-and-built-in-commands)
- [Built-in functions](#built-in-functions)
- [Interactive shell commands](#interactive-shell-commands)
- [Constants and include files](#constants-and-include-files)

## How to read syntax and target tags

Square brackets mean an optional item; `...` means repeat the preceding item.
`#channel` is a channel selector, and names ending in `%` or `$` are integer
and string variables. Keywords are case-insensitive. Commands and procedures
are written without call parentheses; functions use parentheses.

Target tags mark behavior that depends on a particular runtime or driver:

| Tag | Meaning |
| --- | --- |
| `[HOST]` | Linux and macOS host builds |
| `[ESP32]` | ESP32 Arduino firmware profiles |
| `[CYD]` | ESP32-2432S028 profile; prefer `esp32_2432s028_spiffs` |
| `[TENSTAR]` | QL/32 Tenstar ESP32-S3 profile |
| `[VGA32]` | LILYGO VGA32 and Olimex ESP32-SBC-FabGL profiles |
| `[DISPLAY]` | A target with a graphics display service |
| `[TOUCH]` | CYD screen-touch event driver |
| `[NETWORK]` | A target with the network service enabled |
| `[HARDWARE]` | Hardware channel or managed-memory API; available device drivers vary by target |

An untagged entry is part of the portable language or shell. A tag means that
the entry relies on that target or service; it does not claim that every
physical-board behavior has been verified. See the driver matrix below.

## Language concepts

### Source and modules

- A program is UTF-8 text, commonly saved as `.bas`. Newline ends a source
  line; `:` separates statements on one line. Decimal line numbers are
  optional.
- Names and keywords are case-insensitive. Strings use double quotes, with
  `""` for a quote inside a string. `REM` or apostrophe starts a line comment.
- The compiler checks the whole module and verifies its bytecode before
  execution. A lexical, syntax, or compile-time semantic failure does not run
  part of the module.
- `INCLUDE` expands relative BASIC source files before compilation. Includes
  and module declarations belong at the start of the source, before executable
  statements and definitions.
- A program runs in a session. Variables, random state, channels, hardware
  handles, memory buffers, graphics windows, and task state belong to that
  session and are cleaned up when it ends.

### Values, names, and arrays

| Form | Value type | Initial value |
| --- | --- | --- |
| `amount` | Numeric (binary64 by default) | `0` |
| `count%` | Signed 32-bit integer | `0` |
| `title$` | Dynamic UTF-8 string | `""` |

Variables are implicitly created on assignment. `%` assignment requires an
exact integer in the signed 32-bit range. `$` assignment requires a string.
Numbers do not automatically convert to strings or vice versa. Conditions
require numbers; zero is false and any other finite number is true. Boolean
operators and comparisons return numeric `1` or `0`.

`DIM a%(3)` creates indices `0` through `3`, inclusive. Arrays may have
multiple dimensions and are checked on every access. Numeric and string array
elements start at their type's default value. Dimensioned arrays passed to a
procedure or function are shared by reference.

### Expressions and operators

Precedence, highest first: parentheses; unary `+`/`-`; `^`; `*`, `/`, `DIV`,
`MOD`; binary `+`/`-`; one comparison; `NOT`; `AND`; `OR`/`XOR`. Operators at
the same binary level associate left to right. Thus `-2^2` means `(-2)^2` and
`2^3^2` means `(2^3)^2`. Parentheses make grouping explicit.

Numeric comparisons compare numbers. String `=` and `<>` compare strings
exactly and case-sensitively. Binary `+` adds two numbers or joins two strings.
`AND`, `OR`, `XOR`, and `NOT` are logical operators, not bitwise operators;
use the bit functions for masks. Division by zero, invalid numeric results,
integer overflow, and bad conversions are runtime errors.

### Procedures, functions, and scope

Definitions are at module level. Procedures perform actions and are called as
statements; functions return values and appear in expressions. Parameters are
listed in parentheses in the definition. `LOCAL` declares invocation-local
names. Lookup checks locals and parameters before session globals. Recursion is
supported up to a bounded call-stack limit. A function that reaches its end
without returning a value raises a runtime error.

### Control flow and data

`IF`, `FOR`, `REPeat`, and `SELect ON` support block forms. `FOR` also supports
an inline form. Named `NEXT` and `EXIT` control loops. `GOTO` may target a
numbered line or a QL/32 named label; labels are a QL/32 extension. `DATA`
declares an ordered stream, `READ` consumes values, and `RESTORE` moves its
cursor. Each BASIC session has its own data cursor.

### I/O and blocking operations

Channels are session-owned handles. `#0` is the command/error console and
keyboard input; `#1` is the default user output and graphics channel. `OPEN`
allocates additional handles. File, serial, window, null, hardware, and touch
channels are selected through services available on the current target.

A command that waits for UI input, a touch event, serial input, or a task
completion suspends only its BASIC session. Other scheduler sessions continue.

## Supported targets and drivers

| Target/profile | Display and input | Storage | Driver notes |
| --- | --- | --- | --- |
| Linux | Host graphics compositor; terminal input | Directory-backed files | Virtual GPIO, ADC, SPI, I2C, serial, parallel, capacitive touch, and timer drivers for development and tests. |
| macOS | Host graphics compositor; terminal input | Directory-backed files | Same virtual service model as Linux. |
| `esp32_2432s028_spiffs` `[CYD]` | 320×240 TFT; serial REPL; screen touch | Internal SPIFFS as `FFS:` and SD as `SDC:` | ESP32 GPIO, ADC, SPI, I2C, UART, parallel, capacitive touch, timer, Wi-Fi, graphics, and screen-touch events. Touch event behavior verified at rotation 0. |
| `ql32_tenstar_esp32s3` `[TENSTAR]` | 240×135 TFT; USB serial REPL | SPIFFS as `FFS:` | GPIO, ADC, SPI, I2C, UART, timer, Wi-Fi, and graphics services. Some board checks remain open. |
| `lilygo_vga32` `[VGA32]` | 320×200 VGA; PS/2 input | SD as `SDC:` | FabGL display/input with ESP32 GPIO, ADC, SPI, I2C, UART, parallel, capacitive touch, timer, and network service adapters. Physical verification is incomplete. |
| `olimex_esp32_sbc_fabgl` `[VGA32]` | 320×200 VGA; PS/2 input | SD and configured flash filesystem | FabGL display/input with ESP32 hardware services. Physical verification is incomplete. |

`esp32s3_tft`, `esp32p4_tft`, and experimental Olimex PSRAM/DMA environments
are not presented here as supported user targets. The language APIs are
portable; `OPEN` reports unavailable when the selected driver is absent.

### Hardware drivers

These device kinds use `OPEN(kind, ...)`. Pin and bus aliases are supplied by
`ql32.inc` and board-specific `ql32-hardware.inc` files.

| Driver | Open form | Operations | Availability |
| --- | --- | --- | --- |
| GPIO | `OPEN(KDeviceGPIO, pin, mode[, pull[, KPinInitialValue, value]])` | `IN(channel)`, `OUT channel, value` | `[HARDWARE]` host virtual driver and ESP32 profiles with GPIO capability |
| ADC | `OPEN(KDeviceADC, pin[, resolution])` | `IN(channel)` returns raw counts | `[HARDWARE]` host virtual and ESP32 profiles with analog capability |
| SPI | `OPEN(KDeviceSPI, bus, csPin, clockHz, mode[, bitOrder])` | `XFER`, buffered I/O | `[HARDWARE]` host virtual and ESP32 profiles with bus capability |
| I2C | `OPEN(KDeviceI2C, bus, address[, clockHz])` | Scalar register I/O, `IOREAD`, `IOWRITE`, `XFER` | `[HARDWARE]` host virtual and ESP32 profiles with bus capability |
| Serial | `OPEN(KDeviceSerial, port, baud[, frame[, flow]])` | `WRITE`, `READ$`, `IOREAD`, `IOWRITE` | `[HARDWARE]` configured UART on host virtual and ESP32 profiles; UART 0 is reserved for REPL |
| Parallel GPIO | `OPEN(KDeviceParallel, bit7Pin,...,bit0Pin, KPinMode, mode[, KPinInitialValue, byte])` | `IN`, `OUT` | `[HARDWARE]` host virtual and profiles with parallel capability; not Tenstar |
| Capacitive touch pad | `OPEN(KDeviceCapTouch, pad)` | `IN(channel)` returns 0 or 1 | `[HARDWARE]` host virtual and profiles with capacitive-touch capability; not Tenstar |
| Screen touch | `OPEN(KDeviceTouch)` | `INPUT #ch,type%,x%,y%,window%` waits for an event | `[TOUCH]` CYD only |
| Timer | `OPEN(KDeviceTimer, periodMs, KTimerOneShot\|KTimerPeriodic)` | `IN(channel)` returns elapsed expirations | `[HARDWARE]` host virtual and ESP32 profiles with timer capability |
| IrDA | Reserved | None | Not available |

Host hardware is simulated for development, not a physical device. ESP32
transfers preserve bytes and use native target byte order for word/long buffer
access. See [QL32-HW-001](../specification/QL32-HW-001.md) for parameter
limits and board pin ownership.

## Keywords and declarations

| Keyword | Syntax | Description | Short example |
| --- | --- | --- | --- |
| `PRINT` | `PRINT [#channel,] [expression {separator expression} [separator]]` | Writes values to the selected output. `;` joins directly, `!` spaces, `,` advances to an 8-column tab, and `\` emits a line feed. A trailing `;` suppresses the final line feed. | `PRINT "x="; x` |
| `AT` | `AT [#channel,] row, column` | Positions text output using one-based row and column coordinates. | `AT 1, 1` |
| `IF` | `IF condition THEN statement` or `IF condition THEN ... [ELSE ...] END IF` | Conditional inline or block execution. Block form may use nested `IF` after `ELSE`. | `IF score% > 0 THEN PRINT score%` |
| `THEN` | `IF condition THEN statement` | Separates an `IF` condition from its branch. | `IF ready% THEN PRINT "Go"` |
| `ELSE` | `... ELSE statement` or block `ELSE` | Runs when the preceding condition is false. `ELSE IF` is a nested `IF`, closed by its own `END IF`. | `IF n% = 0 THEN PRINT "zero" ELSE PRINT "other"` |
| `END IF` | `END IF` | Closes a block `IF`. | `IF ok% THEN`<br>`  PRINT "yes"`<br>`END IF` |
| `FOR` | `FOR name = value TO value [STEP value] [,...] : statements` or block `FOR ...` | Counts through one or more values or inclusive stepped ranges. | `FOR i% = 1 TO 3: PRINT i%` |
| `TO` | `FOR name = start TO end [STEP step]` | Gives the end of a loop range; also used in `RESTORE` and `SELECT` range arms. | `FOR i% = 1 TO 5` |
| `STEP` | `... TO end STEP amount` | Sets a nonzero loop increment; direction must approach the endpoint. | `FOR i% = 10 TO 0 STEP -2: PRINT i%` |
| `END FOR` | `END FOR [name]` | Closes a block `FOR`. Inline `FOR` has no closing keyword. | `FOR i% = 1 TO 2`<br>`  PRINT i%`<br>`END FOR i%` |
| `NEXT` | `NEXT name` | Advances a named `FOR`; in a `REPeat` block, restarts that loop. | `NEXT i%` |
| `REPeat` | `REPeat name[: statements]` or `REPeat name ... END REPeat name` | Starts an indefinite named loop. | `REPeat again: PRINT "tick"` |
| `END REPeat` | `END REPeat name` | Closes a block repeat loop and returns to its start. | `END REPeat again` |
| `EXIT` | `EXIT name` | Leaves the named `FOR` or `REPeat` loop. | `IF done% THEN EXIT work` |
| `SELect` | `SELect ON expression ... END SELect` | Starts a numeric multi-way selection block. | `SELect ON choice%`<br>`= 1: PRINT "one"`<br>`END SELect` |
| `ON` | `SELect ON expression` | Introduces the value tested by `SELect`. | `SELect ON mode%` |
| `REMAINDER` | `= REMAINDER` | Selects the final `SELect` arm when no prior arm matches. | `= REMAINDER: PRINT "other"` |
| `END SELECT` | `END SELECT` | Closes a `SELect ON` block. Singleton arms use `= value`; ranges use `= first TO last`. | `= 2 TO 4: PRINT "range"` |
| `DEFINE` | `DEFine PROCedure name(parameters)` or `DEFine FuNction name(parameters)` | Begins a procedure or function definition. `DEF PROC` and `DEF FN` are accepted abbreviations. | `DEF PROC Greet(name$)` |
| `PROCEDURE` | `DEFine PROCedure name(parameters)` | Defines a statement-callable operation. | `DEF PROC Greet(who$): PRINT "Hi "+who$` |
| `FUNCTION` | `DEFine FuNction name(parameters)` | Defines a value-returning function. | `DEF FN Twice(x): RETURN x*2` |
| `END DEFINE` | `END DEFine` | Ends a procedure/function definition. | `END DEFine` |
| `LOCAL` | `LOCal name[, name ...]` | Declares names local to the current procedure or function call. | `LOCAL total%` |
| `RETURN` | `RETURN [expression]` | Returns from a procedure, or supplies the required function result. | `RETURN total%` |
| `DIM` | `DIM array(bound[, bound ...])` | Creates an array with zero-based inclusive upper bounds. | `DIM grid%(4, 4)` |
| `GOTO` | `GOTO lineNumberOrLabel` or `GO TO lineNumberOrLabel` | Jumps to a stored numbered line or QL/32 named label. | `GOTO retry` |
| `GO` | `GO TO lineNumberOrLabel` | First word of the spaced jump spelling. | `GO TO 100` |
| `DATA` | `DATA expression[, expression ...]` | Declares ordered values read later by `READ`. Expressions are evaluated when read. | `DATA 12, "hello"` |
| `READ` | `READ variable[, variable ...]` | Assigns the next values in the session's data stream. | `READ count%, name$` |
| `RESTORE` | `RESTORE [lineNumberOrLabel]` | Resets the data cursor to the start, or to the first data item at/after a target. | `RESTORE 100` |
| `STOP` | `STOP` | Ends the current run while retaining enough state for shell `CONTINUE`. | `IF pause% THEN STOP` |
| `REM` | `REM text` or `' text` | Starts a comment through the end of the source line. `REMark` is also accepted. | `REM initialize counters` |
| `CONST` | `CONST name = [sign] number` or `CONST name$ = string` | Declares an immutable compile-time numeric or string value in the module preamble. | `CONST Limit = 8` |
| `OPTION` | `OPTION KOptMathSingle\|KOptMathDouble\|KEspCpuFreq80\|KEspCpuFreq160\|KEspCpuFreq240` | Selects math precision per module or requests device-wide ESP32 CPU frequency. Include `ql32.inc`; options must precede definitions and executable code. | `OPTION KOptMathSingle` |
| `DIV` | `integer DIV integer` | Floor quotient for integral Int32 operands. | `q% = 7 DIV 3` |
| `MOD` | `integer MOD integer` | Remainder paired with floor `DIV`; divisor cannot be zero. | `r% = 7 MOD 3` |
| `AND` | `number AND number` | Logical AND; returns `1` when both operands are nonzero. | `IF a% AND b% THEN PRINT "both"` |
| `OR` | `number OR number` | Logical OR; returns `1` when either operand is nonzero. | `IF a% OR b% THEN PRINT "some"` |
| `XOR` | `number XOR number` | Logical exclusive OR; returns `1` when exactly one operand is nonzero. | `PRINT 1 XOR 0` |
| `NOT` | `NOT number` | Logical negation; returns `1` for zero and `0` otherwise. | `IF NOT ready% THEN PRINT "wait"` |
| `INCLUDE` | `INCLUDE "relative/path.bas"` | Preprocessor directive that expands BASIC source and definitions into the module. Supports nested includes with cycle and size limits. | `INCLUDE "ql32.inc"` |

Keyword spellings accept the minimum abbreviations encoded by the SuperBASIC
profile (`REP`, `DEF`, `PROC`, `FN`, `LOC`, `RET`, `SEL`, `REM`, etc.). `LIST`
shows recognized words in canonical form. QL/32-specific labels, `CONST`,
`OPTION`, source inclusion, and several expression rules are not claims of
SMSQ/E compatibility.

## Statements and built-in commands

### Text, files, and channels

| Command | Syntax | Description | Short example |
| --- | --- | --- | --- |
| `INPUT` | `INPUT [#prompt,] [promptExpression;] [#read,] variable[, variable ...]` | Reads a line into one or more variables. Keyboard input suspends only this session; file input reads synchronously. | `INPUT "Name? "; name$` |
| `WRITE` | `WRITE channel, string$` | Writes one string and a newline to an open output channel. | `WRITE file%, "hello"` |
| `CLOSE` | `CLOSE channel` | Releases an opened endpoint and its resources. | `CLOSE f%` |
| `OUT` | `OUT channel, value` or `OUT channel, register, value` | Writes a scalar device value or register. `[HARDWARE]` | `OUT gpio%, 1` |
| `XFER` | `XFER #channel, txAddress, txLength, rxAddress, rxLength` | Performs a raw SPI transaction; I2C supports raw transfer through its adapter. `[HARDWARE]` | `XFER #spi%, tx%, 2, rx%, 2` |
| `CHANNEL` | `CHANNEL #channel` or `CHANNEL #source TO #destination` | Changes the default output channel or session-local channel route. | `CHANNEL #0` |
| `GETNET` | `GETNET ssid$, password$` | Connects to a Wi-Fi network. `[NETWORK]` | `GETNET ssid$, pass$` |
| `DISNET` | `DISNET` | Disconnects the network service. `[NETWORK]` | `DISNET` |
| `FLASH` | `FLASH mode` inside `IPRINT` or `BUSY` options | Selects a toast flash mode; it is an option in these commands, not a standalone display command. `[DISPLAY]` | `IPRINT FLASH KInfoFlashText, "Ready"` |

### Graphics and display

All entries in this subsection require a graphics display service `[DISPLAY]`.

| Command | Syntax | Description | Short example |
| --- | --- | --- | --- |
| `INK` | `INK [#window,] color` | Sets the current drawing/text ink palette index. | `INK 2` |
| `PAPER` | `PAPER [#window,] color` | Sets the current paper palette index. | `PAPER 0` |
| `OVER` | `OVER [#window,] mode` | Chooses set, clear, or XOR drawing behavior (`0`, `1`, `2`). | `OVER 2` |
| `PLOT` | `PLOT [#window,] x, y` | Draws one pixel in the current ink. | `PLOT 10, 12` |
| `LINE` | `LINE [#window,] x1, y1, x2, y2` | Draws a line between pixel coordinates. | `LINE 0, 0, 30, 20` |
| `RECT` | `RECT [#window,] x, y, width, height[, fill]` | Draws a rectangle; nonzero `fill` makes it solid. | `RECT 4, 4, 30, 20, 1` |
| `CIRCLE` | `CIRCLE [#window,] x, y, radius[, fill]` | Draws a circle at a center point. | `CIRCLE 40, 30, 12` |
| `TEXT` | `TEXT [#window,] x, y, string$` | Draws a string at a pixel position. | `TEXT 2, 2, "QL/32"` |
| `CLS` | `CLS [#window][, ink, paper]` | Clears the selected graphics surface; optional colors remain active. | `CLS 0, 7` |
| `WINDOW` | `WINDOW [#window,] x, y, width, height` | Positions and sizes a retained graphics window. `[DISPLAY]` | `WINDOW 0, 0, 160, 120` |
| `CLOSEWINDOW` | `CLOSEWINDOW [#window]` | Releases the selected window surface. `[DISPLAY]` | `CLOSEWINDOW` |
| `ROTATE` | `ROTATE degrees` | Sets display orientation where the target presenter supports rotation. `[DISPLAY] [CYD]` | `ROTATE 90` |
| `MODE` | `MODE id` | Selects a display mode exposed by the runtime. `[DISPLAY]` | `MODE 0` |
| `GUPDATE` | `GUPDATE [0\|1]` | `0` suppresses automatic presentation, bare command presents now, `1` resumes automatic presentation. `[DISPLAY]` | `GUPDATE 0` |
| `LOADFONT` | `font% = LOADFONT(path$)` | Loads a session-owned bitmap font. `[DISPLAY]` | `font% = LOADFONT("font.fnt")` |
| `FONT` | `FONT [#channel,] handle, style` | Selects a normal text-output font and style flags. `[DISPLAY]` | `FONT font%, 1` |
| `GFONT` | `GFONT [#window,] handle, style` | Selects the graphics `TEXT` font and style. `[DISPLAY]` | `GFONT font%, 0` |
| `UNLOADFONT` | `UNLOADFONT handle` | Releases a loaded font. `[DISPLAY]` | `UNLOADFONT font%` |

### User interface

UI requests use the platform-neutral UI service. The host CLI and embedded
display presenters provide different interaction surfaces.

| Command | Syntax | Description | Short example |
| --- | --- | --- | --- |
| `ALERT` | `ALERT title$, message$` | Shows an acknowledgement dialog and resumes this session after dismissal. | `ALERT "Notice", "Saved"` |
| `IPRINT` | `IPRINT [INK color,] [PAPER color,] [FLASH mode,] text$[, seconds[, position]]` | Posts a temporary graphical information toast. `[DISPLAY]` | `IPRINT "Done"` |
| `BUSY` | `BUSY [INK color,] [PAPER color,] [FLASH mode,] text$[, position]` or `BUSY OFF` | Schedules a busy toast after three seconds; it lasts until turned off or the run ends. `[DISPLAY]` | `BUSY "Working"` |
| `DTEXT` | `DTEXT dialog, label$` | Adds static text to a dialog definition. | `DTEXT dialog%, "Settings"` |
| `DEDIT` | `DEDIT dialog, label$, initial$` | Adds an editable text field to a dialog. | `DEDIT dialog%, "Name", name$` |
| `DINT` | `DINT dialog, label$, initial, min, max` | Adds an integer field with inclusive bounds. | `DINT dialog%, "Count", 2, 0, 9` |
| `DFLOAT` | `DFLOAT dialog, label$, initial, min, max` | Adds a floating-point field with inclusive bounds. | `DFLOAT dialog%, "Gain", 1.0, 0.0, 2.0` |
| `DCHOICE` | `DCHOICE dialog, label$, initialChoice, choices$` | Adds a choice field; choices are separated by `|` and indices are one-based. | `DCHOICE dialog%, "Mode", 1, "Slow|Fast"` |
| `DCHECK` | `DCHECK dialog, label$, initialState` | Adds a check field with initial state `0` or `1`. | `DCHECK dialog%, "Enabled", 1` |
| `DBUTTONS` | `DBUTTONS dialog, accept$, cancel$` | Sets dialog action labels. | `DBUTTONS dialog%, "OK", "Cancel"` |
| `DIALOG` | `result% = DIALOG(dialog)` | Presents a dialog and returns `1` on submit or `0` on cancel. | `result% = DIALOG(dialog%)` |
| `DGET` | `value = DGET(dialog, fieldNumber)` | Reads a numeric dialog field result. | `count% = DGET(dialog%, 3)` |
| `DGET$` | `value$ = DGET$(dialog, fieldNumber)` | Reads a string dialog field result. | `name$ = DGET$(dialog%, 2)` |
| `MINIT` | `menu% = MINIT(title$)` | Creates a session-owned menu definition. | `menu% = MINIT("File")` |
| `MCARD` | `MCARD menu%, id, label$, shortcut$` | Adds a stable-ID command to a menu. | `MCARD menu%, 10, "Open", "O"` |
| `MENU` | `item% = MENU(menu%)` | Presents a menu and returns the selected item ID. | `item% = MENU(menu%)` |
| `CONFIRM` | `answer% = CONFIRM(title$, message$)` | Shows a confirmation request and returns its result. | `answer% = CONFIRM("Exit", "Continue?")` |

### Tasks and runtime control

Task supervisor commands are available in the embedded scheduler REPL. BASIC
`TASK` calls are available where task control is installed.

| Command | Syntax | Description | Short example |
| --- | --- | --- | --- |
| `TASK` | `TASK "path.bas"[, KTaskWait]` | Starts a worker session; optional wait suspends only the caller until completion. | `TASK "worker.bas"` |
| `TASKVIEW` | `TASKVIEW id, x, y, width, height[, offsetX, offsetY]` | Positions/clips a task's graphics canvas. `[DISPLAY]` | `TASKVIEW id%, 0, 0, 120, 120` |
| `KILL` | `KILL taskId` | Stops an active task and cleans its session resources. | `KILL id%` |
| `PAUSE` | `PAUSE milliseconds` | Suspends only the current task; `PAUSE 0` yields to the scheduler. | `PAUSE 100` |
| `RANDOMIZE` | `RANDOMIZE seed` | Resets the current session's deterministic random generator. | `RANDOMIZE 1234` |

The shell-only task controls are documented in [Interactive shell commands](#interactive-shell-commands).

### Hardware and memory

These QL/32-specific operations use session-owned devices and buffers `[HARDWARE]`.

| Command | Syntax | Description | Short example |
| --- | --- | --- | --- |
| `ALLOC` | `buffer = ALLOC(byteCount)` | Allocates a zero-filled session-owned byte buffer. | `buf% = ALLOC(4)` |
| `DEALLOC` | `DEALLOC buffer` | Releases a managed buffer. | `DEALLOC buf%` |
| `POKE` | `POKE address, byte[, byte ...]` | Writes bytes in source order. | `POKE buf%, 17, 34` |
| `POKE_W` | `POKE_W address, word` | Writes a native-endian 16-bit value. | `POKE_W buf%, 1024` |
| `POKE_L` | `POKE_L address, long` | Writes a native-endian 32-bit value. | `POKE_L buf%, 305419896` |
| `XFER` | `XFER #channel, txAddress, txLength, rxAddress, rxLength` | Transfers raw bytes through SPI/I2C channel drivers. | `XFER #bus%, tx%, 1, rx%, 2` |

## Built-in functions

### Math and random values

| Function | Syntax | Description | Short example |
| --- | --- | --- | --- |
| `SIN` | `SIN(angle)` | Sine of an angle in radians. | `y = SIN(1.2)` |
| `COS` | `COS(angle)` | Cosine of an angle in radians. | `x = COS(1.2)` |
| `INT` | `INT(value)` | Greatest integer less than or equal to a finite number. | `row% = INT(y)` |
| `RND` | `RND()`; `RND(limit)`; `RND(low, high)` | Session-local pseudo-random value; upper bound is excluded except equal bounds return that value. | `x = RND(1, 10)` |

### Strings and conversion

| Function | Syntax | Description | Short example |
| --- | --- | --- | --- |
| `LEN` | `LEN(string$)` | Number of UTF-8 code points. | `n% = LEN(name$)` |
| `LEFT$` | `LEFT$(string$, count)` | First `count` code points. | `prefix$ = LEFT$(name$, 3)` |
| `RIGHT$` | `RIGHT$(string$, count)` | Last `count` code points. | `suffix$ = RIGHT$(name$, 2)` |
| `MID$` | `MID$(string$, start[, count])` | One-based string slice; omitted count means through the end. | `part$ = MID$(name$, 2, 4)` |
| `REPT$` | `REPT$(string$, count)` | Repeats a string the requested number of times. | `bar$ = REPT$("-", 8)` |
| `CODE` | `CODE(string$)` | Unicode code point of the first character, or zero for empty string. | `c% = CODE("A")` |
| `ASC` | `ASC(string$)` | Alias for `CODE`. | `c% = ASC("A")` |
| `CHR$` | `CHR$(code)` | String containing one Unicode code point. | `letter$ = CHR$(65)` |
| `LOC` | `LOC(haystack$, needle$)` | One-based position of a substring, or zero when absent. | `p% = LOC(text$, "QL")` |
| `UPPER$` | `UPPER$(string$)` | Converts ASCII letters to uppercase. | `answer$ = UPPER$(name$)` |
| `LOWER$` | `LOWER$(string$)` | Converts ASCII letters to lowercase. | `key$ = LOWER$(name$)` |
| `STR$` | `STR$(number)` | Compact numeric formatting used by `PRINT`. | `text$ = STR$(score)` |
| `VAL` | `VAL(string$)` | Converts a complete decimal numeric string. | `value = VAL("12.5")` |
| `EVAL` | `EVAL(expression$)` | Compiles and evaluates one numeric QL/32 expression in the current session. | `value = EVAL("2+3")` |
| `EVAL$` | `EVAL$(expression$)` | Evaluates an expression and returns a string value or formatted number. | `text$ = EVAL$("2+3")` |
| `HEX$` | `HEX$(integer)` | Formats a 32-bit integer as hexadecimal. | `s$ = HEX$(255)` |
| `BIN$` | `BIN$(integer)` | Formats a 32-bit integer as binary. | `s$ = BIN$(5)` |

### Bit operations

All bit operations use 32-bit values. `AND`, `OR`, `XOR`, and `NOT` remain
logical operators.

| Function | Syntax | Description | Short example |
| --- | --- | --- | --- |
| `BAND` | `BAND(a, b)` | Bitwise AND. | `mask% = BAND(flags%, 3)` |
| `BOR` | `BOR(a, b)` | Bitwise OR. | `flags% = BOR(flags%, 4)` |
| `BXOR` | `BXOR(a, b)` | Bitwise exclusive OR. | `bits% = BXOR(bits%, 1)` |
| `BNOT` | `BNOT(a)` | Bitwise complement. | `bits% = BNOT(bits%)` |
| `ROL` | `ROL(value, count)` | Rotate left. | `v% = ROL(v%, 1)` |
| `ROR` | `ROR(value, count)` | Rotate right. | `v% = ROR(v%, 1)` |
| `SRA` | `SRA(value, count)` | Arithmetic right shift, preserving sign. | `v% = SRA(v%, 2)` |
| `SRL` | `SRL(value, count)` | Logical right shift, filling with zero. | `v% = SRL(v%, 2)` |
| `SLA` | `SLA(value, count)` | Left shift, filling low bits with zero. | `v% = SLA(v%, 2)` |
| `SLL` | `SLL(value, count)` | Left shift, filling low bits with one. | `v% = SLL(v%, 2)` |

### Channels, runtime, and system information

| Function | Syntax | Description | Short example |
| --- | --- | --- | --- |
| `OPEN` | `OPEN(KDeviceKind, args...)` or `OPEN(path$, mode$)` | Opens a session-owned channel and returns its handle. Driver dependent. `[HARDWARE]` | `f% = OPEN("data.txt", "READ")` |
| `IN` | `IN(channel)` or `IN(channel, register)` | Reads a scalar device value or register. `[HARDWARE]` | `state% = IN(gpio%)` |
| `EOF` | `EOF()` or `EOF(channel)` | Tests whether the DATA stream or selected file channel is exhausted. | `IF EOF() THEN PRINT "done"` |
| `GETCHANNEL` | `GETCHANNEL(selector[, positionOrHandle])` | Returns channel count, handle at a zero-based position, or channel type; selectors are in `ql32.inc`. | `n% = GETCHANNEL(KChannelCount)` |
| `GETTASK` | `GETTASK(selector[, position])` | Returns task count or a task ID by zero-based position. | `n% = GETTASK(KTaskCount)` |
| `TASK` | `TASK(path$[, options])` | Starts a task and returns its ID; `KTaskWait` waits for completion. | `id% = TASK("worker.bas")` |
| `TASKRESULT` | `TASKRESULT(taskId, selector)` | Returns one task result property selected by a `KTaskResult...` constant. | `state% = TASKRESULT(id%, KTaskResultStatus)` |
| `ERR` | `ERR()` | Current QL/32 error code. | `code% = ERR()` |
| `ERR$` | `ERR$(code)` | Message for an error code. | `message$ = ERR$(ERR())` |
| `ERRX$` | `ERRX$()` | Contextual detail for the current task error. | `detail$ = ERRX$()` |
| `FREE` | `FREE()` or `FREE(selector)` | Returns formatted memory information or selected total/contiguous/fragment values. | `bytes = FREE(0)` |
| `CPUFREQ` | `CPUFREQ()` | Current CPU clock in MHz, or zero when unavailable. `[ESP32]` | `mhz% = CPUFREQ()` |
| `MODE` | `MODE()` | Returns the current display mode. `[DISPLAY]` | `mode% = MODE()` |

### Graphics information and fonts

These functions require a graphics display service `[DISPLAY]`.

| Function | Syntax | Description | Short example |
| --- | --- | --- | --- |
| `GWIDTH` | `GWIDTH()` | Width of the current graphics surface. | `w% = GWIDTH()` |
| `GHEIGHT` | `GHEIGHT()` | Height of the current graphics surface. | `h% = GHEIGHT()` |
| `GDEPTH` | `GDEPTH()` | Bits-per-pixel depth reported by the graphics service. | `d% = GDEPTH()` |
| `GORIGINX` | `GORIGINX()` | Screen-space X origin of the default graphics window. | `x% = GORIGINX()` |
| `GORIGINY` | `GORIGINY()` | Screen-space Y origin of the default graphics window. | `y% = GORIGINY()` |
| `GROTATION` | `GROTATION()` | Current presenter rotation. | `r% = GROTATION()` |
| `GPALETTESIZE` | `GPALETTESIZE()` | Number of palette entries. | `n% = GPALETTESIZE()` |
| `GPALETTE$` | `GPALETTE$(index)` | Palette entry as a `#RRGGBB` string. | `color$ = GPALETTE$(1)` |
| `LOADFONT` | `LOADFONT(path$)` | Loads a font and returns a handle. | `font% = LOADFONT("font.fnt")` |
| `GETFONT` | `GETFONT(selector, handle)` | Returns font metrics selected by `KFontMetric...` constants. | `height% = GETFONT(KFontMetricHeight, font%)` |

### Network status and user interface

| Function | Syntax | Description | Short example |
| --- | --- | --- | --- |
| `NETSTAT$` | `NETSTAT$()` | Network status text. `[NETWORK]` | `status$ = NETSTAT$()` |
| `NETADDR$` | `NETADDR$()` | Current network address. `[NETWORK]` | `ip$ = NETADDR$()` |
| `NETSIGNAL` | `NETSIGNAL()` | Current signal level. `[NETWORK]` | `signal% = NETSIGNAL()` |
| `CONFIRM` | `CONFIRM(title$, message$)` | Asks for confirmation and returns a result. | `ok% = CONFIRM("Quit", "Sure?")` |
| `DINIT` | `DINIT()` or `DINIT(title$[, options])` | Starts a dialog definition and returns its handle. `options` is a flag mask from `ql32.inc`. | `dialog% = DINIT("Setup")` |
| `DIALOG` | `DIALOG(dialog)` | Presents a dialog definition and returns `1` on submit or `0` on cancel. | `accepted% = DIALOG(dialog%)` |
| `DGET` | `DGET(result%, fieldId)` | Gets a numeric result from a dialog field. | `age% = DGET(result%, 1)` |
| `DGET$` | `DGET$(result%, fieldId)` | Gets a string result from a dialog field. | `name$ = DGET$(result%, 2)` |
| `MINIT` | `MINIT(title$)` | Starts a menu definition and returns its handle. | `menu% = MINIT("Main")` |
| `MENU` | `MENU(menu%)` | Presents a menu and returns the selected command ID. | `selected% = MENU(menu%)` |

### Managed memory access

These functions use session-owned buffers `[HARDWARE]`.

| Function | Syntax | Description | Short example |
| --- | --- | --- | --- |
| `ALLOC` | `ALLOC(byteCount)` | Allocates a zero-filled byte buffer. | `buf% = ALLOC(16)` |
| `PEEK` | `PEEK(address)` | Reads one byte. | `b% = PEEK(buf%)` |
| `PEEK_W` | `PEEK_W(address)` | Reads a native-endian 16-bit value. | `w% = PEEK_W(buf%)` |
| `PEEK_L` | `PEEK_L(address)` | Reads a native-endian 32-bit value. | `v% = PEEK_L(buf%)` |
| `IOREAD` | `IOREAD(channel, address)` or `IOREAD(channel, register, address)` | Reads bytes into a managed buffer. `[HARDWARE]` | `n% = IOREAD(dev%, buf%)` |
| `IOWRITE` | `IOWRITE(channel, address)` or `IOWRITE(channel, register, address)` | Writes bytes from a managed buffer. `[HARDWARE]` | `rc% = IOWRITE(dev%, buf%)` |
| `READ$` | `READ$(channel)` | Reads a line from file or configured serial channel. `[HARDWARE]` | `line$ = READ$(port%)` |

## Interactive shell commands

These are prompt commands, not statements that can appear in a stored BASIC
program unless specifically listed above. File and task commands depend on the
target shell services. Examples use prompt syntax.

| Command | Syntax | Description | Short example |
| --- | --- | --- | --- |
| `HELP` | `HELP` | Lists shell commands, BASIC words, functions, and optional-service notes. | `HELP` |
| `RUN` | `RUN` | Compiles and executes the numbered program; clears variables for a new run. | `RUN` |
| `LIST` | `LIST [first[-last]] [#channel]` | Lists stored numbered lines in ascending order. | `LIST 10-50` |
| `EDIT` | `EDIT [lineNumber]` | Opens a numbered program line for replacement. | `EDIT 100` |
| `NEW` | `NEW` | Clears the stored numbered program. | `NEW` |
| `LOAD` | `LOAD "path.bas"` | Loads numbered or unnumbered source from storage. | `LOAD "FFS:/demo.bas"` |
| `SAVE` | `SAVE "path.bas"` | Saves the stored program. | `SAVE "SDC:/demo.bas"` |
| `DIR` | `DIR [path] [#channel]` | Lists a directory in sorted name order. | `DIR "FFS:/"` |
| `CD` | `CD [path]` | Changes the current directory for this interpreter session. | `CD "SDC:/programs"` |
| `DELETE` | `DELETE line[, line ...]` or `DELETE path$` | Deletes program lines atomically or removes a file. | `DELETE 10, 20` |
| `RENAME` | `RENAME oldPath, newPath` | Moves/renames a file. | `RENAME "old.bas", "new.bas"` |
| `FORMAT` | `FORMAT path` | Recursively clears the selected directory or volume. Destructive. | `FORMAT "SDC:/"` |
| `PAGER` | `PAGER ON\|OFF` | Enables or disables per-session pagination of generated listings. | `PAGER ON` |
| `TASKS` | `TASKS [#channel]` | Lists scheduler tasks and state. Embedded REPL. `[ESP32]` | `TASKS` |
| `TASKRESULT` | `TASKRESULT id` | Prints available properties for a task. Embedded REPL. `[ESP32]` | `TASKRESULT 2` |
| `TASKCHANNELS` | `TASKCHANNELS id` | Lists a task's reserved and opened channels. Embedded REPL. `[ESP32]` | `TASKCHANNELS 2` |
| `LOGON` | `LOGON id` | Routes interactive commands to an idle worker session. Embedded REPL. `[ESP32]` | `LOGON 2` |
| `LOGOFF` | `LOGOFF` | Returns the interactive prompt to the supervisor session. Embedded REPL. `[ESP32]` | `LOGOFF` |
| `KILL` | `KILL id` | Stops an active task. Embedded REPL. `[ESP32]` | `KILL 2` |
| `SUSPEND` | `SUSPEND id` | Suspends a task while preserving its state and resources. Embedded REPL. `[ESP32]` | `SUSPEND 2` |
| `RESUME` | `RESUME id` | Restores a suspended task's saved scheduling weight. Embedded REPL. `[ESP32]` | `RESUME 2` |
| `SLICE` | `SLICE [instructions]` | Reports or changes the scheduler instruction slice. Embedded REPL. `[ESP32]` | `SLICE 256` |
| `PRIORITY` | `PRIORITY id, weight` | Sets a task's scheduling weight. Embedded REPL. `[ESP32]` | `PRIORITY 2, 3` |
| `TASKVIEW` | `TASKVIEW id, x, y, width, height[, offsetX, offsetY]` | Positions a task's display viewport. Embedded display target. `[DISPLAY]` | `TASKVIEW 2, 0, 0, 120, 120` |
| `MODE` | `MODE [id[, options]\|name\|LIST]` | Lists, reports, or changes the display mode. `[DISPLAY]` | `MODE LIST` |
| `WINDOWMODE` | `WINDOWMODE ISOLATED\|SHARED` | Selects a task's window mode where supported. | `WINDOWMODE SHARED` |
| `CONTINUE` | `CONTINUE` | Resumes a stored run after BASIC `STOP`. | `CONTINUE` |
| `REBOOT` | `REBOOT` | Restarts embedded firmware. `[ESP32]` | `REBOOT` |
| `DEBUG` | `DEBUG taskId` | Attaches the debugger to and pauses a task. Embedded debugger. `[ESP32]` | `DEBUG 2` |
| `BREAK` | `BREAK lineNumber` | Adds a breakpoint. | `BREAK 100` |
| `BREAKPOINTS` | `BREAKPOINTS` | Lists breakpoints. | `BREAKPOINTS` |
| `CLEARBREAK` | `CLEARBREAK lineNumber` | Removes a breakpoint. | `CLEARBREAK 100` |
| `STEP` | `STEP` | Executes one BASIC statement while debugging. | `STEP` |
| `NEXT` | `NEXT` | Runs through calls from the current statement while debugging. | `NEXT` |
| `DEBUG CONTINUE` | `DEBUG CONTINUE` | Resumes the debugger target. | `DEBUG CONTINUE` |
| `WHERE` | `WHERE` | Shows the paused task's source location. | `WHERE` |
| `LOCALS` | `LOCALS` | Lists local values and array bounds. | `LOCALS` |
| `GLOBALS` | `GLOBALS` | Lists session globals and array bounds. | `GLOBALS` |
| `STACK` | `STACK` | Shows the procedure/function call stack. | `STACK` |
| `EVAL` | `EVAL expression` | Evaluates a side-effect-free expression in the paused task. | `EVAL A+B*2` |
| `WATCH` | `WATCH expression` | Adds a side-effect-free expression watch. | `WATCH count%` |
| `UNWATCH` | `UNWATCH expression` | Removes an expression watch. | `UNWATCH count%` |
| `WATCHES` | `WATCHES` | Displays current watch values. | `WATCHES` |
| `DIAGNOSTICS` | `DIAGNOSTICS ON\|RESET` | Enables per-task counters or resets counters and trace. | `DIAGNOSTICS ON` |
| `TRACE` | `TRACE ON` | Keeps a bounded recent instruction trace. | `TRACE ON` |
| `STATS` | `STATS` | Displays execution counters and available memory information. | `STATS` |
| `DEBUGBAR` | `DEBUGBAR` | Displays the debug bar when a presenter supports it. | `DEBUGBAR` |

## Constants and include files

`ql32.inc` provides portable selectors and named values for options, channels,
tasks, graphics, fonts, dialogs, touch, devices, pins, buses, and timers. Include
the common file with `INCLUDE "ql32.inc"`. Board pin mappings are provided by
`boards/<profile>/ql32-hardware.inc`; embedded copies are stored under
`data/boards/`.

Typical program setup:

```basic
INCLUDE "ql32.inc"
OPTION KOptMathDouble

FOR i% = 1 TO 3
  PRINT "value "; i%; ": "; RND(10)
END FOR i%
```

Storage prefixes depend on firmware profile. CYD SPIFFS builds mount internal
flash as `FFS:` and removable SD as `SDC:`. Include paths resolve relative to
the containing source file and cannot escape the configured storage root.

For detailed, evolving contracts, see [QL32-LANG-001](../specification/QL32-LANG-001.md),
[QL32-HW-001](../specification/QL32-HW-001.md), and
[QL32-API-001](../specification/QL32-API-001.md). The external manuals in the
[reference index](README.md) are provenance and design references; their text
is not reproduced here.
