# QL/32 BASIC language specification

**Document ID:** QL32-LANG-001  
**Version:** 0.6 (M1 language contract)
**Status:** Approved profile; M1 acceptance complete on Linux; source inclusion, GOTO, and module constants implemented
**Sources:** [reference index](../reference/README.md), [language decision](../decisions/ADR-002-language-profile.md)

## 1. Language profile

QL/32 BASIC is an interactive, structured, case-insensitive dialect based primarily on SMSQ/E SBASIC. Psion OPL inspires managed UI, not the core grammar. Complete source compatibility with either language is not promised.

Toolkit II is treated as a near-standard SuperBASIC extension: it added substantial facilities planned for the QL but absent from the rushed initial release. QL/32 may draw on those features as compatibility references, while identifying toolkit-derived behavior separately from original QL ROM behavior.

Compatibility labels apply per command: **S** = syntax and behavior verified against a cited SuperBASIC/SMSQ/E or named toolkit reference and tests; **Q** = QL/32 difference or QL-inspired command; **P** = Psion-inspired UI; **N** = new native API; **X** = unsupported. Historical spelling alone never earns S. All mappings here are candidates until tests pass.

M1 is delivered in executable slices. The first slice is expressions, assignments, variables, and `PRINT`; the full M1 set includes block control flow, procedures, functions, and arrays.

## 2. Source format and tokens

- Source is UTF-8 text, normally stored as `.bas`.
- Keywords and identifiers are case-insensitive. Recognized keywords are stored in canonical form for `LIST`; user identifiers retain their entered spelling.
- Decimal line numbers are optional. Newline ends a source line; colon separates statements on the same line.
- Whitespace outside strings separates tokens. Strings use double quotes; two consecutive double quotes represent one embedded double quote.
- `REMark`, its minimum abbreviation `REM`, and an apostrophe outside a string start a comment that continues to the end of that source line. Apostrophe is a QL/32 alias.
- Proposed M1 identifier subset: ASCII letter followed by ASCII letters, digits, or underscores, optionally ending in `%` or `$`. Wider Unicode identifiers are deferred.
- Decimal integer and floating literals are in scope. Historical hex/binary notation awaits reference checks.
- Lexical and syntax failures report line and column and never execute partial bytecode.

### 2.1 SuperBASIC keyword abbreviations

The mixed-case spelling in the SuperBASIC reference encodes an abbreviation:
uppercase letters are mandatory, lowercase letters may be omitted. Matching is
case-insensitive; the capitalization in the manual describes the rule, not how
the programmer must type it. Thus `DEF FN` means `DEFine FuNction`,
`DEF PROC` means `DEFine PROCedure`, `REP` means `REPeat`, and
`REM` means `REMark`. The [original QL user guide](../reference/README.md)
also says recognized keywords are reproduced in full.

QL/32 normalizes recognized abbreviated keywords to their canonical mixed-case
spelling when storing/listing a numbered program. For example, entering
`10 def fn Square(x)` shall list as
`10 DEFine FuNction Square(x)`. Keyword resolution uses a table of canonical
spellings and mandatory letters, rather than a blanket prefix rule: `FN`
matches `FuNction` even though it is not a contiguous prefix. The full spelling
also matches. Intermediate omissions of lowercase letters are accepted when
they preserve order and satisfy the mandatory-letter sequence. If more than
one registered spelling matches, tokenization reports an ambiguous keyword.
Intermediate spellings and collision handling are QL/32 rules until checked
against a live SMSQ/E interpreter.

Expansion applies to keyword tokens in code, not text inside strings or
comments, nor to longer user identifiers. The current keyword table implements
the profile for the supported M1 keyword set. Conformance tests cover `DEF FN`,
`RET`, mixed case, and canonical `LIST` output; remaining spellings still need
coverage against the complete keyword table.

### 2.2 Source inclusion

`INCLUDE "relative/path.bas"` is a QL/32 source directive processed before
tokenization. It must appear in the module preamble, before executable module
code and outside procedure bodies; blank lines and comments may precede it.
Included files may contain more preamble includes and BASIC procedure/function
definitions. Those definitions are textually inserted into the including
module and can be called like local definitions. Includes resolve relative to
the containing file; the initial source resolves from the configured storage
root. Absolute paths and paths that escape that root are rejected.

Expansion is recursive, rejects cycles, limits nesting to 16 files and
expanded source to 64 KiB, and uses the platform storage service. A numbered
program may place an `INCLUDE` directive on a line before its executable
statements. The directive's line number is removed during expansion. Diagnostics
for included code currently refer to line numbers in the expanded source.

This borrows OPL32's quoted `INCLUDE` spelling, module-preamble placement, and
textual expansion. OPL32 headers declare constants and procedure prototypes and
do not contain module procedure bodies. QL/32 supports module `CONST`
declarations in included BASIC headers; it has no `EXTERNAL` or OPX interface.
`INCLUDE` does not compile C/C++ headers or link object libraries. See the
[OPL reference notes](../reference/README.md).

QL/32 adopts the OPL-style use of a common include file for named constants and
public selectors. Prefer a small set of general operations parameterized by
those constants over adding a new keyword for each operation. Module-level
`CONST name = literal` declarations accept numeric literals (optionally signed)
or string literals. They must appear before definitions and executable code,
are case-insensitive, and are immutable compile-time values. They cannot be
reassigned, dimensioned, or used as loop variables. `ql32.inc` defines
`KChannelCount = 0`, `KChannelHandle = 1`, and `KChannelType = 2` for
`GETCHANNEL`; programs include it with `INCLUDE "ql32.inc"`.

The first slice accepts this grammar (later sections extend it):

```text
program       := source_line*
source_line   := [line_number] [statement (":" statement)*] [comment] newline
statement     := const_declaration | assignment | print
const_declaration := "CONST" identifier "=" (signed_number | string)
comment       := ("REMark" | "REM" | "'") characters_to_end_of_line
assignment    := identifier "=" expression
print         := "PRINT" ["#" expression [("," | ";")]]
                [expression (print_separator expression)* [print_separator]]
print_separator := ";" | "!" | "," | "\\"
expression    := literal | identifier | "(" expression ")" | unary | binary
```

## 3. Values and variables

| Form | QL/32 type | Initial value | Difference |
| --- | --- | --- | --- |
| `value` | IEEE-754 binary64 | 0 | Modern representation |
| `count%` | signed 32-bit integer | 0 | 32-bit width |
| `title$` | dynamic UTF-8 string | empty | UTF-8 representation |

Implicit scalar declaration is allowed. Boolean expressions produce numeric
`1` for true and `0` for false, matching the cited SuperBASIC behavior. Any
finite numeric value other than zero is true in a condition; zero is false.
Strings are not conditions. A Boolean result is an ordinary numeric value and
can participate in later arithmetic. Native resource handles are opaque and
session-owned, never raw memory addresses. Invalid conversion, integer
overflow, division by zero, and array bounds violations are defined runtime
errors.

## 4. Expressions

M1's target operators are unary `+ - NOT`, binary `+ - * / DIV MOD ^`,
numeric comparisons, string equality/inequality, `AND OR XOR`, parentheses,
function calls, and array indexing. `&`, `INSTR`, approximate equality `==`,
bitwise operators, and chained comparisons are outside M1.

Precedence from highest to lowest is:

| Level | Operators | Associativity |
| --- | --- | --- |
| 1 | parentheses | innermost first |
| 2 | unary `+ -` | right to left |
| 3 | `^` | left to right |
| 4 | `* / DIV MOD` | left to right |
| 5 | binary `+ -` | left to right |
| 6 | `< <= = <> >= >` | one comparison per unparenthesized expression |
| 7 | `NOT` | right to left; applies to the comparison expression to its right |
| 8 | `AND` | left to right |
| 9 | `OR XOR` | left to right |

Thus `-2^2` is `(-2)^2`, `2^3^2` is `(2^3)^2`, and
`NOT value = 0` means `NOT (value = 0)`. Parentheses can make another grouping
explicit. The table is the QL/32 precedence profile: it groups `*`, `/`,
`DIV`, and `MOD` together and groups all comparisons together, unlike the
reference's ordered precedence list. These simplifications are not class S
compatibility claims.

Numeric literals and unsuffixed numeric variables are binary64. Arithmetic
operators require numeric operands and do not parse strings implicitly. Binary
`+` adds two numbers or concatenates two strings; it rejects a mixed pair.
This string use of `+` is a QL/32 choice; the cited SuperBASIC operator is
`&`, which M1 does not implement. String equality and inequality compare UTF-8
byte sequences exactly and are case-sensitive. Relational comparisons require
two numbers. Numeric comparison and logical operators return numeric `1` or
`0`. `NOT` and `AND`, `OR`, `XOR` accept numeric operands only; logical
operators test each operand for nonzero and evaluate both operands from left
to right. No implicit number-to-string or string-to-number conversion occurs.
`PRINT` formats either value type directly.

`/` is binary64 division and division by zero is an error. `DIV` and `MOD`
require finite, integral operands in the Int32 range. `DIV` is floor division,
and `MOD` satisfies `x = y * (x DIV y) + (x MOD y)`; divisor zero is an error.
`DIV` also errors if its quotient is outside the Int32 range. `^` uses
binary64 operands; a non-finite result, zero raised to a negative
power, or a negative base with a non-integral exponent is a runtime error.
`0^0` evaluates to `1`. Assignment to a `%` variable requires an exact
integral result in the Int32 range; it does not truncate or wrap. Assignment to
a `$` variable requires a string. These strict conversion rules are QL/32
choices where they differ from the dynamic SuperBASIC value model.

## 5. Statements and blocks

`name = expression` assigns a scalar. `PRINT` accepts a comma-separated list
of expressions and the SuperBASIC separators `;`, `!`, `,`, and `\` between
items. Semicolon adds no spacing. Exclamation inserts one space between items
when the cursor is not at the start of a line. Comma advances to the next
8-column tab stop. Backslash emits a line feed before the next item. A
trailing separator suppresses PRINT's automatic final line feed; a trailing
backslash supplies one explicitly. `PRINT` without items emits a line feed.
The Linux text stream has no device width, so `!` does not wrap and comma tabs
are fixed at eight character columns. `TO column`, channel selection, and
device-specific wrapping are deferred to display/output integration.

Numbers print using locale-independent shortest round-trip decimal notation;
zero, including negative zero, prints as `0`. Strings print as their contents.
There is no implicit padding or number-to-string conversion outside PRINT.

The implemented block set includes:

```basic
IF condition THEN
    PRINT "yes"
ELSE
    PRINT "no"
END IF

FOR i% = 1 TO 5 STEP 1
    PRINT i%
END FOR

REPeat loop
    EXIT loop
END REPeat loop
```

QL/32 supports historical numeric-line jumps with `GOTO line_number` and
`GO TO line_number`. `GOTO` may also target a QL/32 named label declared as
`label:` at the start of a statement. Labels are case-insensitive and scoped
to the main program or the procedure/function where they are declared. Line
number targets must exist; `GOTO 0` restarts the main program. Named labels are
a QL/32 extension: the [cited SuperBASIC `GO TO` syntax](https://superbasic-manual.readthedocs.io/en/latest/G/go--to.html) targets a line number.
To protect loop and selection state, a jump cannot enter or leave a structured
`FOR`, `REPeat`, or `SELect` block, nor cross a procedure/function boundary.
Use structured loop control such as `EXIT` to leave those blocks.

```basic
10 PRINT "start"
20 GO TO 40
30 PRINT "skipped"
40 PRINT "line target"

GOTO finish
PRINT "skipped"
finish: PRINT "label target"
```

An `ELSE IF condition THEN` branch is written as an `ELSE` followed by a
nested `IF`; it is not a distinct `ELSEIF` keyword. Each nested `IF` has
its own `END IF`:

```basic
IF score% >= 90 THEN
    PRINT "A"
ELSE IF score% >= 80 THEN
    PRINT "B"
ELSE
    PRINT "Keep going"
END IF
END IF
```

The first `END IF` closes the nested condition and the second closes the
outer condition. The parser and pretty-printer shall preserve that nesting.
This matches the structure documented by the [SuperBASIC IF reference](https://superbasic-manual.readthedocs.io/en/latest/I/if.html).

Block `IF`, `FOR`, `REPeat`, `EXIT`, and `SELect ON` use SuperBASIC
spellings. The M1 `FOR` target preserves the useful SuperBASIC range model:

```basic
FOR n% = 2, 4 TO 10 STEP 2, 12, 15 TO 9 STEP -2
    PRINT n%
END FOR n%

FOR n% = 1 TO 100: PRINT n%;: NEXT n%
```

The range list accepts comma-separated single values and intervals, mixed in
one list. An interval is `start TO end [STEP step]`; omitted step is 1.
Each interval proceeds inclusively in the direction of its step. A step that
points away from its end yields no values for that interval. A zero step is a
runtime error. QL/32 loop variables are numeric; Int32 variables follow QL/32
overflow rules, while floating variables use binary64. Historical ROM/SMSQ/E
differences in loop-variable types are not silently copied.

Both block and inline forms are in M1 scope. The block form ends with
`END FOR variable`. The inline form puts its body on the same source line and
does not require `END FOR`. The first implementation rejects nested inline
loops; block loops may still contain other block or inline loops.

`NEXT variable` advances to the next iteration, skipping the rest of the
current body; at the final value of an interval it does not advance beyond that
interval. It then proceeds through subsequent items in the range list before
ending the loop. When `NEXT` has no later value, it has no effect, allowing the
remaining body to run before `END FOR` closes the loop. Range expressions are
evaluated once, in source order, when the loop is entered.

`REPeat name` starts a block loop closed by `END REPeat name`; `REPeat name:`
starts an inline loop whose body continues to the end of that source line.
`NEXT name` restarts the named loop at its first body statement. A repeat loop
continues indefinitely unless an `EXIT name` leaves it. `EXIT name` also leaves
a named `FOR` loop, continuing after `END FOR` or after the inline loop body.
Loop-control statements must identify an open matching loop in M1, and may
target an outer loop from a nested loop. Omitted identifiers and nested inline
loops remain outside this explicit M1 syntax.

`SELect ON expression` is a numeric multi-way branch. Each arm starts with
`= value[, value ...]` or `= start TO end`; the optional final arm is
`= REMAINDER`. The first matching arm runs, then control continues after
`END SELect`. Ranges are inclusive. QL/32 currently compares singleton values
with exact binary64 equality, a QL/32-specific choice; historical
SuperBASIC's floating-point match behavior has not been established here.
Only the block form is implemented; inline `SELect` is deferred. Optional
loop identifiers and historical ROM-specific quirks are deferred where they
do not fit this portable, explicit M1 contract.

Named loop ends and exits must match the opening loop. `IF` conditions use the logical truth rule finalized with the Boolean conversion table.

## 6. Procedures, functions, and scope

```basic
DEFine PROCedure ShowMessage(message$)
    LOCal prefix$
    prefix$ = "Message: "
    PRINT prefix$; message$
END DEFine

DEFine FuNction Square(value)
    RETurn value * value
END DEFine
```

Definitions are program-level. A function is called in an expression and must return a value; reaching `END DEFine` without one is a runtime error. A procedure is invoked as a statement. `LOCal` creates invocation-local names. Name lookup searches locals, parameters, then session globals. Recursion is supported with a bounded call stack; nested definitions are excluded from M1. Scalar arguments are copied into each invocation. A dimensioned array passed as a bare argument is aliased by the callee, so element updates are visible to the caller; forwarding that alias through another procedure/function is also supported.

Procedure declarations retain parenthesized parameter lists, but procedure
calls omit call parentheses: `ShowMessage "Hello"`. Function calls require
parentheses in expressions: `value = Square(4)`.

**QL/32 difference:** scalar arguments are passed by value by default; array parameters use reference semantics when the actual argument names a dimensioned array. Historical SuperBASIC procedure parameters can be passed by reference. These calls are class Q unless a compatible form is added later.

## 7. Arrays

`DIM array%(upper)` and multidimensional variants are implemented. QL/32 indexing starts at zero: `DIM a%(3)` creates indexes 0 through 3 inclusive. [SuperBASIC's numeric `DIM`](https://superbasic-manual.readthedocs.io/en/latest/D/dim.html) uses the same zero-based inclusive bounds; the indexing rule may earn class S after conformance tests. QL/32's dynamic UTF-8 string arrays do not reproduce the historical fixed-length string-array model and are class Q. Elements start with the type default. Every dimension is bounds-checked; allocation failure is a runtime error. The host allocation limit is one million elements per array; board-specific limits remain to be measured. Dimensioned arrays can be passed to procedure/function parameters by reference.

Scalar string variables support SuperBASIC-style, one-based inclusive
slices: `text$(4 TO 8)`, `text$(TO 10)`, and `text$(10 TO)`. A single index
selects one character, and an empty `text$()` selects the whole string. QL/32
uses start 1 and the string length as the defaults for omitted bounds; an end
past the length is clipped to the string length. Indices count UTF-8 code
points. A start outside the string or a reversed range is a runtime error.
This is a QL/32-defined subset: slice assignment and descriptors on string
arrays are not yet supported, and SuperBASIC ROM variants differ in omitted
bound behavior. `LEN(string$)` returns the number of UTF-8 code points in a
string or string slice. Unlike the original QL ROM's coercion behavior, QL/32
requires a string argument. See the [SuperBASIC string-slicing reference](https://superbasic-manual.readthedocs.io/en/latest/D/dim.html) and [`LEN` reference](https://superbasic-manual.readthedocs.io/en/latest/L/len.html).

`FREE()` returns the current free-memory estimate in bytes as a number. On
ESP32 it reports free byte-addressable heap; on Linux it reports host memory
available for new allocations. This is platform memory, not an exact count of
bytes owned by the current BASIC session. Use repeated `PRINT FREE()` calls
to observe allocation changes. The target shell's separate `MEM` command is
not implemented.

## 8. Immediate mode, commands, and errors

Immediate statements execute without changing stored program source. Entering a
line numbered 1 through 32767 stores it without executing it; entering another
line with the same number replaces the whole stored line. The last occurrence
of a repeated number in one input batch wins. A numbered line containing only
whitespace deletes that stored line. A comment-only line is stored as a no-op,
so it can retain a program comment. In stdin command batches and the persistent
terminal REPL, numbered lines are stored in memory and `LIST`, `RUN`, and
`NEW` are supported. The REPL processes one entry at a time, preserves session
state, and continues after an entry error. `LIST` prints
ascending line numbers with recognized keywords canonicalized; `RUN` clears
variables and executes the stored lines; `NEW` clears the program and
variables. Each numbered replacement is checked against a candidate program
before the old line is replaced. Incomplete block prefixes are accepted so
multi-line structures can be entered incrementally; unresolved procedure and
function calls are bound when `RUN` compiles the complete program. Unnumbered
statements execute immediately. A `.bas` file passed to the Linux CLI is a
complete source unit executed in file order; it does not edit a persistent
interactive program. The Linux command loop provides `LOAD`, `SAVE`, and `DIR`.
The scheduler-backed embedded REPL also provides `SPAWN "path.bas"`, `TASKS`,
`TASKCHANNELS id`, `LOGON id`, `LOGOFF`, and `CANCEL id`. `SPAWN` reads source
through the configured storage service and starts an independent worker
session. When its startup source completes, the worker remains alive at its own
interactive prompt with its variables, program, graphics window, and channels
retained. `TASKS` lists session IDs, supervisor/worker kind, state, and errors;
`TASKCHANNELS id` lists a session's reserved and opened handles with endpoint
types. `LOGON id` routes embedded console input to an idle worker so immediate
BASIC commands operate in that session; `LOGOFF` returns to the supervisor.
The prompt includes the attached task ID. `CANCEL id` stops an active worker;
log off before cancelling the currently attached session. These QL/32-specific
commands are not available in the Linux command loop.
M2 also provides QL/32-specific `OPEN`, `READ$`, `EOF`, `WRITE`, and `CLOSE`
channel operations. File endpoints support reads and writes; serial endpoints
write to serial output, and null endpoints discard output. Value-returning
operations use function syntax while `WRITE channel, text$` and `CLOSE channel`
are statement commands. See
[QL32-API-001](QL32-API-001.md). `EDIT` and the target
shell's `STOP` and `HELP`, and later `CONT`, remain future work.

Channels are universal session-owned I/O handles. `#0` is reserved for the
command/error console and keyboard input; `#1` is reserved for user/program
output and graphics. Window, file, serial, null, and future network endpoints
share one handle namespace. Generic `OPEN(deviceKind, parameters...)` returns
an automatically allocated channel handle; the device kind is a typed token,
not a string. Programs do not select channel numbers when opening endpoints.
Commands that perform I/O or drawing may select a channel explicitly, for
example `PRINT #out, text$`, `LIST #out`, or
`CIRCLE #window, x, y, radius`. Unqualified output uses the current output
default. `CHANNEL #channel` changes the logical default for all unqualified
output operations, including text and graphics. `CHANNEL #source TO
#destination` directly rebinds the source handle to the endpoint currently
bound to the destination. The binding is shared, session-local, and applies to
all operations; the source retains its original endpoint, restored by
`CHANNEL #source TO #source`. If the default is still `#1`, rebinding `#1` to
`#2` makes unqualified output and explicit `#1` operations use `#2`'s endpoint.
Graphics fail if that endpoint lacks graphics capability. Changing the default
to `#0` selects `#0` for unqualified output; explicit selectors still use the
selected handle's active binding. The current runtime still resolves per-session
routes per operation; direct binding is the approved target architecture.
Channel handles belong to one interpreter session. A worker launched by the
embedded `SPAWN` command has its own `#0`, `#1`, and opened handle numbers; a
number in one session does not select a handle in another. The sessions still
use shared platform services and physical devices, such as the same display,
SD card, and serial console. Worker text is routed to the supervisor's output
stream. `GETCHANNEL(KChannelCount)` returns the open-channel count;
`GETCHANNEL(KChannelHandle, position)` returns the handle at the zero-based
position in ascending-handle order; and `GETCHANNEL(KChannelType, #handle)`
returns the endpoint type. These selector constants are provided by
`ql32.inc`. `OPEN(KDeviceFile, path, mode)`, `OPEN(KDeviceSerial)`, and
`OPEN(KDeviceNull)` open file, serial-output, and null-output endpoints;
the legacy `OPEN(path, mode)` file form remains supported. The null endpoint
discards text, byte, and graphics output. Serial and null input are not
implemented, and reserved channels `#0` and `#1` are still managed outside
the opened-endpoint registry.

For text output, `WRITE channel, text$` is a one-string, newline-terminated
write. `PRINT #channel` supports multiple expressions and separators. A
trailing backslash emits an explicit line feed; a trailing semicolon suppresses
`PRINT`'s automatic final line feed. For example:

```basic
file = OPEN("messages.txt", "WRITE")
WRITE file, "Written with WRITE"
PRINT #file, "Written with PRINT"\
CLOSE file
```

Both lines are written to `messages.txt`, each followed by a line feed.

The interpreter supports channel-qualified `PRINT`, `LIST`, `DIR`, and
`WRITE`: text output can go to the retained window for `#1`, the console stream
through `#0`, or an open file, serial, or null channel. These operations follow
the default and route rules above. Graphics commands also accept a channel
selector and follow those rules; drawings target the graphics window `#1`,
while null-target drawings are discarded. `READ$` and `EOF` work with open file
channels. Line-oriented
BASIC `INPUT` defaults to keyboard channel `#0`; file channels can be selected
explicitly. A prompt defaults to output channel `#1` and may select an
independent prompt channel. Supported forms are:

```basic
INPUT variable
INPUT promptExpression; variable
INPUT #readChannel, variable
INPUT promptExpression #readChannel, variable
INPUT #promptChannel, promptExpression, #readChannel, variable
INPUT #1, "Serial line?", #3, response$
```

Prompt expressions must evaluate to strings. Keyboard input
suspends only its BASIC session and resumes when the UI presenter submits a
line. File-channel `INPUT` reads one line synchronously and raises an error at
EOF. Serial and null input endpoints are not implemented yet.

Exact binary I/O and random access use channel-context forms:

```basic
#channel AT byteOffset
#channel IN byteCount, buffer$
#channel OUT byteCount, buffer$
```

`AT` seeks to a zero-based absolute file offset. `IN` reads exactly the
requested count from the current position; `OUT` writes that many bytes from
the start of the string buffer. Counts are limited to 8192 bytes per
operation. Linux directory and ESP32 SD file adapters support these
operations. Strings preserve NUL and arbitrary byte sequences for binary I/O,
but text operations such as slicing and printing assume UTF-8. `APPEND`
channels are sequential and cannot be repositioned.

The compiler pipeline is UTF-8 source → tokens → syntax tree → semantic checks → bytecode → verifier → VM. A VM state contains variables, stacks, source location, default output channel, and error state for one session. No mutable BASIC program state belongs in process-wide globals. Errors have stable code, category, message, and source span. A failed session does not reset the host or another session. Instruction slices and cancellation checkpoints prepare for multitasking.

## 9. UI boundary and deferrals

Native commands and procedures are statement forms: write the name followed
by zero or more comma-separated arguments, without call parentheses (for
example, `ROTATE 90`, `ALERT title$, message$`, or `draw_line x1,y1`).
Functions return values and require parentheses around their arguments,
including empty parentheses for zero-argument functions (for example,
`GWIDTH()` or `CONFIRM(title$, message$)`). A function call must be used in an
expression; a procedure cannot be used as an expression. Parentheses remain
part of other syntax such as definition parameter lists, array indexing,
string slicing, and grouping. These rules apply to built-ins and
user-defined procedures/functions alike. OPL-inspired `ALERT` and `CONFIRM`
are M2 native APIs, drawing is M3, and declarative `DINIT`, `DEDIT`, `DINT`,
`DIALOG`, `MINIT`, `MCARD`, and `MENU` are M4 APIs. SuperBASIC remains the core
language, including its loop forms; selected OPL-inspired refinements may be
added when they simplify QL/32 without replacing that core. See
[QL32-UI-001](QL32-UI-001.md). These are class P, not OPL source-compatible.

Full historical numeric formats, `GO SUB` compatibility, third-party toolkit commands, historical channel syntax, `WHEN ERRor`, `TRY/CATCH`, `IMPORT`, native plugins, and unrestricted hardware access are outside the first M1 slice.

## 10. M1 conformance and unresolved choices

| Slice | Executable evidence |
| --- | --- |
| 1 | Linux build and CLI; lexer/parser for assignments and expressions; source-span diagnostics |
| 2 | Verified bytecode VM; isolated session state; variables and `PRINT`; `hello.bas` runs |
| 3 | Block `IF`, block and inline `FOR` with mixed ranges, `NEXT`/`EXIT`, `REPeat`; loop and branch edge cases |
| 4 | Procedures, functions, locals, verified recursion limit, arrays, block `SELect ON` |
| 5 | In-memory numbered lines; persistent terminal REPL; `LIST`, `RUN`, `NEW`, immediate statements; shared Linux conformance suite |

The 33-case suite covers success cases, syntax/type failures, overflow,
division by zero, array bounds/allocation limits and parameter type/rank,
stored-program validation, array references, source-order range evaluation,
session isolation, REPL recovery, recursion limits, and cancellation. Expression and output
cases shall include `-2^2` → `4`, `2^3^2` → `64`, `NOT 2 = 0` → `1`,
`-7 DIV 3` → `-3`, and `-7 MOD 3` → `2`, plus mixed-type rejection and each
`PRINT` separator. Class S requires a source link and tests of syntax and
behavior.

The expression precedence/conversion rules, Boolean truth model, `PRINT`
separators/formatting, and line-number replacement behavior are specified
above. M1 acceptance is complete: the supported-keyword and semantic cases
pass on a clean Linux build, the compiler and VM are separate portable layers,
and emitted bytecode is structurally and stack-flow verified before execution.
Forward calls and incomplete blocks are intentionally accepted during line
entry; unresolved calls are checked when the complete stored program is
compiled by `RUN`. Multiple-range evaluation is tested once in source order.
Nested inline loops are outside the explicit M1 grammar, and board allocation
limits are a later hardware qualification item. Broader SMSQ/E compatibility
claims remain limited to behavior with both source references and tests.

