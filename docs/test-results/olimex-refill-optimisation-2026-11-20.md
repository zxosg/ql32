# Olimex 640x480 refill optimisation (step 5) - 2026-11-20

## Hypothesis
Per-row refill (target < 25 us/row) is slow because of the encoder code or
instruction-cache misses on core 1, so a cheaper encoder or IRAM placement
would cut late refills on the split build.

## Code review
The 640x480 profile is Packed4. `encodePacked4Row` already uses one aligned
32-bit source load per 8 pixels, a pair LUT and writes in physical pixel order
(no `applyDmaPixelOrder` second pass). The Rgb222 per-pixel path is not used at
this width. No further encoder gain is available without changing the
framebuffer format, so no encoder change was made.

## Change
Diagnostic env `olimex_esp32_sbc_fabgl_psram_dma_display_640x480_present_split_iram_test`
(`-DQL32_OLIMEX_DMA_REFILL_IRAM_TEST`) puts `refillWorker`,
`refillCompletedRows`, `refillRow` and `encodePacked4Row` in IRAM
(`QL32_REFILL_HOT`). Other builds are unchanged.

## Hardware results (split cores + IRAM, owner present)
| Run | Late share | avg us/row | max batch us | Previous baseline |
|---|---|---|---|---|
| idle 20 s | 14.3% | 37.8 | 9283 | 16% / 38.1 |
| CPU-only 20 s | 67.3% | 100.8 | 11140 | 62-66% / 98 |
| attractor 30 s | 46.8% | 63.4 | 27712 | 46% |

Standard idle (earlier): 0% late, 33.9 us/row.

## Conclusion
IRAM placement gives no measurable change. Instruction fetch is not the
limiter; the cost is data-side: PSRAM reads of the source row and contention
with the other core for the shared external-memory path (CPU-only load nearly
triples us/row). The 25 us/row target is not met and not reachable by code
placement alone.

## Status
- Built: iram env, plus the standard present_profile.
- Host-tested: no new portable logic; ctest unchanged (80/83, 3 known failures).
- Hardware-verified: numbers above. Owner confirmed stable readable picture
  on the reflashed standard present_profile. Split+IRAM picture was not
  separately confirmed.

## Proposed step 6
Test the PSRAM-read hypothesis directly: copy each source row group with a
single bulk read (or DMA/prefetch next row into internal RAM from the
presenter side) and measure us/row; alternatively shrink the source stream
(keep a hot internal-RAM copy of dirty/active rows) and compare to the
standard 33.9 us/row. If bandwidth-bound, accept standard placement for
640x480 and keep split only for 320x200.
