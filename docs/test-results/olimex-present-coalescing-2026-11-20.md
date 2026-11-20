# Olimex present coalescing and encoder A/B (2026-11-20)

Standard placement (VM, refill worker on core 1). Attractor = `examples/attractor.bas`
from FFS, 30 s window after `PRESRESET`, board reset between runs.

## What was added

- `WindowedService::setPresentCoalescing(minIntervalUs, costRatio, clockUs)`:
  region presents and explicit `GUPDATE` are merged into one pending dirty
  rectangle and published when `max(minIntervalUs, lastPresentUs * costRatio)`
  has passed since the last present ended. `pollPendingPresentation()` is
  called from the Arduino `loop()` so a deferred tail is always shown;
  `flushTextOutputBatches` (program end/error) flushes it too.
- Diagnostic env `..._640x480_present_coalesce_test` and
  `..._640x400_present_coalesce_test` (`-DQL32_OLIMEX_PRESENT_COALESCE`):
  default 40 ms / ratio 1, runtime `PRESRATE <min_us> <ratio>` (`PRESRATE 0 0`
  = off), counters appended to `PRESDIAG` ("present coalesce:").
- A/B env `..._present_legacy_encoder_test` (`-DQL32_OLIMEX_LEGACY_ENCODER`).
- Standard `present_profile` is unchanged (coalescing is not enabled there).

## A/B: encoder fix, same session, 640x480 attractor

| Build | refill us/row | late refills | PRESDIAG window |
|---|---|---|---|
| legacy encoder | 33.9 | 0 | 6 calls (1 full, max 3.6 s), 4.7 s presenting |
| new encoder | 28.1 | 0 | 103 calls (24 full, avg 0.12 s, max 0.5 s), 12.3 s presenting |

The new encoder cuts a full present from up to ~3.6-4.2 s to 0.5 s and the
refill row cost by 17%. The attractor frame count rose from a handful to
~100 present requests per 30 s. (One legacy run per config; the second legacy
run had no usable pre-run DMADIAG sample, so its late share is cumulative: 0.)

## Coalescing, new encoder (requests = VM frames/draws that asked to present)

| Mode | Rate | requests | presents | full | present time |
|---|---|---|---|---|---|
| 640x480 | none (1 0) | 102 | 102 | 23 | 11.8 s |
| 640x480 | 40 ms + 1x cost | 100 | 54 | 23 | 11.5 s |
| 640x480 | 40 ms only | 102 | 57 | 23 | 11.6 s |
| 640x400 | none | 103 | 103 | 25 | 9.8 s |
| 640x400 | 40 ms + 1x cost | 104 | 58 | 25 | 9.5 s |

Late refills 0 in every run (22k-26k EOF events each); refill 28.1 us/row.
Full present: 0.50 s at 640x480, 0.38 s at 640x400.

## Interpretation

- Coalescing halves the number of presents but changes neither the present time
  nor the VM's frame/request count. The ~23 full-frame presents (one per
  `GUPDATE`, ~0.5 s each) are kept because the rate limit is not stricter than
  the draw rate (~3 requests/s). The VM is not limited by present *count*.
- ~11.5 s of every ~33 s is present time inside the VM task; the remaining
  ~21 s goes to VM execution of ~91 `PLOT`s/frame (about 3 frames/s, i.e. a few
  hundred plots/s) on a core where the refill worker uses ~80% (28.1 us x
  28,800 rows/s). So the VM is CPU-starved by refill on core 1, which presenter
  coalescing cannot fix.
- The attractor's animation rate is bounded by the full-frame present cost
  (0.5 s/0.38 s) plus VM time; 640x400 is only ~25% cheaper than 640x480.
- Not measured: FabGL 400x300 attractor frames/s; PRINT-loop wall time (no
  BASIC timer exists, and PRINT output is not visible on serial).

## Status

- Built: coalesce 480/400, legacy encoder, standard present_profile.
- Host-tested: `testWindowedGraphicsCoalescesPresentationsByTimeAndCost`
  (interval, merge, poll, flush, counters, explicit `GUPDATE`). ctest: 80/83
  plus the new assertions; only the 3 known failures (scheduler, channel,
  hardware).
- Hardware-verified: runs above; board left on the standard present_profile,
  picture owner-confirmed stable and readable.

## Next (step proposal, not started)

Move the VM off the refill core (VM on core 0, refill/ISR stay on core 1 at
standard placement) or cap refill duty; the earlier split-core builds showed
the late-refill risk, so this needs the PSRAM-contention hypothesis tested
again with the cheaper encoder (28 us/row vs the ~34 us measured then).
Present coalescing should stay off in the standard profile until it helps a
measured workload (e.g. a PRINT-heavy loop with a timing source).
