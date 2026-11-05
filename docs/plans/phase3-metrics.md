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
