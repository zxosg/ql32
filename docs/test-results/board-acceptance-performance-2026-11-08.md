# Board acceptance and performance record — 2026-11-08

This record captures the Linux pager-test fix, the requested CYD and Tenstar
retests, explicit user-directed skips, and the incomplete performance attempt.
“Passed” means the listed serial/build check passed or the user confirmed the
physical display. Skipped checks are not considered passed.

## Linux scheduler pager

- **Build and tests:** Native Linux CMake build succeeded; full CTest passed
  **65/65**. `ql32_scheduler_tests` also passed in isolation.
- **Correction:** The assertion expected lowercase `Enter=page`; the pager's
  current text is `Enter=Page, L=Line, Q=Quit`. The test now matches the prompt.
- **Contract retained:** The scheduler test still checks that pagination
  suspends only its session while another task continues.

## CYD — ESP32-2432S028 SPIFFS

- **Connection/profile:** `/dev/cu.usbserial-11440`, MAC `04:b2:47:9c:f0:ec`;
  current SPIFFS firmware flashed. Existing FFS contents remained available.
- **Boot and storage:** Boot/REPL and FFS checks passed; recent string/shift
  examples and file round-trip checks returned expected output.
- **Graphics:** The user confirmed the four-color filled rectangle pattern at
  0°, 90°, 180°, and 270°. Initial clipping came from testing on console
  channel `#0`, whose overlay occupies part of the screen, and from incorrect
  test rectangle arguments. The corrected test used graphics channel `#1`
  within the area above the console overlay. Rotation and `OVER` set/clear/XOR
  behavior were visually checked.
- **Dialogs/messages/tasks:** `IPRINT` appeared; `BUSY` was exercised in a
  running program with a pause; a bottom-right dialog was resumed with Enter.
  Task start/list/suspend/resume/kill cleanup was exercised for worker tasks.
- **Known storage artifact:** An empty directory named
  `FFS:/ql32-task-smoke.bas` remains from a previous temporary transfer. The
  BASIC filesystem interface has no `RMDIR`; the filesystem was not formatted.

## Tenstar — TS-ESP32-S3

- **Connection/profile:** MAC `3c:0f:02:df:e9:40`, 4 MiB flash and 2 MiB
  PSRAM; firmware built and flashed over `/dev/cu.usbmodem114401`. SPIFFS was
  preserved. REPL reported 240×135, and the FFS root retained its original
  files.
- **Rotation regression:** Added a host integration test for repeated
  orientation changes. `rotationChanged_` previously ran before the window
  manager geometry had been updated; the presenter now notifies after rotation
  has been applied. Linux tests passed, and the user confirmed the four-color
  pattern at 90°, 180°, 270°, and 0° after flashing the fix.
- **Wi-Fi:** Skipped at the user's request. The last status query reported
  disconnected; no credentials were entered.
- **Mandelbrot timing:** A bounded 80×56 workload completed once in 33.929 s.
  Later attempts exceeded the observation window or required prompt recovery,
  so this is not a reliable benchmark or median.
- **Attractor benchmark:** After correcting the launch method, the finite
  attractor ran as a task and emitted both serial markers. The user observed
  it on the display. One run took **26.681 s** with the source requesting
  SINGLE precision and 80 MHz. Actual CPU frequency was not read back, and
  this is one sample, not a median.
- **Launch error captured:** `LOAD` then `RUN` on this module file produced
  `BASIC session 1 VM error: line 3, column 1: expected assignment or PRINT`.
  A subsequent attempt to use `TASK(...)` as a bare statement also failed
  because `TASK` returns a value. Assigning the task ID,
  `taskId=TASK("FFS:/attractor-benchmark.bas")`, ran the module successfully.
  The screenshot captured through YMODEM is
  [`tenstar-error-current.png`](/private/tmp/tenstar-error-current.png).

## User-directed skips

- **Tenstar:** Wi-Fi connection/status/disconnection check skipped.
- **Olimex:** Latest physical acceptance session skipped, including PS/2
  shortcuts, clock, Wi-Fi, line UI, startup/storage, and task/window checks.
  Earlier dated Olimex checks remain valid but do not cover this plan's full
  checklist.
- **LILYGO:** Physical-board identity and all physical acceptance checks
  skipped. The profile has build coverage only.

## Performance attempt and YMODEM diagnosis

The requested matrix is SINGLE and DOUBLE at 80, 160, and 240 MHz, three runs
per configuration, on CYD, Tenstar, Olimex, and LILYGO, with actual CPU
frequency readback. No complete matrix was produced. The source artifact is
[`examples/attractor-benchmark.bas`](../../examples/attractor-benchmark.bas).

A temporary CPU-frequency readback diagnostic was built for the Tenstar
attempt, then removed. The YMODEM cancellation root cause was investigated on
2026-11-08:

- Before the fix, Tenstar accepted the YMODEM file header and requested data,
  then cancelled during the first 1,024-byte block with
  `YMODEM receive timed out`. The receiver's block parser timed out mid-frame;
  this was not a filename, path, CRC, or FFS-open error.
- Host utility writes each 1,029-byte STX frame in a single burst. Tenstar's
  Arduino USB CDC RX queue defaults to 256 bytes. Pacing the same source into
  32-byte writes with 2 ms gaps succeeded before the firmware change, strongly
  confirming that the burst exceeded the effective receive buffering.
- Tenstar setup now enlarges the CDC queue to 2,048 bytes, enough for one full
  frame with headroom. The host sender now reads and reports the monitor's
  `Transfer failed: ...` detail after a peer cancellation instead of hiding it
  behind a generic CAN message.
- The firmware built and flashed without writing a SPIFFS image. The standard
  unpaced upload then transferred the 1,027-byte benchmark file successfully.
  The test file was deleted from FFS, and the REPL returned to its prompt.
  Host YMODEM tests passed (5 tests).

This confirms the cause and validates the fix for the tested file size. It does
not yet cover repeated large transfers or the complete CPU-frequency benchmark
matrix. The attractor has one 80 MHz/SINGLE timing, but no actual-frequency
readback or matrix measurements have been collected.

## Remaining work

1. Collect the full per-board SINGLE/DOUBLE and clock-frequency matrix with
   three repetitions and actual-frequency readback.
2. Repeat the Tenstar transfer with a larger file and several consecutive
   batches to strengthen the CDC buffering validation.
3. Resume skipped Tenstar Wi-Fi, Olimex, and LILYGO acceptance only when the
   user chooses to make those checks active again.
