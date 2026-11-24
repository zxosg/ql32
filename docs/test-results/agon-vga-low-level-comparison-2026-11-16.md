# Agon VGA low-level comparison: first device results

**Date:** 2026-11-16

**Status:** Partial; comparison plan remains in progress

**Board:** Olimex ESP32-SBC-FabGL, ESP32-D0WD-V3 revision 3, 240 MHz, 4 MiB
flash, 4 MiB PSRAM

**Firmware profile:** `olimex_esp32_sbc_fabgl_graphics_profile`

**FabGL dependency:** 1.0.9, source revision `4edd0d5`

This report records the completed FabGL baseline benchmark and device mode
checks. It is not a qualification report for the custom PSRAM DMA scanout and
does not close the broader comparison plan.

## FabGL swap and update benchmark

The BASIC fixture holds automatic presentation with `GUPDATE 0`, then executes
999 explicit `GUPDATE` calls. Starting the task performs one initial
presentation, for 1,000 measured updates total. The frame is unchanged, so
this isolates the presenter/update path from BASIC drawing work. The diagnostic
build retains 1,000 samples and computes percentiles after the run.

Captured serial result:

```text
GFXPROFILE requests=1000 full=1000 partial=0 fallback=0 rows=200000 composed_px=64000000 written_px=64000000 compose_us=7097959 convert_us=44248 write_us=14109373 update_us=28284166 update_samples=1000 update_p50_us=27823 update_p95_us=28823 update_p99_us=28825 swap_requested=1000 swap_queued=1000 swap_executed=1000 swap_skipped=0 swap_us=6572652 swap_calls=1000 swap_avg_us=6572 swap_min_us=4833 swap_max_us=13408 swap_samples=1000 swap_p50_us=6135 swap_p95_us=7134 swap_p99_us=7137
```

| Measurement | Result | Plan check |
| --- | ---: | --- |
| Requests / queued / executed / skipped | 1000 / 1000 / 1000 / 0 | Counters reconcile |
| Swap-call p50 / p95 / p99 | 6.135 / 7.134 / 7.137 ms | Pass: p99 is below one nominal refresh period plus 2 ms |
| Maximum swap-call duration | 13.408 ms | Below the approximately 14.236 ms nominal 320x200@70 Hz period |
| Full presenter update p50 / p95 / p99 | 27.823 / 28.823 / 28.825 ms | Informational; this measures the whole update and can span frames |
| Composition / conversion / writes, total | 7.100 / 0.044 / 14.109 seconds | Across 1,000 unchanged full-frame updates |

The swap duration measures the `Canvas::swapBuffers()` caller interval. It
includes queueing, the wait for primitive execution, and task wakeup; it is not
a direct measurement of electrical VSYNC wait. In the locally pinned FabGL
source, `Canvas::swapBuffers()` submits a swap primitive and waits for executor
notification. `VGAController::VSyncInterrupt()` services pending primitives
from the I2S EOF interrupt, configured at the start of vertical front porch.
This links swap completion to the controller's interrupt service point, but
does not add a timestamp at the VSYNC edge itself.

### Concurrent worker check

Two copies of [`gupdate-swap-worker.bas`](../../examples/graphics/gupdate-swap-worker.bas),
uploaded as `FFS:/gupdate-swap-worker2.bas`, were started. Each emits a serial progress marker every 250 iterations and
pauses for 20 ms per iteration. In a six-second idle control, each worker
advanced to 250 and emitted one marker. They were left running while the same
1,000-update `RUN` benchmark executed in the REPL. The run took 28.267 seconds;
neither worker emitted a progress marker during it. In the six seconds after
the run, both advanced to 500 and emitted the next marker. Both were then
stopped with `KILL`; a follow-up `TASKS #0` showed only the supervisor.

The measured swap p99 remained 7.135 ms, but the second-worker scheduling
criterion **failed**: cooperative BASIC workers did not progress for the
28.267-second full-frame benchmark. The synchronous `GUPDATE` execution path
therefore needs a separate scheduling investigation. This does not indicate a
VSYNC timing failure, and this comparison activity does not change the default
presenter path.

Physical check after the profile build: the user confirmed the 320x200 VGA
screen was stable and readable. No sync loss, artifacts, or reset was reported
during the mode check.

## Mode allocation and recovery

The controlled device check requested `MODE 3,0` (640x400) and `MODE 4,0`
(640x480). FabGL returned partial viewport heights of 214 and 206 pixels,
respectively. QL/32 rejected each incomplete allocation and restored 320x200.
`MODE 4,1` was also rejected because 640x480 double buffering is disabled on
this target. Ten cycles between 320x200 single and double buffering completed
without a crash. The final 320x200 screen was stable and readable.

An initial rapid, unpaced test attempt crashed while changing modes. The
failure was not reproduced in the controlled mode test after board reset and
command pacing. It is retained here as a test-harness incident, not treated as
evidence that the mode loop is failure-free under all timing conditions.

## Memory snapshot

One boot of the final profile build reported approximately:

| Checkpoint | Measurement |
| --- | ---: |
| PSRAM total / free at boot | 4,192,123 / 4,192,123 bytes |
| Internal free before FabGL double-buffer allocation | 230,280 bytes |
| Internal free after FabGL double-buffer allocation | 79,600 bytes |
| Largest internal free block after double-buffer allocation | 40,948 bytes |
| QL/32-reported free / largest block | 4,134,703 / 4,063,220 bytes |
| Firmware static RAM / flash image from profile build | 54,820 / 1,804,681 bytes |

This is a single boot, not the three-boot median/range required by the plan.
The QL/32 free-memory figure is not an internal-heap-only figure. Post-service
internal and DMA-capable heap checkpoints, plus post-cycle fragmentation, have
not yet been captured in a comparable way.

## Custom DMA scanout trial

Three consecutive 65-second captures were collected on the experimental
`olimex_esp32_sbc_fabgl_psram_dma_test_packed4` firmware with PSRAM write
stress enabled. The monitor reported no signal and entered standby. This is a
visual **fail**: serial counters alone cannot qualify scanout. The captured
status output reported zero late refills and zero spurious interrupts in every
sample, but it did not expose per-refill duration or backlog, so those values
are still unmeasured. The board has since been restored to the FabGL profile.
After host serial access was enabled, `/dev/ttyUSB0` appeared, no process held
it, and the image flashed with hash verification. A carriage return received
the normal REPL prompt. The monitor was in standby, so VGA visual recovery
was not confirmed in that observation. After waking the monitor, the user
confirmed the restored 320x200 display was stable and readable.

A separate profiler build variant has since been added. The stress-enabled
variant compiled successfully (RAM 33,912 bytes, flash 762,117 bytes). The
stress-disabled variant was also compiled and flashed to isolate contention.
Its 55-second capture logged 54 samples, with maximum batch duration 3,156 us,
50-row maximum batch/backlog, zero late refills, zero unexpected interrupts,
and no PSRAM stress writes. Despite these counters, the user observed rolling
bars or sync loss. This is a second visual failure with PSRAM contention
removed, so contention is not the sole explanation. Its 3,723-frame delta over
53 one-second sample intervals is about 70.25 Hz, matching the 70.24 Hz nominal
rate; gross frame-rate error is unlikely to explain the roll. The complete
no-stress serial capture is in [the DMA profiler log](agon-vga-dma-profile-idle-2026-11-16.log).

The next isolation profile disabled EOF interrupts and live refills while
leaving the same modeline and descriptor chain. The user reported synchronized,
stable output. This localizes the instability to the active refill path rather
than static DMA modeline/descriptor output. With the 100-row ring and 50-row
refill batch, the nominal reuse deadline is 50 logical rows:
`50 × 2 × 400 / 12,587,500 = 3.178 ms`. The measured maximum batch was
3.156 ms, leaving about 22 us before accounting for interrupt-to-worker wakeup
delay. The refill profiler checks only the first sequence in the next batch,
so it cannot establish that every row met its individual reuse deadline. A
200-row-ring A/B test is prepared to increase the lead to 150 logical rows
while retaining the 50-row batch. The [static isolation log](agon-vga-dma-profile-static-2026-11-16.log)
records the no-interrupt/no-refill counters and visual result.

The 200-row ring by itself did not fix the moving-bars profile: its 55-second
run still rolled/lost sync, with a 3,166 us maximum batch and no late-refill or
unexpected-IRQ counters. A fixed triangle on the same live-refill setup stayed
stable for 55 seconds; its maximum batch was 599 us. This differentiates the
moving/shifted source conversion workload from the static descriptor chain and
from live refills as a whole, although it does not yet identify whether the
remaining cause is conversion timing, source reads, writes overlapping active
scanout, or the changing pattern itself. See the [ring-200 moving-bars
capture](agon-vga-dma-profile-ring200-idle-2026-11-16.log) and [fixed-triangle
capture](agon-vga-dma-profile-ring200-triangle-2026-11-16.log).

An opt-in packed-4 pair-LUT encoder was then tested with the same 200-row ring
and 50-row batches. In the initial 55-second run, the user described
horizontal rolling while vertical sync remained stable, with the bars changing
colors about twice per second. Later A/B tests found fixed-palette moving bars
and a rotating-palette checker both stable. A repeat of the exact moving-bars /
rotating-palette profile clarified that the bars translated smoothly as
intended while the rest of the image stayed steady. The initial “rolling”
report for this profile was pattern motion, not whole-image jitter. The repeat
measured 2,838 us max batch and 103 us max row, with zero active-slot
overwrites, late refills, or unexpected interrupts. The initial pair-LUT run
improved batch time from 3,166 us to 2,769 us. Captures: [initial pair-LUT
run](agon-vga-dma-profile-ring200-shift-lut-2026-11-16.log) and
[repeat](agon-vga-dma-profile-ring200-shift-lut-rotate-repeat-2026-11-16.log).

To separate scanout writes from animation and palette churn, a stationary
full-screen checker was then run with the same pair-LUT encoder, 200-row ring,
and 50-row batches. The 58-second serial capture contained 59 samples, a
2,709 us maximum batch, zero late refills/unexpected IRQs, and zero palette
updates or stress writes. See the
[stationary-checker capture](agon-vga-dma-profile-ring200-checker-lut-2026-11-16.log).
The user confirmed stable synchronization. This is evidence that stationary
high-transition packed-4 data and live refill are stable under this profile.

The moving-bars/pair-LUT run was repeated with palette rotation disabled. The
user confirmed the bars moved cleanly and stayed synchronized. Over 58 seconds,
the maximum batch was 2,798 us and the maximum row encode was 104 us; there
were zero active-slot overwrites, late refills, unexpected interrupts, palette
updates, or stress writes. The result complements the stationary checker and
combined moving-bars tests; the latter shows that both intended motion and
palette cycling operate while the rest of the image stays steady. The
[fixed-palette moving-bars capture](agon-vga-dma-profile-ring200-shift-lut-fixed-palette-2026-11-16.log)
records the measurements.

Palette rotation was then enabled on the stationary checker. The user
confirmed stable output with clean color cycling. The 58-second run applied
the palette 113 times; maximum batch/row times were 2,830/101 us, with zero
active-slot overwrites, late refills, or unexpected interrupts. Palette
rotation by itself therefore does not reproduce the reported roll. The
[rotating-checker capture](agon-vga-dma-profile-ring200-checker-lut-rotate-2026-11-16.log)
contains the full counters.

Together, these checks qualify the 320x200 live-refill diagnostic pattern
with fixed and rotating palettes. This does not change earlier unresolved
roll/sync reports from the 100-row-ring profile or qualify other modes and
patterns.

The next pattern was eight full-width horizontal bands with palette cycling.
The user confirmed stable VGA and clean band boundaries. The 58-second capture
logged 145 palette applications, 2,830 us maximum batch, and 100 us maximum
row encode time, with zero active-slot overwrites, late refills, or unexpected
interrupts. See the [horizontal-bands capture](agon-vga-dma-profile-ring200-bands-lut-2026-11-16.log).

The moving 16-level full-width gradient also moved smoothly and remained
synchronized. Over 58 seconds it applied the palette 161 times; max batch/row
were 2,837/104 us, with zero active-slot overwrites, late refills, or
unexpected interrupts. See the [gradient capture](agon-vga-dma-profile-ring200-gradient-lut-2026-11-16.log).

Two matching 58-second tests ran with the concurrent PSRAM stress writer
enabled. The moving gradient remained stable and smooth; the eight horizontal
bands stayed stable with clean boundaries. Both runs recorded zero active-slot
overwrites, late refills, and unexpected interrupts. The gradient peaked at
3,344 us per refill batch and 120 us per row encode while writing 85,131,264
stress bytes; the bands peaked at 3,294 us and 117 us while writing
81,985,536 bytes. Palette updates completed 151 and 145 times, respectively.
See the [gradient stress capture](agon-vga-dma-profile-ring200-gradient-lut-stress-2026-11-16.log)
and [bands stress capture](agon-vga-dma-profile-ring200-bands-lut-stress-2026-11-16.log).
These are single-run 320x200 observations and do not yet satisfy the plan's
three-run matrix or qualify 640x400/640x480 DMA scanout.

The moving-gradient stress profile was also built for 640x400 and 640x480
using the configured 100-row and 120-row line rings. The 640x400 image was
stable and smooth at full height on two visual checks; 640x480 was stable and
smooth at full height on the initial check. However, the serial port produced
no boot or periodic profiler output on either high-mode profile, so requested
geometry and refill metrics were not captured. Treat these as encouraging
visual observations only, not telemetry-backed mode qualification. Investigate
why UART diagnostics are absent before running the remaining high-mode matrix.

An opt-in follow-up build added per-row maximum encode time and an
`active-slot-overwrites` counter to detect a ring slot being rewritten before
its previous sequence is consumed. It compiled successfully (RAM 33,904 bytes;
flash image 761,625 bytes) and was used for the checker and moving-bars repeats.

| Run | Capture | Start → end cumulative frames | Late refills | Spurious interrupts | PSRAM stress bytes | VGA result |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | ~65 s | ~1,758 → ~6,253 | 0 | 0 | ~17.4 → ~62.0 MB | Monitor standby / no signal |
| 2 | 65.08 s | 11,732 → 13,137 | 0 | 0 | 116.3 → 130.1 MB | Monitor standby / no signal |
| 3 | 65.10 s | 18,124 → 22,620 | 0 | 0 | 179.4 → 223.9 MB | Monitor standby / no signal |

Raw capture files: [run 2](agon-vga-dma-stress-2026-11-16-run2.log) and
[run 3](agon-vga-dma-stress-2026-11-16-run3.log). Run 1's interval summary was
recorded from the serial session but its raw log was not saved. These were
low-transition moving-bar/stress captures, not the full pattern matrix. Do not
interpret the zero late-refill counters as evidence of a valid VGA signal.

The recovery used the `olimex_esp32_sbc_fabgl_graphics_profile` image (54,820
bytes RAM, 1,804,681 bytes flash). Esptool completed the flash and verified
the image hash. The REPL answered on `/dev/ttyUSB0`; the user confirmed that
the restored 320x200 output was stable and readable with the monitor awake.

## Source comparison and current decisions

The pinned VDP 2.15.0 firmware source provides several wrapper-level findings
even though its exact `vdp-gl` dependency is unavailable. `switchBuffer()` calls
`Canvas::swapBuffers()` in double-buffer mode and otherwise queues a no-op and
waits for plot completion with the VSYNC flag. `changeResolution()` creates a
new Canvas and rejects a mode if controller screen height differs from viewport
height. `vdu_mode()` waits for drawing, attempts the requested mode, and tries
to restore the previous mode after a failure. It also resets drawing contexts.
These paths correspond to QL/32's explicit full-viewport check and rollback.

The same release exposes free PSRAM as high and low 16-bit variables using
`heap_caps_get_free_size(MALLOC_CAP_SPIRAM)`, and reports used buffers through
`buffers.size()`. `changeResolution()` logs internal, 8-bit, and 32-bit free
heap after recreating the Canvas. These are comparable reporting categories,
though the underlying buffer counts and allocator state remain implementation
specific.

| Topic | Agon reference | QL/32 evidence | Interim decision |
| --- | --- | --- | --- |
| VSYNC wait and swap | Firmware wrapper calls `Canvas::swapBuffers()` for double-buffer mode and explicitly calls wait-for-VSYNC after a no-op otherwise. Exact `vdp-gl` source remains unresolved. | FabGL swap primitive and executor wait are identified in local revision `4edd0d5`; 1,000 counters reconcile and measured swap p99 passes the timing threshold. However, the REPL's synchronous 1,000-update run prevented both BASIC workers from advancing for 28.267 seconds. | Swap timing passes; cooperative scheduling fails. Investigate a yield-aware presentation boundary as a separate change. |
| Draw completion | Must be checked against the exact pinned Agon VDP and dependency source before claiming implementation parity. | QL/32 records composition, conversion, write, and swap intervals separately. | Defer Agon implementation-level comparison until its dependency is pinned. |
| Framebuffer / DMA memory | VDP 2.15.0 reads free PSRAM with heap capabilities, exposes buffer count, and logs internal/8-bit/32-bit free heap after mode changes. Exact allocator implementation is in unresolved `vdp-gl`. | One profile boot captured useful starting heap figures; no three-boot or post-cycle fragmentation series yet. | Follow-up measurement required. |
| Mode allocation / failure | VDP 2.15.0 checks screen height against viewport height and attempts the previous mode after a failed change. | Partial 640x400/640x480 allocations are rejected and 320x200 is restored. 640x480 double buffer is rejected. | Already covered at the wrapper-policy level; collect repeated heap recovery data. |
| PSRAM scanout deadlines | Exact Agon scanout source comparison remains open. | At 320x200, checker, moving bars, eight horizontal bands, and a moving 16-level gradient were stable with palette cycling. Max batch/row remained under 2.84 ms/104 us; there were no active-slot overwrites, late refills, or IRQs. | Olimex-only experiment; 320x200 no-stress pattern checks pass. Stress-enabled runs and additional modes remain open. |
| Sprite composition | Version-specific Agon details remain to be confirmed. | QL/32 uses retained canvases and portable composition; there is no hardware-sprite plane in this scope. | Not applicable to the current comparison objective. |

## Remaining work

- Resolve and record an immutable Agon `vdp-gl` commit, then inspect scanout,
  swap, allocator, memory-reporting, and sprite code at that revision.
- Capture three-boot memory checkpoints, including internal and DMA-capable
  heaps after startup and after repeated successful and rejected mode changes.
- Repeat swap measurements with automatic updates, explicit updates while
  updates are held, and frame skipping. Run two workers and record scheduler
  progress during swaps. The explicit held-update case has been measured; the
  two-worker test exposed a 28.267-second progress gap and now needs a focused
  scheduling investigation. The 2026-11-17 Mandelbrot test reproduced severe
  progress starvation under tight worker `PRINT` loops: with one such worker,
  a Mandelbrot row took 78.958 seconds, while suspending it restored roughly
  0.66 seconds per row. Three workers yielded no next-row marker in a 120-second
  window. This isolates synchronous screen output as the dominant contention,
  but tick and presentation durations still need instrumentation; see the
  [Mandelbrot worker report](olimex-mandelbrot-worker-starvation-2026-11-17.md).
- Repeat moving bars plus palette rotation with per-row timing and
  active-slot checks. The checker stayed stable during palette rotation and
  moving bars stayed clean with a fixed palette, so ask whether the whole
  picture shifts/jitters or only the intentionally moving bars translate.
- Instrument readiness per row within each 50-row refill batch, including
  worker wakeup time and actual ring reuse deadline. Current batch-level late
  counters only check the first row in the next batch.
- If stationary high-transition rows are stable but moving rows roll, isolate
  source/palette updates and PSRAM writes during active scanout. If stationary
  high-transition rows also roll, isolate packed-row writes, descriptor row
  boundaries, and DMA buffer handoff before more pattern tests.
- Keep custom-DMA sync failures open; a stable FabGL baseline does not qualify
  that separate backend.

The Linux CMake test suite was not run during this device iteration because
`cmake` is unavailable in the environment. The FabGL profile build and restore
upload succeeded (`RAM 54,820 bytes; flash image 1,804,681 bytes`); the serial
REPL answered after flashing, and the user confirmed stable, readable VGA with
the monitor awake. The stress-enabled, stress-disabled, and static DMA
profiler variants built. The stress-disabled live-refill profile rolled/lost
sync, while the static profile was stable. FabGL was restored after both DMA
diagnostic campaigns, and the latest restore returned the `>` REPL prompt.
`git diff --check` passed.
