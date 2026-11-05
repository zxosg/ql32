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
