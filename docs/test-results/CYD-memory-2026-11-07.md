# CYD worker memory acceptance — 2026-11-07

The v2 memory runner completed five repetitions each of array, channel,
graphics, debugger, and combined workloads (25 worker runs) through
`/dev/ttyUSB0` (CH340 USB serial, VID:PID 1a86:7523). Fixtures were uploaded
through YMODEM. The board was rebooted before and after the workloads.
The existing flashed image was tested; its revision was not independently
verified. The report's profile name is the runner's configured label, not
firmware identification.

Command:

```sh
python3 tools/ql32-memory-test.py /dev/ttyUSB0 \
  --firmware-revision connected-image-unverified \
  --report docs/test-results/CYD-memory-2026-11-07.json
```

Result: **PASS**, no trend warnings. Every repetition passed total-free and
largest-block recovery within the 2,048-byte tolerance. Worker allocation and
explicit channel/window release checks passed. Each repetition sampled after
cancellation, after removal of the terminal task record with a second `KILL`,
and after temporary-file cleanup.

| Checkpoint | Total free bytes | Largest free block bytes | Free blocks |
| --- | ---: | ---: | ---: |
| Initial warmed baseline median | 222,496 | 110,580 | 16 |
| After array workload | 220,948 | 110,580 | 22 |
| After channel workload | 220,904 | 110,580 | 21 |
| After graphics workload | 220,164 | 110,580 | 23 |
| After debugger workload | 220,176 | 110,580 | 24 |
| After combined workload | 220,680 | 110,580 | 22 |
| Rebooted warmed baseline median | 222,436 | 110,580 | 17 |

The final pre-reboot total was 1,816 bytes below the initial baseline, within
tolerance. The largest block remained unchanged at all reported workload
checkpoints. Rebooted baseline was 60 bytes below the initial baseline.

Net total recovery between cancellation and record removal ranged from -304
to +752 bytes. These short-interval readings include telemetry and allocator
activity; they cannot identify an exact per-record allocation cost. This run
removes retained result accumulation from the workload recovery checks, but
does not prove that the historical 5,796-byte decline was entirely task records.
The unverified image revision and different initial heap also prevent direct
attribution of differences from that historical run.

This is bounded cleanup acceptance, not proof of absence of all leaks or a
qualification of the maximum concurrent-worker/compiler memory envelope.
The runner leaves the board rebooted with a supervisor serial telemetry channel
open and no test worker tasks running.

Raw measurements: [CYD-memory-2026-11-07.json](CYD-memory-2026-11-07.json).
