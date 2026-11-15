# Agon VGA low-level comparison plan

**Status:** Proposed  
**Scope:** Compare Agon Light 2 VDP timing and memory techniques with QL/32's current FabGL VGA path and Olimex DMA scanout experiment. This is an evaluation plan; it does not authorize changing the default VGA backend.

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
| VGA generation | Olimex and LILYGO use FabGL `VGAController` and `Canvas`; QL/32 owns presentation and mode selection. | The Olimex 320x200 FabGL double-buffer profile is physically stable. Agon also uses a FabGL-derived VGA path, so its controller is not a separate driver implementation to port. |
| VSYNC | `FabglFramePresenter` writes the draw viewport and calls `swapBuffers()` for double-buffered frames. FabGL documents that swaps occur during vertical retrace. | Measure where the call blocks in the actual QL/32 scheduler and how often it is reached under automatic updates, `GUPDATE`, and partial presentation. |
| Async drawing | Direct raw-scanline writes and a row/band Canvas path are supported. The band path waits for each submitted bitmap before reusing scratch storage, then drains the update. | Preserve the distinction between draw completion and display-buffer swap when measuring command latency. |
| Modes and allocation | QL/32 supports 320x200, 320x240, 400x300, 640x400, and 640x480. Mode changes roll back on failure. 640x480 double buffering is rejected after a reproducible FabGL viewport corruption on a later mode change. | Heap figures in the Olimex graphics plan were captured at different startup stages. Record comparable checkpoints after all runtime services start. |
| Custom scanout | The opt-in Olimex experiment keeps a logical framebuffer in PSRAM and uses internal DMA RAM for descriptors and a reusable line ring. It tracks frame/row/refill counts, late refills, and unexpected interrupts. | Low-transition moving bars passed repeated checks under PSRAM write load. High-transition checker and broad-band patterns have lost sync; runtime integration is not qualified. |
| Portable graphics | BASIC and the VM use retained indexed canvases and a platform-neutral graphics service. | Keep scanout conversion and hardware timing inside the Olimex adapter. |

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
  1.04-era firmware.
- Current Agon Platform documentation includes later behavior, including
  double-buffer VSYNC commands and experimental scanout-time hardware sprites.
- Record which revision supports each finding; do not attribute later features
  to the original Olimex VDP source.

Primary references:

- [Agon VDP architecture](https://agonplatform.github.io/agon-docs/VDP/)
- [Agon screen modes and VSYNC swap](https://agonplatform.github.io/agon-docs/vdp/Screen-Modes/)
- [Agon system commands](https://agonplatform.github.io/agon-docs/vdp/System-Commands/)
- [Agon VDP memory variables](https://agonplatform.github.io/agon-docs/vdp/VDP-Variables/)
- [Agon scanout-time palette conversion](https://agonplatform.github.io/agon-docs/vdp/Copper-API/)
- [Agon software and hardware sprite limits](https://agonplatform.github.io/agon-docs/vdp/Bitmaps-API/)
- [Agon Platform VDP repository](https://github.com/AgonPlatform/agon-vdp)

## Comparisons and measurements

### 1. VSYNC and scheduler impact

Compare Agon's explicit wait/swap-at-VSYNC operation with FabGL's
`Canvas::swapBuffers()` and QL/32's actual presentation call sites.

For the QL/32 320x200 double-buffer profile:

- Capture elapsed time for 1,000 full-frame swaps with drawing disabled and
  with a small dirty update. Report median, p95, p99, maximum, and measured
  display refresh period.
- Repeat with automatic updates, `GUPDATE 0` followed by explicit `GUPDATE`,
  and active frame skipping. Count presentation requests and actual swaps.
- Run at least two scheduled BASIC workers while one session draws. Record
  worker progress and maximum interval between worker slices during the swap
  workload.
- Verify that one explicit update produces at most one buffer swap and that
  skipped/suppressed updates do not cause hidden extra swaps.

**Pass criteria:** every accepted frame swap is synchronized to a FabGL VSYNC
event; the 99th-percentile swap wait does not exceed one refresh period plus
2 ms; the second worker continues to make progress; and QL/32's update counters
match the issued/presented/skipped requests exactly. If a platform wait blocks
the cooperative scheduler longer than that, record it as a scheduling issue
for QL/32 rather than treating Agon's separate VDP behavior as equivalent.

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
4. After ten cycles through 320x200 single, 320x200 double, and 640x480
   single-buffered modes.
5. After rejecting 640x480 double buffering, confirming that the prior working
   mode and allocations remain valid.

Run three firmware boots and report median and range for each checkpoint. Also
report estimated image bytes separately from measured controller/descriptor
overhead. Use the actual framebuffer count, stride, and format rather than
assuming all modes cost exactly width × height bytes.

**Pass criteria:** mode changes either succeed with valid geometry or fail and
restore the prior mode; ten cycles show no monotonic free-heap or largest-block
loss beyond 1 KiB; and post-startup internal/DMA reserve is explicitly reported.
Do not use Agon's memory fallback as a reason to silently degrade a QL/32 mode;
preserve QL/32's explicit success/error result.

### 3. Scanout deadlines and PSRAM contention

Compare Agon's documented row-by-row conversion approach with QL/32's DMA
producer and refill ring. The key measure is deadline margin, not average
rendering throughput.

Use the opt-in QL/32 diagnostic profile at its qualified 320x200 mode. For each
pattern, collect three 60-second runs with and without a concurrent PSRAM write
worker:

- Static palette bars with a moving row marker (known passing reference).
- Full-width alternating bands and row-varying checkerboard (known failing or
  unresolved stress patterns).
- A moving full-width gradient or equivalent high-transition pattern.

Record frames, rows, refill batches, maximum refill duration, maximum backlog,
late refills, spurious interrupts, dropped/reused frames, and PSRAM stress bytes.
Capture monitor output for each run and note sync loss, rolling, tearing, or
color instability.

**Pass criteria:** the reference pattern remains stable for all runs with zero
late refills and zero unexpected interrupts; no tested pattern may be described
as qualified if any run loses sync or rolls. A high-transition failure remains
an open defect even if aggregate frame rate is nominal. Compare the same
patterns on the FabGL double-buffer profile as a known-good display reference,
but do not compare its internal mechanism to QL/32's ring-buffer refill rate.

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

For every finding, use this table:

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
