# Olimex 640x480 bulk row copy (step 6) - 2026-11-20

## Hypothesis
Refill time is dominated by scattered PSRAM reads and bus contention; copying
each source row to internal RAM with one bulk `memcpy` before encoding should
cut refill time and late refills.

## Change
Diagnostic env `olimex_esp32_sbc_fabgl_psram_dma_display_640x480_present_split_bulk_test`
(`-DQL32_OLIMEX_DMA_REFILL_BULK_COPY_TEST`): in `refillRow` the Packed4 source
row (320 B) is copied into a static aligned internal buffer, then
`encodePacked4Row` reads from it. Other builds unchanged.

## Hardware results (split cores, owner present)
| Run | Late (bulk) | us/row (bulk) | Baseline late | Baseline us/row |
|---|---|---|---|---|
| idle 20 s | 6.4% | 36.5 | 14-16% | 37.8-38.1 |
| CPU-only 20 s | 61.5% | 86.3 | 62-67% | 98-101 |
| attractor 30 s | 19.4% | 41.6 | 46-47% | 63.4 |

Max batch: idle 4.1 ms (was 9.3), attractor 29 ms (was 27.7). Standard: 0% late.
Owner: picture better, still blinks, nearly holds sync but not yet.

## Conclusion
Supported: PSRAM read pattern and contention matter. Attractor late share fell
by more than half and idle by more than half; CPU-only barely moved. The
<25 us/row target was not met (idle us/row is nearly unchanged, so lateness
fell mainly through fewer long stalls). Still not usable on the split build.
Per instruction, no further iteration in this step.

## Status
- Built: bulk env, standard present_profile.
- Host-tested: no new portable logic; ctest unchanged (80/83, 3 known failures).
- Hardware-verified: numbers above; standard present_profile reflashed and the
  owner confirmed a stable, readable picture.

## Proposed step 7 (not started)
1. Larger lead: put the line ring (or a larger part of it) in internal RAM
   and/or raise the refill lead so a 10-30 ms stall does not become late
   (needs a telemetry-backed ring hypothesis; internal RAM budget check first).
2. Smaller batches: refill 10-20 rows per wake instead of 60 so a stall costs
   fewer rows; measure max-batch and late share.
3. DMA-assisted copy (async PSRAM->internal row prefetch) to take the refill
   task off the bus; measure CPU-only late share.
4. Prefetch the next row while encoding the current one.
Each: owner-present A/B against idle 6.4% / attractor 19.4% / CPU-only 61.5%.
