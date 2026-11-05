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
