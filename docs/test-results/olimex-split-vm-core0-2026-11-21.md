# Olimex option 5: VM on core 0, refill on core 1 (2026-11-21)

Env: `..._640x480_present_split_coalesce_test` = existing split placement
(`ARDUINO_RUNNING_CORE=0`, refill task + EOF ISR on core 1) with the faster
encoder (e7f8780) and present counters; no new pinning. Compared against the
standard-placement numbers in `olimex-present-coalescing-2026-11-20.md`.

## Results, 640x480

| | Standard (VM+refill core 1) | Split (VM core 0) |
|---|---|---|
| refill us/row (attractor) | 28.1 | 45.8 |
| idle late refills | 0 | 0 (15 s window; 44 late since boot) |
| attractor late share | 0% (0/22,000) | 26.7% (5,888/22,081) |
| present requests per 30 s | ~102 | 160 (1.6x) |
| full / partial presents | 23 / 79 | 81 / 79 |
| picture | stable (owner) | visible but flickering / out of sync (owner) |

## Interpretation

- Moving the VM off the refill core gives the VM ~1.6x more attractor frames
  but costs visible instability: the refill row cost rises 28 -> 46 us because
  core 0 PSRAM traffic (VM + presenter writes) contends with the refill's
  PSRAM reads, and 27% of the attractor's refills miss their deadline.
  Consistent with the earlier split results (PSRAM contention, not scheduling).
- The cheaper encoder did not remove the split instability; only the idle
  state is clean now (0% late in the measured window, was 14-16%).
- Not measured: 640x400 split env (built, not flashed, after the flicker
  result), BASIC loop timing (no timer), WiFi/audio core-0 contention
  (BEEP/music is not in this branch; its task is unpinned, priority 3).

## Status

- Built: both split coalesce envs (640x480, 640x400).
- Host-tested: n/a (placement/config only).
- Hardware-verified: 640x480 split idle and attractor as above. The board
  was reflashed to the standard present_profile; picture owner-confirmed
  stable and readable.

## Decision

Keep the standard placement as the default. Split placement is not
acceptable at 640x480 (27% late, flicker) despite 1.6x VM throughput.
Next candidate: reduce the PSRAM traffic of the VM/presenter itself
(cheaper composition, dirty-rect GUPDATE for CLS-redraw programs) rather than
changing core placement.
