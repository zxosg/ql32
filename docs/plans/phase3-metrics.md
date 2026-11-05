# Phase 3 memory and performance metrics

## Baseline before Track A0 — 2026-11-05

Checkout: tag `pre-compact-vm` at `b2e0b3b`, plus plan-only commit `d67a39a`.
PlatformIO Core 6.2.0, Espressif32 6.12.0, Arduino-ESP32 2.0.17, Xtensa
toolchain 8.4.0+2021r2-patch5. Figures are firmware image size and static RAM
reported by PlatformIO's `pio run` summary; they are not free heap or runtime
largest-block measurements.

| Environment | Static RAM | Flash image |
|---|---:|---:|
| `lilygo_vga32` | 53,616 B | 1,541,833 B |
| `olimex_esp32_sbc_fabgl` | 54,252 B | 1,616,353 B |
| `esp32_2432s028` | 51,252 B | 1,481,489 B |

Host baseline: Debug and ASan+UBSan CTest each passed 60/60 on macOS with
AppleClang 21. Host object sizes and VM high-water measurements will be added
in Track A1. No attached board was identified, so runtime heap figures are
pending.

## Track A0 benchmark baseline — 2026-11-05

macOS, AppleClang 21, Debug build. Three process runs per workload; report the
median wall time. Informational only. Mandelbrot measures the numeric iteration
kernel without rendering, so the result does not include graphics/presenter cost.

| Workload | Median wall time |
|---|---:|
| 10,000 loop iterations | 0.024317 s |
| Recursive function depth 200 | 0.004821 s |
| Fill 10,000-element numeric array | 0.021055 s |
| 1,000 string concatenations | 0.006180 s |
| 40×28 Mandelbrot, 24 iterations/pixel | 0.238021 s |

After adding the A0 target, Debug and ASan+UBSan CTest each passed 61/61,
including the curated 11-case golden corpus.

## Track A1 host measurements — 2026-11-05

Host: macOS, AppleClang 21, Debug. `ql32_sizecheck` compiled a synthetic
2,000-instruction program and paused a sample program inside a function after
creating a global, a local, and a 100-element numeric array.

| Type / metric | Measurement |
|---|---:|
| `sizeof(Value)` | 32 B |
| `sizeof(StackEntry)` | 32 B |
| `sizeof(Instruction)` | 224 B |
| `sizeof(RuntimeFrame)` | 152 B |
| `sizeof(RuntimeArray)` | 48 B |
| `sizeof(unordered_map<string, Value>)` | 40 B |
| 2,000 instruction records | 448,000 B |
| Estimated bytecode sequence storage | 498,176 B |
| Runtime sample bytecode estimate | 6,289 B |
| Runtime global storage estimate | 135 B |
| Runtime local storage estimate | 214 B |
| Runtime array storage estimate | 3,359 B |
| Operand stack high-water | 2 entries |
| Call frame high-water | 1 frame |

The bytecode estimate includes container objects, element storage, side tables,
and string capacities. It counts inline string capacity and omits allocator
bookkeeping and `std::deque` block slack, so use it as a repeatable estimate,
not an exact allocator measurement.

### After A1 firmware sizes

| Environment | Static RAM | Flash image | Flash delta from baseline |
|---|---:|---:|---:|
| `lilygo_vga32` | 53,616 B | 1,545,065 B | +3,232 B |
| `olimex_esp32_sbc_fabgl` | 54,252 B | 1,619,613 B | +3,260 B |
| `esp32_2432s028` | 51,252 B | 1,484,709 B | +3,220 B |

### After A2 firmware sizes

| Environment | Static RAM | Flash image | Flash delta from A1 |
|---|---:|---:|---:|
| `lilygo_vga32` | 53,616 B | 1,548,149 B | +3,084 B |
| `olimex_esp32_sbc_fabgl` | 54,252 B | 1,622,737 B | +3,124 B |
| `esp32_2432s028` | 51,252 B | 1,487,805 B | +3,096 B |

The A2 `PlatformLimits` defaults retain the existing 1,000,000-element array
and 16 MiB buffer/transfer ceilings; strings remain uncapped at the language
level. No board-specific reserve or tighter ceiling is enabled because live
board heap measurements have not been obtained. Free-memory checks are
estimates and do not reserve memory.

### Track A3 host typed-array measurements — 2026-11-05

Same macOS AppleClang 21 Debug host and `ql32_sizecheck` workload as A1.

| Type / metric | A1 | After A3 |
|---|---:|---:|
| `sizeof(RuntimeArray)` | 48 B | 56 B |
| Runtime sample array storage estimate | 3,359 B | 967 B |
| Numeric array payload | 32 B per `Value` element | 8 B per `double` element |

For the 100-element numeric sample, the total estimate fell 71.2%. The estimate
still includes inline string capacity when measuring string arrays and omits
allocator metadata, as described above.

### Post-A3 benchmark comparison — 2026-11-05

Same three-run median method and host configuration as A0. All measurements
remain informational and separate from correctness tests.

| Workload | A0 | After A3 | Change |
|---|---:|---:|---:|
| Loops | 0.024317 s | 0.024055 s | −1.1% |
| Recursion | 0.004821 s | 0.004655 s | −3.4% |
| Array fill | 0.021055 s | 0.020117 s | −4.5% |
| String concat | 0.006180 s | 0.006018 s | −2.6% |
| Mandelbrot iteration | 0.238021 s | 0.237271 s | −0.3% |

### After A3 firmware sizes

| Environment | Static RAM | Flash image | Flash delta from A2 |
|---|---:|---:|---:|
| `lilygo_vga32` | 53,616 B | 1,549,677 B | +1,528 B |
| `olimex_esp32_sbc_fabgl` | 54,252 B | 1,624,285 B | +1,548 B |
| `esp32_2432s028` | 51,252 B | 1,489,313 B | +1,508 B |

### Track B1 host measurements — 2026-11-05

Same macOS AppleClang 21 Debug host and `ql32_sizecheck` workload as A1/A3.

| Type / metric | A3 | After B1 |
|---|---:|---:|
| `sizeof(Instruction)` | 224 B | 232 B |
| `sizeof(RuntimeFrame)` | 152 B | 136 B |
| 2,000 instruction records | 448,000 B | 464,000 B |
| Estimated bytecode sequence | 498,176 B | 514,176 B |
| Runtime sample bytecode estimate | 6,289 B | 6,701 B |
| Runtime global storage estimate | 135 B | 478 B |
| Runtime local storage estimate | 214 B | 205 B |
| Runtime array storage estimate | 967 B | 951 B |

Global storage now includes the symbol-name-to-slot table, slot values, and
uninitialized symbols referenced in compiled functions. The sample has few
globals and therefore highlights table overhead rather than scale savings.
`RuntimeFrame` storage excludes allocator details and includes vector capacity.

### Post-B1 benchmark comparison — 2026-11-05

Same three-run median method and host configuration as A3.

| Workload | A3 | After B1 | Change |
|---|---:|---:|---:|
| Loops | 0.024055 s | 0.020029 s | −16.7% |
| Recursion | 0.004655 s | 0.004625 s | −0.6% |
| Array fill | 0.020117 s | 0.015561 s | −22.6% |
| String concat | 0.006018 s | 0.005960 s | −1.0% |
| Mandelbrot iteration | 0.237271 s | 0.201053 s | −15.3% |

### After B1 firmware sizes

| Environment | Static RAM | Flash image | Flash delta from A3 |
|---|---:|---:|---:|
| `lilygo_vga32` | 53,616 B | 1,556,185 B | +6,508 B |
| `olimex_esp32_sbc_fabgl` | 54,252 B | 1,630,761 B | +6,476 B |
| `esp32_2432s028` | 51,252 B | 1,495,829 B | +6,516 B |

### Track B2 string representation probe — 2026-11-05

Host probe run: macOS AppleClang 21 Debug via `ql32_sizecheck`. Current
`sizeof(std::string)` is 24 B; `sizeof(ql32::Value)` is 32 B. An illustrative
tagged value with a `double` or 32-bit `StrId` is 16 B, and an illustrative
pool entry containing a string, reference count, and free-list link is 32 B.
The probe compares 1,000-element string arrays:

| Representation / workload | Estimated structural bytes |
|---|---:|
| Current string objects, all empty | 24,000 B |
| Handles + one shared empty pool entry and index node | 4,096 B |
| Current string objects, 1,000 unique short strings | 24,000 B |
| Handles + 1,000 unique pool entries and index nodes | 100,000 B |

These are optimistic structural estimates, not allocator measurements. They
exclude string payload allocations, hash bucket arrays beyond the simple node
pointer estimate, allocator metadata, and temporary hashing storage. The pool
has a large upside for highly shared values and a large downside for unique
short values, so B2 retains the existing RAII `std::string` representation.

Static RAM did not change at PlatformIO's reported granularity.

### Post-A1 benchmark comparison

Same host and three-run median method as A0. Percent change compares with the
A0 baseline above.

| Workload | A0 | Post A1 | Change |
|---|---:|---:|---:|
| Loops | 0.024317 s | 0.024478 s | +0.7% |
| Recursion | 0.004821 s | 0.005153 s | +6.9% |
| Array fill | 0.021055 s | 0.020877 s | −0.8% |
| String concat | 0.006180 s | 0.006306 s | +2.0% |
| Mandelbrot iteration | 0.238021 s | 0.236258 s | −0.7% |

### B3 managed buffer index probe

On the host used for `ql32_sizecheck`, the `BufferManager` record is 32 B and
the map value estimate is 40 B (the key duplicates the base already stored in
the record). For 1,000 allocations:

| Index representation | Estimated structural bytes |
|---|---:|
| Map values plus four pointer widths per estimated tree node | 72,000 B |
| Sorted vector records at capacity 1,024 | 32,768 B |

This is a structural estimate, not an allocator census. Actual tree node layout,
allocator rounding, vector growth, and payload buffers depend on the STL and
workload. The vector removes per-node allocation and duplicate base metadata;
deallocation now shifts following records.

### After B3 firmware sizes

All three supported PlatformIO profiles built with the sorted-vector index.
Values below are PlatformIO firmware image size and static RAM, not free heap
or runtime largest-block measurements.

| Environment | Static RAM | Flash image | Flash delta from B1 |
|---|---:|---:|---:|
| `lilygo_vga32` | 53,616 B | 1,556,521 B | +336 B |
| `olimex_esp32_sbc_fabgl` | 54,252 B | 1,631,101 B | +340 B |
| `esp32_2432s028` | 51,252 B | 1,496,161 B | +332 B |

### B4 compact bytecode measurements

The host `CompactInstruction` is exactly 16 B. The sizecheck uses the same
2,000-instruction synthetic `PRINT 1` program recorded in A1/B1:

| Representation | Estimated full program bytes | Change |
|---|---:|---:|
| Rich `InstructionSequence`, including tracked side data | 514,176 B | baseline |
| Compact records plus string, sidecar, function, and debug tables | 32,208 B | −93.7% |

For the larger mixed `benchmarks/mandelbrot.bas` workload (80 instructions),
the rich estimate is 20,742 B and the compact estimate is 17,292 B (−16.6%).
This difference reflects its higher share of named and control-flow
instructions, which retain rich indexed sidecars. The estimate includes
container capacities and source/debug metadata, but it excludes allocator
headers/rounding and implementation-specific `std::deque` block slack in the
transient rich compiler builder.

#### Post-B4 VM benchmark medians

Same host workload suite, five process runs per workload. Compare cautiously
with the earlier three-run B1 medians because run counts differ.

| Workload | Post-B4 median |
|---|---:|
| 10,000 loop iterations | 0.021681 s |
| Recursive function depth 200 | 0.004535 s |
| Fill 10,000-element numeric array | 0.015784 s |
| 1,000 string concatenations | 0.006077 s |
| 40×28 Mandelbrot iteration | 0.205741 s |

#### After B4 firmware sizes

Values are PlatformIO firmware image size and static RAM; they are not free heap
or runtime largest-block measurements.

| Environment | Static RAM | Flash image | Flash delta from B1 |
|---|---:|---:|---:|
| `lilygo_vga32` | 53,616 B | 1,556,129 B | −56 B |
| `olimex_esp32_sbc_fabgl` | 54,252 B | 1,630,785 B | +24 B |
| `esp32_2432s028` | 51,252 B | 1,495,741 B | −88 B |

### B4 test-plan REF/CAND measurements — 2026-11-05

Host: macOS, AppleClang 21, Debug. REF is `58bf799` (B3); CAND is `cd5d6b5`
(B4). The timed workloads use five process runs per binary; listed values are
median wall times. REF and CAND were run sequentially with the same input files.

| Workload | REF | CAND | CAND / REF |
|---|---:|---:|---:|
| 10,000 loop iterations | 0.019631 s | 0.020789 s | 105.9% |
| Recursive function depth 200 | 0.004980 s | 0.004684 s | 94.1% |
| Fill 10,000-element numeric array | 0.015396 s | 0.015721 s | 102.1% |
| 1,000 string concatenations | 0.005798 s | 0.006049 s | 104.3% |
| 40×28 Mandelbrot iteration | 0.200182 s | 0.202510 s | 101.2% |
| Tight FOR loop, 1,000,000 iterations | 1.615154 s | 1.628333 s | 100.8% |
| 100,000 function calls in a loop | 0.383066 s | 0.386004 s | 100.8% |
| `ql32_sizecheck` on 2,000-line source | 0.019233 s | 0.024325 s | 126.5% |

The sizecheck timing includes compilation/lowering and diagnostic probes, so it
is a proxy rather than a pure compile timer. A full `ql32 --console` run of the
2,000-line PRINT source took 0.022056 s on REF and 0.034192 s on CAND (155.0%);
that includes executing all 2,000 PRINT statements and is not the plan's
compile-only measurement. Both builds fail the attempted depth-1,000 recursive
workload with the same call-stack-limit error; successful recursion timing is
therefore reported at depth 200 from the curated runner.

The following size figures use `ql32_sizecheck` on identical source files. The
compact estimate includes record/sidecar/literal capacities and program maps,
but is not an allocator-level heap census.

| Sample | REF estimated sequence | CAND encoded program | CAND / REF |
|---|---:|---:|---:|
| 10-line sample (40 instructions) | 10,456 B | 848 B | 8.1% |
| 2,000-line PRINT sample (8,000 instructions) | 2,056,176 B | 128,208 B | 6.2% |
| `examples/mandelbrot.bas` (142 instructions) | 36,722 B | 34,026 B | 92.7% |
| `benchmarks/mandelbrot.bas` (80 instructions) | 20,742 B | 17,292 B | 83.3% |
| `examples/attractor.bas` (114 instructions) | 29,652 B | 33,581 B | 113.3% |

The two synthetic targets pass the plan's ≤50% tiny and ≤30% 2,000-line
thresholds. The attractor sample is 13.3% larger in the structural estimate.
The candidate stores 67 sidecars in a vector with capacity 128; the unused 61
records account for 14,152 B at `sizeof(Instruction) == 232`. In a read-only
temporary-object experiment, `shrink_to_fit()` changed the encoded estimate
from 33,581 B to 19,429 B. The exact-reserve change is not yet applied.

### B4 compile peak and allocation measurements — 2026-11-05

A temporary host harness overrode global `operator new`/`delete`, counted live
allocated bytes while `compileSource()` ran, then continued tracking while the
candidate lowered the rich sequence. The source string was loaded before
tracking. This captures standard-library allocation peak on this macOS host;
it does not emulate ESP32 allocator metadata or board concurrency.

| Workload | REF rich-compile peak | CAND rich-compile peak | CAND lowering peak total | CAND final encoded live |
|---|---:|---:|---:|---:|
| 2,000-line PRINT source | 2,666,800 B | 2,666,800 B | 2,004,856 B | 140,112 B |
| `examples/attractor.bas` | 51,404 B | 51,404 B | 54,948 B | 40,068 B |
| `benchmarks/mandelbrot.bas` | 30,038 B | 30,038 B | 31,966 B | 24,510 B |

The rich-compile peak is unchanged by B4. This confirms that compile-time heap
still pays for the old instruction builder before lowering; direct compact
emission is needed to reduce that peak. Attractor's tracked final allocation is
41.3% above REF; exact sidecar capacity would remove the observed capacity
waste, but the code change and full regression run remain pending.

### B4 execution profile — 2026-11-05

Read-only macOS `sample` profiles were collected from REF and CAND on a
repeated straight-line PRINT workload. In the candidate stack samples,
`verifyBytecode()` and `std::any_of` traverse `CompactProgram` through its
by-value const iterator, whose dereference constructs/returns a rich
`Instruction`. The hot VM loop instead calls `instructionAt(pc, scratch)`.
This is a profile lead for the >150% 2,000-line result, not proof of its exact
share; instrumented timing and a pure execution workload are still required.

#### B4 ESP32 profile verification

Fresh `pio run` results compared with the recorded B3 values:

| Environment | B3 static RAM | CAND static RAM | B3 flash | CAND flash | Flash delta |
|---|---:|---:|---:|---:|---:|
| `lilygo_vga32` | 53,616 B | 53,616 B | 1,556,521 B | 1,556,129 B | −392 B |
| `olimex_esp32_sbc_fabgl` | 54,252 B | 54,252 B | 1,631,101 B | 1,630,785 B | −316 B |
| `esp32_2432s028` | 51,252 B | 51,252 B | 1,496,161 B | 1,495,741 B | −420 B |

All three builds succeeded; static RAM did not increase and no new compiler
warnings appeared in VM/compiler sources.
