# Olimex 640x480 PSRAM-DMA investigation

Status: open, experimental. The qualified Olimex path remains FabGL
(`olimex_esp32_sbc_fabgl_psram`). This file holds the detailed notes that used
to live in `PROJECT-HANDOFF.md`. Env names are explained in the handoff.

## 2026-11-17 hardware session

- The 640x400 and 640x480 runtime DMA telemetry profiles build; idle captures
  reported zero late refills, IRQ errors, and active-slot overwrites. Stress
  profiles at both sizes showed stable moving gradients by owner observation
  but produced no serial telemetry.
- The 640x480 runtime now accumulates counters in memory and the diagnostic
  profile exposes `DMADIAG` to print one snapshot after a test. Verified over
  serial: no late refills, IRQ errors, or active-slot overwrites. Full 640x480
  allocation and printed graphics/text were visually correct.
- Mandelbrot/worker contention results and raw serial captures:
  `docs/test-results/olimex-mandelbrot-worker-starvation-2026-11-17.md`.
- The owner reports full-screen REPL/BASIC feels about one tenth as fast as
  320x200.
- Two split-core 640x480 probes moved the Arduino loop to core 0; the second
  also pinned the EOF interrupt and refill worker to core 1. CPU-only BASIC
  took 14.562/14.670 s, but the second profile had 1,457 late refills in 9,345
  EOF events, whole-console flashing, and an unstable image during the
  attractor. A 160-row ring on the same split setup was worse even before the
  attractor: 8,781 late refills in 38,451 events.
- The standard 120-row profile was restored and returned to zero late refills
  in 15,904 events. It then ran the attractor across two snapshots about
  4 min 53 s apart with zero late refills across 286,600 EOF events.
- Conclusion at the time: core-placement or shared memory-contention effect;
  PSRAM bus stalls are not directly instrumented. Core pinning or ring-depth
  increases alone are not the next step.
- Reports: `olimex-dma-split-core-performance-2026-11-17.md`,
  `olimex-vga-replay-640x480-2026-11-17.md` (the standard attractor was stable
  but nearly still; the finite draw-only test opened a second channel-2
  console and caused slight flashing until the board was reflashed; the owner
  then confirmed a stable, readable screen).

## 2026-11-20 findings (branch `jan-pal-automatic-potato`)

Commits `6e31b27` (present telemetry, split timers, per-frame byte budget,
cheaper row composition), `79b3815` (`refill-total-us` counter and split-core
late-refill diagnosis) and `3d54fbc` (IRAM refill env, step 5 report). Reports
live on that branch under `docs/test-results/`
(`git show jan-pal-automatic-potato:docs/test-results/<file>`):

- `olimex-present-profile-640x480-2026-11-20.md`
- `olimex-present-profile-split-timers-ab-2026-11-20.md`
- `olimex-present-budget-2026-11-20.md`
- `olimex-split-core-late-refill-diagnosis-2026-11-20.md`
- `olimex-refill-optimisation-2026-11-20.md`

Results, all hardware-verified with the owner present:

1. **Presentation is the cost on standard placement.** A full 640x480 present
   takes about 4.4 s (about 9 ms per row); 16-row text updates cost about 4 ms
   per row. During a 30 s attractor about 34% of time is in the presenter. Row
   composition (`provider-us`) is 66-80% of row time, the PSRAM write 12-35%;
   the frame clone is only about 7%.
2. **Split cores present about 30x faster** (about 0.17 ms per row, full frame
   about 0.1 s) but the picture flickers: 14-16% late refills at idle, about
   62-67% CPU-only, about 46% during the attractor, versus 0 on the standard
   placement.
3. **Per-frame byte budget did not help** (`PRESBUDGET`; 43% to 39% late at
   half budget). Lateness exists at idle with no presenter load.
4. **IRAM placement of the refill path did not help** (14.3% idle, 67.3%
   CPU-only, 46.8% attractor). The encoder already uses aligned 32-bit loads
   and a pair LUT; instruction fetch is not the limiter.
5. Refill costs about 34 us per row on standard against a 31.8 us row period
   (only the ring lead saves it); on split it rises 12% at idle and about 2.6x
   CPU-only.

Leading hypothesis: PSRAM data reads in the refill worker plus cross-core bus
contention on the shared external-memory path. Not directly instrumented.

## Proposed step 6 (not started)

Test the PSRAM-read hypothesis directly: copy each source row group with one
bulk read (or prefetch the next group into internal RAM from the presenter
side) and measure us/row against the 33.9 us/row standard baseline; or keep a
hot internal-RAM copy of dirty/active rows. Owner test: standard and split
pictures with `DMADIAG` late refills. If the path is bandwidth-bound, keep
standard placement for 640x480 and use the split only where it is proven.
Ring depth, core pinning and resolution stay unchanged until step 6 has data.

## Open issues from these runs

- Benchmark output goes to the console window, so no serial markers exist and
  100-frame CPU-only/full timings did not finish on the standard build within
  120/300 s; no fixed-work wall time is reported.
- A `LOAD`/`RUN` hang during the benchmark runs is unresolved.
- Standard CPU-only could not be measured (REPL did not answer `DMADIAG`;
  board reset needed). Flash-cache stalls from FFS writes were sized only
  roughly (about 6 points on the split build).
