# Olimex 640x480 smaller batches + larger ring (step 7) - 2026-11-20

## Hypothesis
With bulk row copy (step 6), a 160-row ring refilled in 20-row batches (140 rows
of lead, a stall costs fewer rows) lowers late refills further.

## Change
Diagnostic env `..._present_split_bulk_g20_test` (`QL32_OLIMEX_DMA_RING160_GROUP20_TEST`):
ring 160 rows (102 KB internal DMA RAM, static RAM 17.7%), group 20. Other
builds unchanged. No new portable logic, so no new host test.

## Hardware results (split cores, bulk copy, one run set)
| Run | Late / EOF | us/row | max batch us | Step 6 (ring 120, group 60) |
|---|---|---|---|---|
| idle 20 s | 24.2% (9082 / 37458) | 36.6 | 8746 | 6.4% (805 / 12485) |
| CPU-only 20 s | 94.5% | 87.2 | 19893 | 61.5% |
| attractor 30 s | 58.4% | 41.5 | 58553 | 19.4% |

EOF events triple (20-row groups), so shares are per-event; absolute late
counts are also much worse (idle 9082 vs 805).

## Conclusion
Hypothesis rejected. Smaller batches add ISR/wake overhead on core 1 and give
more deadline checks without reducing the per-row stall; the extra lead did not
help. Idle is nowhere near zero, so per instructions I stopped, reflashed the
standard present_profile and did not iterate. Picture on this build was not
separately recorded; the owner confirmed a stable, readable picture on the
reflashed standard build.

## Status
- Built: g20 env, standard present_profile.
- Host-tested: nothing new; ctest unchanged (80/83, 3 known failures).
- Hardware-verified: numbers above.

## Proposed step 8 (not started)
Return to ring 120/group 60 + bulk copy (best so far: idle 6.4%, attractor
19.4%). Try larger groups (e.g. 120-row ring with 2 x 60 unchanged, or ring 240
if internal RAM allows) rather than smaller; and the deferred items: async
DMA row prefetch, and moving work off core 1 (e.g. VM/presenter bus traffic
throttling during refill windows). Otherwise accept standard placement for
640x480.
