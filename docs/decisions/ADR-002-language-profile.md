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

M1 is delivered in slices: expression/variable/`PRINT` core first, then blocks, procedures/functions, arrays, and interactive commands by M1 completion. The full set includes `IF`, `FOR`, `REPeat`, and `SELect ON`.

Structured conditionals use `ELSE IF condition THEN` as an `ELSE` with a
nested `IF`. `ELSEIF` is not a separate keyword; nested conditionals close
with matching `END IF` statements.

M1 retains SuperBASIC's flexible `FOR` range lists: mixed single values and
`start TO end [STEP step]` intervals, inline and block forms, and explicit
`NEXT variable` / `EXIT variable`. Historical implementation quirks remain
outside the contract unless verified.

