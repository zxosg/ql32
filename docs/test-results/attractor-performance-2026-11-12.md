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

## Olimex double-buffer retest — 2026-11-13

The original `attractor-benchmark.bas` was loaded from FFS and run on the
320x200 FabGL double-buffer profile. `LOAD` auto-numbers unnumbered source;
the interpreter's line-number recognizer had omitted module-level `CONST` and
`OPTION`, causing the generated line number before those statements to be
parsed as BASIC code. Adding both token kinds fixed the load/run path. A
storage regression test covers an unnumbered `CONST`/`OPTION` file.

Each sample is the interval between the program's serial `ATTRACTOR_START` and
`ATTRACTOR_DONE` markers. The same 100-frame, same-density workload was used;
CPU frequency was confirmed by `CPUFREQ()` on every run.

| CPU MHz | Math mode | Run times (s) | Median (s) |
|---:|:---|---:|---:|
| 80 | SINGLE | 41.309, 41.557, 41.308 | 41.309 |
| 160 | SINGLE | 24.291, 24.286, 24.282 | 24.286 |
| 240 | SINGLE | 19.529, 19.778, 19.779 | 19.778 |
| 240 | DOUBLE | 21.281, 21.530, 21.278 | 21.281 |

On this run, 240 MHz/SINGLE was 2.09x faster than 80 MHz/SINGLE. At 240 MHz,
SINGLE was 7.1% faster than DOUBLE. Unlike earlier measurements, precision
selection made a noticeable difference for this revision and profile.

The owner confirmed stable, readable VGA during the 80 MHz and 160 MHz/SINGLE
runs. During an explicit 240 MHz/SINGLE run, the monitor was reported blank or
out of range, although all serial benchmark markers completed. A prior
no-option 240 MHz run was stable. This pointed to the CPU service needlessly
reapplying an already-active 240 MHz clock after FabGL startup. The service now
returns success without calling `setCpuFrequencyMhz()` when the requested and
current frequencies match.

On 2026-11-13, the revised service passed three 240 MHz attractor runs. The
owner confirmed stable, readable VGA during the first and both repeats. A live
160 MHz run followed by a 240 MHz run, without resetting between them, also
completed with both `ATTRACTOR_DONE` markers; the owner confirmed VGA stayed
stable and readable. `CPUFREQ()` reported the requested rate in each run. This
qualifies repeated 240 MHz use and the tested 160-to-240 MHz transition on the
320x200 FabGL double-buffer profile.

A separate experiment restarted FabGL after each runtime frequency setting.
It could allocate only a 320x92 viewport while the BASIC program was loaded;
QL/32 still expected 320x200 and raised a framebuffer-geometry error, then
rebooted. That restart approach was removed. After reboot the owner confirmed
normal VGA returned.

## Packed retained-canvas update — 2026-11-13

The packed `IndexedCanvas` now caches its arena storage pointer, fills solid
spans with repeated-byte writes, and uses `memmove` for byte-aligned scrolling.
The existing `graphics_tests` suite passed, and the Olimex double-buffer
profile built and flashed successfully. The saved
[`graphics-fill-benchmark.bas`](../../examples/graphics-fill-benchmark.bas)
ran on the board at 240 MHz and 320x200. Three runs measured 4,000 64x32
filled rectangles in 3.206, 3.005, and 3.004 s (3.005 s median), and 2,000
radius-12 filled circles in 1.602, 1.802, and 1.804 s (1.802 s median). The
saved [`graphics-scroll-benchmark.bas`](../../examples/graphics-scroll-benchmark.bas)
scrolled 1,000 text lines in 4.408, 4.407, and 4.408 s (4.408 s median).

| Operation | Workload | Run times (s) | Median (s) |
|:---|:---|---:|---:|
| Filled rectangle | 4,000 at 64x32 | 3.206, 3.005, 3.004 | 3.005 |
| Filled circle | 2,000 at radius 12 | 1.602, 1.802, 1.804 | 1.802 |
| Text scroll | 1,000 lines | 4.408, 4.407, 4.408 | 4.408 |

The owner confirmed the drawing was correct and VGA stayed stable. A repeated
240 MHz attractor run completed in 21.433 s with stable, readable VGA. This
math-heavy test does not isolate the new fill/scroll paths; a comparable
pre-change retained-canvas baseline is still needed before claiming a speedup.
The scroll benchmark uses `GUPDATE 0`, so it displayed only the final text
frame. The scrolling passed too quickly to observe; the owner reported that
the display stayed stable without apparent glitches and that the final text
was probably correct.

## Draw-only retest — 2026-11-13

The draw-only attractor workload was rerun on the Olimex FabGL double-buffer
profile at 320x200 and 240 MHz. The source was transferred over YMODEM and
loaded from `FFS:/draw240.bas` (the short basename keeps within the SPIFFS
filename limit). Timing uses the program's serial start and done markers;
`CPUFREQ()` reported 240 MHz on all three runs.

| Workload | Run times (s) | Median (s) |
|:---|---:|---:|
| Draw-only attractor | 11.388, 11.433, 11.436 | 11.433 |

Compared with the earlier double-buffer draw-only median of 14.021 s, this is
an 18.5% reduction in elapsed time. This gives a comparable end-to-end result
for the draw-only attractor workload after the retained-canvas hot-path
changes; it does not isolate which individual optimization accounts for the
gain. The program completed all three runs with its end marker. VGA appearance
during the runs was confirmed by the owner, who saw pixels on screen; overall
stability was not separately reported.

## Full attractor precision retest — 2026-11-13

The full 100-frame attractor benchmark was rerun at 240 MHz on the same
320x200 FabGL double-buffer profile, using saved SINGLE and DOUBLE variants.
Three additional interleaved SINGLE/DOUBLE pairs followed the first three
grouped runs. `CPUFREQ()` reported 240 MHz in all twelve runs.

| Math mode | Run times (s) | Median (s) |
|:---|---:|---:|
| SINGLE | 18.513, 21.268, 21.078, 21.325, 18.562, 18.315 | 19.820 |
| DOUBLE | 21.326, 21.328, 20.020, 19.960, 21.371, 19.965 | 20.673 |

SINGLE's median is 4.1% lower, but the run ranges are 3.010 s for SINGLE and
1.411 s for DOUBLE. Both modes show clusters of faster and slower runs, even
in the interleaved set. The full-screen measurements alone do not establish a
reliable precision-related speed difference; the previous three-run results
showed a 7.1% SINGLE advantage. Both variants completed all serial end
markers; screen stability during this retest was not separately reported.

## Sine/cosine identity experiment — 2026-11-13

An experimental SINGLE-precision variant applied the sum identities
`sin(a)+sin(b)=2*sin((a+b)/2)*cos((a-b)/2)` and
`cos(a)+cos(b)=2*cos((a+b)/2)*cos((a-b)/2)`. This reduces four trig calls per
point to three but adds intermediate calculations and BASIC variables. Three
alternating comparisons at 240 MHz measured the original at 21.269, 18.356,
and 18.359 s (18.359 s median), and the identity version at 23.821, 23.226,
and 23.824 s (23.821 s median). `CPUFREQ()` reported 240 MHz in every run.
The identity variant was slower in all three pairs, so it is not an
optimization on the current interpreter. The saved
[`attractor-benchmark-identity.bas`](../../examples/attractor-benchmark-identity.bas)
is experimental and is not used by the demo.

An inline version removed the intermediate variables but repeated the
sum/difference arithmetic in each expression. Three paired runs measured the
original at 18.355, 18.412, and 18.558 s (18.412 s median), and the inline
variant at 22.780, 22.774, and 22.768 s (22.774 s median), about 23.7% slower.
Its saved source is
[`attractor-benchmark-identity-inline.bas`](../../examples/attractor-benchmark-identity-inline.bas).
Both source-level forms therefore lose to the original on this VM; a trig-pair
optimization would need to avoid extra BASIC expression and variable work.

## CPU-only precision retest — 2026-11-13

To separate math work from the variable full-screen update timings, the CPU-only
attractor benchmark was run in three interleaved SINGLE/DOUBLE pairs at
240 MHz. It keeps the recurrence and loop counts but omits VGA calls.

| Math mode | Run times (s) | Median (s) | Checksum |
|:---|---:|---:|---:|
| SINGLE | 9.430, 9.381, 9.436 | 9.430 | 394.64776611328125 |
| DOUBLE | 10.433, 10.789, 10.794 | 10.789 | 394.6477526207737 |

`CPUFREQ()` reported 240 MHz for all six runs. SINGLE was 12.6% faster on this
math-only workload, with consistent timings. The final checksums differ by
about 1.35e-5, though a chaotic recurrence can diverge in its exact visual
trajectory. The main `attractor.bas` demo now selects SINGLE; the original
DOUBLE version is retained as `attractor-double.bas`.

## PRINT update batching — 2026-11-13

The saved [`graphics-print-present-benchmark.bas`](../../examples/graphics-print-present-benchmark.bas)
compares 20 lines of graphics-window text that fit without scrolling. It ran
at 240 MHz on the Olimex double-buffer profile, with three passes each for
automatic presentation and for `GUPDATE 0` followed by one `GUPDATE`.

| Update mode | Run times (s) | Median (s) |
|:---|---:|---:|
| Automatic | 1.708, 1.709, 1.708 | 1.708 |
| Batched | 0.097, 0.095, 0.097 | 0.097 |

Batching was 17.6x faster for this workload. The double-buffer presenter
cannot safely swap a partial update because the other FabGL buffer would hold
stale pixels, so each automatic dirty text update falls back to a full-frame
compose, row conversion, and swap. For bulk BASIC output, `GUPDATE 0` around
the print loop followed by one `GUPDATE` avoids those repeated full-frame
presentations. It intentionally hides intermediate lines while the loop runs.
