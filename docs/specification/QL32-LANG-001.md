# QL/32 BASIC language specification

**Document ID:** QL32-LANG-001  
**Version:** 0.3 (M1 language contract)
**Status:** Approved profile; M1 implementation acceptance complete on Linux
**Sources:** [reference index](../reference/README.md), [language decision](../decisions/ADR-002-language-profile.md)

## 1. Language profile

QL/32 BASIC is an interactive, structured, case-insensitive dialect based primarily on SMSQ/E SBASIC. Psion OPL inspires managed UI, not the core grammar. Complete source compatibility with either language is not promised.

Compatibility labels apply per command: **S** = syntax and behavior verified against a cited SMSQ/E reference and tests; **Q** = QL/32 difference or QL-inspired command; **P** = Psion-inspired UI; **N** = new native API; **X** = unsupported. Historical spelling alone never earns S. All mappings here are candidates until tests pass.

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

The first slice accepts this grammar (later sections extend it):

```text
program       := source_line*
source_line   := [line_number] [statement (":" statement)*] [comment] newline
statement     := assignment | print
comment       := ("REMark" | "REM" | "'") characters_to_end_of_line
assignment    := identifier "=" expression
print         := "PRINT" [expression (print_separator expression)* [print_separator]]
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
M2 also provides QL/32-specific `OPEN`, `READ$`, `EOF`, `WRITE`, and `CLOSE`
file-channel calls; see [QL32-API-001](QL32-API-001.md). `EDIT` and the target
shell's `STOP`, `MEM`, `HELP`, and later `CONT` remain future work.

The compiler pipeline is UTF-8 source → tokens → syntax tree → semantic checks → bytecode → verifier → VM. A VM state contains variables, stacks, source location, default output channel, and error state for one session. No mutable BASIC program state belongs in process-wide globals. Errors have stable code, category, message, and source span. A failed session does not reset the host or another session. Instruction slices and cancellation checkpoints prepare for multitasking.

## 9. UI boundary and deferrals

OPL-inspired `ALERT` and `CONFIRM` are M2 native calls, drawing is M3, and declarative `DINIT`, `DEDIT`, `DINT`, `DIALOG`, `MINIT`, `MCARD`, and `MENU` are M4 native calls. They do not alter M1 parser grammar. SuperBASIC remains the core language, including its loop forms; selected OPL-inspired refinements may be added when they simplify QL/32 without replacing that core. See [QL32-UI-001](QL32-UI-001.md). These are class P, not OPL source-compatible.

Full historical numeric formats, third-party toolkit commands, `GO TO`/`GO SUB` compatibility, historical channel syntax, `WHEN ERRor`, `TRY/CATCH`, `IMPORT`, native plugins, and unrestricted hardware access are outside the first M1 slice.

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

