# Tenstar ESP32-S3 hardware test, 2026-11-28

Board: Tenstar (ESP32-S3, 240x135 ST7789, QSPI PSRAM, USB-CDC), firmware `ql32_tenstar_esp32s3_diag`
(= `ql32_tenstar_esp32s3` + `QL32_HEAP_DIAGNOSTICS`, `QL32_COMPACT_CODE` on, 240 MHz). Port `/dev/cu.usbmodem11401`.
Not tested: audio and SD (the profile reports none), keyboard beyond USB serial, WiFi/`NETTIME$`.

## Results

| Check | Result |
|---|---|
| Boot, REPL over USB-CDC, debug bar, `SCREEN` capture | pass |
| Startup splash (`examples/startup/startup.bas`, `FFS:/startup.bas`) | pass after fix: the default graphics window was the full 240x135 so the platform line sat under the 40 px console. Window 1 now excludes the console strip; all lines centred. |
| FFS (512 KB SPIFFS): MKDIR, COPY, overwrite refusal, COPY FORCE, MOVE, wildcard DELETE FORCE, RMDIR | pass |
| Golden regression (`tools/ql32-golden-device.py`) | 16/16 pass (one transient "device did not become ready for YMODEM" on the first run; the rerun passed). The tool leaves its tasks in WAITING_INPUT. |
| 1 to 6 concurrent tasks (mandelbrot, sincos grid, plasma, arrays, fin, mandelbrot) | all ran, no reset, no failedalloc |
| 3 minute soak, 4 tasks | stable; heap flat; free after kill within 0.5 KB of before |
| PSRAM | present, 2.07 MB free; only allocations of 16 KB or more go there (default malloc threshold). The small-block working heap is internal RAM. |

## Memory (HEAP STATS; "free/largest" is the combined heap, internal = `ifree/ilargest`)

| Scenario | CYD free/largest | Tenstar free/largest (combined) | Tenstar internal free/largest |
|---|---|---|---|
| idle | 176K / 110K | 2,372K / 2,064K | 297K / 270K |
| 1 task | 165K / 110K | 2,361K / 2,064K | 286K / 270K |
| 2 tasks | 152K / 110K | 2,348K / 2,064K | 278K / 262K |
| 3 tasks | 140K / 110K | 2,337K / 2,064K | 267K / 254K |
| 4 tasks | 130K / 110K | 2,327K / 2,064K | 257K / 246K |
| 6 tasks | 108K / 102K | 2,315K / 2,064K | 245K / 229K |
| after killing all | about 175K / 110K | 2,371K / 2,064K | 296K / 262K |

The Tenstar working heap is about 1.7x the CYD's free and 2.5x its largest block. The per-task cost is about
10 KB internal, the same as on the CYD.

## Speed (`tools`-independent benchmark, 240 MHz, compact bytecode, debug bar on or off made no difference)

| Workload | Tenstar |
|---|---|
| Mandelbrot 40x28x24 (float) | 9.2 s |
| 100k-iteration integer loop | 10.9 s |
| 40k array stores | 3.8 s |
| 5k string concatenations | 6.4 s |

No CYD baseline was measured in this session (the CYD was not connected); the benchmark is
`examples/stress/bench.bas` for a rerun. Speed is low, about 9k loop iterations per second. Both boards run
the same `loop()`: one 128-instruction scheduler slice followed by `delay(1)`. The delay alone is a large
share of each slice, so batching several slices per `loop()` is the obvious follow-up (not changed here).

## Changes made

- Tenstar default graphics window 1 is the screen above the 40 px console.
- `HEAP STATS` also reports `ifree`, `ilargest`, `psfree` and `pslargest`.
- New env `ql32_tenstar_esp32s3_diag` for these measurements.
