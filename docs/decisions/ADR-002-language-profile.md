# ADR-002: M1 language profile

**Status:** Accepted direction; detailed syntax is under QL32-LANG-001 review  
**Source:** “QL32 Project Idea” chat and follow-up decisions, 30 October 2026

QL/32 uses SMSQ/E SBASIC for core syntax and Psion OPL for managed UI ideas. M1 aims for a **verified subset** of SMSQ/E syntax and behavior. A compatibility label applies per command and requires both a primary reference and conformance tests.

The chat chose UTF-8 text, binary64 and Int32 numbers, value-passed scalar arguments, and zero-based arrays. Numeric array bounds are also zero-based and inclusive in SuperBASIC; that shared behavior can be class S after testing. Dynamic UTF-8 string arrays, numeric representation, and default value passing are QL/32 differences. OPL-style UI is class P and arrives after M1.

Follow-up choices: double a double quote within a string literal; accept `REMark`, `REM`, and apostrophe comments in M1; clear session variables when `RUN` starts. The first release implements a small runnable core, then completes the structured set within M1.

SuperBASIC's mixed-case keyword notation is also required: uppercase letters
mark the minimum abbreviation, lowercase letters are optional, and recognized
tokens expand to full canonical spelling when listed. `REM` is the historical
abbreviation of `REMark`; apostrophe remains a QL/32 comment alias.

M1 is delivered in slices: expression/variable/`PRINT` core first, then blocks, procedures/functions/locals, arrays, block `SELect ON`, and the in-memory command loop. The Linux implementation now meets the defined M1 acceptance matrix, including the persistent terminal REPL and verified bytecode. Later milestones add editor/filesystem and UI integration.

Structured conditionals use `ELSE IF condition THEN` as an `ELSE` with a
nested `IF`. `ELSEIF` is not a separate keyword; nested conditionals close
with matching `END IF` statements.

M1 retains SuperBASIC's flexible `FOR` range lists: mixed single values and
`start TO end [STEP step]` intervals, inline and block forms, and explicit
`NEXT variable` / `EXIT variable`. `REPeat name` supports inline and block
forms; `NEXT name` restarts it, while `EXIT name` leaves the named loop. A
control statement may target an outer loop. Nested inline loops and omitted
loop names are outside the initial implementation. Historical implementation
quirks remain outside the contract unless verified.

The expression profile, Boolean rules, `PRINT` output, and stored-line update
rules are specified in QL32-LANG-001. In brief, arithmetic is strict about
types, booleans are numeric 0/1 with nonzero numeric truth, comparisons share
one precedence level, and the supported PRINT separators have deterministic
text-stream behavior. Numbered lines replace the same number, and a blank
numbered line deletes it. These are implementation decisions until covered by
the conformance suite; they do not by themselves establish SMSQ/E
compatibility.

Current implementation decisions: numeric arrays use zero-based inclusive
bounds and are limited to one million host elements per array; scalar
procedure/function arguments are copied by value and array arguments by
reference; `SELect` singleton arms use exact binary64 equality; and stdin
command batches and the terminal REPL support `LIST`, `RUN`, and `NEW`. Those
behaviors remain QL/32-specific where they have not been verified against
SMSQ/E. M1 acceptance and the next milestone are tracked in `PROJECT-HANDOFF.md`
and the language specification.

