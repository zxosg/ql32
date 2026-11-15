# Olimex startup window lifetime — 2026-11-15

## Root cause

Olimex loads `FFS:/startup.bas` into the long-lived interactive supervisor.
The source opens a graphics endpoint with `OPEN(4)`, which initially returns
handle `#2`, then configures the right pane. When that startup source completed,
the VM closed channels opened during the source and the scheduler called
`releaseGraphicsResources()` on the supervisor. The red pane therefore
appeared while startup ran and disappeared as the supervisor returned to its
prompt. Later console output recreated default graphics surfaces, making the
window layout appear to reset.

The local startup program also hardcoded `#2`, even though `OPEN(4)` returns a
session handle that can differ when the startup program is rerun.

## Fix

- Interactive sessions now retain opened channels and graphics windows across
  program completion and subsequent REPL commands. Explicit `CLOSE` and session
  teardown still release them.
- The supervisor no longer tears down graphics after its startup source
  completes.
- `data/startup.bas` uses `rightChannel`, the value returned by `OPEN(4)`, for
  the right pane's `WINDOW`, `PAPER`, `INK`, and `CHANNEL` commands.

## Verification

- Added a scheduler regression test covering the two panes, the bottom console,
  the open-channel count, and a later `CLS #2` command from the REPL.
- The complete scheduler test executable passed on the host.
- Olimex PSRAM firmware built and flashed successfully.
- Backed up the original board startup file to
  `/tmp/olimex-startup-before.bas`; it matched the repository source except for
  the hardcoded channel references.
- Uploaded the handle-safe startup file to `FFS:/startup.bas` and rebooted.
  Serial boot output confirmed `Running FFS:/startup.bas` and clean return to
  the REPL. `GETCHANNEL(0)` returned `3` (reserved channels `#0/#1` plus the
  opened window endpoint), and `CLS #2` succeeded afterward without reducing
  the count.
- The serial port was closed after testing. VGA visual confirmation of the
  blue pane, red pane, and bottom console remains pending.
