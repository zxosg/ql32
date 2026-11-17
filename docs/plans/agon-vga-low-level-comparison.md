# Agon VGA low-level comparison plan

**Status:** In progress

**Scope:** Compare Agon Light 2 VDP timing and memory techniques with QL/32's current FabGL VGA path and Olimex DMA scanout experiment. This is an evaluation plan; it does not authorize changing the default VGA backend.

## Mode qualification priority

First confirm and close the modes that already work, before developing the
remaining behavior. Record results separately for each firmware profile and mode:
full requested viewport allocation, stable sync, readable console and input,
drawing and scrolling, mode transitions and recovery, and a sustained workload
with serial counters where available. Include the build identifier and raw
results; a passing diagnostic does not by itself qualify the BASIC runtime.

The basic mode pass covers FabGL 320x200, 320x240, and 400x300, plus all five
resolutions through the switchable DMA runtime. FabGL rejects full
640x400/640x480 allocations on this Olimex configuration; DMA supplies those
resolutions. The 640x400 and 640x480 scrolling-gradient stress visuals were
stable, but scripted and interactive serial capture returned no profiler
lines, so startup geometry and refill counters are missing. A core-0 heartbeat
reporter did not resolve it. A post-diagnostic switchable-runtime flash showed
blank/no-sync output; the known-good FabGL 320x200 profile recovered. Recheck
switchable-runtime recovery before closing that qualification. Remaining work:
restore serial telemetry, repeat high-resolution stress runs with full startup
geometry and counters, measure per-row deadline margin, and repeat mode
transitions. Verify the board and serial port before device testing.

The current functional check confirms FabGL modes 0 (320x200), 1 (320x240),
and 2 (400x300), fixed DMA profiles for 320x200 and 400x300, and modes 1
through 4 in the switchable DMA runtime. Modes 3 and 4 passed full-window
drawing, scrolling, and a ten-frame workload on the monitor. A later
post-diagnostic restore attempt showed blank/no-sync output, so recovery for
the switchable profile needs a controlled repeat. The
[per-profile evidence](../test-results/olimex-vga-functional-modes-2026-11-16.md)
includes BASIC mode reports, graphics and scrolling checks, physical monitor
observations, and FabGL framebuffer captures. The 400x300 DMA `SCREEN`
transfer did not become ready for YMODEM; that capture limitation is recorded
separately. High-resolution PSRAM stress, serial telemetry under contention,
deadline instrumentation, and transition soaks remain open for 640x400 and
640x480 DMA.

## Goal

Produce an evidence-based decision on whether any Agon low-level VGA technique
should change QL/32's display timing, memory accounting, or scanout experiment.
Compare behavior and resource use, not raw performance numbers across unlike
boards.

The activity is complete when:

1. A source comparison records exact VSYNC, buffer-swap, display-mode allocation,
   memory-reporting, and scanout behavior for both systems, with firmware/library
   versions attached to each finding.
2. QL/32 has repeatable measurements for the FabGL baseline and the existing
   Olimex PSRAM DMA profile using the criteria in this plan.
3. Each Agon technique is marked **already covered**, **useful follow-up**,
   **platform-specific/defer**, or **not applicable**, with evidence and an
   owner-facing recommendation.
4. No change to the default VGA path is proposed unless the FabGL baseline is
   unstable and a tested alternative is both stable and measurably useful.

## Current QL/32 baseline

| Area | Current implementation | Current evidence / open point |
| --- | --- | --- |
| VGA generation | Olimex and LILYGO use FabGL `VGAController` and `Canvas`; QL/32 owns presentation and mode selection. Current pinned FabGL dependency is v1.0.9 (`4edd0d5`). | The Olimex 320x200 FabGL double-buffer profile is physically stable. Agon uses `vdp-gl`, a FabGL fork, so it is a reference for policy and implementation changes, not a separate driver to port. |
| VSYNC | `FabglFramePresenter` writes the draw viewport and calls `Canvas::swapBuffers()` for double-buffered frames. In the pinned FabGL source this queues a swap primitive and blocks the caller until the primitive executor notifies it. | 1,000 unchanged-frame requests: 1,000 queued/executed, zero skipped, swap p99 7.137 ms. The exact source wait path is confirmed. With two BASIC workers, neither advanced during the 28.267-second benchmark run; see the comparison results. |
| Async drawing | Direct raw-scanline writes and a row/band Canvas path are supported. The band path waits for each submitted bitmap before reusing scratch storage, then drains the update. | Preserve the distinction between draw completion and display-buffer swap when measuring command latency. |
| Modes and allocation | FabGL mode IDs enumerate 320x200, 320x240, 400x300, 640x400, and 640x480. A mode is usable only if its full viewport allocates. A 2026-11-16 flash measured only 640x214 for a 640x400 request and 640x206 for 640x480; the new guard rejected both and restored 320x200. 640x480 double buffering is also explicitly rejected after a reproducible FabGL viewport corruption on a later mode change. | Heap figures in the Olimex graphics plan were captured at different startup stages. Record comparable checkpoints after all runtime services start and include the exact actual dimensions on failed requests. |
| Custom scanout | The opt-in Olimex experiment keeps three packed-4 logical frames in PSRAM and uses internal DMA RAM for descriptors and a reusable line ring. At 640x400/640x480, the three source frames use 384,000/460,800 bytes; a 120-row raw line ring uses 76,800 internal bytes. It tracks frame/row/refill counts, late refills, and unexpected interrupts. | Static scanout synchronized; moving bars rolled/lost sync with both 100- and 200-row rings. A fixed triangle was stable for 55 s with live refills. Pair-LUT moving bars still rolled horizontally with vertical sync stable, despite reducing max batch from 3.166 to 2.769 ms. Runtime integration is not qualified. |
| Portable graphics | BASIC and the VM use retained indexed canvases and a platform-neutral graphics service. | Keep scanout conversion and hardware timing inside the Olimex adapter. |

The instrumented FabGL measurement, controlled mode checks, and DMA diagnostic
trials are recorded in [`Agon VGA comparison results`](../test-results/agon-vga-low-level-comparison-2026-11-16.md).
The FabGL swap timing and mode rollback measurements are complete for this
iteration, but the worker scheduling criterion failed. The static DMA output
was user-confirmed stable. The 200-row live-refill diagnostic is now
user-confirmed stable for stationary triangle/checker patterns and moving bars
with fixed or rotating palette; the bars translate smoothly while the rest of
the image stays steady. The known-good FabGL profile was flashed and the REPL
prompt responded before diagnostics resumed. Scheduler, memory-cycle, source
dependency, other modes/patterns, and broader DMA qualification remain open.
The checker measured a 2.709 ms max refill batch with zero late-refill/IRQ
counters. Moving bars had max batch/row of 2.798 ms/104 us with fixed palette
and 2.838 ms/103 us while rotating; neither run reported active-slot
overwrites. Palette rotation on a stationary checker was also stable (113
applications, 2.830 ms max batch). The eight-band run stayed stable with clean
boundaries (145 palette applications, 2.830 ms max batch, 100 us max row). The
moving full-width gradient also stayed smooth (161 palette applications,
2.837 ms max batch, 104 us max row). Both had zero active-slot overwrites.
Two matching stress-enabled runs now pass visually at 320x200: the moving
gradient stayed stable and smooth (3.344 ms maximum batch, 120 us maximum row,
85,131,264 stress bytes), and eight horizontal bands stayed stable with clean
boundaries (3.294 ms, 117 us, 81,985,536 stress bytes). Both reported zero
active-slot overwrites, late refills, and unexpected interrupts. These are
single runs, not the plan's three-run qualification. Repeat selected runs, then
test at 640x400 and 640x480 with full-geometry confirmation. Initial stress
gradient builds for 640x400 and 640x480 produced stable, smooth full-height
images in user visual checks (640x400 repeated once), but no boot or periodic
profiler text was received over UART. Do not count those as geometry- or
telemetry-qualified until the serial path is understood and the reported mode
and counters are captured.

Relevant QL/32 code and evidence:

- [`FabglFramePresenter`](../../src/platform/fabgl_vga/frame_presenter.cpp)
- [`DisplayModes`](../../src/platform/fabgl_vga/display_modes.cpp)
- [`PsramDmaScanout`](../../include/ql32/platform/olimex_vga/psram_dma_scanout.hpp)
- [`Olimex VGA graphics plan`](../olimex-vga-graphics-plan.md)
- [`QL32 platform qualification`](../specification/QL32-PLAT-001.md)

## Agon reference baseline

Agon's ESP32 VDP is a VGA coprocessor that receives VDU command bytes from a
separate eZ80 CPU. The VDP uses a FabGL fork (`vdp-gl`). It is therefore a
reference for timing policy, memory behavior, and diagnostics, rather than a
drop-in alternative VGA signal driver.

Keep the firmware revisions distinct during review:

- The OLIMEX Agon Light 2 source tree cited in the initial assessment is VDP
  1.04-era firmware; the exact source commit is not recorded yet and must be
  pinned before making implementation claims.
- Use Agon Platform VDP 2.15.0, source commit
  `5e628a81d2a1329cf33873b783684f1b7d9d8b7f`, as the current source
  comparison unless the review selects another explicit release.
- The current VDP Variables documentation says the variable API was added in
  VDP 2.12.0. In particular, free-PSRAM and buffer-count variables must not be
  attributed to VDP 1.04. Apply the same version check to VSYNC commands,
  scanout-time Copper behavior, and hardware sprites.
- The VDP 2.15.0 `platformio.ini` points at `vdp-gl#hard-cursors`, but that
  branch is no longer listed by the public repository branches endpoint (checked
  2026-11-16). The dependency commit used to build the release cannot currently
  be reproduced from that reference. Keep source-level `vdp-gl` claims open
  until the release dependency SHA is recovered from a build record or another
  immutable source.
- Record firmware tag/commit and the resolved `vdp-gl` dependency commit beside
  every source or behavior finding. Do not attribute later features to the
  original Olimex VDP source.

Primary references:

- [Agon VDP architecture](https://agonplatform.github.io/agon-docs/VDP/)
- [Agon screen modes and VSYNC swap](https://agonplatform.github.io/agon-docs/vdp/Screen-Modes/)
- [Agon system commands](https://agonplatform.github.io/agon-docs/vdp/System-Commands/)
- [Agon VDP memory variables](https://agonplatform.github.io/agon-docs/vdp/VDP-Variables/)
- [Agon scanout-time palette conversion](https://agonplatform.github.io/agon-docs/vdp/Copper-API/)
- [Agon software and hardware sprite limits](https://agonplatform.github.io/agon-docs/vdp/Bitmaps-API/)
- [Agon Platform VDP 2.15.0 release](https://github.com/AgonPlatform/agon-vdp/releases/tag/v2.15.0)
- [Agon Platform VDP repository](https://github.com/AgonPlatform/agon-vdp)
- [VDP 2.15.0 build configuration](https://github.com/AgonPlatform/agon-vdp/blob/v2.15.0/platformio.ini)
- [Agon Platform `vdp-gl` branches](https://github.com/AgonPlatform/vdp-gl/branches)
- [VDP 2.15.0 screen and mode implementation](https://github.com/AgonPlatform/agon-vdp/blob/v2.15.0/video/agon_screen.h)
- [VDP 2.15.0 VDU mode handling](https://github.com/AgonPlatform/agon-vdp/blob/v2.15.0/video/vdu.h)
- [VDP 2.15.0 memory variables](https://github.com/AgonPlatform/agon-vdp/blob/v2.15.0/video/vdp_variables.h)

## Comparisons and measurements

### 1. VSYNC and scheduler impact

Compare Agon's explicit wait/swap-at-VSYNC operation with FabGL's
`Canvas::swapBuffers()` and QL/32's actual presentation call sites.

For the QL/32 320x200 double-buffer profile:

- Measure a swap-only path using an unchanged back buffer, then measure the
  end-to-end presenter path with a small dirty update. Report composition,
  conversion/write, `swapBuffers()` call duration, and total update latency
  separately; report median, p95, p99, maximum, and measured refresh period.
  Do not label the `swapBuffers()` duration as raw VSYNC wait: it includes
  primitive queueing and task-notification wait.
- For 1,000 requests, count presentation requests, swap primitives queued,
  swap primitives executed, and skipped/suppressed requests. Associate each
  executed swap with the FabGL VSYNC service point using instrumentation in the
  pinned local library; timestamps around the caller alone cannot prove that
  association.
- Repeat with automatic updates, `GUPDATE 0` followed by explicit `GUPDATE`,
  and active frame skipping. Count presentation requests and actual swaps.
- Run at least two scheduled BASIC workers while one session draws. Record
  worker progress and maximum interval between worker slices during the swap
  workload.
- Verify that one explicit update produces at most one buffer swap and that
  skipped/suppressed updates do not cause hidden extra swaps.

**Pass criteria:** every accepted frame swap is synchronized to a FabGL VSYNC
event; the 99th-percentile swap-call duration does not exceed one refresh
period plus 2 ms; the second worker continues to make progress; and QL/32's
request/queue/execute/skip counters reconcile exactly. If a platform wait
blocks the cooperative scheduler longer than that, record it as a scheduling
issue for QL/32 rather than treating Agon's separate VDP behavior as
equivalent.

### 2. Framebuffer and DMA memory accounting

Compare Agon's free-PSRAM/buffer reporting and mode-allocation failure behavior
with QL/32's measured memory use. Absolute byte totals are not comparable across
different firmware builds; compare allocation categories and trends.

At each checkpoint, record total free internal heap, largest internal block,
DMA-capable free heap, free PSRAM, PSRAM allocation, selected mode, and whether
double buffering is active:

1. Before display initialization.
2. After display initialization.
3. After all QL/32 runtime services and the REPL are started.
4. After ten cycles through 320x200 single/double-buffered modes plus requests
   for 640x400 single, 640x480 single, and 640x480 double-buffered modes. Record
   exact requested and allocated viewport dimensions; on the current FabGL
   build, 640x400/640x480 single-buffer requests are expected to fail the full
   viewport check and roll back, while 640x480 double buffering is rejected.
5. After each rejected request and at the end of all cycles, confirm the prior
   mode/geometry and allocations remain valid. Include repeated failures in
   the heap recovery trend.

Run three firmware boots and report median and range for each checkpoint. Also
report estimated image bytes separately from measured controller/descriptor
overhead. Use the actual framebuffer count, stride, and format rather than
assuming all modes cost exactly width × height bytes.

**Pass criteria:** mode changes either succeed with the requested full geometry
or fail and restore the prior mode; ten cycles show no monotonic free-heap or
largest-block loss beyond 1 KiB; and post-startup internal/DMA reserve is
explicitly reported. Do not use Agon's memory fallback as a reason to silently
degrade a QL/32 mode; preserve QL/32's explicit success/error result.

### 3. Scanout deadlines and PSRAM contention

Inspect the Agon `vdp-gl` scanout implementation at the pinned commit and
compare its actual scanline production/deadline behavior with QL/32's DMA
producer and refill ring. Do not treat the Copper API as a general scanout
conversion design: its documented per-row palette signal lists are a separate
scanout-time palette effect. The key measure for the refill comparison is
deadline margin, not average rendering throughput.

Use the opt-in QL/32 diagnostic profile. First collect the 320x200 reference
results; then repeat the active-update stress at each intended full-height mode
(640x400 and 640x480) before describing that mode as qualified. For each
pattern/mode combination, collect three 60-second runs with and without a
concurrent PSRAM write worker:

- Static palette bars with a moving row marker (known passing reference).
- Full-width alternating bands and row-varying checkerboard (known failing or
  unresolved stress patterns).
- A moving full-width gradient or equivalent high-transition pattern.

For 640x400 and 640x480, verify that source pixels actually move before
counting a gradient run as moving-framebuffer stress. The default diagnostic
animation previously shifted source pixels only at width 320; the high-
resolution stress profiles now define `QL32_OLIMEX_DMA_TEST_FORCE_SOURCE_SHIFT`
to scroll the source at any tested width. Record the motion mechanism with
each profile result.

Record frames, rows, refill batches, maximum refill duration, maximum backlog,
late refills, spurious interrupts, dropped/reused frames, and PSRAM stress bytes.
Capture monitor output for each run and note sync loss, rolling, tearing, or
color instability.

**Pass criteria:** the reference pattern remains stable for all runs with zero
late refills and zero unexpected interrupts; no tested pattern/mode may be
described as qualified if any run loses sync or rolls. The first three
low-transition/stress captures showed zero late refills and zero unexpected
interrupts, but the monitor entered standby for all three; therefore this is a
failed scanout qualification. A 55-second no-stress capture also rolled/lost
sync despite 3.156 ms max refill batches, 50-row max backlog, and zero late
refills/stray interrupts (see the [raw capture](../test-results/agon-vga-dma-profile-idle-2026-11-16.log)). Static
scanout with the same modeline and descriptors was stable when EOF interrupts
and live refills were disabled (see the [static capture](../test-results/agon-vga-dma-profile-static-2026-11-16.log)). The 200-row tests showed stable stationary triangle/checker output, clean moving bars with either fixed or rotating palette, clean eight-band output, and a smooth moving gradient. Across these no-stress runs, max refill batch/row remained below 2.838 ms/104 us, with zero active-slot overwrites, late refills, or IRQs. The checker stayed stable through 113 palette rotations (2.830 ms max batch, 101 us max row). Stress-enabled gradient and horizontal-band runs also passed visually, with zero slot overwrites, late refills, or IRQs; their single-run metrics are recorded above. The 50-row refill batch deadline is about 3.178 ms. New 640x400 and 640x480 scrolling-gradient stress builds appeared stable and smooth, but scripted serial captures and the interactive monitor produced no profiler lines; a core-0 heartbeat reporter also produced no output. Repeat these runs after diagnostic telemetry is restored and capture full geometry and counters. Earlier 100-row-ring sync reports still need reconciliation. Compare
the same patterns on the FabGL double-buffer 320x200 profile as a known-good
signal reference, but do not compare its internal mechanism to QL/32's
ring-buffer refill rate. A 640x400/640x480 stress run is valid only when the
diagnostic reports the requested full geometry and the full frame allocation.

### 4. Sprite and scanout coupling (informational)

Record Agon's software sprite save-under behavior separately from its newer
experimental hardware-sprite scanout behavior. The latter has per-scanline
pixel/time limits and may disrupt VGA output at high sprite density. QL/32 has
no equivalent hardware sprite plane; its portable renderer composes retained
canvases.

Do not add a hardware-sprite path in this activity. If future measurements
justify it, propose a separate Olimex-only experiment with explicit line-budget
limits and retain the portable compositor as the reference behavior.

## Comparison report format

For every finding, record the firmware tag/commit and library dependency commit,
then use this table:

| Topic | Agon implementation and version | QL/32 implementation | Evidence / measurement | Decision |
| --- | --- | --- | --- | --- |
| VSYNC wait and swap |  |  |  |  |
| Draw completion |  |  |  |  |
| Framebuffer and DMA memory |  |  |  |  |
| Mode allocation and failure |  |  |  |  |
| PSRAM scanout deadlines |  |  |  |  |
| Sprite composition |  |  |  |  |

Allowed decisions are **already covered**, **adopt as a QL/32 follow-up**,
**Olimex-only experiment**, **defer**, and **not applicable**. For any proposed
follow-up, identify the QL/32 file/service boundary, expected memory and
scheduling impact, measurable success threshold, and rollback path.

## Exit

Publish the filled comparison table and a short recommendation. Close the
activity only when every measurement has raw logs or a linked report, all
thresholds have a pass/fail result, the known checker/broad-band sync issue is
explicitly retained if unresolved, and the recommended QL/32 changes (if any)
are small, testable, and isolated behind the platform adapter. No BASIC syntax,
portable graphics contract, or default board profile changes are part of this
plan.
