# QL/32 Phase 3 – Step B4 (Compact Bytecode) Test Plan and Acceptance Criteria

Scope: verify that replacing `detail::Instruction` / `std::deque` with compact 16-byte
records + side tables + line table (plan section 3.4) changes no BASIC-visible behaviour
and delivers the measured memory benefit. Applies to branch `codex/compact-vm` after B3.

## 0. Preconditions and definitions

- **Reference binary (REF):** built from the last B3 commit (tag it `pre-b4`). Kept
  unchanged for the whole B4 effort: `build-ref/` (Debug) and `build-ref-asan/`.
- **Candidate binary (CAND):** current B4 head, same build flags.
- **Differential run:** the same input program/commands through REF and CAND; compare
  stdout, stderr, exit code, and (where available) trace/diagnostic output.
- **Golden corpus:** curated deterministic programs in `tests/golden/` (plan A0).
- **Gate levels:** G1 (every commit), G2 (before B4 PR is opened), G3 (before merge:
  includes hardware).

## 1. Test levels

### T1 – Existing suites (G1)
- Run: full `ctest` in `build/` and `build-asan/` (ASan+UBSan).
- Pass: 100% of tests that passed at the pre-B4 baseline still pass. No test skipped,
  disabled or re-baselined by B4. Pre-existing failures recorded in `phase3-log.md` stay
  identical.

### T2 – Golden corpus differential (G1)
- Run every golden program on REF and CAND; diff outputs byte-for-byte.
- Pass: zero diffs. Any diff is a stop; expected files may only change in a separate,
  justified commit that also explains why REF was wrong.

### T3 – Opcode/feature coverage corpus (G2) — new `tests/language/b4_*`
Each case runs on REF and CAND, diffs output, and also asserts expected output literally.
Coverage matrix (every `Op` must appear in at least one case; add a test that fails if a
new `Op` is added without coverage):
- Arithmetic/logic: Push, Add…Power, comparisons, And/Or/Xor/Negate/Not, string concat,
  integer divide, modulo by zero and overflow errors.
- Variables: load/store globals and locals, scalar `$` strings, shadowing, recursion
  depth 1000, locals declared after use.
- Control flow: Jump, JumpIfFalse, nested IF, ForInit/ForNext with multiple ranges and
  STEP (positive, negative, zero error), `single` ranges, LoopNext, ExitLoop in nested
  and mixed loop types, SELECT with value/range/REMAINDER arms, FOR variable modified
  in body.
- Procedures: Call/Return/FunctionEnd, parameter passing (value and array), array
  arguments by reference, `expectsValue`, builtin calls, channel arguments, dynamic
  evaluation (`compileExpression`) inside a frame, recursive FN.
- Arrays: DimArray (1-4 ranks), ArrayGet/ArraySet bounds errors, StringSlice
  (start/end/single), PushArrayRef.
- I/O: Print with every `PrintSeparator`, PrintChannel, Channel* ops, Input (numeric,
  string, to array element), channel read/write/seek.
- Jumps by line number: `sourceJump`, `targetIsLineNumber`, jump scopes, jumps out of
  loops/procedures (error or allowed exactly as REF).
- Numeric precision: `NumericPrecision::Single` vs Double for arithmetic, FOR, PRINT.
- Pass: all cases identical on REF and CAND; coverage test passes.

### T4 – Encoding and limits (G2) — new unit tests on the encoder/decoder
- Round trip: compile → compact → decode must equal REF's `Instruction` stream for the
  fields that remain semantically relevant (opcode, operands, targets, flags).
- Operand width boundaries: 255/256, 65,535/65,536 instructions; 2^16 and 2^32-1
  constants/side-table indices (use synthetic generators); maximum FOR items, SELECT
  arms, parameter count, locals count, jump scopes depth.
- Pass: either correct behaviour or a clean compile-time error (“program too large”)
  with the same task state afterwards; never silent truncation or wraparound. Assert
  with checked narrowing in the encoder.
- Constant pool dedup: identical literals share one entry; distinct values (including
  `0.0` vs `-0.0`, NaN rejected as before, very long strings) are preserved.
- Side-table index validity: every `c32` index in a compiled program is in range
  (verifier pass `verifyProgram()`, run in debug builds at the end of compile).

### T5 – Debug metadata and errors (G2)
- Compile errors and runtime errors: compare message text, line, column, BASIC line
  between REF and CAND for ≥40 error cases (one per distinct error message in the
  compiler/VM; extract the list by grepping error sources).
- Line table: for every pc in a sample set of programs, `lineFor(pc)` equals REF's
  `Instruction.line/column/basicLine/sourceLineTarget`.
- Debugger: breakpoints by line, step in/over/out across calls, `skipBreakpointAddress`
  behaviour, pause/resume, trace output, inspection of globals/locals — run
  `debugger_tests` plus differential scripted sessions.
- `LIST`, statement-start handling, and `compileExpression` stepping unchanged.
- Release-image mode (if line table can be omitted): errors still reported with pc and no
  crash; documented format.

### T6 – Runtime state and lifecycle (G2)
- FOR state storage (vector stack replacing the hash map): nested loops, EXIT, error
  inside loop then resume, recursion with loops in each frame, task cancellation
  mid-loop.
- Repeated dynamic compilation (e.g. 10,000 EVAL expressions in a loop): memory
  (code + side tables) returns to baseline; no unbounded growth.
- Multiple tasks with different programs and concurrent compile: side tables are
  per-program; no sharing/aliasing bugs; task completion frees code memory (verify via
  allocation counters or the fake MemoryService).
- REPL: define procedures across commands, redefine a procedure, delete lines, `NEW`;
  results identical to REF.
- Allocation failure: fake allocator failing at every N-th allocation during
  compile (sweep N=1..K); each failure yields “Out of memory”, session state intact
  (previous program still runs). Same for loading a large program.
- Pass: no crash, leak or state corruption (ASan/UBSan clean; allocation counters
  balanced).

### T7 – Fuzz / stress (G2, time-boxed)
- Grammar-based random program generator (valid programs) → differential REF vs CAND,
  ≥10,000 programs, fixed seeds logged, zero diffs.
- Mutation fuzz on source text (invalid programs): CAND must not crash/hang; the error
  message must match REF's.
- Run under ASan/UBSan for at least 30 minutes total or the agreed iteration count.

### T8 – Performance (G2, informational with a threshold)
- Benchmarks: tight FOR loop (1M iterations), recursion depth 1000, 10k-element array
  fill/sum, string concat, Mandelbrot, procedure call storm, compile time for a 2,000-line
  program. Median of ≥5 runs, Release build.
- Pass: CAND ≤ 110% of REF run time; compile time ≤ 120% of REF (and compile peak
  memory not higher). Larger regressions require a written explanation in the log; they
  do not auto-fail the step unless > 150%.

### T9 – Memory footprint (G2 host, G3 hardware)
Measure for each: tiny (10 lines), Mandelbrot, a 2,000-line synthetic program, and the
largest real example.
- Host: bytes of code storage = records + side tables + line table + constants +
  per-program maps (`functions`, `arrays`, `constants`), via the A1 diagnostics, plus
  heap-allocation counts for compile.
- Pass: footprint ≤ 30% of REF for the 2,000-line program and ≤ 50% for tiny programs
  (record count × 16 B + side tables; if not met, document the cause and measured
  figures — this is a flag for review, not an automatic fail, per plan section 6).
  Allocation count for compile ≤ REF.
- Device (Xtensa) `size` output and `ESP.getFreeHeap` figures recorded per env.

### T10 – ESP32 build gate (G2)
- `pio run` for every environment in `platformio.ini` that built at pre-B4 succeeds.
- Pass: no new warnings in the VM/compiler files; flash size increase ≤ 2% (or explained),
  static RAM (`.data+.bss`) not increased.

### T11 – Hardware soak on a connected board (G3) — `tools/hw_soak.py`
Board: Olimex ESP32 SBC (primary); also LILYGO or CYD if connected.
1. Flash CAND; boot; capture baseline FREE(), FREE(1), free blocks at idle.
2. Run the golden corpus on-device (programs not needing absent hardware); compare to
   REF-flashed output (flash REF first, record, then CAND).
3. Run the handoff stress sequence: repeated load/run/free (≥200 cycles), 3 concurrent
   workers, Mandelbrot compile with workers running (the previously failing scenario),
   recompile loop (≥500 compiles).
4. 30-minute mixed soak: loops, array use, channel I/O, scheduler start/stop.
- Pass:
  - Outputs identical to REF on-device.
  - No reboot, panic, watchdog, `bad_alloc` or “Out of memory” in scenarios REF passed.
  - Mandelbrot + 3 workers compile succeeds if it succeeded on REF; improvement noted
    if REF failed (not a hard requirement).
  - Idle free heap after the soak within ±2 KB of the pre-soak value (no drift); largest
    block not lower than REF's equivalent measurement.
  - Free heap at the same program load ≥ REF (expected higher by roughly the T9 savings).
- Hardware unavailable: mark G3 “pending”; B4 may be merged to the work branch but not
  to the mainline.

## 2. Acceptance criteria summary

| ID | Criterion | Gate |
|---|---|---|
| AC1 | Existing ctest suites (normal + ASan/UBSan) pass with no new skips | G1 |
| AC2 | Golden corpus identical on REF and CAND; no expected-file edits in B4 commits (except separately justified) | G1 |
| AC3 | Every `Op` covered by a differential test; coverage guard test exists | G2 |
| AC4 | Encoder never truncates silently; width-limit cases give clean errors; verifier passes on all compiled programs | G2 |
| AC5 | Error messages, line/column/BASIC-line info and debugger behaviour identical to REF on ≥40 error cases and all debugger tests | G2 |
| AC6 | No leaks, no state corruption under allocation-failure sweep; sanitizers clean | G2 |
| AC7 | ≥10,000 generated programs differential-clean; mutation fuzz without crash/hang | G2 |
| AC8 | Benchmarks ≤ 110% REF runtime, compile ≤ 120%; anything worse explained | G2 |
| AC9 | Code footprint reported incl. side tables/line table; ≤ 30% of REF on 2,000-line program (or documented explanation) | G2 |
| AC10 | All ESP32 envs compile; no static RAM increase; flash growth ≤ 2% or explained | G2 |
| AC11 | Hardware soak per T11 passes with no drift, no crash, outputs identical | G3 |
| AC12 | Results (tables, REF vs CAND, commands used, seeds) appended to `docs/plans/phase3-log.md` and `phase3-metrics.md` | G2 |

**Stop conditions:** any unexplained diff in AC2/AC3/AC5; any sanitizer finding; any
hardware crash/boot loop not reproduced on REF. Fix before continuing; do not weaken or
re-baseline tests to pass.

## 3. Execution order for Codex

1. Tag `pre-b4`; build REF (normal + ASan) once.
2. Write T3/T4/T5 tests against REF first (they must pass on REF unchanged), commit them
   before implementing the encoder changes.
3. Implement B4 in sub-steps (encoder + verifier → VM dispatch → line table → FOR state →
   cleanup), running G1 after each.
4. Run T6–T10 (G2), fix, record.
5. Flash and run T11 (G3); record or mark pending.
6. Write the final B4 report comparing REF vs CAND per AC.
