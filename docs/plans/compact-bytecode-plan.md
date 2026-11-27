# Compact bytecode plan

Status: plan only; nothing here is implemented. Date: 2026-11-27, baseline `main` at 36159bc plus #49.

Goal: shrink the retained compiled program (today ~55-60 KB for the 1162-instruction
`dirplayer.bas` on the CYD) so that the CYD (ESP32, no PSRAM, ~180 KB free at idle)
keeps enough free heap and a large enough contiguous block for the player UI,
canvas and audio. The language semantics do not change. See also
`docs/plans/bytecode-32b-repack-analysis-2026-11-22.md` (field repack, rejected as
insufficient) and `docs/notes/dirplayer-workarounds.md` (P10-P13).

## 1. Measured baseline

### 1.1 Layout

| Item | Host (LP64) | ESP32 (ILP32) |
| --- | ---: | ---: |
| `sizeof(Instruction)` | 40 B | 32 B (layout estimate, matches P12 on-device report) |
| `sizeof(Token)` | 24 B | n/a (tokens are freed after compile) |
| Instruction store | `std::deque`, 512 B nodes (12 per node host, 16 per node ESP32) | same, node slack up to one record |
| `InstructionExtras` | allocated only for instructions that need runtime data (`forItems`, `parameters`, `localNames`, `inputVariables`, `targetVariable`, `selectItems`) since P12 | same |
| Names / string literals | `SharedText` pointers into a pooled text pool (deduplicated) | same |

### 1.2 Host measurement (instrumented allocator, `new`/`delete` counted)

Harness: scratch program linking `interpreter.cpp`, counting every allocation
around `Lexer::scan` and `Compiler::compile`; "retained" is live bytes after the
compiler and its tokens are destroyed and the compiled code is kept, minus the
source text. Includes a small fixed overhead (about 4-5 KB: text pool tables,
static maps), which dominates the small programs.

| Program | Source B | Tokens | Instructions | Extras with runtime data | Text-pool strings | Retained (host) | B / instruction (host) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `music-player/dirplayer.bas` | 7861 | 2202 | 1162 | 60 | 144 | 66.7 KB | 57.4 |
| `attractor.bas` | 1213 | 249 | 114 | 2 | 30 | 10.4 KB | 90.9 (fixed overhead) |
| `embedded_ui.bas` | 983 | 201 | 122 | 0 | 49 | 11.1 KB | 90.8 (fixed overhead) |

Peak during compile for dirplayer (host): about 108 KB live, i.e. 7861 B source +
2202 tokens (~53 KB as 24 B tokens in deque blocks) + growing code. On the ESP32
tokens are larger (~112 B/token measured earlier with `std::string` text), which is
why compile peak, not retained size, caused the original ALLOCFAIL (P1-P3, now
chunked/flat). Distinct opcodes used by dirplayer: 38 of 65.

Breakdown of dirplayer retained size on the host: instruction store ~46.5 KB
(1162 x 40 B) + deque slack, remainder ~20 KB = text pool (144 strings plus
shared-pointer control blocks), 60 extras records with vectors of names, function
tables, fixed overhead. Of the 1162 instructions 598 carry a `name`.

### 1.3 CYD figures (from the recorded on-device runs, not re-measured today)

| Point (dirplayer, CYD) | Free | Largest block |
| --- | ---: | ---: |
| Idle prompt | 180.7K | 110.6K |
| After LOAD (source retained) | 164.7K | 110.6K |
| RUN start (after P12) | 96.7K | 47.1K |
| After 25.6 KB canvas (160-line window) | 66.2K | 22.5K |
| Playing z00m (after P12) | 44.6K | 8.2K |
| Playing (P13 soak, whole-run min) | min free 27.7K | |

The RUN step costs ~68 KB (164.7K -> 96.7K) which, with 1162 x 32 B = 37 KB of
instructions, means ~30 KB is pool, extras, deque slack, VM state, symbols and
compile-time fragmentation that never returns. Per instruction that is about 58 B
retained on the device, matching the host figure. **Phase 0 must re-measure the
per-instruction cost on the CYD** (instrumented via HEAP STATS or a debug-only
`MEMSTATS` dump) for dirplayer and two other programs before any phase starts; the
table above is the baseline to compare against.

## 2. Target encoding

Inspired by Psion OPL QCode (byte opcodes, variable-length operands, constants
referenced by index, procedure-at-a-time translation), but keeping QL/32's
instruction-index addressing so that EVAL append/erase and TRAP rollback keep their
current semantics.

### 2.1 Code stream

- One `std::vector`-free **segmented byte buffer** (fixed 1 KB segments, linked by
  an index array; no allocation above 1 KB, so there is no large contiguous
  requirement). Segments never move, so decoded pointers stay valid.
- Instruction = 1 byte opcode + operands:
  - **Operands are variable length (LEB128-style, 1-3 bytes).**
  - Names, string literals, parameter lists, local names, FOR item specs and
    SELECT items live in a **side table** (`ProgramTables`), referenced by a
    1-2 byte index. The pooled text and `SharedNames` already exist and map
    almost directly.
  - Jump targets stay **instruction indices**; a small **index table** (one
    4-byte byte offset per 8 instructions, plus a decode of at most 7 opcodes)
    turns an index into a code position. A jump is thus O(8) decode steps
    rather than O(1); hot loops cache the resolved position in the decoded
    cursor (see risk in section 5).
- **Source position** (line, column, basicLine; 6 B today) moves to a separate
  **position table**, delta-encoded per statement (1-3 B per instruction on
  average, ~2 B typical), because positions are only needed for diagnostics,
  the debugger and error reporting, never for execution. Unusually long lines
  keep full 16-bit values (no truncation: the 16-bit overflow check stays).
- Flags (`builtin`, `expectsValue`, `channelArgument`, slice flags, `sourceJump`,
  `statementStart`, ...) become opcode variants or one flags byte only where
  needed; most instructions have none.

### 2.2 Expected size per instruction

| Part | Today (ESP32) | Target |
| --- | ---: | ---: |
| Opcode + operands (code stream) | 32 B (fixed record) | 3-6 B average (1 B opcode, 1-2 B names/literal ids, 1-3 B jump/arg count) |
| Positions | included | ~2 B delta table |
| Names / literals / extras | pool + extras, ~16-20 B amortised | same pool, ~10-14 B amortised (shared pointers become 16-bit indices; extras records become packed tables) |
| Index table (jump lookup) | none | 0.5 B |
| Deque slack | up to 16 B per node | < 0.5 B (1 KB segments) |
| **Total** | **~58 B** | **~16-22 B** |

Realistic saving for dirplayer (1162 instructions): 58 B -> 18-22 B gives about
**22-26 KB retained instead of 67 KB on the CYD; saving 35-45 KB**, with a wide
uncertainty (the pool and extras share is the least certain; a conservative
estimate is 28-30 KB saved, i.e. retained ~38 KB). Largest contiguous request
becomes 1 KB (the segment), removing the dependence on holes.

### 2.3 What would that buy dirplayer (CYD estimates)

| Scenario | Needed | Expected availability after compaction |
| --- | --- | --- |
| 14-row layout (full-screen 38.4 KB canvas instead of 25.6 KB, +12.8 KB) | +12.8 KB free and a 38.4 KB contiguous block (the canvas itself) | RUN start free ~125-135K; largest block ~60-70K (was 47K) after code moves into 1 KB segments; the canvas then fits with ~25K free left. |
| KNUCKLE digi-drums (6810 samples, 13.6 KB in 128-sample blocks; budget is 4096 today) | +5.4 KB free, no contiguous block (128-sample blocks) | Raising the ESP32 drum budget to 8192 samples becomes affordable. |
| Playback admission (32 KB total, 8 KB contiguous) | | Idle-in-app free ~65-75K and largest > 20K, so the admission passes with margin. |

These are estimates; they are gates, not promises: a phase passes only if the
measured numbers meet the gates in section 4.

## 3. Phases

Each phase is independently shippable and revertable (separate PR, feature gated
by a compile-time option until Phase 3).

### Phase 0: measurement and differential-test harness (no behaviour change)

- Promote the scratch harness into `tools/` (host-only): per-program table of
  instructions, extras, pool, retained bytes, compile peak, largest block, with
  the instrumented allocator. Add a debug `MEMSTATS`/HEAPSTATS field (retained
  program bytes) for the device.
- Re-measure the CYD for dirplayer and two other programs (idle, after LOAD, RUN
  start, after canvas, playing) and record the baseline in this file.
- Differential harness: run each `examples/*.bas` and the ~90 test programs on
  the current VM, record output and error `line:column` for each runtime
  error, store as golden data. Later phases compare against it.
- Exit gate: harness merged; baseline numbers recorded; no VM change.

### Phase 1: encoder/decoder plus dual-run mode

- Add `CompactProgram` (segmented byte stream, side tables, position table, jump
  index) and an `encode(const std::deque<Instruction>&)` plus
  `decode(index) -> Instruction` that must round-trip every field.
- Dual-run mode (host-only debug flag): the VM keeps executing the old form and
  after every instruction decodes the compact form at the same index and asserts
  equality (op, operands, flags, line, column, targets). Property test: random and
  corpus programs, including the 16-bit limits.
- Exit gate: round-trip equality over all example programs and all test
  programs; host + sanitized ctest; no device behaviour change (feature off on
  device builds).

### Phase 2: VM executes from compact code

- Replace instruction fetch by a cursor over `CompactProgram`; the VM decodes the
  next instruction into a small register-held struct. Frames keep an
  instruction index as the resume point (index-based, so EVAL append/erase
  and TRAP rollback keep working).
- EVAL/immediate-mode append and erase operate on the segmented stream
  (truncate to index N; erase of the tail only, as today).
- Debugger, breakpoints, error positions read the position table.
- Both forms still exist; a build flag selects the executor. Differential
  harness from Phase 0 runs both.
- Exit gate: differential output and error positions identical; golden 16/16;
  execution speed on the CYD within the budget in section 5.

### Phase 3: remove the old form

- Drop `Instruction` as a stored form (keep it as the decoder's transient view),
  delete the deque store, `ExtrasPtr` and the compile-time-only metadata paths
  that are replaced by tables.
- Compiler emits straight into the segmented stream (no intermediate
  `std::deque<Instruction>`), which also lowers compile peak.
- Exit gate: memory targets of section 4 achieved on the CYD.

### Phase 4: tuning

- Opcode variants for common patterns (e.g. push small integer, load/store local
  by slot, compare-and-jump), a peephole pass only if profiling shows decode
  cost; tune segment size (512 B - 2 KB), index-table stride (4-16), position
  table encoding.
- Optional: per-procedure lazy compile only if Phase 3 is insufficient (separate
  decision, not part of this plan).
- Exit gate: speed back within 5% of the Phase 0 baseline on the CYD, or
  documented and accepted.

## 4. Gates per phase

Every phase must pass, on the exact merged SHA:

1. Host `ctest` (91 tests today) and sanitized `ctest` (ASan/UBSan).
2. The 3 pio builds (`esp32_2432s028_spiffs`, `olimex_esp32_sbc_fabgl`,
   `ql32_tenstar_esp32s3`).
3. Golden device regression 16/16 (`tools/ql32-golden-device.py`).
4. Hardware gate on the CYD with `DEBUGBAR ON`, no `FORMAT`: dirplayer foreground
   run, play KNUCKLE.YM, z00m.vtx, LyraII1.vtx, switching songs, big directory,
   with `late=0`, `failedalloc=0`, and the heap numbers below.

Per-phase memory gates (dirplayer, CYD, FREE / largest at RUN start; baseline
96.7K / 47.1K):

| Phase | Gate |
| --- | --- |
| 0 | Baseline recorded for 3 programs; no regression |
| 1 | No device change (feature off); host round-trip passes |
| 2 | Both forms present: no memory gate; speed gate only |
| 3 | RUN start >= 125K free, largest >= 60K; compile peak not above baseline |
| 4 | Same plus speed within 5%; stretch: 14-row layout runs, then KNUCKLE drum budget 8192 |

Phase 3 passing is the condition for restoring the 14-row layout and the larger
drum budget (separate PRs).

## 5. Risks and required tests

| Risk | Mitigation / required tests |
| --- | --- |
| EVAL append/erase of immediate code | tests for append then erase of the tail, repeated, with live frames and jump targets; verify no segment leak under the sanitized build |
| TRAP rollback | tests that roll back into code before and after the trapped region; instruction indices must not shift |
| Instruction-index jump targets | the jump index table must round-trip every target; property test jumping to every index of every corpus program |
| Debugger / error line and column | differential comparison of every runtime error `line N, column M`, breakpoints, current location, remote debug JSON `line` and `column` |
| 16-bit field overflow (`Narrow<uint16_t>`) | keep the "program exceeds a bytecode field limit" error; add boundary tests at 65535 lines, 65535 columns, 65535 names, side table index overflow (2-byte index limit must raise the same error, not wrap) |
| Compile-time peak heap | measure with the Phase 0 harness; require peak <= baseline at every phase (the compiler must not hold both forms at once in Phase 3) |
| Execution speed on the CYD | benchmark `attractor-benchmark*.bas` and loop-heavy programs before and after on the device; decode must stay under about 5% overhead; the cached cursor and the 8-instruction jump window bound it |
| Fragmentation | no allocation above 1 KB in the code store; verify the largest block at RUN start (gate above) |
| Concurrency (cooperative workers sharing a program) | the code store is immutable once running except through EVAL on the foreground session; test two tasks executing the same procedure |
| Semantic drift | the differential harness is the primary guard; any mismatch blocks the phase |
| Schedule risk | each phase is revertable; the dual-run mode allows shipping Phase 1 with no device effect |

## 6. Pros and cons of the approach

Pros: biggest remaining lever (35-45 KB, the only one that also removes the
contiguous-block dependence), compile-peak reduction in Phase 3, no language
change, incremental and revertable phases, positions table lowers per-instruction
cost for a feature used rarely.

Cons: touches the VM hot loop (speed risk on a 240 MHz core), decode cost for
jumps, a significant test burden (differential harness), temporary complexity
while both forms coexist, and the saving range is uncertain until Phase 0 measures
the CYD per-instruction cost.

## 7. Out of scope

Language changes, a new instruction set visible to users, lazy per-procedure
compile (re-evaluate after Phase 3), moving code to flash, PSRAM boards.

## 8. Related: compiling from disk and a `.q32b` cache

See `docs/plans/compile-on-disk-review.md`: removing the duplicate source copies
(Step A) is independent of this plan; an on-disk artifact (Step B) should reuse this
plan's segment, side-table and position layouts and therefore follows Phase 1-3.

## 9. Phase 0 and Phase 1 status (implemented)

### 9.1 Full-screen dirplayer on the CYD (firmware 85dc17c, repo `dirplayer.bas`, 320x240, 14 rows)

Measured with `FREE()` markers (free, largest block) and `HEAP STATS`:

| Phase | Free | Largest | Gate (>=125K / >=60K) |
| --- | ---: | ---: | --- |
| Idle | 180.7K | 110.6K | |
| LOAD | 164.7K | 110.6K | |
| RUN start (after compile) | 96.0K | 47.1K | gap 29K free, 13K largest |
| After 38.4 KB canvas | 65.5K | 18.4K | |
| Ready screen | 70.7K | 18.4K | |
| Playing (z00m) | 44.0K | 4.6K | late=0, failedalloc=0 |
| After exit | 138.9K | 55.3K | lifetime min free 32.8K |

Full-screen playback worked (late=0, failedalloc=0, 1311 music blocks) but with only 4.6K contiguous
left; the margin is thin.

### 9.2 Host baseline (`ql32_bytecode_footprint`, instrumented `operator new`)

| Program | Instr | Retained | Compile peak | Compact code+pos+index | per instr |
| --- | ---: | ---: | ---: | ---: | ---: |
| dirplayer | 1162 | 66.6 KB | 91.7 KB | 9.5 KB | 8.2 B |
| attractor | 114 | 10.2 KB | 20.1 KB | 0.9 KB | 8.0 B |
| artcircles | 150 | 9.5 KB | 19.6 KB | 1.2 KB | 7.9 B |
| embedded_ui | 122 | 10.9 KB | 20.7 KB | 1.0 KB | 8.3 B |

Retained (66.6 KB) matches the layout estimate (66.5 KB) so the model is sound. In the Phase 1
encoding, instruction stream = ~4 B/instr, positions = ~3 B/instr (higher than the 2 B planned:
column deltas are rarely small), jump index = 1 B/instr (two 4 B entries per 8 instructions;
can be halved by sharing one table). Side tables are still copies of the full records (names
144 entries, extras 60 records for dirplayer); packing those is Phase 2/4 and is where the rest
of the saving has to come from. So the stream is ~8 B/instr against 40 B (host) / 32 B (ESP32)
records; total retained will be bounded by the pool and extras, not the stream.

### 9.3 What was built

- `src/language/compact_program.{hpp,cpp}`: `CompactProgram::encode`/`decode`, 1 KB segmented
  byte buffers, delta position table, jump index every 8 instructions, side tables for names and
  extras, `instructionsEqual`, `verifyCompactRoundTrip`, `compactDualRunCheck`.
- Dual-run: CMake option `QL32_COMPACT_DUALRUN` (host debug, off by default, never defined for
  firmware). The VM checks every instruction before executing it; a stale encoding (EVAL
  append/erase) is re-encoded once before a mismatch is reported. Verified sensitive by
  injecting a deliberate mismatch: 5+ test binaries fail.
- `tests/compact_program_tests.cpp`: literal edge cases (-0.0, NaN, inf, int32 boundaries),
  4000-line program across many segments and index strides, 16-bit field limits, extras and
  procedures, mismatch detection, and a corpus walk over every compilable `*.bas` in
  `examples/` and `tests/` (99 programs, 8243 instructions round-trip exactly).
- `tools/bytecode_footprint.cpp`: instrumented allocator (retained and peak bytes) plus compact
  columns.
- The old VM is authoritative; device behaviour is unchanged.

### 9.4 Gate results

Host ctest 92/92, sanitized 92/92, dual-run build 92/92 (every executed instruction of every
test compared against its compact decode), 3 pio builds, golden 16/16 on CYD.

### 9.5 Next

Phase 2 needs the decoder cheap enough for the VM hot path (the O(8) seek per jump and the
3-varint position stream are not an issue for execution, since positions are only read on
errors; the real cost is decoding every instruction into an `Instruction`). Before Phase 2:
pack names/extras tables (shared name list instead of `SharedText` copies; extras as packed
records), and measure decode cost per instruction on the CYD.

## 10. Phase 2 results: VM executes from compact code

`InstructionSequence::makeCompact()` (called after compile, before the first slice) replaces
the instruction deque with the compact stream. The VM reads only through
`instructionCount()`, `instructionAt()`, `truncateInstructions()` and `appendInstruction(s)`
(EVAL append/erase, TRAP rollback, debugger and error locations all use them). Decoded
instructions live in an 8-slot ring so `activeInstruction` stays valid; sequential reads
continue from a cursor, random reads seek from the nearest 8-instruction checkpoint.
Extras records are borrowed from the side table (tagged `ExtrasPtr`), names are shared.
Segments are 512 B (4 KB and 1 KB were measured; 4 KB was worse for the largest block).

Switches: `QL32_COMPACT_CODE` (CMake option; enabled for the CYD pio env) and
`QL32_COMPACT_DUALRUN` (host debug: keeps the deque and compares every decoded instruction).
Phase 3 (dropping the deque) is already in effect when `QL32_COMPACT_CODE` is on without
dual-run. The compiler still builds into a deque, and the other pio envs still use the old form.

Gates: ctest 92/92 in normal, compact-only, dual-run and sanitized builds; golden 16/16 on
the CYD; unit test for truncate-at-every-point plus append and sequential/reverse decode.

Speed (CYD, 20000-iteration loop with a function call): 14110 -> 14145 ticks (+0.25 %, budget 5 %).

Full-screen dirplayer on the CYD (free / largest block, KB):

| Point | Phase 1 (old VM) | Phase 2 | Gate |
|---|---:|---:|---:|
| RUN start | 96.0 / 47.1 | 122.4 / 43.0 | 125 / 60 |
| After canvas | 65.5 / 18.4 | 91.8 / 18.4 | |
| Playing (z00m) | 44.0 / 4.6 | 70.3 / 8.2 | |
| After exit | 138.9 / 55.3 | 138.9 / 61.4 | |

Free memory improves by about 26 KB; late=0, failedalloc=0 during playback. The free gate is
missed by 2.6 KB and the largest-block gate by 17 KB: the remaining blocker is contiguity
(pinned small allocations and the compile-time token vector), not total bytes.

## 11. Closing the largest-block gap: findings (no code merged)

Full-screen dirplayer on the CYD after Phase 2: RUN start 122.4K free / 43.0K largest (gate 125K / 60K).

- `HEAP FREEMAP` (new diagnostic, `QL32_HEAP_DIAGNOSTICS`) walks the free list at a STOP. The old 110K idle block is shattered into about 13 holes (9.7K, 7.7K, 5.1K, ...). They are separated by only about 12K of live pieces (gaps of 570-1730 B), so the largest block is the 43K tail, not 110K.
- The streaming lexer already exists (TokenWindow), so the token vector is not a retained cost.
- Tried, no gain (reverted): re-homing names/extras/pool into fresh allocations after encode (43.0K); steering placement into the largest block during `makeCompact` (43.0K, free -2.5K); realloc of the compact holder (+1.7K free, 47K largest).
- Conclusion: the live pieces are created during compile, interleaved with transients, so fixing at `makeCompact` is too late. Needed next: either a compile-time arena (monotonic allocator for transients, freed wholesale) or allocating the retained program structures (functions map, names, pool, VM state) from a reserved region before compile; plus a ProgramStore spill/segmentation (about 16K) for the free-heap gap of about 2.6K.

## 13. ProgramStore spill during RUN

`RUN` writes the editable program to `FFS:.q32-run` (atomic, streamed, FNV hash + size, verified by streaming read-back) right after compile, then frees the RAM copy. Any later edit/LIST/SAVE/RUN restores it (hash checked); NEW/LOAD discard it; if any step fails the RAM copy is kept. Enabled by `AdmissionPolicy::programSpillPath` (CYD only). (Superseded by section 14: per-session names, sidecar metadata, never deleted on destruction.) Spill/restore latency depends on the storage backend (FFS/SPIFFS here).

Full-screen dirplayer, CYD, free/largest KB (main at same time / with spill):

| Phase | main | spill |
|---|---:|---:|
| RUN start | 120.0 / 43.0 | 136.0 / 43.0 |
| after canvas | 89.4 / 16.4 | 105.4 / 18.4 |
| ready | 94.6 / 16.4 | 110.6 / 20.5 |
| playing | 68.0 / 4.1 | 83.9 / 18.4 |
| exit (after canvas hook) | 107.2 / 25.6 | 123.2 / 43.0 |

Free gate (>=125K at RUN start) met; largest gate (>=60K) not met: the largest block at RUN start is still 43K (pinned small allocations, see sections 11-12). The arena (section 12) gives ~49K at a cost of ~7K free, so spill + arena would reach ~129K / 49K, still short of 60K.

## 14. Review fixes, drum fallback and final full-screen state

Rubber-duck review of #53-#58 led to these changes:

- **Spill ownership.** Recovery files are `FFS:.q32-run-<session>[.n]` with a `.meta` sidecar (`Q32RECOVER1 session bytes fnv`). Existing files are never overwritten or deleted: a leftover that holds exactly the program being run is adopted, any other leftover makes the spill pick the next name (16 tries), otherwise the spill is refused and the RAM copy is kept. The interpreter destructor no longer deletes a spilled program (it may be the only copy); only a successful restore, NEW or LOAD of that session discards it. The recovery file is plain numbered BASIC, so `LOAD "FFS:.q32-run-N"` restores it by hand; the CYD prints `Recovery file kept: ...` on the serial console at boot. Restore streams the file in 256-byte reads into a fresh `ProgramStore` and only swaps after the FNV hash and size match (damaged file: error, file kept). Tests: orphan not overwritten, reset keeps the file, damaged file reported and kept (`scheduler_tests`).
- **UB in the compact encoder.** `double -> long long` was cast before the range check (UB for 1e300/inf/NaN). Now the range test (false for NaN) comes first. The sanitizer build adds `float-cast-overflow` (non-recoverable); it reproduced the bug on the old code and the existing literal round-trip test now passes under it.
- **Drum admission.** Free/contiguous admission (48 KiB / 4 KiB) picks the full (8192) or reduced (4096) drum budget per song; if the open still throws `bad_alloc` (peak or fragmentation exceeded the estimate) the failed attempt unwinds, and it is retried once with the reduced budget before the load is refused cleanly. The cache key includes the budget so full and reduced caches never mix; the player title gets `[no drums]`. Device evidence (serial log `music: YM drums ...`): normal memory `kept`; with ~60 KB of BASIC arrays held `dropped title=' [no drums]'` and playback continued; with ~85 KB held the load is refused with `music: not enough memory (heap fragmented)` and the player stays alive. Host test: reduced budget drops drums, flags the title, uses another cache entry and returns to the full one.

Device state on this build (full-screen dirplayer, FFS): RUN start 135.5K free / 40.9K largest (spill active), ready 97K / 21K, playing KNUCKLE with drums 71K / 3.6K, exit 152K free. KNUCKLE, LyraII1 and LyraII9: late=0, failedalloc=0, writeerr=0, min free 45.6K, maxgap ~11.7 ms. The 60K largest gate is **not** met (about 41-43K); only chase more fragmentation work if tied to a measured requirement.

### Cheap-optimization review (ranked)

Measured costs come from sections 9-13; "est." is an estimate.

| # | Candidate | Free gain | Largest gain | Effort | Verdict |
|---|---|---:|---:|---|---|
| 1 | Spill ProgramStore during RUN | +16K (done) | 0 | done | shipped |
| 2 | Drum budget 8192 with fallback | KNUCKLE audio | 0 | done | shipped |
| 3 | Pack names/extras/constant tables (ql32.inc retains 20.6K) | est. 5-8K | est. small | medium (touches decode) | worth doing next, needs speed gate |
| 4 | Position table 3 B -> 2 B per instruction | est. 1-2K | small | low | marginal, skip |
| 5 | shrink_to_fit on retained containers | est. 1-3K | may help largest | low | try after 3 |
| 6 | Music worker stack 6K / VFS setvbuf 4K / LH5 tables 2K reuse | est. 2-4K only while playing | none | medium | only if playback min-free becomes the constraint |
| 7 | Lazy canvas for programs that never print | 39K for text-only programs | large | already measured in section 9; benchmark is full screen | no gain for the benchmark |
| 8 | Compile-time transients | compile peak only | largest at compile | arena tried (section 12) | parked: +6K largest costs -7K free |
| 9 | Drum cache block size, task stacks, debug bar | < 1-2K each | none | low-medium | not paying for themselves |

Recommendation: do #3 then #5 as one measured step (host differential tests already cover the compact encoding); stop there unless a new requirement appears. Only #3 and #5 are not yet implemented, so no code change for them in this step.
