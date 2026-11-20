# Olimex 640x480 per-frame present budget on split cores (2026-11-20)

Env `..._640x480_present_split_budget_test` (split cores, 120-row ring, `PRESBUDGET <bytes/frame>`), flashed app-only over /dev/ttyUSB0 with the owner present. One boot, budgets run in the order 64000, 32000, 0. PRINT x20 then 30 s attractor each. Standard `present_profile` reflashed at the end; owner confirmed a stable, readable picture.

## Results (late refills / EOF events, per run delta)

| Budget | PRINT x20 | Attractor 30 s | Throttle waits (attractor) |
| --- | --- | --- | --- |
| 64000 | 1,154 / 7,203 (16%) | 7,725 / 18,238 (42%) | 0 (never binds: ~83 rows/frame) |
| 32000 | 1,169 / 7,203 (16%) | 7,085 / 18,314 (39%) | 608, 3.0 s total |
| 0 (off) | n/a (run mis-sequenced) | 7,793 / 18,252 (43%) | 0 |
| standard placement (previous report) | 0 | 0 | - |

- Idle at boot, before any BASIC activity: 755 late / 4,651 EOF (16%). Late refills are present with no presenter load, and PRINT (tiny writes) adds nothing.
- Halving the per-frame budget spent 3 s waiting but moved late refills only from 43% to 39%.
- Composition change (single viewport lookup, 1-bit byte expansion): split provider-us 6.6 s vs 6.8 s before in a comparable workload (within noise; no measurable hardware win). Provider remains about 2x write.
- The owner saw flicker on the split build (sync kept). Standard stable.

## Conclusion

- PSRAM write bandwidth from presents is **not** the main cause of split-core late refills: lateness exists at idle and barely responds to a 2x budget cut. The budget hypothesis is not supported.
- Remaining candidates: refill worker/ISR scheduling or priority on core 1, flash-cache/IRAM effects, or fixed per-batch cost (refill-max-us ~10 ms for 60 rows vs ~4 ms ring lead), plus core-0 PSRAM reads contending with scanout.
- Cache review: the ISR is IRAM-safe (`ESP_INTR_FLAG_IRAM`), but the refill task and PSRAM data reads are not. A SPIFFS/flash write that disables cache would stall refills beyond the ring lead. These runs did not write FFS, so it does not explain these numbers; it is untested.
- Wall-time timers include preemption on the standard placement, so its per-row costs overstate the work.

## Status

- Built: budget env and standard present_profile.
- Host-tested: `ql32_olimex_present_stats_tests` including the budget; ctest 80/83 (3 known failures: scheduler, channel, hardware).
- Hardware-verified: counters on split+budget (budget did not fix lateness); standard picture stable.

## Proposed step 4

1. Hypothesis: split lateness comes from refill batch cost (up to ~10 ms for 60 rows) exceeding the ring lead, i.e. PSRAM read contention with core 0. Compare late share for idle BASIC, CPU-only (`abcpu`) and the attractor; telemetry: refill-max-us and late share per workload.
2. List which tasks run on core 1 in the split build and their priorities versus the refill worker/ISR.
3. Measure a late-refill delta around an FFS file save to size the cache-disable risk.
Ring depth, pinning and modes stay unchanged until those results exist.
