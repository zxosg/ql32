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

- The current committed baseline is `3145eb0` (`Add session-aware UI services and BASIC support`); the current work is uncommitted.
- The Linux CLI reads complete source from a `.bas` file, accepts stdin source/batches, and opens a persistent line-oriented REPL on an interactive terminal. The REPL keeps session and numbered-program state between entries and reports a failed entry without exiting.
- Implemented language coverage includes assignments; numeric and string values; expression and `PRINT` rules; comments; block `IF`; block and inline `FOR` with mixed range lists; named `NEXT` and `EXIT`; block and inline `REPeat`; procedure/function definitions, parameters, invocation locals, return, forward calls, recursion, and array-by-reference parameters; zero-based inclusive arrays; block `SELect ON` with lists, ranges, and `REMAINDER`; keyword abbreviations for the supported keyword set; and canonicalized `LIST` output.
- Numbered lines are validated against candidate program state before replacement, while incomplete block prefixes and forward calls are accepted. Calls unresolved in the complete program are diagnosed by `RUN`. Tests cover replacement rollback, incremental block entry, REPL recovery, interpreter isolation, array rank/type errors, stored-program errors, and cooperative cancellation through `std::stop_token`.
- Known profile limits: `SELect` singleton comparisons use exact binary64 equality and only its block form is supported; allocation is capped at one million elements per array; nested inline loops are outside the explicit M1 grammar. See the specification for other QL/32 choices and deferrals.
- The suite has 30 CTest cases across CLI and runtime behavior, including recursion depth, allocation limits, and single-pass source-order evaluation of FOR ranges. `cmake -S . -B build`, `cmake --build build`, and `ctest --test-dir build --output-on-failure` pass with GNU C++ 15.2 and CMake 4.2.3.
- M1 BASIC core acceptance is complete locally: the supported keyword and semantic cases are covered, compiler and VM are separate portable source layers, execution verifies bytecode, and a clean host build passes all 30 CTest cases. The changes remain uncommitted and unpushed. Editor/filesystem commands and hardware integration belong to later milestones.

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

M2 adds the editor, files/channels, alerts, and confirmation dialogs. Start by
defining platform-neutral file/channel and asynchronous UI service contracts;
then implement Linux adapters and conformance tests before adding board
integration. Keep unverified SMSQ/E behavior labeled QL/32-specific. M1 code is
currently local and uncommitted; publish it only when explicitly requested.

## Starting a new chat

After cloning or pulling the repository, point the new chat at
`PROJECT-HANDOFF.md` and ask it to continue the next work item. The chat
history itself is not required: this handoff and the linked specification,
decisions, references, and `AGENTS.md` carry the project context.
