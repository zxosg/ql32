# Phase 3 implementation log

## 2026-11-05 — Baseline / before A0

- **Step:** baseline before Track A0.
- **Starting point:** tag `pre-compact-vm` points to `b2e0b3b`. Work branch is
  `codex/compact-vm`, created from that tag. The updated plan commit was carried
  onto the branch as `d67a39a`.
- **User work:** the original `codex/newvm` checkout had uncommitted user edits.
  They remain in place and untouched. Implementation is isolated in the
  `/private/tmp/ql32-compact-vm` worktree.
- **Plan path deviation:** the implementation instructions referenced
  `docs/plans/PHASE3-compact-vm.md`; the actual committed plan is
  `docs/plans/plans_PHASE3-compact-vm.md`. That actual plan is the spec used.
- **Host baseline:** macOS, AppleClang 21. Debug configure/build succeeded;
  CTest passed 60/60. ASan+UBSan configure/build succeeded; CTest passed 60/60.
  Both builds emitted one existing warning in `src/platform/esp32/drive_router.cpp`
  about a captured structured binding being a C++20 extension under C++17.
- **ESP32 baseline:** `pio run` succeeded for `lilygo_vga32`,
  `olimex_esp32_sbc_fabgl`, and `esp32_2432s028`. Flash/static RAM figures are in
  `phase3-metrics.md`.
- **Hardware:** `pio device list` found `/dev/cu.usbserial-1110` (VID:PID
  `1A86:7523`), but did not identify its chip or board. No hardware test or flash
  was attempted; hardware gates are pending identification.
- **Decisions:** keep all existing user modifications out of this worktree;
  use the current tracked plan despite the path mismatch; establish this clean
  baseline before adding the curated corpus and sanitizer preset.
- **Open risks:** no board-specific live heap measurements yet; connected USB
  serial device is unidentified.

## 2026-11-05 — A0 complete

- **Step:** Track A0, deterministic safety net.
- **Commit:** `7891823 test: establish compact VM safety net`.
- **Changes:** added the `ql32_golden_corpus` CTest target over 11 stable CLI
  fixtures; added a CMake sanitizer option and `sanitized` preset; added a
  separate benchmark runner and BASIC workloads for loops, recursion, array
  fill, string concatenation, and Mandelbrot iteration.
- **Tests:** Debug CTest passed 61/61, including the curated corpus. The
  ASan+UBSan preset build and CTest passed 61/61. Corpus output and errors
  matched the checked-in CLI expectations.
- **Benchmarks:** three-run median baseline is recorded in `phase3-metrics.md`.
  Benchmarks are informational and not part of CTest.
- **ESP32:** no embedded source changed in A0. Baseline builds for all three
  supported environments passed; their size figures are recorded in
  `phase3-metrics.md`.
- **Hardware:** pending board identification; no flash attempted.
- **Deviation:** the existing rendered Mandelbrot example takes the graphics
  path and is not a stable VM-only timing workload. The benchmark uses the same
  numeric iteration algorithm without drawing, to keep VM timings independent
  of the presenter.
- **Open risks:** device runtime heap measurements remain pending.

## 2026-11-05 — A1 complete

- **Step:** Track A1, measurement.
- **Commit:** `826c7ea perf: add compact VM host memory measurements`.
- **Changes:** added `ql32_sizecheck`; added estimates for compiled bytecode,
  scalar globals/locals, arrays, and diagnostic-only operand-stack/call-frame
  high-water marks. Exposed these values through debugger `STATS` and the
  existing remote-debug stats object. High-water updates only run when
  diagnostics are enabled.
- **Host measurements:** recorded in `phase3-metrics.md`. The 2,000-instruction
  synthetic program has 448,000 B of instruction records and a 498,176 B
  estimated sequence footprint.
- **Tests:** Debug and ASan+UBSan CTest passed 61/61, including new assertions
  for storage estimates, high-water marks, and `STATS` output. `ql32_sizecheck`
  completed its compile-time and live-runtime probes.
- **Benchmarks:** post-A1 medians are recorded in `phase3-metrics.md`; compared
  with A0, all changes were below 7%. No regression crossed the 10%
  investigate threshold.
- **ESP32:** all three environments compiled. Updated flash/static RAM figures
  are in `phase3-metrics.md`.
- **Hardware:** pending board identification; no flash attempted.
- **Estimate limits:** string capacity accounting includes inline-capacity
  strings; allocator bookkeeping, deque block slack, and other allocator
  metadata are excluded. Treat sequence bytes as a repeatable host estimate,
  not an exact allocator measurement or board heap claim.
- **Open risks:** runtime free heap and largest-block measurements on physical
  boards remain pending.

## 2026-11-05 — A2 complete

- **Step:** Track A2, memory budget, allocation failure handling, and platform
  limits.
- **Commit:** pending commit of this step.
- **Changes:** added platform-neutral `MemoryBudget` and `PlatformLimits`.
  Defaults preserve measured/current language behavior: arrays remain capped
  at 1,000,000 elements; buffers and transfers remain capped at 16 MiB; strings
  have no new size cap. The ESP32 adapter reports no unmeasured reserve, so the
  default reserve is zero. MemoryService reports are advisory and do not
  guarantee a later allocation.
- **Safety:** preflights compilation source estimates, DIM dimensions and
  payload, ALLOC, both XFER scratch buffers, I/O scratch buffers, WINDOW resize
  estimates, string concatenation and slicing. Array replacement is staged
  through complete dimension and payload construction before table update.
  `std::bad_alloc` at the VM instruction boundary and during source/debugger
  compilation is converted to `runtime::Error(OutOfMemory)`.
- **Tests:** added low-reported-memory and deterministic mid-DIM allocation
  failure coverage. Both failures return BASIC Out-of-memory and the same
  interpreter remains able to run another command with its prior scalar state.
  Arrays currently belong to one execution, so the test cannot inspect a prior
  array after the failed execution is discarded; table replacement is staged
  before mutation and will receive direct transition coverage in A3.
  Debug CTest passed 61/61; ASan+UBSan CTest passed 61/61.
- **ESP32:** all supported PlatformIO profiles compiled successfully. Updated
  firmware sizes are recorded in `phase3-metrics.md`; static RAM stayed at its
  prior reported values. No hardware flash or runtime heap measurement was
  performed because the attached serial device remains unidentified.
- **Decision:** do not set a nonzero embedded reserve or stricter embedded
  allocation limits until board/workload runtime measurements justify them.
- **Open risks:** estimates may undercount allocator metadata and concurrent
  platform activity. Preflight does not prevent `bad_alloc`; the instruction
  boundary translation remains necessary. Canvas sizing uses a conservative
  four-byte-per-pixel estimate, which is not a claim about each backend's
  physical representation.

## 2026-11-05 — A3 complete

- **Step:** Track A3, typed arrays.
- **Commit:** pending commit of Track A2/A3 work.
- **Changes:** `RuntimeArray` now stores numeric elements in
  `std::vector<double>` and string elements in `std::vector<std::string>`,
  selected by the BASIC `$` suffix at DIM time. Name-based array lookup,
  array aliases, bounds checks, debugger inspection, and public `Value`
  boundaries remain in place. Array estimates now account for actual typed
  payload capacity and string capacities.
- **Tests:** curated corpus and Debug CTest passed 61/61; ASan+UBSan CTest
  passed 61/61. Added typed array initialization, assignment, Int32 edge
  precision, string concatenation/lifetime coverage; existing tests cover
  bounds, rank errors, and array parameters. The language rejects re-DIM of an
  existing array, so replacement semantics were not added or tested as a
  user-visible feature.
- **Host measurement:** `sizeof(RuntimeArray)` is now 56 B. The A1 size probe's
  runtime numeric array estimate fell from 3,359 B to 967 B (−71.2%), with the
  same 100-element workload. Numeric payload bytes are 8 per element.
- **Benchmarks:** final three-run medians are in `phase3-metrics.md`. All five
  workloads are within 5% of the A0 baseline; string concatenation is −2.6%.
  An initial per-operation free-memory query caused a measured concat slowdown;
  checks are now limited to strings at least 4 KiB, which matches the plan's
  requirement to preflight large operations and restored baseline timing.
- **ESP32:** all three supported PlatformIO environments compiled. Updated
  firmware sizes are recorded in `phase3-metrics.md`; static RAM is unchanged.
- **Open risks:** typed string arrays still own each element as
  `std::string`; Track B2 decides whether a measured handle pool is worthwhile.

## 2026-11-05 — B1 complete

- **Step:** Track B1, session and function symbol resolution.
- **Commit:** pending commit of this step.
- **Changes:** replaced the session's name-to-`Value` map with a stable global
  name-to-slot table and parallel value storage. Each function has a stable
  session symbol list for parameters and `LOCAL` names; active frames store
  values in a contiguous vector. Compiler binding assigns global/local slots
  to variable instructions. Array tables and array-parameter aliases now use
  stable symbol slots; array references on the operand stack carry a slot and
  scope bit. Debugger inspection maps slots back to names.
- **Dynamic paths:** EVAL bytecode binds names against the active frame and
  session globals. Debugger EVAL continues to resolve the active local and
  global symbols without creating missing globals.
- **Tests:** added session recompilation, local shadowing, dynamic EVAL in a
  function, and debugger EVAL of an active local. Existing recursion, array
  aliasing, INPUT, FOR, and golden tests passed. Debug CTest passed 61/61;
  ASan+UBSan CTest passed 61/61.
- **Host measurement:** `sizeof(Instruction)` increased from 224 B to 232 B
  for two 32-bit slot fields; `sizeof(RuntimeFrame)` fell from 152 B to 136 B.
  The 2,000-instruction sequence estimate increased from 498,176 B to
  514,176 B (+3.2%) because symbol/local metadata is now explicit. The runtime
  probe's array estimate is 951 B. The one-sample global metadata estimate
  increased from 135 B to 478 B because it now includes symbols referenced by
  the compiled functions, including uninitialized names. This is a small
  sample overhead to track while the slot/value representation evolves.
- **Benchmarks:** post-B1 three-run medians and comparisons with A3 are in
  `phase3-metrics.md`; the array-fill workload improved 22.6% and no workload
  regressed.
- **ESP32:** all three supported PlatformIO environments compiled. Updated
  firmware and flash deltas are recorded in `phase3-metrics.md`; static RAM
  remained unchanged. No hardware was flashed.
- **Open risks:** the host sample has few globals and cannot establish the
  scaling break-even point for symbol table metadata. String ownership,
  allocator changes, and compact bytecode remain ordered in B2-B4.

## 2026-11-05 — B2 complete (retain `std::string`)

- **Step:** Track B2, string ownership and value representation.
- **Commit:** pending commit of the ownership review and representation probe.
- **Ownership review:** wrote `phase3-string-ownership.md` before making any
  representation changes. It records owners/lifetimes for globals, locals,
  stack entries, arrays, literals/constants, calls, debugger values, and
  service boundaries, plus the retain/release obligations a handle pool would
  introduce. Current RAII copy/move/destruction paths match that contract.
- **Measurement:** extended `ql32_sizecheck` with structural host models for
  tagged 16-byte values and a refcounted, deduplicating string pool. For 1,000
  shared empty strings, an optimistic handle model is 4,096 B versus 24,000 B
  of current `std::string` array objects. For 1,000 unique short strings, the
  same pool model is 100,000 B versus 24,000 B, before heap metadata. The
  result is highly dependent on sharing and excludes payload allocations and
  allocator details; it is not a measured heap census.
- **Decision:** keep `std::string` for globals, locals, stack, and string
  arrays. The current curated corpus and benchmarks do not establish that
  duplicate immutable strings dominate, while a plausible unique-string array
  workload regresses materially in the pool model. Ownership assertions and
  explicit retain/release paths would add risk without a demonstrated
  whole-workload benefit. Revisit if a representative device corpus shows
  sustained string sharing.
- **Tests:** no runtime representation changed. Existing A3 string array
  initialization/assignment and B1 function argument/result, session,
  dynamic-EVAL, debugger-EVAL, error-unwind, and service regression coverage
  remains green. Debug and ASan+UBSan CTest each passed 61/61 during B1.
- **Open risks:** no target-board heap census of string-heavy workloads exists;
  the probe is a structural estimate and cannot establish embedded allocator
  behavior.

## 2026-11-05 — B3 complete (compact BufferManager index)

- **Step:** Track B3, allocator measurements and per-session allocation
  overhead. No general size-class pool was added: this repository has no
  evidence that pooled payload allocation would improve fragmentation, while
  B2 showed that pooling can regress unique small values.
- **Measurement:** extended `ql32_sizecheck` with an explicitly structural
  comparison for 1,000 managed buffers. On this host, a map value is 40 B;
  estimating four pointer widths for tree links gives 72,000 B, compared with
  32,768 B for 1,000 32-byte records in vector capacity 1,024. This excludes
  payload byte vectors, allocator rounding, and STL-specific node layout.
- **Change:** replaced `BufferManager`'s map with a sorted vector. Allocation
  bases increase monotonically, so insertion appends in normal use and
  `lower_bound`/`upper_bound` retain logarithmic lookup. This removes a key/base
  duplicate and per-node tree overhead. `clear()` releases vector capacity as
  the former map clear released its nodes; allocation addresses are not reset.
- **Tests:** added direct coverage for neighboring buffers, exact base sizes,
  access after freeing an adjacent allocation, invalid interior DEALLOC, and
  clear invalidation. Debug and ASan+UBSan CTest each passed 61/61. All three
  supported PlatformIO profiles compiled; firmware and static RAM are recorded
  in `phase3-metrics.md`. No hardware was flashed.
- **Tradeoff:** DEALLOC shifts later metadata records in O(n), while allocate
  appends and address lookup remains O(log n). Payload buffers remain separate
  vectors. General allocation pools and task-history retention remain
  unchanged because neither has measured evidence for a beneficial redesign.

## 2026-11-05 — B4 compact instruction records

- **Step:** Track B4. The compiler uses rich instructions as a transient builder;
  before execution, the VM lowers them into 16-byte records and indexed side
  tables. This keeps the compiler's patching and verification logic intact while
  runtime dispatch reads the compact representation.
- **Encoding:** records store the opcode, statement flags, physical source line
  and column when each fits 16 bits, BASIC line number, and either a numeric
  literal or side-table index. String literals use a separate string table.
  Instructions with names, symbol slots, control-flow targets, parameters, or
  other complex metadata use indexed rich sidecars. Source coordinates wider
  than the record fields stay in a sidecar and are never truncated.
- **Runtime paths:** symbol binding runs against sidecars after lowering.
  Dynamic EVAL compiles to the builder form, stages and appends encoded records
  and side tables atomically, then removes the temporary tail on return. The
  debugger and verifier continue to read decoded instructions with full source
  metadata.
- **Tests:** added record/sidecar round-trip checks, dynamic append/erase checks,
  and a wide-source-column fallback check. The curated golden corpus and all
  Debug and ASan+UBSan tests passed 61/61. All three supported ESP32 profiles
  built. No board was flashed; Linux was not available on the Darwin build host.
- **Host size:** the synthetic 2,000-operation `PRINT 1` program estimates
  514,176 B in the rich representation and 32,208 B encoded, including tracked
  side tables and capacity slack (−93.7%). A larger mixed Mandelbrot program
  estimates 20,742 B rich and 17,292 B encoded (−16.6%), showing that sidecar
  heavy programs gain less. Estimates include source/debug and function tables,
  but exclude allocator headers/rounding and implementation-specific deque
  slack in the transient compiler builder.
- **Benchmarks:** five-run post-B4 medians are recorded in `phase3-metrics.md`.
-  Against the earlier three-run B1 medians, changes range from −1.9% to +8.2%;
  the run counts differ, so treat these as indicative rather than a controlled
  comparison.
- **Embedded image sizes:** all profiles retain the prior static RAM totals;
  flash image changes from B1 range from −88 B to +24 B and are recorded in
  `phase3-metrics.md`.
- **Open risks:** dynamic code and sidecar-heavy instructions still hold rich
  `Instruction` objects. Flat compact-record allocation requests one contiguous
  record buffer; its largest-block impact has not been measured on hardware.
  Linux host validation remains outstanding because this Darwin host has no
  Docker, Podman, or Lima runtime.

## 2026-11-05 — B4 test-plan execution

- **Reference/candidate:** REF is the exact B3 parent `58bf799`
  (`pre-b4` tag created at that commit); CAND is B4 commit `cd5d6b5`.
  REF was archived into `/private/tmp/ql32-b4-test-ref` and built there so the
  candidate worktree stayed untouched.
- **T1 / AC1:** both REF and CAND rebuilt and passed all 61 CTest cases in
  Debug and ASan+UBSan configurations (61/61 in each run; no skips).
- **T2 / AC2:** the 11 curated corpus cases were run directly on both binaries.
  Stdout, stderr, and exit codes matched byte-for-byte in all 11 cases. The
  expected files were not changed. The plan's separate `tests/golden/` directory
  is not present; this used the existing `ql32_golden_corpus` fixtures.
- **T3 / AC3:** incomplete. `tests/language/` contains no B4 cases and there is
  no opcode-coverage guard, so the required every-op differential matrix was
  not run.
- **T4 / AC4:** partial. Existing runtime tests cover 16-byte records,
  round-trip of a representative instruction stream, dynamic append/erase, and
  wide source-column fallback. The VM's `verifyBytecode()` does run after
  lowering, but operand/index boundary cases, full semantic field comparison,
  and checked-narrowing tests are absent; acceptance is not established.
- **T5 / AC5:** existing debugger tests pass on both binaries, but the required
  40-case error differential, complete line-metadata comparison, and scripted
  debugger session differential were not present and remain unverified.
- **T6 / AC6:** partial. Existing allocation-failure tests exercise DIM payload
  failure and interpreter recovery. The compile-allocation sweep, large-load
  failure sweep, repeated EVAL lifecycle, concurrent compile, and task/code
  allocation accounting were not run.
- **T7 / AC7:** not run; no grammar generator or mutation fuzzer is present.
- **T8 / AC8:** five-run benchmark comparisons and additional workload results
  are recorded in `phase3-metrics.md`. The existing suite and 1M-loop / 100k
  procedure-call additions stay within the 110% runtime target. The 2,000-line
  `ql32_sizecheck` compile/lowering proxy is 126.5% of REF, above the 120%
  target but below 150%; it needs a pure compile/lower timer because sizecheck
  includes diagnostic accounting. The 2,000-line execution workload is 155.0%
  of REF and **fails T8's >150% stop threshold**. Read-only macOS sampling of a
  longer straight-line workload shows repeated `Instruction` materialization
  in `verifyBytecode()` and pre-execution scans through
  `CompactProgram::const_iterator::operator*()`; the VM dispatch loop itself
  uses the reusable `instructionAt()` scratch path. This points first to
  by-value iterator scans, but a fix waits for REF-first coverage tests. At
  recursion depth 1,000 both builds produce the identical `call stack limit
  exceeded` error; retain this as an expected compatibility case and time the
  deepest successful baseline recursion separately.
- **T9 / AC9:** the 10-line and 2,000-line synthetic thresholds pass, but AC9
  is **blocked** by the real attractor sample growing after encoding. The
  structural estimate is 33,581 B versus 29,652 B for REF; a host allocation
  tracker measured 40,068 B retained versus 28,364 B. The sidecar vector has
  67 elements and capacity 128; a temporary `shrink_to_fit()` experiment
  reduces the encoded estimate to 19,429 B. This implicates geometric
  sidecar-capacity waste, which must be corrected and remeasured before AC9 is
  accepted. The tracker is a host probe, not an ESP32 allocator measurement.
- **Compile peak:** a read-only global-`operator new` allocation tracker on the
  2,000-line source measured an identical 2,666,800 B peak for REF and CAND
  during rich compilation. CAND then retains 140,112 B after lowering, but
  lowering does not reduce the already-observed compile peak. Compact records
  must be emitted during compilation, after the REF-first tests exist, to
  reduce peak heap. On the attractor sample the lowering step itself peaks at
  54,948 B versus the 51,404 B rich-compile peak.
- **T10 / AC10:** `pio run` succeeded for all three profiles that built before
  B4: `lilygo_vga32`, `olimex_esp32_sbc_fabgl`, and `esp32_2432s028`. Static
  RAM is unchanged from B3; flash images are each smaller than B3. No new
  VM/compiler warnings appeared in these builds.
- **T11 / AC11:** pending. The CYD candidate firmware boots to `BASIC REPL
  ready`, but reports `Storage unavailable`; the plan's `tools/hw_soak.py` is
  absent, and the existing CYD memory harness requires storage fixtures. No
  B3 image was flashed for comparison and no soak result is claimed.
- **Commands:** `cmake --build build --parallel 4`; `ctest --test-dir build
  --output-on-failure`; corresponding commands for `build-asan`, REF
  `build-ref`, and REF `build-ref-asan`; `python3 tools/run-vm-benchmarks.py
  <binary> --runs 5`; `ql32_sizecheck <sample.bas>`; and the three `pio run -e
  <profile>` commands. Temporary benchmark sources were created only under
  `/private/tmp`; random/fuzz seeds were not applicable because T7 was not run.
- **Overall:** AC1, AC2, and AC10 pass. AC8 fails its >150% stop threshold on
  the 2,000-line execution case; AC9 is blocked by the attractor growth. AC3,
  AC4, AC5, AC6, and AC7 are incomplete; AC11 is pending. B4 is not ready for
  mainline merge under the stated criteria.

### Follow-up after REF-first coverage — 2026-11-05

- Added an explicit 50-opcode coverage test across 17 deterministic fixtures,
  compact instruction field round trips, an INPUT compile fixture, and a
  call-depth boundary fixture. The boundary accepts depth 255 and reports the
  same `call stack limit exceeded` error at depth 256 on REF and CAND. These
  new tests were first run against REF `58bf799` unchanged.
- Added `tools/hw_soak.py` in host-only mode. Its 32-fixture REF/CAND
  comparison and 200 candidate process load/run/free cycles passed. Host
  processes do not validate embedded heap behavior or satisfy T11.
- Reserved exact sidecar and literal counts before compact lowering. The
  attractor estimate is now 19,429 B versus 29,652 B REF (65.5%); the mixed
  Mandelbrot estimate is 11,260 B versus 20,742 B REF (54.3%). AC9's earlier
  growth blocker is cleared by this host estimate.
- Changed bytecode verification and program setup scans to decode into
  reusable scratch instructions rather than materializing rich instructions
  through the by-value iterator. The repeated 2,000-line PRINT run is now
  123.3% of REF (9 runs, first warm-up discarded), below the >150% T8 stop
  threshold but above the 110% target. The previous 155.0% result remains the
  pre-fix measurement.
- Full CAND CTest passes 65/65 after these changes. The compile peak remains
  unresolved: the read-only host allocation probe measured 2,666,800 B on
  both REF and CAND before lowering. Compact records are still built after the
  rich compiler sequence, so direct compact emission remains required to
  improve peak compile heap.
- B4 remains open. Error/allocation sweeps, fuzz coverage, direct compact
  compiler emission, and the device soak still need completion. Hardware
  acceptance requires G2; do not merge B4 to mainline before then.
