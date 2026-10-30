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

- The current committed baseline is `3f7fd71` (`Link project handoff from README`); the current work is uncommitted.
- The Linux CLI reads complete source from a `.bas` file, or accepts stdin source. In stdin command batches, numbered lines are stored in memory and `LIST`, `RUN`, `NEW`, and unnumbered immediate statements are supported. This is batch command processing, not yet a persistent terminal REPL.
- Implemented language coverage includes assignments; numeric and string values; expression and `PRINT` rules; comments; block `IF`; block and inline `FOR` with mixed range lists; named `NEXT` and `EXIT`; block and inline `REPeat`; procedure/function definitions, parameters, invocation locals, return, forward calls, and recursion; zero-based inclusive arrays; and block `SELect ON` with lists, ranges, and `REMAINDER`.
- Known implementation limits: keyword abbreviations and canonical `LIST` formatting are not implemented; array parameters are not passed by reference; stored line bodies are validated only when `RUN` compiles them; `SELect` singleton comparisons use exact binary64 equality; only the block SELECT form is supported; allocation is capped at one million elements per array; nested inline loops are unsupported. See the language specification for further semantics and deferrals.
- The CTest CLI suite has 24 cases. `cmake -S . -B build`, `cmake --build build`, and `ctest --test-dir build --output-on-failure` pass with GNU C++ 15.2 and CMake 4.2.3.
- This remains an incomplete M1 language/runtime slice. Session isolation, cancellation, broader conformance, editor/filesystem commands, and embedded integration remain open.

## Decisions already made

- M1 targets a small, verified SMSQ/E SuperBASIC subset. Claim compatibility only when both a reference and conformance test support the behavior; otherwise mark it QL/32-specific.
- Keywords are intended to be case-insensitive. Mixed-case reference spelling indicates mandatory uppercase letters and optional lowercase letters; abbreviation expansion and canonical listing remain unimplemented.
- `ELSE IF ... THEN` is nested `IF`, not a distinct keyword.
- M1 `FOR` supports mixed comma-separated values and inclusive stepped ranges; both block and inline forms are supported.
- String literals escape a quote by doubling it. M1 accepts `REMark`, `REM`, and apostrophe comments.
- Scalar procedure/function arguments currently pass by value. The accepted design calls for arrays by reference, which remains unfinished.
- Numeric arrays use zero-based inclusive bounds. QL/32 dynamic UTF-8 string arrays differ from historical fixed-length arrays.
- `RUN` clears session variables. Numbered lines replace a prior line with the same number; a blank numbered line deletes it.
- `SELect` block arms accept values, inclusive ranges, and a final `REMAINDER`; exact singleton matching is currently a QL/32-specific choice.
- The language contract and remaining open questions are in
  [`QL32-LANG-001.md`](docs/specification/QL32-LANG-001.md). References and
  provenance are in [`docs/reference/README.md`](docs/reference/README.md).

## Next work

1. Add compatibility coverage for keyword abbreviations and canonical source listing, or revise the milestone contract if those requirements change.
2. Implement array-by-reference procedure/function parameters and test mutation through calls.
3. Make line entry validate/compile atomically, preserving the old stored line on invalid input; add tests for duplicate numbers, deletion, invalid bodies, and block program storage.
4. Expand tests for SELECT edge cases, arrays and type suffixes, call-frame errors, and separate interpreter sessions. Reconcile singleton matching with a trusted SBASIC reference/runtime before labeling it compatible.
5. Continue architecture work toward session isolation/cancellation and platform-neutral runtime services before embedded integration. Do not mark M1 complete while any required item in the specification remains unfinished.

## Starting a new chat

After cloning or pulling the repository, point the new chat at
`PROJECT-HANDOFF.md` and ask it to continue the next work item. The chat
history itself is not required: this handoff and the linked specification,
decisions, references, and `AGENTS.md` carry the project context.
