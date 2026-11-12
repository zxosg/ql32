# Attractor performance results — 2026-11-12

## Olimex ESP32-SBC-FabGL with WROVER PSRAM

The bounded workload in [`examples/attractor-benchmark.bas`](../../examples/attractor-benchmark.bas)
runs 100 frames at the point density used by `attractor.bas`. Each timing is
the median of three runs. `CPUFREQ()` reported the requested clock on every
run.

| CPU MHz | Math mode | Run times (s) | Median (s) |
|---:|:---|:---|---:|
| 80 | DOUBLE | 46.388, 46.994, 46.991 | 46.991 |
| 80 | SINGLE | 46.441, 46.438, 46.392 | 46.438 |
| 160 | DOUBLE | 27.784, 28.231, 27.784 | 27.784 |
| 160 | SINGLE | 26.740, 29.190, 27.432 | 27.432 |
| 240 | DOUBLE | 21.915, 21.764, 21.764 | 21.764 |
| 240 | SINGLE | 21.919, 21.918, 21.922 | 21.919 |

Increasing the clock from 80 to 240 MHz reduced the SINGLE median by 2.12×,
not 3×. SINGLE and DOUBLE are within about 1.3% at each clock, with no
consistent winner. For this workload, choosing SINGLE does not provide a
meaningful speed gain over DOUBLE.

The CPU/math-only companion (`examples/attractor-benchmark-cpu.bas`) took
17.152 s and 17.050 s at 80 MHz/SINGLE. Compared with the full 46.438 s
median, this suggests that per-point plotting, clearing, and display updates
account for a large part of the total time. This subtraction is indicative,
not a standalone measurement of drawing cost: the CPU-only and full variants
were not run as a tightly interleaved pair.

## Tuning in progress

`attractor.bas` and the benchmark now calculate `r * i` once per row and
reuse named `angleV` and `angleX` values for the sine/cosine pairs. This
removes one multiplication per plotted point while preserving the recurrence
update order and the SINGLE/DOUBLE arithmetic option. The demo now uses
DOUBLE at 240 MHz: measured DOUBLE was 21.764 s and SINGLE was 21.919 s, so
DOUBLE provided the full available precision without a measured speed penalty
for this workload. The loop refactor has not yet been run on the board. It
should be compared with the prior results using the same clock, precision,
and three-run procedure before claiming a speedup.

The attempted draw-only diagnostic could not be transferred to SDC after the
FFS became unable to create additional temporary files. The board is currently
not present at `/dev/ttyUSB0`. CMake is unavailable in the current host
environment, and PlatformIO could not build because its shared package lock is
read-only in this session, so host execution and an optimized-source board
retest remain open.
