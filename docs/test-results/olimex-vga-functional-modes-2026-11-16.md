# Olimex VGA functional mode checks

**Date:** 2026-11-16  
**Board:** Olimex ESP32-SBC-FabGL Rev B, ESP32-WROVER, `/dev/ttyUSB0`  
**Firmware:** `olimex_esp32_sbc_fabgl_graphics_profile`, FabGL v1.0.9 (`4edd0d5`)  
**Scope:** Confirm functional display mode/profile combinations before further stress development.

| Mode | Resolution | Result | Evidence |
| --- | --- | --- | --- |
| 0 | 320×200 | Functional baseline confirmed | The board rebooted into `FFS:/startup.bas`, returned the serial REPL prompt, and reported `MODE(0),MODE(1) = 0,0`. Prior physical checks reported stable, readable VGA and drawing. |
| 1 | 320×240 | Functional confirmed | `MODE 1,0` succeeded, `MODE(0),MODE(1) = 1,0`, line/circle and console were visible, 12 console lines scrolled readably with stable sync, and the framebuffer capture was exactly 320×240. |
| 2 | 400×300 | Functional confirmed | `MODE 2,0` succeeded, `MODE(0),MODE(1) = 2,0`, line/circle and console were visible, 12 console lines scrolled readably with stable sync, and the framebuffer capture was exactly 400×300. |
| 3 | 640×400 | Allocation rejected on this FabGL profile | Earlier controlled checks returned a partial viewport and restored mode 0. This mode remains for PSRAM DMA development. |
| 4 | 640×480 | Allocation rejected on this FabGL profile | Earlier controlled checks returned a partial viewport and restored mode 0; double buffering is disabled. This mode remains for PSRAM DMA development. |
| 3, DMA | 640×400 | Functional confirmed in switchable DMA runtime | Full scanout allocation reported; window expanded to 640×400; full-screen diagonal confirmed stable; console scroll and ten-frame drawing benchmark passed. |
| 4, DMA | 640×480 | Functional confirmed in switchable DMA runtime | Full allocation reported; window expanded to 640×480; full-screen diagonal/circle and console scroll confirmed stable; ten-frame drawing benchmark passed; transitions back through modes 3, 2, 1, 0 succeeded. |

For modes 1 and 2, the BASIC fixture selected the mode, cleared graphics
channel #1, drew a diagonal and circle, and printed to console #0. Twelve
additional console lines exercised scrolling. The longest serial REPL response
was 801 ms at mode 1 and 503 ms at mode 2. The expanded graphics-window edge
captures show the diagonal continuing below the old top pane; the console
overlays the last rows as expected.

Framebuffer evidence:

- [320×240 shapes](olimex-fabgl-mode1-320x240-2026-11-16.bmp)
- [320×240 expanded window](olimex-fabgl-mode1-edge-320x240-2026-11-16.bmp)
- [400×300 shapes](olimex-fabgl-mode2-400x300-2026-11-16.bmp)
- [400×300 expanded window](olimex-fabgl-mode2-edge-400x300-2026-11-16.bmp)

Mode 1 and 2 full viewport allocation is enforced by the FabGL mode adapter,
which rejects dimensions shorter than the selected preset. The BMP headers
independently report the selected dimensions. These are functional checks,
not long-duration load or heap-fragmentation qualifications. After the checks,
`REBOOT` restored the standard 320×200 startup layout and the REPL reported
`MODE(0),MODE(1) = 0,0`.

## PSRAM DMA runtime: 400×300

The current `olimex_esp32_sbc_fabgl_psram_dma_display_400x300_packed4`
profile built and flashed successfully. Its BASIC REPL responded on
`/dev/ttyUSB0`; `MODE(0),MODE(1)` returned `2,0`. A line and circle on graphics
channel #1 and twelve scrolling lines on console #0 completed without BASIC
errors. The longest REPL response during that fixture was 102 ms. The owner
confirmed that shapes and text were correct, readable, and stable on VGA.
This confirms the existing 400×300 DMA runtime as functional on the current
build. Earlier stress testing of the 400×300 diagnostic is recorded in the
[Olimex graphics plan](../olimex-vga-graphics-plan.md); this runtime check did
not repeat that long stress run.

The debugger `SCREEN` command did not become ready for YMODEM on this DMA
profile, so no framebuffer capture accompanies the physical check. The serial
REPL remained responsive. Screen capture is a separate open issue for this
profile.

## PSRAM DMA runtime: 320×200

The current `olimex_esp32_sbc_fabgl_psram_dma_display` profile also built and
flashed successfully. The REPL reported `MODE(0),MODE(1) = 0,0`. The same
graphics line and circle commands and twelve scrolling console lines completed
without BASIC errors; the longest measured REPL response was 101 ms. The owner
confirmed stable, correct shapes and readable scrolling text on VGA. This
confirms the existing 320×200 DMA runtime as functional on the current build.
Long-duration contention and ring qualification remain tracked in the
[Agon comparison plan](../plans/agon-vga-low-level-comparison.md).

## Switchable PSRAM DMA runtime: 320×240

The current `olimex_esp32_sbc_fabgl_psram_dma_dynamic_mode_test` profile
built and flashed successfully. It started in mode 4, then `MODE 1,0`
successfully selected 320×240. The scanout startup report confirmed a
320×240 visible area, 160-byte indexed-4 stride, three PSRAM source frames
totaling 115,200 bytes, and a 120-row internal DMA ring refilled in groups of
60. `MODE(0),MODE(1)` returned `1,0`. A line and circle and twelve scrolling
console lines completed without BASIC errors; the longest REPL response was
100 ms. The owner confirmed stable, correct graphics and readable scrolling
text. This confirms the 320×240 switchable DMA runtime as functional. A
separate expanded-window diagonal was sent to the full 320×240 extent and
returned without error; the owner confirmed it reached into the lower part of
the screen with stable sync.

The functional result does not qualify sustained PSRAM contention, repeated
mode transitions, or the higher modes in the switchable profile.

## Switchable PSRAM DMA runtime: 640×400

The `olimex_esp32_sbc_fabgl_psram_dma_dynamic_mode_test` profile switched
from 320×240 to mode 3 successfully. Startup reported 640×400 at 70.09 Hz,
384,000 PSRAM source bytes, a 100-row/50-row-group internal ring, 75,948 DMA
bytes, and refill on core 1. `MODE(0),MODE(1)` returned `3,0`. The initial
graphics window remained 320×240, so it was explicitly expanded with
`WINDOW #1,0,0,640,400`; `GWIDTH(),GHEIGHT()` then returned 640,400. The owner
confirmed that a diagonal reached across the full screen with stable sync.
Line/circle drawing and twelve scrolling console lines also rendered correctly.

The ten-frame drawing benchmark initially reported the inherited 320×240
window size. After expanding the window, it reported `MODE_DRAW_SIZE=640x400`,
`MODE_DRAW_CPUFREQ=240`, and completed `MODE_DRAW_DONE` in 18.6 seconds
including REPL command overhead. The owner confirmed stable VGA throughout.
This confirms basic 640×400 DMA runtime operation without PSRAM write stress;
stress qualification remains open. `SCREEN` over YMODEM remains unavailable
on the DMA profile.

## Switchable PSRAM DMA runtime: 640×480

Mode 4 successfully started at 640×480 and reported 59.94 Hz, 460,800 PSRAM
source bytes, a 120-row ring refilled in groups of 60, 90,620 internal DMA
bytes, and refill on core 1. `MODE(0),MODE(1)` returned `4,0`. The graphics
window expanded to 640×480 and reported those dimensions. The owner confirmed
the diagonal, circle, console scrolling, and sync were stable and correct. The
ten-frame full-window drawing benchmark reported `MODE_DRAW_SIZE=640x480` and
240 MHz, completed in 27.4 seconds including REPL command overhead, and stayed
stable throughout the physical check.

Mode transitions from 640×480 back through 640×400, 400×300, 320×240, and
320×200 all succeeded. Each returned its expected mode ID/options pair. The
reported internal DMA heap remaining after startup rose from 108,012 bytes at
640×480 to 122,096 at 640×400, 122,656 at 400×300, 147,236 at 320×240, and
155,516 at 320×200. No startup allocation failed during this sequence. This
confirms basic functional operation of all five resolutions through the
switchable DMA runtime; sustained PSRAM write stress and repeated transition
soaks remain open for the larger modes.

## High-resolution PSRAM write-stress visual checks

The new `olimex_esp32_sbc_fabgl_psram_dma_test_640x400_gradient_stress_profile`
and `...640x480_gradient_stress_profile` builds compiled and flashed
successfully. Both profiles use the scrolling indexed source gradient, so the
test changes source pixels as well as cycling the palette. The owner reported
stable, smooth output at both 640×400 and 640×480.

Scripted serial captures returned zero bytes during both runs, and the
interactive monitor also showed no periodic profiler lines. Therefore startup
geometry and refill/stress counters were not recorded. These are visual
stress passes, not complete stress qualification; repeat them after restoring
diagnostic telemetry and capture counters for multiple runs.

An opt-in core-0 reporter with a one-second heartbeat was also built and
flashed. The interactive monitor still received no diagnostic lines, while the
owner confirmed the display remained stable and smooth. The reporter experiment
was removed because it did not provide telemetry.

After the stress runs, flashing the switchable-mode profile resulted in blank
or no-sync output. The known-good `olimex_esp32_sbc_fabgl_graphics_profile`
was flashed afterward, and the owner confirmed a stable, readable 320×200
REPL. Recovery from a diagnostic image back into the switchable runtime needs
another controlled check; this run does not establish the cause of the blank
screen.

## Controlled 640×480 diagnostic boot capture

A controlled reset on `/dev/ttyUSB0` captured the beginning of the diagnostic
startup message: `640x480`, scan count 1, 25.175 MHz, nominal 59.94 Hz,
indexed4, stride 320. The output stops partway through that same startup line,
after scanout has started; no periodic statistics follow. The monitor showed
scrolling bars. A serial write held across activation of the higher-priority
refill worker is a possible explanation, but has not been confirmed. See the
[raw capture](olimex-640x480-gradient-reset-capture-2026-11-16.log).

The Linux build completed in Debug and Release, but three of 80 CTest cases
failed in both configurations: `ql32_scheduler_tests` (completed child did not
raise the caller's windows), `ql32_channel_tests` (binding endpoint not open),
and `ql32_hardware_tests` (duplicate pin reservation accepted). The same
failures occurred outside the sandbox. The contribution guide requires these
host tests to pass before further embedded integration. The FabGL 320×200
profile was restored afterward and its serial REPL prompt was confirmed.
