# QL/32 project handoff

Use this file to continue work across Codex chats. Read it together with
[`AGENTS.md`](AGENTS.md) before changing the project. The repository is the
source of truth; update this handoff when the milestone state or next task
changes.

## Project

QL/32 is a portable, native BASIC computer inspired by SMSQ/E SuperBASIC and
Psion-style application interfaces. The initial development host is Linux.
The first hardware target is LILYGO FabGL VGA32. Keep the language, VM,
runtime, system, and UI layers independent of board libraries.

## Current state

- Current baseline: commit `e287e24` (`Build the first Linux BASIC execution slice`).
- The Linux CLI reads a program from stdin or a `.bas` file.
- The first runnable slice supports assignments, numeric and string values,
  `PRINT`, parentheses, unary signs, `+ - * /`, colon-separated statements,
  line numbers, `REM`/`REMark`, apostrophe comments, and doubled quotes inside
  string literals.
- This is an early execution slice, not the completed M1 language. Structured
  control flow, procedures, functions, arrays, stored-program `RUN`/`LIST`, and
  a conformance suite remain to be implemented.
- CMake builds the `ql32_core` library and `ql32` executable. Test integration
  is not yet present beyond enabling CTest.

## Decisions already made

- M1 targets a small, verified SMSQ/E SuperBASIC subset. Mark behavior as
  compatible only when a reference and conformance test support the claim;
  otherwise label it QL/32-specific.
- Implement a tiny runnable core first, then finish the structured subset by
  M1 completion.
- Keywords are case-insensitive. SuperBASIC's mixed-case reference spelling
  indicates mandatory uppercase letters and optional lowercase letters;
  recognized abbreviations expand to canonical spelling in `LIST`.
- `ELSE IF ... THEN` is a nested `IF`; there is no separate `ELSEIF` keyword.
- M1 `FOR` is intended to support mixed comma-separated values and inclusive
  `start TO end [STEP step]` ranges, as well as block and inline forms.
- String literals escape a quote by doubling it. M1 accepts `REMark`, `REM`,
  and apostrophe comments.
- Scalar procedure/function arguments are passed by value; arrays by
  reference. Numeric arrays use zero-based inclusive bounds. Dynamic UTF-8
  string arrays are a QL/32 difference.
- `RUN` clears variables for that session.
- Full reference links and provenance are kept in
  [`docs/reference/README.md`](docs/reference/README.md); do not copy the
  reference manuals into this repository.

The detailed language contract and open questions are in
[`docs/specification/QL32-LANG-001.md`](docs/specification/QL32-LANG-001.md).
Accepted language-profile decisions are also summarized in
[`docs/decisions/ADR-002-language-profile.md`](docs/decisions/ADR-002-language-profile.md).

## Next work

1. Continue M1 from the existing Linux slice. Before extending language
   behavior, add focused host-side conformance coverage for the existing
   executable and connect it to CTest, following `AGENTS.md`.
2. Resolve the open language choices in section 10 of
   `docs/specification/QL32-LANG-001.md` in the order needed by implementation:
   expression precedence/conversions and Boolean truth rules first, then
   `PRINT` separators/formatting and line-number replacement behavior.
   Update the specification before implementing constructs that depend on
   those choices.
3. Implement the structured M1 subset in small, covered slices: block `IF`,
   `FOR` (including mixed range lists and inline form), `REPeat` with `NEXT`
   and `EXIT`, procedures/functions and locals, arrays, then `SELect ON` and
   immediate/program commands. Resolve `SELect` syntax and loop edge cases
   against the cited references and tests before claiming SMSQ/E compatibility.
4. Keep this file current with the implemented slice, build/verification
   status, and the next concrete task. Do not mark M1 complete while any of
   the full-M1 items in the language specification remain unfinished.

## Starting a new chat

After cloning or pulling the repository, point the new chat at
`PROJECT-HANDOFF.md` and ask it to continue the next work item in this file.
The chat history itself does not need to be available: this document plus the
linked specification, decisions, references, and `AGENTS.md` carry the project
context.
