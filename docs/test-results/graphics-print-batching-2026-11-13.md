# Olimex PRINT batching check — 2026-11-13

## Change under test

The VM brackets each BASIC `PRINT` statement with a graphics output batch.
`WindowedService` accumulates the affected screen rectangle and presents it
when the statement ends. Trailing separators close the batch without adding a
newline. Runtime error cleanup flushes a partially written statement.

## Host checks

- Olimex profile build: `pio run -e olimex_esp32_sbc_fabgl_psram_double_buffer_test`
  passed (54,748 bytes internal RAM, 1,758,237 bytes flash).
- `ql32_graphics_tests`, `ql32_runtime_tests`, and `ql32_scheduler_tests` built
  and passed.
- The all-target CMake build stopped at the pre-existing
  `ql32_fabgl_graphics_tests` fake-header mismatch: the fake FabGL API does not
  provide the `VGAController`, `RGBA8888`, `RGB222`, or `Canvas` methods used by
  the current frame presenter. The Olimex firmware build is unaffected.

## Board measurement

Board: Olimex ESP32-SBC-FabGL, PSRAM double-buffer test profile, 320×200 VGA,
240 MHz. Fixture: `examples/graphics-print-present-benchmark.bas`, already
stored on the board as `FFS:/printbench.bas`. Each pass prints 20 no-scroll
lines, with three repetitions per mode.

| Mode | Pass times | Median | Compared with baseline |
|---|---:|---:|---:|
| `GUPDATE 1` per PRINT | 0.853, 0.853, 0.853 s | 0.853 s | 2.0× faster than 1.708 s |
| `GUPDATE 0`, then one `GUPDATE` | about 0.05–0.10 s | not precise | Serial polling interval limits timing resolution |

The run emitted all start/done markers and returned to the REPL without a
reset or reported BASIC error. The screen was not captured for independent
visual inspection in this run.

A second fixture, `examples/graphics-print-scroll-benchmark.bas`, printed 40
lines with `GUPDATE 1`, causing multiple text-canvas scrolls. It completed in
1.706 s and returned to the REPL without a reported BASIC error or reset. The
screen was not captured for independent visual inspection.

## Remaining qualification

- The original run above did not include a visual check. On 2026-11-14 the
  benchmark was repeated on the flashed Olimex double-buffer profile with
  live serial timing capture. `GUPDATE 1` took 0.855, 0.855, and 0.856 s;
  batched output took 0.097, 0.096, and 0.096 s. The owner confirmed VGA text
  was stable and readable.
- Re-run the full CMake test suite after aligning the FabGL test fake with the
  current platform API.
