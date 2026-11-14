# GUPDATE SKIP — Olimex check, 2026-11-13

## Implementation and host checks

`GUPDATE SKIP,n` sets the number of automatic or explicit presentation
requests to skip after each displayed frame. It is independent of elapsed time
and the VGA refresh rate. Retained drawing continues during skipped requests;
the next presented frame contains the latest canvas state. `GUPDATE 0/1` and
bare `GUPDATE` keep their existing mode behavior, with requests subject to the
configured skip count.

The Olimex PSRAM double-buffer profile built and flashed. The Linux graphics,
runtime, and scheduler tests passed. The REPL accepted `GUPDATE SKIP,1` and
`GUPDATE SKIP,0` without errors.

## Attractor check

- With `GUPDATE SKIP,1`, a single attractor worker ran as task 6. The owner
  reported blinking pixels with no other artifacts.
- With `GUPDATE SKIP,0`, the control also showed blinking pixels.
- Task 6 was stopped and `GUPDATE SKIP,0` restored at the REPL.

The display remained usable in both runs. The blinking appears in the control
as well, so this check found no new artifact specific to SKIP 1. This is a
visual smoke check; it does not measure the exact rate of presentation skips.

## Attractor retest after GUPDATE 0 fix

The attractor initially appeared to flicker while two runnable attractor
workers were drawing to the shared display. Both test workers were stopped,
then one attractor worker was started alone. The owner confirmed the isolated
display was stable. The single test worker was stopped after confirmation.
