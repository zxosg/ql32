# QL/32 Phase 3 – Compact VM Redesign (Implementation Plan)

Audience: implementing agents. Work starts from `codex/newvm` at `b2e0b3b`;
create the `pre-compact-vm` tag at that baseline before implementation. Line
references below were checked against the older `de88fe5` commit and may have
moved. Recheck symbols and locations against the current branch.

The goal is to reduce VM memory use on ESP32 (Olimex/LILYGO/CYD) while preserving
BASIC-visible behavior. The design draws on QL SuperBASIC ideas: compact code,
slot-based names, compact values, and per-session state. Work is split into three
tracks so graphics and network changes can proceed independently of the VM.

## 0. Ground rules

1. Preserve BASIC-visible behavior. Existing tests and the curated deterministic
   corpus are the compatibility oracle; add focused tests for every behavior
   changed by a step.
2. Keep each step reviewable and ordered within its track. Track B must follow
   the strict order in section 5; compact bytecode is last.
3. Use C++17 and add no third-party dependencies. Do not add exceptions or RTTI
   to hot paths.
4. Keep public APIs in `include/ql32/*` source compatible. Debugger and
   inspection APIs must continue to expose names for globals and locals.
5. Host builds are the development loop. Build and run CTest after each step;
   compile supported ESP32 profiles at the end of each step that affects their
   sources.
6. Measure the current checkout before setting device-specific targets. Record
   before/after measurements with the change that produced them.

## 1. Baseline and measurement

The earlier plan recorded these host x86-64 sizes; treat them as historical
figures until reproduced on `codex/newvm`:

| Type | Historical `sizeof` | Cost center |
|---|---:|---|
| `Value` = `variant<double,string>` | 40 B | Every variable and array element |
| `StackEntry` = `variant<double,string,ArrayReference>` | 40 B | Every VM stack entry |
| `detail::Instruction` | 240 B | One object per bytecode operation |
| `RuntimeFrame` | 176 B | Plus per-call local and alias maps |
| `RuntimeArray` | 48 B | Plus `vector<Value>` payload |
| `unordered_map<string,Value>` | 56 B empty | Plus node and name storage per variable |
| `map<uint64_t,Allocation>` | 48 B empty | Plus node per managed allocation |

Track A establishes reproducible host baselines for object sizes, allocated
bytecode including all side tables and debug metadata, scalar/array storage,
stack and frame high-water marks, and benchmark runtime. Keep benchmark results
separate from correctness tests. Measure device heap and largest allocatable
block on each board only after the current firmware and workload baseline has
been recorded; do not use historical board figures as acceptance targets.

## 2. Current design facts

- `src/language/bytecode.hpp` defines `Op`, a large `Instruction`, and
  `InstructionSequence : std::deque<Instruction>` with function, array, and
  constant maps.
- `src/language/interpreter.cpp` contains the lexer and compiler, including
  `compileSource` and `compileExpression(source, enclosing)` for dynamic
  evaluation.
- `src/vm/interpreter.cpp` resolves local names in the active frame and falls
  back to globals. Array parameters use name aliases. Debug inspection and
  INPUT also consume names.
- `src/vm/execution_state.hpp` contains `VMExecutionState`, `RuntimeFrame`,
  `RuntimeArray`, and per-loop state.
- Each scheduler task owns an `Interpreter`. Sessions can persist across REPL
  commands, so globals and their symbol table must live for the whole session.
- Relevant implementation symbols and line numbers in this section were
  originally documented from commit `de88fe5`; search the current checkout
  before editing.

## 3. Target architecture

### 3.1 Typed arrays (Track A)

Use numeric and string array payloads selected by the BASIC variable suffix at
DIM time: `NumArray { dims; vector<double> data; }` and
`StrArray { dims; vector<string-or-handle> data; }`. Numeric elements occupy
8 bytes; string storage follows the selected string representation. Keep the
name-keyed array table for this step so storage changes remain isolated.

### 3.2 Session and function symbol tables (Track B)

- Globals and the global symbol table live for the entire interpreter session,
  not for one compiled program. This preserves variables across interactive
  submissions. New globals append slots; do not retain pointers into a vector
  across a compile that may grow it.
- Each PROC/FN has a stable local symbol table for its parameters and declared
  `LOCAL` names. Frames refer to local slots in contiguous per-session storage
  rather than owning name maps.
- Compiler slot selection must preserve current behavior: a name is local only
  when declared as a parameter or `LOCAL` in that function; otherwise it is a
  global. Add tests for shadowing, recursion, and globals sharing names with
  locals.
- Dynamic expression compilation and debugger EVAL resolve through the active
  function's local symbol table and the session's global table. Inspection
  maps slot IDs back to names. Unknown-name behavior must match the existing
  command being compiled; do not silently create globals in read-only debugger
  EVAL.
- Represent array parameters as array-slot references instead of name aliases.
  INPUT targets and other by-name VM consumers resolve to stable slot IDs.

### 3.3 String ownership and value representation (Track B)

Before implementation, document ownership and lifetime rules for every place a
string can live: globals, locals, operand stack, arrays, constants, function
arguments/results, and service boundaries. Specify retain/release behavior for
copy, assignment, overwrite, frame exit, array replacement, and error unwind.
Add debug assertions and tests for those transitions.

Then measure alternatives. The preferred compact representation is numeric
values stored as `double`, strings stored through per-session `StrId` handles,
and tagged stack entries. A refcounted pool with a free list is one option; keep
`std::string` if measured savings do not justify the ownership complexity. Keep
the public `Value` variant at API boundaries where required.

### 3.4 Compact bytecode (last step in Track B)

After symbol resolution, string ownership, and allocator behavior are stable,
replace `std::deque<Instruction>` with compact instructions and indexed side
tables. A candidate 16-byte record is:

```cpp
struct Insn {
  uint8_t op;
  uint8_t flags;
  uint16_t a16;
  uint32_t a32;
  uint32_t b32;
  uint32_t c32;
};
```

Move constants, FOR/SELECT specifications, parameter lists, jump scopes,
call-site data, and source positions into per-program side tables. Measure the
full encoded program footprint, including side tables and debug metadata; do
not report only `sizeof(Insn) * instruction_count`. Preserve a chunked storage
option if a flat buffer causes unacceptable contiguous-allocation failures.
Consider a variable-length byte stream only if fixed records and side tables
do not meet the measured host target.

### 3.5 Allocation policy (Track A)

- Add platform-neutral `MemoryBudget` and `PlatformLimits` around the existing
  `runtime::MemoryService`. Memory reports are estimates, not reservations or
  guarantees that a subsequent allocation will succeed.
- Set explicit limits for arrays, buffers/transfers, and strings. Start with
  current host behavior and derive embedded limits from measurements for each
  board rather than adopting unverified example values.
- Preflight DIM, ALLOC, transfers, canvas create/resize, large string
  operations, and compile-time allocations where a reasonable estimate exists.
- **Allocation safety contract:** allocate replacement or additional storage
  before changing VM-visible state. If allocation fails, leave the old state
  valid. Catch `std::bad_alloc` at the instruction boundary and translate it
  into a catchable BASIC out-of-memory error. Do not promise that preflight
  prevents all allocation failures.
- Add failure-injection tests, including an allocation failure partway through
  a multi-allocation operation. Verify the operation reports an error and the
  interpreter remains usable with prior state intact. Keep a documented memory
  reserve where the platform can enforce one.

### 3.6 Allocators and task lifetime (Track B)

After string ownership is specified and tested, measure whether per-session
size-class pools reduce fragmentation and overhead. Use stable handles if later
compaction is needed. Keep allocator work separate from compact instruction
encoding. Consider replacing `BufferManager`'s `std::map` only if measurements
show a useful gain. Define task result/history retention separately from VM
state destruction so debugger and task-management behavior remains compatible.

### 3.7 Graphics and network footprint (Track C)

- Measure default canvas depth and retained memory. Consider choosing a default
  that matches each board's palette only if the BASIC-visible color behavior
  remains compatible.
- Keep row-streaming presenter behavior covered by regression tests; verify
  embedded paths do not allocate a full-frame pixel buffer.
- Measure static and dynamic network footprint with linker maps and board
  builds. Make network removal/configuration a separate change from VM work.
- Treat direct-draw mode and other graphics redesigns as optional, separately
  measured work.

## 4. Main risks and mitigations

| Risk | Mitigation |
|---|---|
| Local/global scoping changes | Pin lookup behavior with conformance tests before changing resolution. |
| REPL persistence | Test globals across multiple submissions and program compilations in one session. |
| Dynamic compile and debugger EVAL | Test active-frame local lookup, global lookup, and read-only EVAL behavior. |
| Array-by-reference parameters | Test recursion, aliasing, and caller-visible writes. |
| String lifetime errors | Document ownership first; assert retain/release and test error unwind and replacement. |
| Partial allocation failure | Allocate before state mutation; inject failures midway and verify recovery. |
| Compact-code regressions | Defer bytecode rewrite until preceding Track B steps pass; measure full encoded size. |
| SINGLE precision differences | Preserve rounding at the same store and arithmetic points; retain conformance tests. |

## 5. Work tracks and ordered steps

### Track A – Safety net, memory limits, typed arrays

**A0 – Deterministic safety net.** Select a curated, deterministic subset of
CLI programs and examples; capture expected output and errors and add a CTest
target. Exclude hardware-dependent, interactive, and nondeterministic cases
unless they have stable fakes. Add an ASan/UBSan build configuration where
supported. Add benchmarks for loops, recursion, array fill, string operations,
and Mandelbrot; report them separately from tests.

**A1 – Measurement.** Add a size probe and diagnostics for current bytecode
bytes, scalar/array storage, and stack/frame high-water marks. Record host
baselines and benchmark methodology in `docs/plans/phase3-metrics.md`.

**A2 – MemoryBudget, failure handling, and PlatformLimits.** Implement the
allocation safety contract in section 3.5 without changing VM data structures.
Test low reported memory and injected allocation failures, including failure
mid-operation. Add platform limits only after documenting their source.

**A3 – Typed arrays.** Replace `RuntimeArray::values` with typed numeric and
string storage while keeping name-based array lookup. Test initialization,
assignment, bounds, precision, and string lifetime behavior.

### Track B – Core redesign (strict order)

**B1 – Symbol resolution.** Introduce session-lifetime globals and symbol
metadata, per-function local symbol tables, and slot-based variable/array
references. Update interactive compilation, INPUT, FOR variables, debugger
inspection, and EVAL. Preserve behavior with focused tests.

**B2 – String ownership and value model.** Write and review the ownership rules
before coding. Measure `std::string` storage against the proposed handle pool;
retain strings if the pool does not provide a worthwhile measured gain. Convert
the operand stack and stored values only after ownership tests pass.

**B3 – Pooled allocators.** Measure and implement per-session allocation pools
only where they improve fragmentation or overhead. Keep failure behavior and
handle invalidation covered. Revisit BufferManager and task-history retention
as separately measurable subchanges.

**B4 – Compact bytecode.** Replace instruction objects with compact records,
side tables, and source-location metadata. Measure the total encoded program
size and execution performance. Keep this last because it touches the compiler,
bytecode verification, dispatch, debugger stepping, and dynamic code paths.

### Track C – Graphics and network footprint

Proceed independently from Tracks A and B. Establish per-board baselines first,
then handle canvas depth/storage, presenter regressions, linker-map analysis,
and any network configuration changes in separate reviewable steps.

## 6. Acceptance criteria

- Host CTest, curated golden corpus, and supported sanitizers pass for each
  relevant step.
- Benchmarks are reported separately, with hardware/compiler/configuration
  recorded. Investigate regressions rather than enforcing an unvalidated
  universal 10% threshold.
- Hard size targets apply only to reproducible host measurements: numeric array
  payload is 8 bytes per element; compact instruction records are at most
  16 bytes; and the encoded program target includes all side tables and debug
  metadata. Confirm the proposed 2,000-operation program budget after A1
  establishes a current baseline.
- Compile the supported ESP32 environments affected by a step. Device heap and
  largest-block goals are set per board only after measuring the current
  checkout under a documented workload. The historical 30 KB improvement is
  not a release criterion.
- The deterministic corpus has zero behavior differences except for changes
  explicitly approved and documented with conformance tests.

## 7. Follow-on work

1. Keep large read-only keyword and error tables in flash where the toolchain
   permits it.
2. Consider savable tokenized programs and a streaming lexer only after the
   current compile-time memory profile is measured; these are separate designs.
3. Add CI builds and size checks for the supported ESP32 profiles after the
   host acceptance criteria are stable.
4. Keep FabGL licensing decisions outside this VM redesign.

## 8. Branch and review workflow

- Start on `codex/newvm` at baseline commit `b2e0b3b`; create tag
  `pre-compact-vm` pointing at that baseline before implementation.
- Keep Track A, Track B, and Track C changes independently reviewable. Within
  Track B, do not reorder B1–B4.
- Each change records its tests, host measurements, benchmark results, and any
  board-specific build or hardware results. Do not claim device-level savings
  without a measured before/after comparison on the same board and workload.

## 9. Expected files

Likely files include `src/language/bytecode.hpp`,
`src/language/interpreter.cpp`, `src/vm/interpreter.cpp`,
`src/vm/execution_state.hpp`, `src/vm/scheduler.cpp`,
`include/ql32/vm/scheduler.hpp`, `include/ql32/runtime/memory.hpp` (and a new
`memory_budget.hpp`), `include/ql32/runtime/buffer.hpp`,
`src/runtime/buffer.cpp`, `include/ql32/interpreter.hpp`, platform adapters,
`CMakeLists.txt`, `platformio.ini`, and tests. Reconfirm this list against the
current branch before implementation.
