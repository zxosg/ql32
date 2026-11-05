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
