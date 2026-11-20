# Olimex 640x480 present profile: split timers and core-split A/B (2026-11-20)

Board: Olimex ESP32-SBC-FabGL, PSRAM-DMA scanout at 640x480 (parked/experimental path, not the qualified FabGL path).
Flashed app partition only; FFS and SDC untouched. Owner present.

## Builds

- `..._640x480_present_profile` (standard placement: BASIC on core 1, refill worker on core 1, 120-row ring).
- `..._640x480_present_split_cores_test` (existing split placement plus the same present counters).
- New counters: `provider-us` (row provider / window composition) and `write-us` (`writeMappedRow` PSRAM write).
- Direct `beginFrameUpdate` callers in main.cpp exist only in the `QL32_OLIMEX_DMA_FRAMEBUFFER_BENCHMARK` code, not in the runtime path, so nothing to count.

## Results (same workloads, back to back)

| Run | Placement | Calls (full) | Rows | provider-us | write-us | Avg / max present | Late refills / EOF events |
| --- | --- | --- | --- | --- | --- | --- | --- |
| PRINT x20 | standard | 83 (0) | 712 | 2.37 s | 0.37 s | ~4 ms per row | 0 |
| PRINT x20 | split | 94 (0) | 800 | 84 ms | 12 ms | 1.5 ms / 16 ms | 635 / 3,978 (16%) |
| Attractor 30 s | standard | 35 (2) | 1,264 | 6.24 s | 3.0 s | max 4.4 s (full) | 0 |
| Attractor 30 s | split | 119 (84) | 40,640 | 6.80 s | 3.41 s | 98 ms / 140 ms | 9,303 / 28,077 (33%) |

Second standard attractor run: provider 5.5 s, write 2.8 s.

## Findings

- **Composition dominates.** provider-us is 66-80% of row time on the standard build; the PSRAM write is 12-35%. Per-row cost on the standard build is about 4-9 ms.
- **The core split gives a large presentation speedup.** Per-row present cost falls from about 5 ms to about 0.17 ms (about 30x); a full 640x480 present drops from about 4.4 s to about 0.1 s. The attractor moved from 2 full frames to 84 in 30 s.
- **The split is not usable as is.** The owner saw flickering on the split build; late refills were 16% (PRINT) and 33% (attractor) of EOF events. The standard build had zero late refills. This matches the 2026-11-17 split-core reports.
- The slowness is therefore a core-contention effect (BASIC, composition and refill worker share core 1), not intrinsic to the row code. The 100-frame CPU-only and full timings did not finish within 120 s / 300 s on the standard build, and serial markers are not available (benchmarks print to the console window), so no fixed-work wall time is reported. The speedup above comes from counters over a fixed 30 s window.
- Caveat: the benchmarks' output-by-serial path and `LOAD`/`RUN` hang are unresolved and unrelated to this step.

## Restore

The standard `present_profile` was reflashed at the end; the owner confirmed a stable, readable picture.

## Status

- Built: both envs.
- Host-tested: `ql32_olimex_present_stats_tests` (ctest 80/83, the 3 known failures: scheduler, channel, hardware).
- Hardware-verified: counters on both placements; split picture flickers; standard picture stable.

## Proposed step 3

1. Hypothesis: the refill worker's deadline misses on the split build come from BASIC/composition now running faster and saturating PSRAM bandwidth. Test: split build, throttle presentation (cap rows per ms or yield between rows) and watch late-refill share fall. Telemetry: PRESDIAG rows/s versus DMADIAG late-refills.
2. Reduce composition cost on the standard build (span copies instead of per-pixel work, no per-row `std::function`). Telemetry: provider-us per row; target well under 1 ms.
3. Coalesce and bound presents (frame-boundary publish, max rows per present). Owner test: attractor and PRINT loop with zero late refills and a stable picture.
Core pinning, ring depth and resolution stay unchanged until step 1 shows a gain.
