# Tenstar follow-ups — 2026-11-23

Board: `ql32_tenstar_esp32s3` on `/dev/ttyACM0` (Olimex `/dev/ttyUSB0` and CYD
`/dev/ttyUSB1` untouched except the Olimex golden verification). Follow-up to
[tenstar-full-test-2026-11-23.md](tenstar-full-test-2026-11-23.md).

## Status

| Item | Built | Host-tested | Hardware |
| --- | --- | --- | --- |
| 1 FFS "path names a directory" for missing files | yes | yes (`esp32_storage_tests`) | Tenstar |
| 2 Memory suite largest-block failure | yes (tool) | n/a | Tenstar: suite passes |
| 2b Tenstar USB-CDC prompt stall (found while testing) | yes | n/a | Tenstar: 0 stalls in 600 commands (was ~1 in 80) |
| 3 Golden `print_separators` / `block_if` | yes | yes (`ql32_channel_tests`) | Olimex 16/16, Tenstar 16/16 |
| 4 Touch / audio / MODE | n/a | n/a | N/A on Tenstar (no hardware / display-mode service) |
| 5 Remote GUI debugger on Tenstar | no change needed | 18 tool tests pass | Tenstar: protocol + `SerialWorker` verified |
| 6 Heavy programs, 1,200-statement heap | n/a | n/a | Tenstar measured |

## 1. `FFS:/arrwrk.bas` "directory"

There never was a directory. Two causes:

* `examples/memory-tests/canvas-resize-workers.bas` hard-coded the legacy
  `FFS:/arrwrk.bas` and `FFS:/chanwrk.bas` worker names from the 2026-11-06
  CYD run; nothing in the repo uploads them. It now starts the
  `ql32_mem_array.bas` / `ql32_mem_channel.bas` fixtures that
  `tools/ql32-memory-test.py` uploads (documented in the file header).
* Arduino SPIFFS `open()` of a missing name returns a childless "directory"
  handle, so every missing FFS file produced the misleading
  `path names a directory`. `SdStorage` and `SdChannels` now treat a childless
  directory handle on the `FFS` drive as not found
  (`file not found`, `channel: cannot open file`). Verified on the Tenstar for
  `TASK`, `LOAD`, `OPEN`. Rerun of `canvas-resize-workers.bas` starts both
  workers and runs the canvas cycle (`CANVAS_HEAP` lines).

## 2. Memory suite (largest block)

Data: with PSRAM the first TASK/KILL cycle moves the largest free block from
2,064,372 B to 2,031,604 B (one 32,768 B step); total free recovers to within
~140 B, and the value then stays constant for every later cycle and across
programs (it is also what `CANVAS_HEAP|before-allocation` shows). There is no
leak and no growth: it is a one-time allocator layout step (a 32 KB
allocation made during first task creation stays in the large PSRAM region
while an equal block is released elsewhere). The CYD has no PSRAM and
therefore never shows it.

Fix (test side, no firmware change): `tools/ql32-memory-test.py` runs one
throwaway worker start/cancel/reap before taking the idle baseline so the
baseline reflects the settled layout. Result: all cases (array, channel,
graphics, debugger, combined, load, restart) pass on the Tenstar.

## 2b. USB-CDC prompt stall

During the suite roughly one command in 80 timed out waiting for `> `; the
prompt appeared only after the next byte was sent. Native USB CDC holds a
short TX tail until the next write. `Serial.flush()` after printing the prompt
(Tenstar `main.cpp`) removed it: 0 stalls in 600 consecutive commands with a
busy worker running. Also made the harness DEBUG/STEP/CONTINUE sequence wait
0.5 s after `STEP` (STEP re-pauses asynchronously).

## 3. Golden programs

* `print_separators`: on any routed channel (serial, file) the VM did not
  track the print column, so `!` printed nothing at column 0 and `,` always
  padded 8 spaces. The VM now tracks the column per routed channel, so output
  equals the host golden (`A B`, `A       B`, `C D`). Host test
  `testPrintSeparatorsOnRoutedChannel`.
* `block_if`: not a firmware bug. Multi-line blocks cannot be typed into the
  REPL, but the unnumbered program runs correctly as a file. New reusable
  runner `tools/ql32-golden-device.py <port> [--drive FFS:]` uploads each
  portable golden (17 exit-0 programs minus `abbreviations`, which lists
  numbered lines and is interactive) over YMODEM, runs it with `TASK()` and
  compares with `tests/cli/*.out` exactly.
* Results: Olimex (`olimex_esp32_sbc_retro`) 16/16; Tenstar 16/16.

## 4. N/A on the Tenstar

Touch (no touch controller), audio (`MUSIC`/`SOUND`, no codec/buzzer) and
`MODE` (fixed 240×135 panel, "display mode service unavailable") are not
available and are recorded as N/A in `QL32-PLAT-001.md` and the handoff.

## 5. Remote GUI debugger

The firmware side (`remote_debug_protocol.cpp`) is platform-neutral and
already wired into the Tenstar. With a PySide6 venv: `hello` returns
`TENSTAR-ESP32-S3`, `supported: true`, screen capture and `FFS:/` root.
Through the repo's `SerialWorker` (offscreen Qt) `tasks.list`, `files.list`,
`task.start`, `debug.attach/inspect/resume(step)/pause` all returned correct
responses; direct protocol runs also covered `eval`, `watch`, `diagnostics`,
`reset`, `task.viewport`. The 18 debugger tool tests pass. Not exercised:
visual use of the full GUI window (needs eyes).

## 6. Heavy programs and heap (Tenstar vs Olimex)

Tool: `tools/ql32-olimex-bytecode-memory.py LABEL OUT.json --port=/dev/ttyACM0
--no-mode` (now port-configurable; `--no-mode` skips MODE sections).

| Measure | Tenstar | Olimex (stage3) |
| --- | ---: | ---: |
| Idle free / largest | 2,372,011 / 2,064,372 | 4,152,523 / 3,997,684 |
| 1,200-statement program, heap used | **269,672 B** | 269,100 B (271,652 B in the 2026-11-22 note) |
| `DIM a(60000)` free after | 1,888,291 | 3,669,167 |
| `DIM a(150000)` free after | 1,168,019 | 2,949,123 |
| `DIM a(450000)` | "array exceeds available memory" (3.6 MB > 2.37 MB) | allocates |
| `DIM a(1000001)` | "array allocation limit exceeded" | same |
| String array 2,000 elements | OK | OK |
| After KILL | total back within ~700 B | within ~200 B |

Bytecode cost per statement matches the Olimex (~225 B/statement), as
expected from the shared VM. The 17 golden programs also pass inside the
measurement run (except interactive `abbreviations`). Static size: Tenstar
1,652,257 B flash (46.7 %) / 50,704 B RAM; Olimex retro 1,966,369 B /
57,260 B.

## CYD regression

CYD (`esp32_2432s028_spiffs`, `/dev/ttyUSB1`) reflashed with this source (flash 1,804,189 B, RAM 52,172 B): 16/16 goldens match. Other builds OK: fabgl_psram (1,966,005 B / 55,060 B), lilygo_vga32 (1,770,125 B / 54,280 B). CTest 89/89.
