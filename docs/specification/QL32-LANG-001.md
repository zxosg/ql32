# QL/32 BASIC language specification

**Document ID:** QL32-LANG-001  
**Version:** 0.7 (M1 language contract)
**Status:** Approved profile; M1 acceptance complete on Linux; source inclusion, GOTO, module constants, and module options implemented and host-tested
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
`GETCHANNEL`; it also defines `KOptMathDouble = 0` and `KOptMathSingle = 1`.
For ESP32 targets it defines `KEspCpuFreq80`, `KEspCpuFreq160`, and
`KEspCpuFreq240`. Programs include it with `INCLUDE "ql32.inc"`.

`OPTION KOptMathSingle` selects binary32 numeric calculations for that source
unit; `OPTION KOptMathDouble` selects binary64. The default is DOUBLE. An
option declaration belongs at module scope before definitions and executable
code. Each option category may appear only once. It must name the corresponding
declared constant, so the common form includes `ql32.inc` first. In SINGLE mode,
numeric literals, floating-point intermediates, stored numeric results, and
`SIN`/`COS` results are rounded to binary32. `%` variables still require
signed 32-bit integral values; expressions involving them use the selected
precision. `DIV` and `MOD` retain their defined integer behavior. SINGLE has
less range and precision than the default, but uses the ESP32's accelerated
single-precision arithmetic. The setting is per compiled source unit and does
not change other BASIC tasks. It changes arithmetic precision, not the VM's
value storage size or program memory use. This option is QL/32-specific. OPL and
Annex32 use double-precision reals, and SuperBASIC uses its default
floating-point format with integer-suffixed variables; none has this
per-source switch.

`OPTION KEspCpuFreq80`, `OPTION KEspCpuFreq160`, and
`OPTION KEspCpuFreq240` request the corresponding CPU frequency in MHz on
supported ESP32 firmware. CPU frequency is a device-wide setting, not a
per-task property. The request is applied when the module starts and remains
active until another module requests a different frequency or the board
restarts; among running tasks, the most recently started module containing
this option wins. Linux and macOS accept these declarations and ignore them.
This option is QL/32-specific and platform-dependent.

The first slice accepts this grammar (later sections extend it):

```text
program       := module_declaration* source_line*
module_declaration := const_declaration | option_declaration
source_line   := [line_number] [statement (":" statement)*] [comment] newline
statement     := assignment | print
const_declaration := "CONST" identifier "=" (signed_number | string)
option_declaration := "OPTION" option_constant
option_constant := "KOptMathDouble" | "KOptMathSingle"
                 | "KEspCpuFreq80" | "KEspCpuFreq160" | "KEspCpuFreq240"
comment       := ("REMark" | "REM" | "'") characters_to_end_of_line
assignment    := identifier "=" expression
print         := "PRINT" ["#" expression [("," | ";")]]
                [print_style_option {"," print_style_option} ","]
                [expression (print_separator expression)* [print_separator]]
print_style_option := ("INK" | "PAPER") expression
print_separator := ";" | "!" | "," | "\\"
expression    := literal | identifier | "(" expression ")" | unary | binary
```

Typical use:

```basic
INCLUDE "ql32.inc"
OPTION KOptMathSingle
OPTION KEspCpuFreq160

x = SIN(1) + COS(1)
PRINT x
```

`OPTION` is a module declaration, not an executable statement. The declaration
is processed during compilation; no executable instruction is generated, and
the selected settings are stored as module metadata.

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

The math functions `SIN(angle)` and `COS(angle)` accept finite numeric angles
in radians and return binary64 results. They do not use a mutable global angle
mode; this matches common host APIs and keeps sessions independent. `INT(value)`
returns the greatest integer less than or equal to the finite numeric value,
which is useful when converting drawing positions to pixel coordinates. These
functions are available for later language slices and are not part of the M1
compatibility claim.

`VAL(string$)` converts a complete decimal numeric value, including exponent
notation, to the selected numeric precision. Leading and trailing whitespace
is ignored; trailing nonnumeric text, expression syntax, and non-finite values
are errors. `EVAL(expression$)` compiles and evaluates one QL/32 expression at
runtime in the current session and call context, including user-defined
functions; its result must be numeric. `EVAL$(expression$)` evaluates the same
expression syntax and returns a string result unchanged, or formats a numeric
result with the normal `PRINT` number formatting. Both honor the active math
precision option. These dynamic functions are QL/32-specific and do not claim
Psion OPL compatibility.

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

By default, numeric literals and unsuffixed numeric variables use binary64.
With `OPTION KOptMathSingle`, they are rounded to binary32 as they enter
calculations and after each result. Arithmetic operators require numeric
operands and do not parse strings implicitly. Binary
`+` adds two numbers or concatenates two strings; it rejects a mixed pair.
This string use of `+` is a QL/32 choice; the cited SuperBASIC operator is
`&`, which M1 does not implement. String equality and inequality compare UTF-8
byte sequences exactly and are case-sensitive. Relational comparisons require
two numbers. Numeric comparison and logical operators return numeric `1` or
`0`. `NOT` and `AND`, `OR`, `XOR` accept numeric operands only; logical
operators test each operand for nonzero and evaluate both operands from left
to right. No implicit number-to-string or string-to-number conversion occurs.
`PRINT` formats either value type directly.

`/` is binary64 division by default and binary32 in SINGLE mode; division by
zero is an error. `DIV` and `MOD` require finite, integral operands in the
Int32 range. `DIV` is floor division,
and `MOD` satisfies `x = y * (x DIV y) + (x MOD y)`; divisor zero is an error.
`DIV` also errors if its quotient is outside the Int32 range. `^` uses the
selected precision; a non-finite result, zero raised to a negative
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
device-specific wrapping are deferred to display/output integration. On
graphical output, leading `INK color` and `PAPER color` options set the current
window colors before the first expression, for example
`PRINT INK 3, PAPER 6, "hello"`. Commas after the first expression retain their
normal tab-stop meaning. Graphical text defaults to 5×7 glyphs in 6×8 cells.
`LOADFONT(path$)` returns a session-owned font handle; built-in handles are
`KFontSmall`, `KFontNormal`, `KFontLarge`, and `KFontTitle`. `FONT handle,style`
selects normal text output, while `GFONT handle,style` independently selects
graphical `TEXT`; both accept an optional `#channel` selector. Styles are
combinable flags: normal `0`, bold `1`, underline `2`. `UNLOADFONT handle`
releases a loaded handle but not its file, and fails while a channel uses it.
Channels may share a handle without reloading the font. `GETFONT(selector,
handle)` returns metrics using the `KFontMetric*` selectors in `ql32.inc`.
Normal output fills paper cells; graphical text draws only glyph pixels.
QFNT v2 preserves each BDF glyph’s bitmap bounds, signed offsets, and advance;
it is limited to 8,192 bytes. Printable ASCII is supported and other characters
map to `?`; Czech glyphs are not included initially.

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

String values, including scalar string variables and quoted string literals,
support SuperBASIC-style, one-based inclusive slices: `text$(4 TO 8)`,
`text$(TO 10)`, `text$(10 TO)`, and `"ahoj"(1 TO 2)`. A single index
selects one character, and an empty `text$()` selects the whole string. QL/32
uses start 1 and the string length as the defaults for omitted bounds; an end
past the length is clipped to the string length. Indices count UTF-8 code
points. A start outside the string or a reversed range is a runtime error.
This is a QL/32-defined subset: slice assignment and descriptors on string
arrays are not yet supported, and SuperBASIC ROM variants differ in omitted
bound behavior. `LEN(string$)` returns the number of UTF-8 code points in a
string or string slice. `LEFT$(string$, count)` and `RIGHT$(string$, count)`
return up to the requested number of code points from the beginning or end.
`MID$(string$, start[, count])` uses a one-based start; if count is omitted,
it returns the rest of the string. Counts may be zero and are clipped at the
available characters. A negative or fractional count, or a non-positive or
fractional `MID$` start, raises an invalid-argument error. A positive start
beyond the string returns an empty string. These functions count UTF-8 code
points, as `LEN` and QL/32 string slices do. Unlike the original QL ROM's
coercion behavior, QL/32 requires string arguments. SuperBASIC expresses
equivalent operations with slices; OPL tests support zero-length results,
clipping at the string end, and invalid negative arguments; Annex32 documents
an optional `MID$` length. QL/32 combines those useful behaviors without
claiming source compatibility with either dialect. See the
[SuperBASIC string-slicing reference](https://superbasic-manual.readthedocs.io/en/latest/D/dim.html),
[`LEN` reference](https://superbasic-manual.readthedocs.io/en/latest/L/len.html),
and OPL/Annex references in [docs/reference](../reference/README.md).
`REPT$(string$, count)` returns `count` complete copies of the string, so
`REPT$("ab", 3)` returns `"ababab"`. A zero count or empty input returns an
empty string; negative or fractional counts are invalid. This OPL-inspired
function is QL/32-specific and differs from SuperBASIC `FILL$`, whose second
argument is the requested output length.
`CODE(string$)` returns the Unicode code point of the first character, or 0
for an empty string; `ASC` is an OPL-compatible alias. `CHR$(code)` returns
the UTF-8 string for a Unicode scalar value (0 through 0x10FFFF, excluding
surrogates). `LOC(haystack$, needle$)` returns the one-based code-point
position of the first match, or 0 if absent; an empty needle returns 1. Search
folds ASCII letter case and compares non-ASCII UTF-8 bytes exactly. These
functions are QL/32-specific adaptations of the SuperBASIC and OPL interfaces;
their character-code semantics do not claim compatibility with historical
8-bit character sets.
`UPPER$(string$)` and `LOWER$(string$)` change ASCII letters and leave all
other UTF-8 bytes unchanged. `STR$(number)` returns the same compact numeric
format used by `PRINT`; it accepts finite numeric values only. These are
QL/32-specific string utilities, not OPL's width- and precision-based
`GEN$`, `FIX$`, `NUM$`, or `SCI$` formatters.

### 32-bit integer bit functions and literals

QL/32 keeps `AND`, `OR`, `XOR`, and `NOT` as logical operators that return
numeric `1` or `0`. Bit manipulation uses the explicit functions `BAND(a,b)`,
`BOR(a,b)`, `BXOR(a,b)`, `BNOT(a)`, `ROL(value,count)`, and
`ROR(value,count)`. `SRA`, `SRL`, `SLA`, and `SLL` provide right and left
shifts. They operate on unsigned 32-bit values represented as
exact binary64 integers, from 0 through 4294967295. A value outside that range,
a fraction, or a non-finite value raises an invalid-argument error. `BNOT`
inverts exactly 32 bits. Rotation counts may be any finite integer; they wrap
modulo 32, including negative counts. Results are unsigned values in the same
range. Shift counts must be nonnegative integers. `SRA` fills vacated high
bits with the original most significant bit; `SRL` fills them with zero.
`SLA` fills vacated low bits with zero; `SLL` fills them with one. For counts
of 32 or more, `SRA` returns all sign bits, `SRL` and `SLA` return zero, and
`SLL` returns `&HFFFFFFFF`. These shift definitions are QL/32-specific.

Hexadecimal integer literals use `&H` or `&h`; binary literals use `&B` or
`&b`. Both accept at most 32 bits and must contain at least one valid digit.
Overflow and invalid digits are syntax errors. `HEX$(value)` and `BIN$(value)`
return uppercase hexadecimal digits or binary digits, with no prefix and no
leading zeroes (zero is returned as `"0"`). These functions and literals
provide conversion to and from numeric values without changing the meaning of
logical operators. QL/32's 32-bit unsigned profile is QL/32-specific; Annex32
provides bitwise operators and shifts, while SuperBASIC references describe
bitwise operations separately from Boolean operations. OPL32 code commonly
combines integer flags with `OR`; these similarities are design references,
not a compatibility claim. See [Annex32's bitwise reference](https://cicciocb.com/annex32help/V1.43/start.htm),
the [SuperBASIC operator appendix](https://superbasic-manual.readthedocs.io/en/latest/Appendices/Appendix11.html),
and the OPL samples noted in [docs/reference](../reference/README.md).

`FREE()` returns a comma-delimited string containing the current free-memory
estimate in bytes, the largest contiguous free block in bytes, and the number
of free blocks reported by the allocator, in that order. A platform without
fragmentation metrics leaves the unavailable fields empty while preserving the
comma positions. `FREE(0)` returns the free-memory estimate as a number;
`FREE(1)` returns the largest contiguous free block as a number, and `FREE(2)`
returns the number of free blocks as a number. ESP32 reports all three values
for byte-addressable heap. Linux supports only selector 0; selector 1 and 2
report that allocator fragmentation metrics are unavailable. Selectors must
be integer values from 0 through 2. `ql32.inc` defines
`KFreeTotal`, `KFreeContiguous`, and `KFreeFragments` for the selector values.
Values are platform memory snapshots, not exact counts of memory owned by the
current BASIC session. The target shell's separate `MEM` command is not
implemented.

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
interactive program. The REPL command processor provides `LOAD`, `SAVE`, `DIR`,
`DELETE`, `RENAME`, `FORMAT`, and `CD`. `DELETE lineNumber1,...` atomically
removes the listed numbered program lines and validates the result once, so a
matching block start and end can be removed in one command. `DELETE path$`
continues to remove a file; a quoted string or string variable is treated as a
path. `LOAD "path.bas"` accepts either
numbered BASIC source or unnumbered source. Unnumbered non-empty lines receive
line numbers 10, 20, and so on in
file order; blank lines are skipped. A file cannot mix numbered and unnumbered
source lines. After loading, use `LIST` to inspect the numbered program and
`RUN` to execute it.

`HELP` lists the implemented REPL commands, BASIC statements and commands, and
built-in functions. It includes commands that require optional runtime services;
those commands may report that a required service is unavailable when used.
`PAGER ON` enables per-session pagination for generated listings such as `HELP`,
`LIST`, `DIR`, and `TASKS`; it is off by default. Page size follows the output
window's text rows and columns. Submit an empty line for the next page, `L` to
advance one line, or `Q` to stop the listing. While waiting, only the requesting
session is suspended. Graphics UI adapters display the prompt as an on-screen
input panel; keyboard and serial text can answer it. `PAGER OFF` disables
pagination and `PAGER` reports the current mode.

Storage paths may select a named drive with a prefix before the colon:
`FFS:` is the internal flash filesystem and `SDC:` is the removable SD card.
For example, `DIR "FFS:/"`, `LOAD "SDC:/mandelbrot.bas"`, and
`SAVE "FFS:/test.bas"`. Drive names are case-insensitive. The slash after the
colon marks the drive root; paths remain inside that drive, and `..` cannot
escape its root. A drive prefix can be used by any operation that accepts a
storage path, including `LOAD`, `SAVE`, `DIR`, `INCLUDE`, `TASK`, and file
channels, as well as the file commands below.

`DIR [path]` lists the immediate entries of a directory in sorted name order;
directory names end in `/`. With no path it lists the session's current
directory. `CD path$` changes that directory for the current interpreter
session. `CD` accepts a quoted path, a string variable, or a bare path such as
`CD ..`; relative paths start at the current directory. `.` and `..` components
walk the directory tree, and a drive-qualified path selects its root or a
directory on that drive.

`DELETE lineNumber1,...` deletes the listed program lines atomically. The
candidate program is validated once after all removals, and a failed edit
leaves the stored program unchanged. `DELETE path$` removes a file and
`RENAME path$,newpath$` moves a file. Their paths are resolved relative to the
current directory unless drive-qualified. Rename fails if the destination
already exists; file deletion applies to files, not directories. `FORMAT path$`
recursively deletes all files and subdirectories
inside the selected directory while leaving that directory in place. To erase
a volume, pass its root, for example `FORMAT "SDC:/"`. Formatting is
destructive. File commands accept quoted paths or string variables; `CD`,
`DIR`, and `FORMAT` also accept bare path text. Relative include paths stay on
the drive of the including file.

Unprefixed paths continue to use the platform's default storage drive.
Attempting to use a drive that is not mounted reports a storage-unavailable
error. These names describe logical storage roles; a platform may implement
`FFS:` with SPIFFS, LittleFS, or another flash filesystem.

The scheduler-backed embedded REPL also provides `TASKS`,
`TASKRESULT id`, `TASKCHANNELS id`, `LOGON id`, `LOGOFF`, `KILL id`,
`TASKVIEW id,origin-x,origin-y,width,height[,offset-x,offset-y]`, `SLICE [instructions]`,
`PRIORITY task-id,weight`, `SUSPEND id`, and
`RESUME id`. `TASK` reads source through the configured storage service and
starts a task. Its statement form `TASK "path.bas"` reports the new task ID to
console channel `#0`; `task_id = TASK("path.bas")` returns the ID without
printing. Add `KTaskWait` to wait for completion:
`TASK "path.bas", KTaskWait` or `task_id = TASK("path.bas", KTaskWait)`.
Wait mode focuses the child while it runs, then resumes the caller. Child
failure details are available through `ERR`, `ERR$`, and `ERRX$`. Without wait
mode, source completion leaves the task at its own interactive prompt with its
variables, program, graphics window, and channels retained. `TASKS` lists task
IDs, kind, priority, state, and errors, along with the global instruction
slice; suspended tasks also show their saved resume priority.
`PAUSE delay` suspends only the calling task for `delay` milliseconds while
the scheduler continues running other tasks. The delay must be numeric and in
`0..4294967295`; fractional values round up to the next millisecond. `PAUSE 0`
yields until the next scheduler turn. A timed wait appears as `WAITING_TIME`.
The millisecond unit follows Annex32's `PAUSE` reference; task-local scheduling
and the accepted range are QL/32-specific ([Annex32 PAUSE](https://www.cicciocb.com/annex32help/V1.518/start.htm)).
`TASKRESULT id` prints all available properties. In expressions,
`TASKRESULT(id, selector)` returns one property selected by `KTaskResultStatus`,
`KTaskResultChannels`, `KTaskResultErrorCode`, `KTaskResultErrorDetail`,
`KTaskResultKind`, `KTaskResultPriority`, `KTaskResultResumePriority`,
`KTaskResultCanvasOriginX`, `KTaskResultCanvasOriginY`,
`KTaskResultCanvasWidth`, or `KTaskResultCanvasHeight`; include `ql32.inc` to
use these named constants. The canvas origin is signed screen-space; width and
height must be positive integers.
`GETTASK(KTaskCount)` returns the number of retained task records, including
the supervisor and completed records. `GETTASK(KTaskHandle, position)` returns
the task ID at a zero-based position in ascending task-ID order. These
supervisor-only selectors allow a program to enumerate tasks and inspect their
state with `TASKRESULT`.
`TASKVIEW id,origin-x,origin-y,width,height[,offset-x,offset-y]` changes the
selected task's screen-space canvas origin and clipping dimensions. It is
supervisor-only. `origin-x` and `origin-y` may be signed integers; `width` and
`height` must be positive integers. Each task's `WINDOW x,y,width,height`
positions remain relative to that task's local origin `(0,0)`. The compositor
adds the optional signed offsets to those local positions, then clips the
rendered content to the fixed viewport rectangle and display. Positive offsets
move content right and down; negative offsets move it left and up. Omitting the
offset pair resets both offsets to zero. For example, `TASKVIEW id,80,60,160,120`
shows a 160×120 crop at screen position `(80,60)`, while
`TASKVIEW id,80,60,160,120,-40,-20` shifts the task canvas 40 pixels left and
20 pixels up inside that same crop.
`TASKCHANNELS id` lists a session's reserved and opened handles with endpoint
types. `LOGON id` routes embedded console input to an idle worker so immediate
BASIC commands operate in that session; `LOGOFF` returns to the supervisor.
The prompt includes the attached task ID. `KILL id` stops an active task, or
removes a completed, failed, or cancelled task record and its saved result.
Log off before killing the currently attached session. `SLICE` reports or
changes the number of bytecode instructions allowed per scheduler turn (default
128, valid range 1..65536). `PRIORITY id,weight` sets a task's scheduling
weight to a non-negative 32-bit integer; the default is 4. Higher weights
receive proportionally more turns under smooth weighted round-robin scheduling.
`SUSPEND id` saves the task's current priority, sets priority to 0, and retains
its interpreter state and channels. `RESUME id` restores the saved priority.
`PRIORITY id,weight` sets the current and baseline priority directly; assigning
0 clears a saved resume priority and disables the task until a positive
priority is assigned. Resumption is manual, with no automatic wake-up timer.
`TASKS` marks tasks with a saved weight as suspended, and zero-baseline tasks as
disabled. It writes to the current print channel,
initially `#1`; `TASKS #channel` redirects one listing. Log off before
suspending the currently attached task.
These QL/32-specific commands are not available in the Linux command loop.
`ERR` returns the current QL/32 error code, `ERR$(code)` returns its message,
and `ERRX$` returns contextual detail for the current task error. Error code 0
means no error; positive codes are stable QL/32 codes, with no SMSQ/E numeric
compatibility claim.

| Code | `ERR$` message |
| ---: | --- |
| 0 | No error |
| 1 | General failure |
| 2 | Invalid argument |
| 3 | Syntax error |
| 4 | Type mismatch |
| 5 | Value out of range |
| 6 | Numeric overflow |
| 7 | Division by zero |
| 8 | Out of memory |
| 9 | Invalid path |
| 10 | Not found |
| 11 | Permission denied |
| 12 | Input/output error |
| 13 | Service unavailable |
| 14 | Invalid handle |
| 15 | Invalid task ID |
| 16 | Invalid task state |
| 17 | Task cancelled |
| 18 | Task resource limit reached |
| 19 | Operation not supported |
| 20 | Invalid task result selector |
| 21 | Task has not completed |
M2 also provides QL/32-specific `OPEN`, `READ$`, `EOF`, `WRITE`, and `CLOSE`
channel operations. File endpoints support reads and writes; serial endpoints
write to serial output, and null endpoints discard output. Value-returning
operations use function syntax while `WRITE channel, text$` and `CLOSE channel`
are statement commands. See
[QL32-API-001](QL32-API-001.md). `EDIT [lineNumber]` loads a stored numbered
line into the REPL editor. With no number, it opens the last line selected with
`EDIT`, or the first program line when no line has been edited yet. On serial
REPLs, Left/Right move the cursor, Home/End move to the line boundaries, and
Backspace/Delete remove characters. Up on an empty prompt recalls the last
submitted BASIC line. Enter replaces the line; clearing the
buffer then pressing Enter submits the line number alone, deleting that line.
The host stream REPL displays the selected line and accepts a replacement
numbered line; an empty response deletes it. Invalid numbers and line numbers
that are not in the stored program are rejected without changing the program.
The target shell's `STOP` and, later, `CONT`, remain future work.

Channels are universal session-owned I/O handles. `#0` is reserved for the
command/error console and keyboard input; `#1` is reserved for user/program
output and graphics. Window, file, serial, null, and future network endpoints
share one handle namespace. Generic `OPEN(deviceKind, parameters...)` returns
an automatically allocated channel handle; the device kind is a typed token,
not a string. Programs do not select channel numbers when opening endpoints.
Commands that perform I/O or drawing may select a channel explicitly, for
example `PRINT #out, text$`, `LIST #out`, `PLOT #window, x, y`, or
`CIRCLE #window, x, y, radius, 1`. `RECT` and `CIRCLE` accept an optional
`fill` flag: omit it or use `0` for an outline, or use `1` for a solid shape.
`PLOT` draws one pixel in the selected window's current ink color. Unqualified
`OVER mode` selects how covered pixels are written by graphics commands:
`0` sets pixels to `INK`, `1` clears them to `PAPER`, and `2` XORs the existing
palette index with `INK`. `OVER` applies to `PLOT`, `LINE`, `RECT`, `CIRCLE`,
and graphics `TEXT`; it does not affect `PRINT`. Its state belongs to the
selected graphics channel and defaults to `0`.
graphics commands use the current graphics output default; channel-qualified
graphics commands use a `#channel` selector. The clear command accepts
`CLS`, `CLS #channel`, `CLS ink,paper`, or `CLS #channel,ink,paper`; the color
form leaves those colors active for subsequent drawing. `CLOSEWINDOW` accepts
an optional `#channel` selector. Unqualified text output uses the current
output default. `CHANNEL #channel` changes the logical
default for all unqualified output operations, including text and graphics.
`CHANNEL #source TO
#destination` directly rebinds the source handle to the endpoint currently
bound to the destination. The binding is shared, session-local, and applies to
all operations; the source retains its original endpoint, restored by
`CHANNEL #source TO #source`. If the default is still `#1`, rebinding `#1` to
`#2` makes unqualified output and explicit `#1` operations use `#2`'s endpoint.
Graphics fail if that endpoint lacks graphics capability. Changing the default
to `#0` selects `#0` for unqualified output; explicit selectors still use the
selected handle's active binding. The current runtime still resolves per-session
routes per operation; direct binding is the approved target architecture.
Channel handles belong to one interpreter session. A task launched by the
embedded `TASK` command has its own `#0`, `#1`, and opened handle numbers; a
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
implemented. `OPEN(KDeviceWindow)` allocates a graphics window channel with no
arguments; use `WINDOW #channel,x,y,w,h` to position and size it. Reserved
channels `#0` and `#1` are still managed outside the opened-endpoint registry.

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

`GUPDATE 0` suppresses automatic presentation while drawing commands still
modify their retained canvases. Bare `GUPDATE` forces the current composed
screen to display without changing the selected mode. `GUPDATE 1` restores
automatic presentation. `ql32.inc` defines `KGupdateOff = 0`, and both
`KGupdateOn` and `KGupdateAuto` as `1`; they name the automatic mode, which is
immediate on current presenters. Other numeric modes are reserved. Update
suppression controls the shared physical display, so it affects drawing from
all sessions until a later `GUPDATE 1`. Use it around a drawing batch, then
call bare `GUPDATE` once to present the batch.

Prompt expressions must evaluate to strings. Keyboard input
suspends only its BASIC session and resumes when the UI presenter submits a
line. File-channel `INPUT` reads one line synchronously and raises an error at
EOF. Configured serial hardware channels support asynchronous line reads as
specified in [QL32-HW-001](QL32-HW-001.md); the legacy zero-argument serial
output endpoint and null channels do not support input.

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

## 8.1 Hardware channels and binary memory operations

QL/32 hardware access uses session-owned channel handles. Programs open a
device with `OPEN`, use common channel operations, and release the endpoint with
`CLOSE`. The portable runtime validates handles and buffer ownership; platform
C++ adapters initialize the selected hardware and perform the device operations.
This API is QL/32-specific. The current Linux/macOS virtual adapter and ESP32
Arduino profiles implement GPIO, ADC, SPI, I2C, serial, parallel GPIO,
capacitive touch, and timers. IrDA is reserved but unavailable. See
[QL32-HW-001](QL32-HW-001.md) for the complete parameter contract and examples.

The scalar operations are `IN(channel)` and `IN(channel, register)`, plus
`OUT channel, value` and `OUT channel, register, value`. Binary transfer uses
managed buffers and these signatures:

```basic
buffer = ALLOC(byteCount)
POKE buffer, byte1, byte2, byte3
value = PEEK(buffer)
word = PEEK_W(buffer)
long = PEEK_L(buffer)
POKE_W buffer, word
POKE_L buffer, long
count = IOREAD(channel, register, buffer)
status = IOWRITE(channel, register, buffer)
XFER #channel, txAddress, txLength, rxAddress, rxLength
DEALLOC buffer
```

`PEEK`/`POKE` operate on bytes; `_W` and `_L` operations use native target
byte order. The byte-list form of `POKE` writes bytes in source order. Device
transfers never byte-swap. `ALLOC` buffers belong to the current session, are
bounds-checked, and are released explicitly with `DEALLOC` or automatically
when the session ends. `IOREAD` returns the number of bytes read or a negative
error code; `IOWRITE` returns zero on success or a negative error code.
`XFER` is the raw SPI operation with separate transmit and receive buffers.
No `IN$` function is defined. `READ$` remains the line-reading form for
configured serial channels and waits asynchronously for a complete line.

The device-kind constants and common selectors are declared in `ql32.inc`;
board-specific pin and bus assignments are supplied by board include files.
Hardware initialization, pin reservation, bus ownership, and rollback on failed
open are responsibilities of the platform adapters. Scalar `IN` and `OUT` do
not repeat initialization.

## 9. UI boundary and deferrals

Native commands and procedures are statement forms: write the name followed
by zero or more comma-separated arguments, without call parentheses (for
example, `ROTATE 90`, `ALERT title$, message$`, or `draw_line x1,y1`).
Functions return values and require parentheses around their arguments,
including empty parentheses for zero-argument functions (for example,
`GWIDTH()` or `CONFIRM(title$, message$)`). A function call must be used in an
expression; a procedure cannot be used as an expression. A side-effecting
operation may deliberately provide both forms when it is useful as a command
and as a value-returning function. The command form performs the operation and
reports its result through normal command output; the function form returns
the result and does not print implicitly. Both forms must use the same runtime
operation. `TASK` is the first example: `TASK "worker.bas"` starts a task
and reports its ID, while `worker_id = TASK("worker.bas")` returns the ID for
later use. Parentheses remain part of other syntax such as definition
parameter lists, array indexing, string slicing, and grouping. These rules
apply to built-ins and user-defined procedures/functions alike.
`IPRINT [INK color,] [PAPER color,] [FLASH mode,] text$[, durationSeconds[, position]]`
posts a non-blocking graphical information toast. Duration must be positive
and defaults to three seconds. Position defaults to bottom-left and may be set
to one of the four `KDlgPos...` corner flags from `ql32.inc`. Ink and paper
default to palette indices 0 and 7. `FLASH` defaults to `KInfoFlashOff`; modes
are `KInfoFlashOff` (steady), `KInfoFlashText` (text color alternates between
ink and paper), and `KInfoFlashPaperInk` (ink and paper swap). The toast has no
title or buttons and disappears automatically. `IPRINT` is QL/32-specific.
`BUSY [INK color,] [PAPER color,] [FLASH mode,] text$[, position]` schedules a
no-title, no-button busy toast after three seconds; it persists until
`BUSY OFF`. It uses the same color, mode, and corner defaults as `IPRINT`.
Program completion and runtime errors clear it automatically. This is
QL/32-specific behavior inspired by OPL's delayed `BUSY` message.
OPL-inspired `ALERT` and `CONFIRM`
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

