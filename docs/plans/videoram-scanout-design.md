# Videoram and VGA scanout design

## Summary recommendation

Keep the retained video image in PSRAM, but do not expose the buffer currently
being scanned as a writable shared surface. Let a session-owned graphics
service compose updates into a non-active frame and publish them; let the
Olimex adapter adopt a published frame at a logical frame boundary, refill an
internal DMA line ring from it, and let I2S DMA read only that ring. Stage
palette edits and apply them at the same boundary. This preserves asynchronous
VM progress without making the VM or language frontend depend on ESP-IDF,
FabGL, DMA, or board memory details.

The evidence supports this as the lowest-risk architecture, not as proof that
the current experimental scanout is production-ready. In particular, the
400x300 base test environment is not the previously qualified 150-row/core-1
variant, and broad/high-transition patterns have lost sync. FabGL remains the
qualified fallback.

## What exists today

`src/platform/olimex_vga/PsramDmaScanout` allocates three logical frames in
PSRAM and a reusable output line ring, descriptors, and blanking lines in
internal DMA-capable RAM. For indexed-4, each logical frame uses two pixels per
byte; the refill path maps those indices through a 16-entry palette and
encodes output bytes. The ring contains already encoded pixels plus sync bits;
I2S DMA does not read the PSRAM frame directly. The ordinary PSRAM/FabGL path
is separate and remains the recovery/default profile.

The current update path is already staged rather than a single shared writable
frame:

1. `beginFrameUpdate` grants a non-active PSRAM slot; when no pending slot is
   available it first catches up the slot's stale damaged region.
2. The presenter writes rows to that slot, then `publishFrameUpdate` records
   damage and marks it pending. A later update can reuse a pending, not-yet-
   visible slot.
3. While refilling the first logical row of a frame, the worker adopts the
   pending slot and updates the active-frame index. It records stale damage
   for the other slots so they can be caught up before reuse.
4. Palette edits use a two-bank `PaletteExchange`; the refill worker applies
   the published palette and rebuilds the output LUT at a frame boundary.

The interrupt is intentionally small: it acknowledges I2S EOF, checks whether
the next ring slot is ready, updates counters, and notifies the refill task.
It is `IRAM_ATTR` and allocated with `ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM`;
that is **not** a highest-priority interrupt level. The normal refill task is
priority 4 on the default core; opt-in high-resolution builds pin it to core 1.
The experimental IRAM option places parts of the refill worker in IRAM, but
does not make the worker's PSRAM data accesses cache-independent.

`PsramFramePresenter` is the platform-side bridge from composed graphics rows
to the scanout. `src/language` and `src/vm` remain hardware-independent under
ADR-001. A future BASIC-visible videoram must retain that boundary and keep
buffer lifetime, updates, and cancellation scoped to its owning session.

## Evaluation of the proposed shared videoram

| Choice | Benefits | Risks / current evidence |
| --- | --- | --- |
| One PSRAM buffer directly writable while scanout reads it | Lowest memory use; writes can begin immediately. | A scanout refill can read a row while the VM changes its pixels, producing a mixed row or frame. Updating palette and pixels independently can also expose mismatched generations. Direct writes do not provide the frame-boundary visibility rule the current triple-buffer path provides. |
| PSRAM retained image plus staged frame publication (current design) | Keeps a large framebuffer out of scarce internal DMA RAM; frame and palette changes become visible at defined boundaries; existing damage and pending-frame reuse avoid some copies. | Three buffers and stale-region catch-up consume PSRAM bandwidth and require correct ownership/damage accounting. A bad catch-up or publication race can still cause artifacts; a previous dirty-region catch-up prototype was rolled back after artifacts/reset. |
| Internal-RAM framebuffer | CPU and DMA access are fast and predictable; avoids PSRAM traffic on refill. | The ESP32's contiguous DMA-capable heap is constrained. Existing FabGL allocation shortens 400x300/640x400/640x480 viewports rather than fitting full-height 8-bit frames; the 640-wide full modes require 256/307 KiB before other allocations. |
| Packed indexed-4 versus 8-bit RGB222 | Packed-4 halves bytes per pixel versus RGB222 and enables palette cycling without rewriting pixel memory. | Requires nibble-aware drawing and row encoding; the encoder needs a pair LUT and the board's three RGB output pins expose eight physical colors, not sixteen simultaneous colors. High-transition patterns have still caused sync loss. Eight-bit RGB222 simplifies pixels but doubles PSRAM bandwidth/storage versus packed-4. |

For packed-4, one 400x300 frame is 60,000 bytes and three are 180,000 bytes;
three 640x400 frames total 384,000 bytes, matching the 640x400 serial startup
log. The raw internal line ring is one output byte per pixel, so a 100-row
640-wide ring alone is 64,000 bytes, in addition to descriptors and blanking
buffers. The source format is a memory/storage decision, not a way to make
PSRAM directly DMA-capable.

### Visibility and tearing

Do not treat “asynchronous writes” as permission to mutate the active source.
The producer should acquire a staging frame, write only while it owns it, then
publish it. The adapter should accept a publication once, switch at the
documented logical frame boundary, and return or retire the old slot only when
it is no longer needed for scanout or delta catch-up. Define what happens when
an update is cancelled, superseded before display, or cannot acquire a slot.
The current presenter returns failure when it cannot acquire or publish a
frame; it does not silently present partial data.

If a future single-buffer mode is needed for memory reasons, make tearing an
explicit QL/32-specific mode and test it separately. Do not silently change
the existing frame-completion or `GUPDATE` visibility semantics.

## Scanout priority, IRAM, and deadlines

The CPU is not responsible for driving every pixel: I2S DMA streams descriptors
from the internal line ring. The EOF ISR only signals completed rows; the
priority-4 task encodes rows from PSRAM into slots that DMA will consume later.
Therefore “put the VGA driver at highest priority in IRAM” is not a single
change: ISR priority/placement, refill-task scheduling, refill deadline, and
PSRAM availability are separate constraints. Raising task or interrupt
priority cannot compensate for a refill that needs more time or is blocked on
the shared external-memory path.

For a given mode, the refill deadline is bounded by the usable ring lead in
logical rows times the logical row period (physical line time multiplied by
the mode's scan count), less the current batch's work and scheduling stalls.
Use the selected mode's actual timing and measured batch maxima; do not infer
that a deeper ring is sufficient just from its row count. The 640x480
diagnostic uses a 120-row ring / 60-row refill group. Measurements give about
31.8 us per logical row and roughly 3.8 ms for two refill groups of lead.
The 640x400 profile uses a 100-row ring / 50-row group. The 400x300
`*_ring150` profile uses 150 / 75 and core 1; the base
`*_400x300_packed4` test profile does not select those overrides and therefore
uses the compile-time 100 / 50 defaults on the default core.

IRAM protects interrupt execution from instruction-cache stalls only for code
and data that are actually resident in suitable memory. It does not make
PSRAM reads safe while the cache is unavailable. In the current design the
ISR does not read PSRAM, which is the safer boundary; the task reads PSRAM
when normal memory access is available. The repository has no evidence that
moving the refill path to IRAM makes PSRAM source access independent of cache
or bus stalls.

Measured results argue against changing pinning or ring depth by intuition:

- The 640x480 standard placement measured 0 late refills at idle; splitting
  BASIC onto core 0 produced 14–16% late refills at idle, 62–67% CPU-only, and
  46–47% under an attractor. The split path ran presentations much faster but
  flickered.
- Putting the refill path in IRAM barely changed those split results.
  Bulk-copying a source row to internal RAM reduced some lateness (for example
  46% to 19.4% during an attractor) but the owner still saw blinking and it
  did not meet the refill-time target. Smaller batches and priority changes
  likewise have not established a general fix.
- The 400x300 150-row/core-1 packed-4 diagnostic previously ran 5,195 frames
  and 1,558,725 logical rows with zero late refills and zero spurious
  interrupts under concurrent PSRAM writes. That result applies to that
  profile and low-transition pattern; it does not qualify the base 100-row
  environment or the QL/32 runtime.
- In the 2026-11-21 staged check, the base 400x300 packed-4 diagnostic
  (100-row ring, core 0) reported zero late refills and zero spurious IRQs in
  10,868 EOF events, 59 palette applications, and about 139.5 MB of PSRAM
  stress writes; the owner saw stable output and smooth cycling. This is a
  35-second diagnostic capture, not sustained/runtime qualification. The
  640x400 base diagnostic (100-row ring, core 1) reported 2,355 late refills
  in 16,840 EOF events (about 14%) and the owner saw top/bottom marker
  flicker. The subsequent 640x400 gradient/stress image looked stable to the
  owner, but its serial capture did not provide refill counters. See
  `docs/test-results/videoram-scanout-analysis-2026-11-21.md`.
- The 640x400 profile has logged zero late refills in a no-stress capture, but
  that is not proof of high-transition stability. Earlier high-transition
  checker/band cases lost sync, including with a 64-row ring. Recent 640x400
  gradient stress profiles exist specifically to retest that risk.

Treat these as measured profile results, not universal hardware guarantees.
The current evidence does not establish that the main limiter is instruction
cache, refill priority, or PSRAM bandwidth alone. More lead can tolerate
occasional stalls, but increases scarce internal DMA usage and cannot prevent
eventual underrun if average refill throughput is insufficient.

## Updating videoram: CPU, copies, DMA, and dirty regions

CPU writes and `memcpy` are the established mechanisms for the logical PSRAM
frame. Use clipped row/span operations, aligned packed-pixel paths, and
`memcpy`/`memmove` where overlap and packing rules allow; preserve explicit
ownership and publish ordering. The framebuffer benchmark is workload-specific:
10 packed-4 full clears took 3,617 us versus 45,140 us for RGB222, while
200 packed-4 80x40 fills took 5,738 us versus 5,458 us for RGB222. Packed-4
also improved the measured random-pixel, line, scroll, and opaque-blit cases,
but a packed-4 full-frame translation still took 493,901 us for 100
iterations. These results do not prove every update is faster.

The current ESP32 scanout DMA consumes internal DMA-capable ring memory. The
logical PSRAM frames are CPU-accessed, and the repository's design explicitly
does not require PSRAM-to-PSRAM DMA. No usable general memory-copy DMA path
with PSRAM source and internal destination has been demonstrated here. Treat
DMA copy as unproven for this chip/profile, not as the default update path.
Before pursuing it, verify the exact ESP32 peripheral/IDF endpoint capabilities
and memory restrictions; then compare against CPU row-copy telemetry. The
existing “bulk copy” experiment is a CPU `memcpy`, not DMA.

Dirty rows/rectangles can reduce composition and write traffic, and the current
presenter already accepts a region. But damage bookkeeping is not enough on its
own: each staging slot can lag by a different set of updates. The current
`staleFrameDamage_` catch-up tracks that state; an earlier per-buffer dirty
catch-up prototype was rolled back after artifacts or a reset, so do not
reintroduce it without a model of slot versions and race-free tests. Measure
clone bytes, dirty bytes, row-provider time, write time, refill maxima, and
late counts together. A lower late-refill counter is not sufficient if visible
corruption appears.

The scanout worker uses a memory barrier before marking each encoded ring slot
ready, and the ISR observes the ready sequence. There is no evidence for a
separate cache flush/invalidate protocol for the internal DMA ring in this
code. Keep this ordering explicit; do not assume that a volatile pointer alone
would establish producer/consumer visibility. PSRAM source visibility across
cores and cache-disabled intervals must be checked against the exact
ESP-IDF/ESP32 memory model before adding cross-core writers.

## Smallest useful experiments

1. On the existing 400x300 base packed-4 test, capture startup allocation,
   selected ring/core, late-refill and spurious-IRQ deltas, and owner-reported
   sync during a static view and the built-in palette animation. Stop on sync
   loss; restore FabGL before trying another profile.
2. Only if 400x300 remains stable, test the existing 640x400 packed-4 profile,
   then its gradient/stress profile as the high-transition probe. Capture
   `refill-max-us`, `refill-row-max-us`, batch/backlog maxima, late-refill
   deltas, PSRAM stress bytes, and visual sync. Keep the 400x300 result and
   640x400 result separate; do not change ring/core settings to improve a
   result during the run.
3. To settle publication semantics, use a small deterministic test that writes
   a recognizable frame into a staging slot while another frame is displayed,
   publishes once, and checks that the owner sees only the old frame then the
   new frame at a frame boundary. Record whether superseded updates coalesce
   and how palette+pixel generations align.
4. To evaluate CPU versus copy/DMA, time identical aligned and unaligned
   dirty-row workloads under active scanout, with and without competing PSRAM
   writes. Compare CPU direct operations and CPU bulk `memcpy`; investigate DMA
   only after endpoint support is confirmed. Record throughput, clone/damage
   bytes, heap, maximum batch time, late counts, and visible output.

These are measurement proposals, not approval to change scanout geometry,
timing, core pinning, or refill priority. Do not add a fast path until its
hypothesis predicts a measurable change and the telemetry/visual check can
falsify it.

## References and evidence quality

- Architecture: `AGENTS.md`, `docs/decisions/ADR-001-portable-core.md`.
- Implementation: `include/ql32/platform/olimex_vga/psram_dma_scanout.hpp`,
  `src/platform/olimex_vga/psram_dma_scanout.cpp`,
  `include/ql32/platform/olimex_vga/frame_presenter.hpp`,
  `src/platform/olimex_vga/frame_presenter.cpp`, `palette.hpp`,
  `scanout_geometry.hpp`, and `platformio.ini`.
- Prior design/status: `docs/olimex-vga-graphics-plan.md`,
  `PROJECT-HANDOFF.md`, and
  `docs/plans/olimex-vga-640x480-dma-investigation.md`.
- Measurements: `docs/test-results/psram-dma-framebuffer-benchmark-2026-11-14.md`,
  `docs/test-results/videoram-scanout-analysis-2026-11-21.md`,
  `docs/test-results/olimex-split-core-late-refill-diagnosis-2026-11-20.md`,
  `docs/test-results/olimex-refill-optimisation-2026-11-20.md`,
  `docs/test-results/olimex-refill-bulk-copy-2026-11-20.md`,
  `docs/test-results/olimex-final-mode-matrix-stress-2026-11-21.md`, and
  `docs/test-results/olimex-fast-modes-matrix-speed-analysis-2026-11-20.md`.

The requested `docs/test-results/olimex-vga-mode-reliability-2026-11-21.md`
is not present in the fetched `origin/main`; current mode/reliability evidence
is in the final-mode-matrix report and the graphics-plan history cited above.
All numeric results are measured for their documented profile/workload.
Descriptions of tearing under hypothetical direct concurrent writes, DMA
copy feasibility, and cross-core/cache visibility are design risks or open
questions unless explicitly identified above as measured.
