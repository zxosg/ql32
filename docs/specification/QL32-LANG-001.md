# QL/32 BASIC language specification

**Document ID:** QL32-LANG-001  
**Version:** 0.2 (M1 implementation draft)
**Status:** Approved language direction; detailed semantics become normative with conformance tests
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

QL/32 shall normalize a recognized abbreviated keyword to its canonical
mixed-case spelling when storing/listing a program. For example, entering
`10 def fn Square(x)` shall list as
`10 DEFine FuNction Square(x)`. Keyword resolution uses a table of canonical
spellings and mandatory letters, rather than a blanket prefix rule: `FN`
matches `FuNction` even though it is not a contiguous prefix. The full spelling
also matches. Intermediate omissions of lowercase letters are accepted when
they preserve order and resolve uniquely. If two registered keywords match
the same token in the same syntactic position, the parser reports an ambiguous
keyword instead of choosing arbitrarily. Intermediate spellings and collision
handling are QL/32 rules until checked against a live SMSQ/E interpreter.

Expansion applies to keyword tokens in code, not text inside strings or
comments, nor to longer user identifiers. The command table must record
abbreviations for every M1 keyword. Conformance tests shall cover minimum,
full, mixed-case, intermediate, and collision cases, plus round-trip
`LIST` output.

The first slice accepts this grammar (later sections extend it):

```text
program       := source_line*
source_line   := [line_number] [statement (":" statement)*] [comment] newline
statement     := assignment | print
comment       := ("REMark" | "REM" | "'") characters_to_end_of_line
assignment    := identifier "=" expression
print         := "PRINT" [expression]
expression    := literal | identifier | "(" expression ")" | unary | binary
```

## 3. Values and variables

| Form | QL/32 type | Initial value | Difference |
| --- | --- | --- | --- |
| `value` | IEEE-754 binary64 | 0 | Modern representation |
| `count%` | signed 32-bit integer | 0 | 32-bit width |
| `title$` | dynamic UTF-8 string | empty | UTF-8 representation |

Implicit scalar declaration is allowed. Boolean expressions produce a logical value usable by conditions. Their numeric representation and conversions are still open. Native resource handles are opaque and session-owned, never raw memory addresses. Invalid conversion, integer overflow, division by zero, and array bounds violations are defined runtime errors.

## 4. Expressions

M1's target operators are unary `+` and `-`, binary `+ - * / DIV MOD ^`, comparisons, `AND OR NOT`, parentheses, function calls, and array indexing. `/` performs floating division. For integer operands, `DIV` rounds the quotient downward and `MOD` satisfies `x = y * (x DIV y) + (x MOD y)`; this follows the cited SuperBASIC operator description. Int32 overflow is an error. Nonintegral operands to `DIV`/`MOD` are rejected in M1, a QL/32 restriction.

The first slice accepts literals, variables, parentheses, unary signs, and `+ - * /` with conventional arithmetic precedence. Adding two strings with `+` concatenates them; mixing strings and numbers in arithmetic is a type error. Before expanding the language, freeze a table for full precedence and associativity, comparison coercion, Boolean evaluation, exponent edge cases, and numeric conversions. Unsupported constructs produce explicit syntax errors.

## 5. Statements and blocks

`name = expression` assigns a scalar. The first runnable slice accepts one
optional expression per `PRINT`; plain `PRINT` emits a newline. Lists of
expressions, comma/semicolon layout, trailing separators, and `#channel`
syntax are deferred until their output behavior is specified.

The full M1 block set is:

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
does not require `END FOR`. Nested inline loops, especially multiple inline
`FOR` statements on one line, require conformance tests before being accepted.

`NEXT variable` advances to the next iteration, skipping the rest of the
current body; at the final value of an interval it does not advance beyond that
interval. It then proceeds through subsequent items in the range list before
ending the loop. `EXIT variable` leaves the named loop and continues after its
matching `END FOR`, or after the inline loop body. Loop-control statements
must identify an open matching loop in M1. Behavior for omitted identifiers,
multiple ranges' exact evaluation timing, and line-level inline-loop edge cases
must be specified by tests before compatibility is claimed.

Exact `SELect` arm syntax still needs verification. Optional loop identifiers
and historical ROM-specific quirks are deferred where they do not fit this
portable, explicit M1 contract.

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

Definitions are program-level. A function is called in an expression and must return a value; reaching `END DEFine` without one is a runtime error. A procedure is invoked as a statement. `LOCal` creates invocation-local names. Name lookup searches locals, parameters, then session globals. Recursion is supported with a bounded call stack; nested definitions are excluded from M1.

**QL/32 difference:** scalar arguments are passed by value by default; arrays by reference. Historical SuperBASIC procedure parameters can be passed by reference. These calls are class Q unless a compatible form is added later.

## 7. Arrays

`DIM array%(upper)` and multidimensional variants are planned for M1. QL/32 indexing starts at zero: `DIM a%(3)` creates indexes 0 through 3 inclusive. [SuperBASIC's numeric `DIM`](https://superbasic-manual.readthedocs.io/en/latest/D/dim.html) uses the same zero-based inclusive bounds; the indexing rule may earn class S after conformance tests. QL/32's dynamic UTF-8 string arrays do not reproduce the historical fixed-length string-array model and are class Q. Elements start with the type default. Every dimension is bounds-checked; allocation failure is a runtime error. Board-specific allocation limits remain to be measured.

## 8. Immediate mode, commands, and errors

Immediate statements execute without changing stored program source. Session variables remain inspectable after execution until explicitly reset. `RUN` clears variables in that session before starting the stored program. M1 requires source storage, `RUN`, and `LIST` on the Linux host; `EDIT`, `LOAD`, `SAVE`, and `DIR` arrive with editor/filesystem work in M2. The target shell also has `NEW`, `STOP`, `MEM`, `HELP`, and later `CONT`. Line-number replacement behavior remains an open decision.

The compiler pipeline is UTF-8 source → tokens → syntax tree → semantic checks → bytecode → verifier → VM. A VM state contains variables, stacks, source location, default output channel, and error state for one session. No mutable BASIC program state belongs in process-wide globals. Errors have stable code, category, message, and source span. A failed session does not reset the host or another session. Instruction slices and cancellation checkpoints prepare for multitasking.

## 9. UI boundary and deferrals

OPL-inspired `ALERT` and `CONFIRM` are M2 native calls, drawing is M3, and declarative `DINIT`, `DEDIT`, `DINT`, `DIALOG`, `MINIT`, `MCARD`, and `MENU` are M4 native calls. They do not alter M1 parser grammar. See [QL32-UI-001](QL32-UI-001.md). These are class P, not OPL source-compatible.

Full historical numeric formats, third-party toolkit commands, `GO TO`/`GO SUB` compatibility, historical channel syntax, `WHEN ERRor`, `TRY/CATCH`, `IMPORT`, native plugins, and unrestricted hardware access are outside the first M1 slice.

## 10. M1 conformance and unresolved choices

| Slice | Executable evidence |
| --- | --- |
| 1 | Linux build and CLI; lexer/parser for assignments and expressions; source-span diagnostics |
| 2 | Bytecode VM; isolated session state; variables and `PRINT`; `hello.bas` runs |
| 3 | Block `IF`, block and inline `FOR` with mixed ranges, `NEXT`/`EXIT`, `REPeat`; loop and branch edge cases |
| 4 | Procedures, functions, locals, recursion limit, arrays, `SELect ON` |
| 5 | Immediate mode and program commands; shared conformance suite on Linux |

The suite covers success cases, syntax/type failures, overflow, division by zero, array bounds, session isolation, and cancellation. Class S requires a source link and tests of syntax and behavior.

Before implementing the relevant feature, decide: full precedence and conversions; `PRINT` separators and formatting; line-number replacement behavior; `SELect` arms; multiple-range evaluation timing and inline-loop nesting; and array limits. These do not block the first expression/VM slice.

