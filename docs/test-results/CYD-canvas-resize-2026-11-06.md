# CYD full-screen canvas resize test

- **Date:** 2026-11-06 (Europe/Prague)
- **Board:** ESP32-2432S028 CYD
- **Port:** `/dev/cu.usbserial-11440`
- **PlatformIO profile:** `esp32_2432s028_spiffs`
- **Git branch/HEAD at first run:** `codex/newvm` / `f8e95b8`, with the resize changes in the worktree
- **First firmware image SHA-256:** `c6009c2aaeca657755e700217821acb400174655b6d199140228fb573f60898b`

The firmware was built with `pio run -e esp32_2432s028_spiffs` and flashed with
`pio run -e esp32_2432s028_spiffs -t upload --upload-port /dev/cu.usbserial-11440`.
The source fixture was transferred over YMODEM to `FFS:/mbfull.bas`, then run
with `LOAD "FFS:/mbfull.bas"` and `RUN`. It renders one 320×240 Mandelbrot at
8 iterations, prints each row to the TFT console, and completes three
full-screen grow/shrink cycles.

## First full-screen grow (9,600 B to 38,400 B)

| Checkpoint | Total free heap | Largest free block |
| --- | ---: | ---: |
| Before replacement allocation | 128,524 B | 110,580 B |
| Old and replacement buffers live | 90,108 B | 73,716 B |
| After swap and old-buffer release | 99,724 B | 73,716 B |

The total-free decrease while both buffers were live was 38,416 B, including
allocator overhead. Releasing the old 9,600 B canvas recovered 9,616 B.

## Repeated cycle recovery

| Checkpoint | Total free heap | Largest free block |
| --- | ---: | ---: |
| End of cycle 1 | 121,916 B | 57,332 B |
| End of cycle 2 | 121,916 B | 57,332 B |
| End of cycle 3 | 121,916 B | 57,332 B |
| Program completion sample | 122,004 B | 57,332 B |

The end-of-cycle samples stabilized for cycles 2 and 3. The final sample is
6,520 B lower in total free heap and 53,248 B lower in largest block than the
immediate pre-allocation sample; these checkpoints have different active
program/window states, so that difference alone does not establish a leak.

## 100-cycle resize soak

The `canvas-resize-soak.bas` fixture performs five warm-up grow/shrink cycles,
records a baseline, then repeats 100 full-screen grow/shrink cycles. It leaves
display updates off to avoid presentation time affecting allocator sampling.
The resize observer recorded four live canvases at each resize checkpoint.
No workers were started; the supervisor session (task 1) was the only active
task running the BASIC driver.

| Checkpoint | Total free | Largest block | Free blocks |
| --- | ---: | ---: | ---: |
| Warmed baseline | 155,928 B | 110,580 B | 29 |
| Every post-cycle sample, cycles 1–100 | 155,752 B | 110,580 B | 29 |
| Program completion | 155,840 B | 110,580 B | 29 |

All 100 cycle samples were identical. The 176 B difference from baseline was
constant throughout the soak; no per-cycle drift was observed.

The soak firmware was rebuilt from the same branch/HEAD and had image
SHA-256 `b938143669da2db1bd39682e00bed60fd08afc3a2121ac9f32d1b965a837dffd`.
The profile reported 51,048 B static RAM and 1,475,529 B flash. The fixture was
sent to `FFS:/rszsoak.bas`, then run with `LOAD "FFS:/rszsoak.bas"` and `RUN`.

## Full-screen Mandelbrot with workers

Two workers ran concurrently with the 8-iteration, 320×240 Mandelbrot: the
array worker (task 2) and the channel worker (task 3), plus supervisor task 1.
At the first full-screen growth, four windows/canvases were live:

| Checkpoint | Total free | Largest block |
| --- | ---: | ---: |
| Before 9,600 B → 38,400 B replacement | 118,560 B | 102,388 B |
| Both canvas buffers live | 80,144 B | 63,476 B |
| After swap | 89,760 B | 63,476 B |

The lowest resize checkpoint during later cycles was 44,964 B total free and
22,516 B largest block with both buffers live. The render and three resize
cycles completed. After cancelling tasks 2 and 3, the serial sample was
159,144 B total free, 55,284 B largest block, and 47 free blocks. The heap
state differs from the pre-run sample because the task/window/session state
also differs; this is recovery evidence, not a same-state leak comparison.

Raw output: [`cyd-fullscreen-workers-2026-11-06.log`](cyd-fullscreen-workers-2026-11-06.log).
The worker fixture and both worker sources were transferred over YMODEM as
`FFS:/wfull.bas`, `FFS:/arrwrk.bas`, and `FFS:/chanwrk.bas`; the command was
`LOAD "FFS:/wfull.bas"` followed by `RUN`. It ran on the same firmware image
SHA-256 listed above. The two workers were killed after the render and resize
cycles.

| Recovery checkpoint | Total free | Largest block | Free blocks |
| --- | ---: | ---: | ---: |
| After killing tasks 2 and 3, same session | 159,144 B | 55,284 B | 47 |
| After reboot and startup warm-up | 181,472 B | 110,580 B | 23 |

The largest block returned to the 110,580 B startup level after reboot. The
same-session post-kill reading is lower because the program/session and window
state differ from the post-reboot baseline.

## Build and sanitizer checks

- Normal host suite: 61/61 passed.
- ASan/UBSan suite (`build-asan`): 61/61 passed, including allocation-failure
  preservation checks.
- PlatformIO builds succeeded for `esp32_2432s028_spiffs`,
  `esp32_2432s028`, and `olimex_esp32_sbc_fabgl`.
- Olimex hardware soak was not run: no Olimex serial device was attached; only
  `/dev/cu.usbserial-11440` (CYD) was present.

## Raw output

[`cyd-fullscreen-resize-2026-11-06.log`](cyd-fullscreen-resize-2026-11-06.log)
[`cyd-resize-soak-2026-11-06.log`](cyd-resize-soak-2026-11-06.log)
[`cyd-worker-recovery-2026-11-06.log`](cyd-worker-recovery-2026-11-06.log)
