# GUPDATE interaction reset — 2026-11-14

## Behavior

When a BASIC program returns to its console, or starts a dialog, menu, pager
prompt, or keyboard `INPUT`, QL/32 restores automatic presentation, sets
`GUPDATE SKIP,0`, and presents the current composed screen. The restored state
persists after the UI interaction until BASIC changes it again.

## Verification

- Linux `ql32_graphics_tests` and `ql32_scheduler_tests` passed.
- The Olimex PSRAM double-buffer profile built and flashed successfully.
- A BASIC test program set `GUPDATE 0` and `GUPDATE SKIP,3` before an alert, then
  repeated that state before keyboard input. The owner confirmed the display
  updated normally through both interactions and after returning to the REPL.

The source is retained at
[`examples/gupdate-interaction-reset-test.bas`](../../examples/gupdate-interaction-reset-test.bas)
and the device copy is `FFS:/gupdreset.bas`.
