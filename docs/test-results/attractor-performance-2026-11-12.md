# Attractor performance results — 2026-11-12

## Earlier Olimex baseline

These results predate the row-loop refactor and framebuffer presenter changes.

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

## Current framebuffer benchmark

`attractor.bas` and the benchmark now calculate `r * i` once per row and
reuse named `angleV` and `angleX` values for the sine/cosine pairs. This
removes one multiplication per plotted point while preserving the recurrence
update order and the SINGLE/DOUBLE arithmetic option. This refactored source
was transferred to FFS over YMODEM and measured at 240 MHz/DOUBLE on a 320×200
viewport. Timing starts at the program's serial start marker and ends at its
completion marker; task launch and mode selection are excluded.

| Workload | Single-buffer runs (s) | Median (s) | Double-buffer runs (s) | Median (s) |
|:---|:---|---:|:---|---:|
| Full attractor | 24.633, 24.433, 24.235 | 24.433 | 23.834, 23.838, 24.033 | 23.838 |
| Draw-only | 14.622, 14.622, 14.620 | 14.622 | 14.221, 14.021, 14.019 | 14.021 |

The CPU-only companion took 11.617, 11.217, and 11.215 s (median 11.217 s).
At this workload and resolution, double buffering improved the draw-only
median by 4.1% and the full benchmark median by 2.4%. This is a modest overall
gain. The current single-buffered median is 12.3% slower than the earlier
21.764 s 240 MHz/DOUBLE result above. Since the source and presenter changed
between those runs, this comparison does not isolate which change caused the
difference.

FabGL's partial DMA allocation for 640×480 double buffering corrupts the heap
during a later mode change on this Olimex target. QL/32 rejects that
mode-option combination; 640×480 single-buffered mode remains usable. The
draw-only benchmark's single-line `IF` was changed to the supported block form
before testing. All benchmark files remain on the board's FFS and in the
working directory.
