# Olimex Mandelbrot progress under printing workers — 2026-11-17

## Setup

The test used the Olimex FabGL graphics-profile firmware already on the board,
320×200 mode, and
[`mandelbrot-progress.bas`](../../examples/graphics/mandelbrot-progress.bas).
That fixture is the existing 80×56, 24-iteration demo with a serial marker at
the start of each row. No firmware was flashed. The BASIC task slice was
128 instructions for the controlled runs. The serial task API showed the
supervisor at 320×200 and each test worker at its own 320×200 canvas.

## Measurements

| Workload | Result |
| --- | --- |
| Mandelbrot alone, slice 128 | All 56 rows completed in 41.484 s. |
| One continuously printing worker plus Mandelbrot, slice 128 | Row 1 arrived 78.958 s after row 0. |
| Same two tasks after suspending the worker | Eight subsequent rows arrived over 4.61 s (about 0.66 s per row). |
| Three continuously printing workers plus Mandelbrot, slice 128 | Only row 0 arrived during the bounded 120 s capture. Rows 1–4 appeared as the three workers were stopped. |
| One worker plus Mandelbrot, Mandelbrot priority raised to 16 | Priority change did not produce a clear recovery; no row 4 arrived during the next 35 s. This short check is inconclusive. |

The earlier slice-4 concurrent capture recorded row 0 at 18.744 s and row 1
at 67.205 s, a 48.461 s interval. In a separate isolated partial run, slice 4
produced roughly 7–8 s per row; changing to 128 reduced subsequent rows to
roughly 0.5–1.0 s. The board rebooted unexpectedly between those partial and
full-run captures. No reset command was issued, and the captured boot log did
not identify a cause. The full slice-128 baseline began after that boot.

## Finding

The Mandelbrot session was not permanently hung. An unthrottled worker that
prints continuously caused severe progress delay; suspending that worker
restored normal row progress immediately. Three such workers prevented
observable Mandelbrot row progress during the 120 s observation window. The
observations point to synchronous graphics/text output from tight worker loops
as the dominant contention in this reproduction. They do not yet identify
whether the blocking cost is in BASIC `PRINT`, text scrolling, frame
presentation, or the scheduler's execution boundary.

`Alt-Tab` changes which task's window is raised; it does not change task
priority. Raising Mandelbrot's priority to 16 was not demonstrated to solve
the issue. Treat priority boosting as an unverified workaround.

## Follow-up

- Add an explicit delay or batched `GUPDATE` to a copy of the worker fixture
  and repeat the one- and three-worker comparisons.
- Instrument scheduler tick duration and graphics-present duration per task.
- Consider a yield-aware or asynchronous presentation boundary only after the
  measurements identify which synchronous operation blocks the main loop.

Raw serial captures:

- [Slice-4 concurrent run](mandelbrot-workers-slice4-2026-11-16.log)
- [Slice-4 to 128 partial run](mandelbrot-slice4-to128-partial-2026-11-17.log)
- [Unloaded slice-128 baseline](mandelbrot-unloaded-slice128-2026-11-17.log)
- [Three workers, slice 128](mandelbrot-three-workers-slice128-2026-11-17.log)
- [One worker, slice 128](mandelbrot-one-worker-slice128-2026-11-17.log)
- [Priority-16 trial](mandelbrot-priority16-slice128-2026-11-17.log)
- [Slice-128 transition capture](mandelbrot-slice128-transition-2026-11-17.log)
