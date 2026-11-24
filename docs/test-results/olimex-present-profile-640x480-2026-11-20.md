# Olimex 640×480 presenter telemetry — 2026-11-20

Step 1 of the VGA high-resolution plan: measure `PsramFramePresenter::presentRegion`.

## Setup

- Env `olimex_esp32_sbc_fabgl_psram_dma_display_640x480_present_profile`
  (`QL32_OLIMEX_PRESENT_PROFILE`; standard 120-row ring, core placement
  unchanged). Built, then flashed over `/dev/ttyUSB0` (app image only; FFS/SDC
  untouched, hash verified).
- Commands: `PRESRESET` zeroes counters, `PRESDIAG` prints presenter counters,
  `DMADIAG` prints scanout counters. Serial was driven from the host.
- Counters cover `presentRegion` only. `rows-us` includes the row provider
  (window composition) and the mapped PSRAM write; they are not separated yet.
- The draw-only benchmark was not run.

## Results

| Run | calls (full/partial) | rows | rows-us | begin-us (clone) | publish-us | avg / max per call |
| --- | --- | --- | --- | --- | --- | --- |
| Boot to first command (startup) | 23 (0/23) | 232 | 1.97 s | 0.08 s | 0.1 ms | 89 ms / 317 ms |
| `FOR i=1 TO 20: PRINT "line ";i: NEXT i` | 76 (0/76) | 656 | 2.79 s | 0.22 s | 17 ms | 40 ms / 334 ms |
| `attractor.bas` task, 30 s | 35 (2/33) | 1,264 | 9.48 s | 0.75 s | 17 ms | 293 ms / 4,388 ms |

- A full 640×480 present took about 4.4 s (max-us 4,387,765 and 4,404,615),
  about 9 ms per row or 14 µs per pixel. Partial 16-row text updates cost about
  4 ms per row.
- Over the attractor's 30 s, about 10.2 s (34%) was spent inside the presenter.
  The `rows-us` share was 92–93%, clone (`begin`) about 7%, publish under 0.2%.
- `DMADIAG` after all runs: late-refills 0, spurious-IRQ 0, active-slot
  overwrites 0, refill-max 2,361 µs, backlog-max 60 rows. The presenter
  telemetry does not disturb the deadline.
- Owner observation: VGA stable throughout both runs and after them.

## Findings

- Supported: presentation is dominant. Per-row present cost (about 4–9 ms) is
  far above anything the scanout needs, and a single full repaint takes
  seconds.
- Not supported: the full-frame clone is not the main cost (about 7% of
  presenter time; 219 clones, 1.38 MB cloned in total).
- Not supported: updates are not mostly full-window. Text printing is already
  partial (16 rows each). The cost is per row, not per call count, so damage
  coalescing alone helps less than expected for PRINT, but it still matters
  for the attractor, which presents full frames.
- Open: whether the per-row cost is the row provider (window composition,
  `std::function`, vector) or `writeMappedRow` (PSRAM nibble writes). The
  counters do not split them.
- Hardware verified: counters, no late refills, stable picture. Not verified:
  320×200 on this profile, so the ratio against 320×200 is not measured here.

## Proposed step 2

1. Hypothesis: row composition dominates `rows-us`. Add a second timer around
   `rowProvider` versus `writeMappedRow` in the same diagnostic env
   (`provider-us`, `write-us`). Owner-present test: repeat the PRINT loop and a
   30 s attractor; expect one of the two to exceed 80%.
2. If the provider dominates, make window composition for a damage rectangle
   cheaper (span copies instead of per-pixel, no per-row `std::function`). If
   the write dominates, write whole aligned bytes and use word stores.
   Telemetry: µs per row in a full and 16-row present, target at least 5×
   lower; `DMADIAG` must stay at zero late refills.
3. Then coalesce automatic updates (one present per tick with unioned
   damage) and re-measure the attractor's presenter share.
