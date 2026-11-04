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
`max(2048 bytes, 2 × idle sample spread)`. Since task-result records are retained
by the scheduler, per-run drift from the initial idle baseline is reported and
consistent declines raise a warning. Array
and combined workers must show at least an 8 KiB active heap decrease; channel,
graphics, and debugger workers must show at least 2 KiB for their active task
state. Graphics results are diagnostic for additional canvas heap deltas
because the CYD graphics service reserves its arena at startup; the graphics
worker explicitly calls `CLOSEWINDOW` and records the post-close sample.
Free-block-count trends are reported as warnings, since the allocator may split
or coalesce blocks independently of total free heap.

After the combined workload, the runner issues `REBOOT`, gathers another
warm-up and baseline set, and compares it with the initial baseline. A passing
run means the REPL stayed responsive, workers were terminated, required
workloads showed their expected allocation behavior, each cleanup recovered
within tolerance, and the fresh-boot heap returned within tolerance. No fixed
heap total is assumed.
