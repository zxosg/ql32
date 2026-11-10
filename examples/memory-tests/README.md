# CYD heap acceptance test

This hardware test drives the `esp32_2432s028` serial REPL and records
`FREE(0)`, `FREE(1)`, and `FREE(2)` at repeatable checkpoints. The BASIC
workers cover session arrays, an open FFS file channel, retained graphics,
debugger state, and a combined workload. Each worker stays alive long enough
for the runner to sample it; the runner then kills it and checks recovery.

Build and flash the CYD SPIFFS profile (`esp32_2432s028_spiffs`) first and
connect the board's USB serial port. Install the serial dependency if needed:

```sh
python3 -m pip install pyserial
```

Run the hardware suite from the repository root:

```sh
python3 tools/ql32-memory-test.py /dev/cu.usbserial-XXXX
```

The runner sends the BASIC fixtures to the FFS root using the existing YMODEM
`RECV` flow. It takes three warm-up readings and five baseline readings, then
runs each workload five times. The JSON report defaults to
`memory-test-report.json`; override it with `--report`. The image revision
defaults to the current Git revision, with `-dirty` appended when the working
tree has changes; pass `--firmware-revision` to identify a different flashed
image. Use `--skip-upload` only when the matching fixtures are already on the
FFS.

Recovery is checked against each case's pre-run sample, using a tolerance of
`max(2048 bytes, 2 × idle sample spread)`. The v2 report samples heap after
cancelling the worker (`after_cancel`), after a second `KILL` removes its
terminal task record (`after_reap`), and after temporary-file cleanup (`after`).
`task_record_recovery_bytes` is the net total-heap recovery between the first
two checkpoints, including any telemetry/allocator effects. Recovery checks
and cumulative drift use the final sample, with no worker result retained.
Consistent declines raise a warning. Array
and combined workers must show at least an 8 KiB active heap decrease; channel,
graphics, and debugger workers must show at least 2 KiB for their active task
state. Graphics results are diagnostic for additional canvas heap deltas
because canvas and session state have separate lifetimes. The canvas arena
allocates pixel buffers per canvas and releases them on close; the graphics
worker explicitly calls `CLOSEWINDOW` and records the post-close sample.
Free-block-count trends are reported as warnings, since the allocator may split
or coalesce blocks independently of total free heap.

After the combined workload, the runner issues `REBOOT`, gathers another
warm-up and baseline set, and compares it with the initial baseline. A passing
run means the REPL stayed responsive, workers were terminated, required
workloads showed their expected allocation behavior, each cleanup recovered
within tolerance, and the fresh-boot heap returned within tolerance. No fixed
heap total is assumed.

The 2026-11-05 CYD run passed those checks across 25 worker runs. Total free
heap declined by 5,796 bytes over the full sequence before reboot, while the
largest free block stayed at 51,188 bytes. Reboot returned total free heap to
158,696 bytes against the initial 158,752-byte median. This cumulative
pre-reboot drift is reported for review but is not currently a suite failure;
the run does not isolate retained task-result allocations from other small
persistent allocations.

That historical report uses the v1 procedure, which retained cancelled task
records. Do not directly compare its cumulative drift with a v2 run. A v2
hardware result is recorded in
[the 2026-11-07 report](../../docs/test-results/CYD-memory-2026-11-07.md):
25 worker runs passed, final pre-reboot drift was 1,816 bytes (within the
2,048-byte tolerance), and the largest block stayed at 110,580 bytes. The
flashed image revision was unverified. The separate
[100-cycle resize soak](../../docs/test-results/CYD-canvas-resize-2026-11-06.md)
showed stable post-cycle readings, but does not establish worker cleanup.

Host checks for runner checkpoint order and residual-drift detection:

```sh
python3 tests/memory_runner_tests.py
```
