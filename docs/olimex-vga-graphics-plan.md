# QL/32 Olimex VGA graphics plan

## Goal

Build a fast graphics subsystem for the Olimex ESP32-SBC-FabGL target that
draws directly into PSRAM framebuffers, uses internal DMA-capable memory only
for scanout staging, and keeps the existing FabGL display path available as a
fallback. Keep the BASIC language, VM, and portable runtime independent of
FabGL and ESP-IDF types.

## Current checkpoint

- `olimex_esp32_sbc_fabgl_psram_dma_test` is the isolated scanout experiment;
  normal Olimex profiles still start FabGL.
- The experiment currently uses a 320x200 PSRAM source, a 100-row internal DMA
  ring, and an EOF-driven refill worker. Fifty logical rows are refilled per
  wakeup; the mode scans each logical row twice. The ring height divides the
  200-row frame so the descriptor slot phase repeats at each frame boundary.
  The active diagnostic
  uses a narrow changing row marker, slowly scrolling color bars, and a
  concurrent PSRAM write worker.
- Its serial report includes frame/row/refill counts, late refills, unexpected
  interrupts, and PSRAM stress traffic.
- The current Olimex pin map lists one GPIO per RGB channel (GPIO 21, 18, 5),
  which provides eight directly selectable output colors. Confirm the board's
  VGA circuitry and available signals before promising 64 visible colors or a
  16-entry palette chosen from all 64 RGB222 colors.
- The owner confirmed the original static color bars and normal profile are in
  sync. Live refill with an 8-pixel row marker and static bars is also stable;
  the owner sees the marker as about 12 pixels of horizontal bars at the left.
  Slowly scrolling the bars with concurrent CPU writes to a separate PSRAM
  buffer also remained stable. During each 9-second capture, stress writes
  exceeded 44 MB; the latest counters showed zero late refills and zero
  spurious interrupts. The tested modeline is 320x200 at 12.5875 MHz and a
  nominal 70.24 Hz. This confirms continuous PSRAM reads for this low-transition
  pattern under write load.
- Row-varying checker and broad-band test patterns have caused monitor sync
  loss, including with a 64-row ring. The simple row marker plus moving bars
  passes; the cause of the other patterns' sync loss is still unknown and they
  are not accepted as representative output yet.
- The earlier RGB222 diagnostic run reports 252,616 bytes free DMA heap
  (110,580-byte largest block) before scanout allocation and 212,764 bytes
  afterward with the stress worker enabled. Scanout used 31,536 bytes of
  internal DMA memory with a 64-row ring; the former 128-row ring used 52,016
  bytes, and the current 100-row ring uses 43,056 bytes. The RGB222 and
  packed-indexed framebuffers use 64,000 and 32,000 bytes of PSRAM respectively;
  the separate stress buffer uses
  65,536 bytes.
- The first QL/32 runtime integration profile failed physical qualification:
  the no-pattern profile lost sync, and the diagnostic runtime image rolled or
  showed artifacts. Starting DMA after PS/2 and storage setup and doubling the
  ring to 128 rows eliminated late-refill counts in the stress-disabled test
  (0 late refills across about 41,000 rows), but the owner still observed a
  rolling image. The actual non-diagnostic profile also rolled or artifacted
  even after serial REPL commands for clear, line, and circle were accepted.
  The normal PSRAM/FabGL profile has been restored. Do not call Phase 6
  qualified or make DMA scanout the default until the image is stable/readable.
- A FabGL-native fallback is now available as
  `olimex_esp32_sbc_fabgl_psram_double_buffer_test`. It enables FabGL's own
  double-buffered VGA viewport at 320x200, where the existing
  `FabglFramePresenter` writes raw pixels through `getScanline()` into the
  hidden viewport and swaps after a full frame. This avoids the custom
  PSRAM-to-I2S scanout and its rolling image while retaining the fast direct
  row conversion path. The profile was built and flashed, and the owner
  confirmed stable, readable VGA plus visible line, rectangle, and circle
  updates. PSRAM and the BASIC REPL initialized normally, and the five drawing
  commands returned without VM errors. Before the remaining startup services
  and REPL task were created, internal free heap was 79,672 bytes with a
  40,948-byte largest block (230,352 bytes before display allocation). This
  profile is physically qualified for the current 320x200 VGA path; measure
  heap again after all runtime services start before treating its remaining
  internal memory as general-purpose capacity.
- A sustained 100-frame attractor smoke workload using the same recurrence and
  point density completed on this profile at 240 MHz. Its serial start and done
  markers arrived twice with the same finite checksum and no VM error or reset.
  The owner confirmed the animation ran with visible white pixels, matching
  the test's white ink on black paper, and earlier confirmed static line,
  rectangle, and circle updates remained visible. A parser fix now recognizes
  auto-numbered module `CONST` and `OPTION` lines after `LOAD`; the original
  attractor benchmark then completed with timing markers. Three-run medians
  were 41.309 s at 80 MHz/SINGLE, 24.286 s at 160 MHz/SINGLE, 19.778 s at
  240 MHz/SINGLE, and 21.281 s at 240 MHz/DOUBLE. The owner confirmed stable
  VGA at 80 and 160 MHz but reported blank/out-of-range when the program
  applied `OPTION KEspCpuFreq240`. A prior 240 MHz run without the runtime CPU
  option was visually stable. The ESP32 CPU-frequency service now skips the
  clock API call if the requested rate is already active. On 2026-11-13 the
  owner confirmed stable VGA through three 240 MHz runs and a live 160-to-240
  MHz transition; all runs completed. A proposed FabGL restart after clock
  changes failed because it could allocate only a 320x92 viewport while a
  program was loaded. That experiment was removed. Runtime CPU frequency
  changes are qualified on the 320x200 double-buffer profile for these tested
  transitions. See
  [attractor results](test-results/attractor-performance-2026-11-12.md).
- The row presenter is still a transport prototype: `WindowedService` composes
  its existing retained canvases and the presenter copies the changed region
  into a staged PSRAM frame. The new framebuffer primitives are not yet the
  backing store for BASIC drawing, and the Phase 6 full benchmark matrix has
  not been run. Runtime acceleration therefore remains incomplete.
- Phase 2 is complete in the test backend. Its display-library-independent
  `FramebufferView` carries width, height, byte stride, and pixel format.
  RGB222 rows use one byte per pixel; packed indexed-4 rows use two
  high-nibble-first pixels per byte. PlatformIO profiles select either format
  without changes to BASIC or VM code. Both formats rendered stable moving
  bars on the monitor under concurrent PSRAM writes, with zero late refills
  and zero unexpected interrupts. Per-pixel conversion initially missed packed
  refill deadlines; a precomputed 256-entry pixel-output table fixed that.
- Phase 3 now has clear, clipped pixel/line/rectangle fills, overlap-safe row
  and rectangle copies, and opaque bitmap copies for both formats. RGB222 uses
  row `memset`/`memmove`; packed-4 paths preserve nibble order and handle odd
  widths. Host-side tests cover clipping, odd-width packing, overlap, line
  drawing, bitmap copies, and palette operations. The Olimex benchmark waits for FabGL's
  asynchronous Canvas work to finish before stopping its timer. At 320x200,
  50 full clears took 7,299 us through Canvas, 223,591 us into PSRAM RGB222,
  239,642 us using aligned word stores, and about 6,000 us into packed-4
  PSRAM across repeated runs.
  This suggests the 32 KB packed buffer benefits from fitting in cache, but
  that needs validation under active scanout and competing PSRAM traffic.
  Phase 4 has started with packed-4 fill spans that write aligned pixel pairs
  using one repeated-byte fill while preserving boundary nibbles. For 1,000
  80x40 fills, Canvas took 53,625 us; RGB222 PSRAM took 17,009 us; packed-4
  took 15,992 us with even-aligned bounds and 23,345 us with odd x bounds.
  This confirms the nibble-edge cost
  and makes alignment important. 20,000 random RGB222 pixels took 893,688 us
  through Canvas and 52,411 us direct. Measurements remain single-run. Phase
  3's 8-bit full clear is still slow; continue palette/indexed-4 work and
  validate clear and fill under active scanout load.
- The native palette core now has 16 RGB222 entries, set/load, signed circular
  rotate, directional shift with inserted color, component-wise fade, and
  save/restore. A double-bank exchange stages changes and exposes them only
  when the scanout refill worker prepares the next frame's first row. Indexed-4
  scanout reads through the active palette. BASIC `PALETTE`/`RGB` and manual
  palette rotate/shift/fade commands are implemented and verified over serial;
  active-scanout palette behavior and broader image patterns remain pending. Palette
  unit tests pass and both RGB222 and packed-4 diagnostic firmware profiles
  compile. On repeat runs of the same firmware revision, the owner confirmed
  stable moving bars for both RGB222 and packed-4. Serial reports showed zero
  late refills and zero spurious IRQs under concurrent PSRAM stress. An earlier
  no-sync report did not reproduce. The packed-4 diagnostic now rotates its
  staged palette every 500 ms. The owner confirmed the bars stayed stable and
  colors changed at each step; serial showed palette-apply counts increasing
  with zero late refills or spurious IRQs. Phase 4's core and low-transition
  palette animation qualification pass. The QL/32 command interface supports
  direct range replacement and program-controlled rotation, shifting, and
  fading; a timer is not used. BASIC `LOADIMAGE`, transparent `BLIT`, and
  `UNLOADIMAGE` now use the QL/32-specific QIMG v2 indexed-4 format with an
  embedded RGB palette. Host tests passed. On 2026-11-15 the Olimex PSRAM image
  was rebuilt/flashed; YMODEM upload, `LOAD`/`BLIT`/`UNLOADIMAGE`, and
  SCREEN-capture checks passed. The capture showed the expected colors and
  transparent pixels revealing the contrasting background. A three-channel `TILE`
  fixture passed over serial and the owner confirmed three separate tiled
  areas on VGA. The owner also visually confirmed that a `MENABLE`-disabled
  menu item is dimmed. See the [bitmap test results](test-results/bitmap-load-blit-2026-11-14.md) and
  [system/UI test results](test-results/system-info-and-dynamic-ui-2026-11-14.md).
- Phase 5 has started in the portable framebuffer core: transparent bitmap
  blits, framebuffer-to-bitmap copies, vertical scroll with exposed-row clear,
  indexed-4 translation, clipped general lines, circle outlines/fills, and
  opaque glyph rows are implemented for the supported formats. The QL/32
  packed retained canvas now caches its arena data pointer instead of searching
  the canvas list for each pixel access. Filled rectangles and circles write
  packed spans with byte fills; aligned canvas scrolls use overlap-safe
  `memmove`. Host graphics tests pass, and the Olimex double-buffer profile
  builds, flashes, and stays stable/readable. A saved BASIC workload
  ([graphics-fill-benchmark.bas](../examples/graphics-fill-benchmark.bas)) ran
  4,000 64x32 filled rectangles in a 3.005 s median and 2,000 radius-12 filled
  circles in a 1.802 s median at 240 MHz on 320x200. A saved
  [graphics-scroll-benchmark.bas](../examples/graphics-scroll-benchmark.bas)
  scrolled 1,000 text lines in a 4.408 s median. It ran with `GUPDATE 0`, so
  only the final frame was shown; the scrolling passed too quickly to observe.
  The owner reported stable VGA with no apparent glitches and considered the
  final text likely correct. These are end-to-end measurements. A comparable
  draw-only attractor retest measured 11.433 s median at 240 MHz, 18.5% faster
  than the earlier 14.021 s median; this does not isolate the individual
  canvas changes. The owner saw pixels during the retest. Comparable pre-change
  fill, circle, and text-scroll timings are still not available.
  A current full-attractor retest at 240 MHz measured 19.820 s SINGLE and
  20.673 s DOUBLE medians across six runs each, with large timing clusters.
  three-run paired sine/cosine sum-identity rewrite was slower in every pair
  (23.821 s median versus 18.359 s for the original). The inline version was
  also slower (22.774 s versus 18.412 s median). Keep both as experimental
  source files; neither is used by the demo.
  The CPU-only precision test was stable at 9.430 s SINGLE versus 10.789 s
  DOUBLE median, supporting the SINGLE default in `attractor.bas`;
  `attractor-double.bas` retains the DOUBLE option. A 20-line, no-scroll PRINT
  test at 240 MHz took 1.708 s with automatic updates and 0.097 s with
  `GUPDATE 0` plus one final `GUPDATE` (17.6x faster). Automatic output on the
  320x200 double-buffer profile recomposes and copies a full frame for each
  text update because partial swaps would expose stale pixels. Batch program
  output with updates disabled and present once at the end when intermediate
  lines need not be visible.
  This does not yet improve `GUPDATE 1`: Phase 6 must reduce the automatic
  `PRINT` path's update cost and remeasure it against the 1.708 s, 20-line
  no-scroll baseline. A partial double-buffer update is acceptable only if it
  keeps both buffers synchronized and passes physical stability checks.
  The 240 MHz attractor completed again in 21.433 s with stable VGA; its
  math-heavy point workload does not directly exercise the new fill and scroll
  paths. Both the normal Olimex and packed-4 PlatformIO environments
  compile. A single Olimex render-benchmark run measured 1,000 clipped/general
  lines at 138,834 us direct versus 301,084 us through Canvas. Five hundred
  filled circles took 38,905 us direct versus 23,593 us through Canvas, so
  circle filling still needs work. One hundred up/down scroll pairs took
  1,086,409 us after switching to a contiguous `memmove`, down from 1,206,434
  us. One thousand full-screen packed-index translations took 4,326,459 us
  before a 256-entry packed-byte lookup was added, and about 3,274,000 us
  after it. These are single-run figures and need repeated measurement under
  active scanout before drawing performance is qualified. The same benchmark
  measured 500 filled circles at 22,688 us in packed-4 versus 23,604 us through
  Canvas and 38,776 us in RGB222. One hundred up/down scroll pairs took 339,332
  us in packed-4, about one-third of RGB222's 1,084,965 us. Packed-4 is the
  current scanout candidate; repeat measurements and active-scanout rendering
  remain necessary.

## Decisions and rules

1. Keep the custom scanout behind its own Olimex PSRAM test/profile until it
   passes physical validation. Do not replace the default FabGL startup yet.
2. Use FabGL's existing `VGAController` as the near-term fast-rendering
   fallback: directly fill its drawing viewport through `getScanline()` and
   swap full frames. Test this before investing further in custom VGA timing.
   Keep PSRAM as the retained BASIC window/framebuffer storage; copy composed
   rows into FabGL's internal DMA viewport for output.
3. Continue custom target-local scanout only as a separate experiment. Consider
   a FabGL `VGA16Controller` PSRAM extension only if measurements show it is
   simpler or more reliable than maintaining the target-local backend.
4. Separate logical framebuffer formats and drawing from VGA byte encoding.
   The renderer owns indexed pixels; the Olimex scanout adapter translates
   pixels, palette, blanking, and sync into DMA rows.
5. Treat PSRAM as CPU-accessed memory on the original ESP32. Use I2S DMA only
   from internal DMA-capable scanline buffers; do not make PSRAM memory-to-
   memory DMA a requirement.
6. Do not implement higher resolutions until the 320-wide path meets its
   output and timing criteria.

## Phases

### 0. Build and preserve the baseline

- Build the diagnostic profile and the normal Olimex PSRAM FabGL profile.
- Record flash/RAM use, internal DMA-capable free heap, PSRAM use, selected
  modeline, pixel clock, and calculated refresh rate.
- Keep the FabGL profile bootable as the known-good recovery path.

**Exit:** both profiles compile; the diagnostic profile remains opt-in and does
not alter normal Olimex startup.

### 1. Validate physical scanout and continuous PSRAM reads

- With the board and monitor, flash the diagnostic profile and confirm
  horizontal/vertical sync, stable geometry, color ordering, and no visible
  tearing, line shifts, or periodic corruption.
- The diagnostic refills the internal line ring from PSRAM and reports
  late-refill and interrupt counts. The row marker and slowly moving bars have
  been accepted visually, including during concurrent CPU PSRAM writes.
- A broad-band and checker pattern still loses sync. Keep those patterns as an
  open hardware/debug item; do not use them to qualify later pixel formats.
- Verify ISR/cache safety. If the ESP32 can make PSRAM inaccessible during a
  cache-disabled interval, keep such accesses out of the ISR and use a safe
  producer/staging strategy instead.
- Resolve the color wiring question from the board schematic and hardware.
  Keep the current result labeled as 8-color if only three RGB inputs are
  connected.

**Exit:** the low-transition 320x200 output and moving row marker are stable
under concurrent PSRAM writes, with no reported late refills or spurious
interrupts. Timing and the current eight-color hardware limit are recorded.
High-activity checker and broad-band patterns remain an open qualification
item.

### 2. Establish framebuffer and scanout interfaces

- Add an Olimex-local framebuffer abstraction with explicit width, height,
  stride, and pixel format. Keep scanout buffers and descriptors internal;
  allocate the logical framebuffer from PSRAM.
- Define two formats: 8-bit storage for RGB222 color values (one byte per
  pixel, six meaningful bits) and packed 4-bit indices (two pixels per byte).
- Define a backend boundary that lets the renderer present a framebuffer
  without including FabGL types in portable interfaces. Leave `Canvas` and
  existing rendering behavior intact as fallback during migration.
- Specify clipping at API boundaries and row-based work chunks for long
  operations so one graphics command cannot starve the cooperative VM.

**Exit:** both framebuffer layouts have documented stride/packing rules and
can be selected in the test backend without changing language or VM code.

### 3. Implement 8-bit direct rendering first

- Add fast clear, pixel, horizontal/vertical line, rectangle fill, row copy,
  and opaque bitmap copy using direct row pointers and byte/word operations.
- Use overlap-safe copy direction for screen-to-screen moves. Use CPU
  `memcpy`/`memmove` for PSRAM; measure before adding staging or special copy
  paths.
- Keep RGB222-to-electrical-output mapping in the VGA backend. Do not assume
  that six logical color bits are physically available until Phase 1 confirms
  the board supports them.

**Exit:** correctness checks cover clipping and row boundaries; benchmark shows
measurable improvement over the existing FabGL Canvas path on the Olimex
target.

### 4. Add 4-bit indexed rendering and palette operations

- Pack two pixels per byte and use aligned byte/word fills for spans; handle
  odd start/end pixels with nibble masks.
- Add a 16-entry screen palette whose values select from the verified hardware
  color set. If the board exposes only eight colors, document that limit and
  map/duplicate entries accordingly until hardware changes.
- Implement `PALSET`, `PALLOAD`, signed circular `PALROT`, directional
  `PALSHIFT` with an inserted color, fade-to/from-color, and save/restore.
- Use a shadow palette or conversion table and swap it at vertical blank so
  scanout never sees a partially applied palette. Palette changes should not
  rewrite framebuffer pixels.

**Exit:** indexed framebuffer renders correctly; palette updates are atomic;
rotation and fade animate the image without touching pixel memory.

### 5. Complete blitter and drawing primitives

- Add fill/copy rectangles, screen-to-bitmap and bitmap-to-screen copies,
  vertical scrolling, transparent bitmap blits, and packed 4-bit translation
  tables.
- Add direct line, circle, filled-circle, and glyph-row text renderers. Use
  spans for horizontal runs and filled circles; avoid calling the public pixel
  operation inside inner loops.
- For full-width vertical scrolling, rotate row pointers and clear only newly
  exposed rows. Keep pointer rotation internal to the framebuffer object.
- Keep large jobs bounded by rows or tiles when needed by the cooperative
  scheduler.

**Exit:** operations pass edge/overlap checks and improve measured throughput
over the Canvas implementation.

### 6. Integrate with QL/32 and qualify performance

- Connect the accelerated operations through the existing graphics service
  and channel `#1`; keep existing BASIC behavior and FabGL fallback available.
- Add presentation-request skipping as `GUPDATE SKIP,n`: after each displayed
  frame, skip the next `n` automatic or explicit presentation requests. `n=0`
  presents every request. Keep `GUPDATE 0`, `GUPDATE 1`, and bare `GUPDATE`
  semantics unchanged apart from applying the configured skip count. Retained
  drawing continues during skipped presentations, so the next displayed frame
  contains the latest canvas state. Count requests rather than elapsed time or
  hardware refresh ticks; this is the only planned rate control, with no FPS
  mode.
- Coalesce the output fragments of each BASIC `PRINT` statement into one
  graphics write/presentation while preserving channel, cursor, and output
  ordering. Measure `GUPDATE 1` with 20 no-scroll lines and with scrolling,
  comparing against the current 1.708 s no-scroll result. Keep the existing
  `GUPDATE 0` plus final `GUPDATE` measurement as the batching reference.
- If adding dirty-region updates to the double-buffer profile, synchronize
  both FabGL buffers and verify the physical display remains stable before
  relying on partial swaps; otherwise report the full-frame update cost.
- Add BASIC syntax only after the native API and semantics are stable. Mark new
  commands QL/32-specific unless a QL/SMSQ/E reference and conformance tests
  establish compatibility.
- Benchmark both framebuffer modes against the current single-buffer FabGL
  path: full clears, 10k lines, 100k random pixels, 1k fills, 1k 64x64 opaque
  and transparent blits, 100 full-width scrolls, and 1k palette updates.
- Report elapsed time, effective copy rate where meaningful, scanout errors,
  internal DMA-capable free heap, PSRAM used, and resolution/refresh.

**Exit:** documented correctness and performance results on the Olimex board,
including a `GUPDATE 1` PRINT result faster than its 1.708 s baseline with
stable VGA. The default profile changes only if the new path is stable and
demonstrably useful.

**Progress (2026-11-13):** PRINT fragments are now grouped per statement in
`WindowedService`, including trailing separators; error cleanup flushes any
partial statement. Linux graphics, runtime, and scheduler tests pass. The
Olimex PSRAM double-buffer profile built and flashed successfully. The 20-line
no-scroll `GUPDATE 1` test measured 0.853 s on three passes at 240 MHz, versus
the recorded 1.708 s baseline (2.0× faster). `GUPDATE 0` plus final `GUPDATE`
completed in roughly 0.05–0.10 s; serial polling is too coarse to give precise
timings for these short passes. The program completed all three rounds and
returned to the REPL without a reset. A 40-line scrolling `GUPDATE 1` run
completed in 1.706 s and returned to the REPL without a reported BASIC error.
The full CMake all-target build still
has an unrelated FabGL fake-header/API mismatch in `ql32_fabgl_graphics_tests`;
the relevant Linux targets build and pass independently. The owner later
confirmed stable readable VGA during the PRINT benchmark.

**GUPDATE SKIP progress (2026-11-13):** `GUPDATE SKIP,n` is implemented as a
presentation-request counter, independent of elapsed time and VGA refresh.
Linux graphics/runtime/scheduler tests pass; the Olimex double-buffer firmware
built and flashed. Serial REPL smoke checks accepted `SKIP,1` and `SKIP,0`.
The attractor was run with `SKIP,1` in isolation. The owner saw blinking pixels
and no other artifacts. A `SKIP,0` control also showed blinking pixels. The
test task was stopped and the board returned to `SKIP,0` at the REPL. See
[GUPDATE SKIP results](test-results/gupdate-skip-2026-11-13.md).

**GUPDATE 0 regression (2026-11-13):** Olimex testing reproduced Screen B
appearing during a `GUPDATE 0` hold. System-layer invalidation temporarily
forced automatic presentation; on the double-buffer presenter, a partial
present fell back to a full-frame present and exposed pending canvas changes.
Removing that override makes system redraws respect the selected update mode.
Linux regression tests pass, and the flashed Olimex firmware now keeps Screen A
visible during the hold and reveals Screen B only after bare `GUPDATE`. See
[GUPDATE 0 results](test-results/gupdate-off-2026-11-13.md).

**Console and UI update reset (2026-11-14):** returning from a BASIC program or
entering a dialog, menu, pager prompt, or keyboard `INPUT` now restores
automatic updates, clears frame skipping, and presents the composed frame.
The setting remains in effect after the UI interaction until the program
changes it. Linux graphics and scheduler suites pass. The Olimex double-buffer
profile built and flashed; the owner confirmed visible updates through the
alert, keyboard input, and return to the REPL. See
[GUPDATE interaction reset results](test-results/gupdate-interaction-reset-2026-11-14.md).

**Phase 6 PRINT qualification and render work (2026-11-14):** the 20-line
`GUPDATE 1` PRINT workload was repeated on the double-buffer profile at
240 MHz: 0.855, 0.855, and 0.856 s, versus 0.853 s in the prior run and 1.708 s
baseline. Batched output took 0.097, 0.096, and 0.096 s. The owner confirmed
VGA text was stable and readable. The render harness now also covers 100k
random pixels, 10k lines, 64×64 opaque and transparent blits, and palette
exchange. It measured 10k RGB222 lines in 1.387 s versus 5.240 s through
Canvas, and 100k RGB222 random pixels in 0.251 s versus 4.675 s through Canvas.
An aligned packed-4 opaque bitmap path reduced the 1k-blit time from 4.248 s to
0.071 s; transparent packed-4 blits improved from 3.737 s to 1.022 s. The
RGB222 transparent path improved from 3.570 s to 0.777 s, while opaque RGB222
did not improve in this single-run comparison. Host graphics, scheduler, and
framebuffer tests pass. The normal double-buffer profile was restored and its
serial boot confirmed PSRAM, VGA viewport, and the REPL initialized. This
benchmark work still does not connect the new framebuffer primitives as the
BASIC graphics backing store; repeated active-scanout measurements and
end-to-end BASIC comparisons remain. Random-pixel and general-line comparisons
are still RGB222-only. See
[render benchmark results](test-results/graphics-render-benchmark-2026-11-14.md).

**Partial-present optimization (2026-11-14):** the FabGL presenter now accepts
partial regions while double buffering is enabled. It updates the hidden
buffer, swaps at a frame boundary, then copies that region into the new hidden
buffer so both stay synchronized. A host regression checks raw scanline byte
mapping, region isolation, and buffer equality. On the Olimex double-buffer
profile, three 20-line `GUPDATE 1` PRINT runs took 0.289 s each, about 3× faster
than the immediately preceding 0.855 s runs. On the final image, timings were
0.289, 0.283, and 0.283 s. After adjusting synchronization to occur only when
a partial update needs it, batched `GUPDATE 0` output plus a full-frame update
took 0.108 s per run, close to the previous 0.096 s. The profile built,
flashed, and completed the PRINT, rectangle/circle, and finite attractor
workloads. The 100-frame attractor took 21.596 s at 240 MHz, within the prior
19.8–21.4 s range. The owner confirmed stable, readable VGA during the PRINT
and scroll workloads, and stable, correct output for mixed partial drawing.
The 40-line scrolling PRINT workload initially measured 2.192 s median, slower
than its earlier 1.706 s result; dirty-region staging did not improve this
timing. The presenter now falls back to one full-frame update when
synchronizing a large dirty region would write more pixels than that full
update. After this change, three board runs measured 1.455, 1.456, and 1.454 s
(1.455 s median), faster than both earlier results. VGA remained stable and
readable. Aligned staged regions are now reordered to FabGL byte layout once
and copied to each buffer with `memcpy`; the host regression covers byte order
and synchronized buffer contents. The three-pass 20-line no-scroll `GUPDATE 1`
control measured 0.351 s median, slower than the 0.283 s result before the
large-region fallback, but still faster than the earlier 0.855 s result. The
owner confirmed both workloads remain stable and readable. The FFS startup
layout now reserves two text rows for channel #0; the owner confirmed channels
#1/#2 no longer overlap it. See
[partial-present results](test-results/graphics-partial-present-2026-11-14.md).

**Retained-canvas raster integration (2026-11-14):** QL/32's `WindowedService`
can receive optional platform-neutral raster operations, and the Olimex adapter
uses packed 4-bit primitives directly on each retained window canvas. Clear,
pixel writes, in-bounds lines, filled rectangles, in-bounds outline circles,
and scroll are accelerated; unsupported cases keep the portable implementation.
Host byte-comparison tests cover the fallback and injected service paths. On
the flashed Olimex profile, the mixed drawing fixture improved from 3.121 s to
2.860 s and the 4,000 filled-rectangle run from 3.054 s to 2.909 s. The owner
confirmed stable and correct VGA output. The scroll and PRINT workloads did
not show a speedup from these raster operations. See
[canvas raster integration results](test-results/olimex-canvas-raster-integration-2026-11-14.md).

**Filled-circle raster (2026-11-14):** the Olimex packed-4 adapter now uses its
span-based filled-circle routine, including clipped circles; invert and
unsupported canvases retain the portable fallback. Host byte comparisons pass
for centered, clipped, zero-radius, and clear-over circles. Three on-board
2,000-circle runs each took 1.403 s, compared with 1.756 s before acceleration
(about 20% faster). The mixed benchmark returned without VM errors, and the
owner confirmed stable, correct shapes on VGA. The owner also confirmed stable,
correct scrolling text and a stable 100-frame attractor animation/final pattern;
the latest attractor run completed in 17.224 s without a VM error.

**Presentation-stage profile (2026-11-14):** an opt-in `GFXPROFILE` build now
separates row composition, palette conversion, scanline writes, and buffer
swap time. Three 20-line `GUPDATE 1` PRINT passes each measured 0.301 s. For the
three-pass automatic-plus-batched benchmark, swap time was 887 ms of 1.656 s
total presenter-stage time. Two repeated 40-line scroll runs measured 1.402 s
and 1.403 s between BASIC markers; each reported 71 requests, 24 full frames,
25 partial updates, and 22 full-frame fallbacks. The scrolling profile spends
most presenter time composing rows, followed by swaps and scanline writes. See
[presentation stage profile](test-results/olimex-presentation-profile-2026-11-14.md).

**Packed-span composition (2026-11-14):** row and full-frame composition now
copy 8-bit canvas spans with `memcpy` and unpack packed 4-bit spans two pixels
per byte, including odd boundaries. The host regression compares full and
clipped rows against a pixel-by-pixel reference for odd-width 4-bit canvases,
8-bit canvases, and 1-bit overlays. On Olimex, scroll composition fell from about 721 ms to
157 ms (4.6× faster), and no-scroll PRINT composition fell from 505 ms to
115 ms (4.4× faster). Two repeated scroll profiles kept the same 71 requests,
24 full frames, 25 partial updates, and 22 full-frame fallbacks; presenter
stage totals fell from 1.555–1.557 s to 0.876–0.877 s. On the normal flashed
firmware, three scroll marker intervals were 0.801–0.903 s; the mixed partial
drawing and 100-frame attractor workloads returned completion markers without
VM errors. The owner visually confirmed stable, correct mixed shapes and
scrolling text, plus a stable attractor animation and final pattern. The board
responded at the REPL after restoration. See
[presentation stage profile](test-results/olimex-presentation-profile-2026-11-14.md).

The final normal firmware also passed a visual `GUPDATE 0` regression retest:
Screen A remained visible for the five-second hold, and Screen B appeared only
after bare `GUPDATE`. The fixture returned to the REPL without a VM error. See
[GUPDATE 0 regression results](test-results/gupdate-off-2026-11-13.md).

**Swap wait characterization (2026-11-14):** the opt-in profile now measures
each FabGL buffer swap. Two repeat PRINT workloads averaged 8.9–9.5 ms per
swap, with maxima of 11.8–14.6 ms and no rare long-tail stalls. FabGL queues a
swap primitive and synchronously waits for the VGA controller to execute it;
the time is primarily display-service/frame-boundary latency, not pixel
copying. This means asynchronous swaps or implicitly batching `GUPDATE 1`
would alter existing completion semantics. Use the explicit `GUPDATE 0` plus
bare `GUPDATE` path when a program wants to batch presentation. The normal
Olimex profile was rebuilt, flashed, and confirmed responsive at the REPL
after profiling; no `GFXPROFILE` output appeared. See
[presentation stage profile](test-results/olimex-presentation-profile-2026-11-14.md).

**Framebuffer benchmark repeat (2026-11-14):** three runs of the existing
render matrix during active FabGL VGA scanout produced tightly grouped times.
Packed-4 remained faster than RGB222 for aligned fills (16.0 vs 17.0 ms per
1,000), filled circles (22.8 vs 38.8 ms per 500), full-height scroll pairs
(340 vs 1,088 ms per 100), and opaque 64×64 blits (69.7 vs 301 ms per 1,000).
Transparent packed-4 blits remained slower (1,021 vs 778 ms). These buffers
were separate from FabGL's internal scanout buffers, so this verifies repeatable
CPU/PSRAM timings while VGA is live, but not PSRAM read/write contention during
scanout. Custom PSRAM-DMA scanout qualification and BASIC backing-store
integration remain open. See
[render benchmark results](test-results/graphics-render-benchmark-2026-11-14.md).

**Active PSRAM-scanout write test (2026-11-14):** new opt-in RGB222 and
packed-4 profiles wrote into a spare PSRAM scanout frame while DMA read the
separate active frame. The shortened 240 MHz workload reported 149 late
refills/158 EOF events for RGB222, but 0/178 for packed-4; neither had
spurious interrupts. The full RGB222 matrix originally triggered the refill
task watchdog after about 11.4 seconds. Instrumentation then showed expired
rows being replayed in batches as large as 1,664 rows (121,440 µs), growing
backlog and starving the worker. A refill-window helper now skips obsolete
rows. In the bounded repeat, maximum batch size was 64 rows, maximum duration
46,232 µs, backlog peaked at 640 rows, and 1,792 expired rows were skipped.
Late refills fell to 28/158 and stayed at 28 through a further 13 seconds of
scanout, with no watchdog reset or spurious interrupt. This removes the
runaway backlog seen before, but late refills and long batches remain; the DMA
path is still unqualified for RGB222. The packed-4 profile also rebuilt and
ran with the helper: about 1,745 frames and 349,056 rows over an 18-second
capture, with zero late refills or spurious interrupts, a 5,877 µs maximum
refill duration, and a 64-row maximum batch/backlog. The owner confirmed stable
moving bars after the helper change. The packed-4 live diagnostic was then
flashed with PSRAM stress enabled. Over an 8-second serial capture it advanced
472 frames/94,464 logical rows and wrote 40,435,712 stress bytes with zero late
refills or spurious interrupts. An additional 20-second capture passed frame
5,600 and 1.13 million logical rows; cumulative PSRAM stress writes exceeded
225 MB with no late refills, spurious interrupts, or watchdog reset. The owner
confirmed the moving bars remained stable on VGA. The standard FabGL profile
was restored afterward and the serial REPL prompt verified. Stale-row skipping
is now in the shared worker for all scanout builds, not just benchmark
profiles. The RGB222 diagnostic was rebuilt and ran for about 20 seconds with
the continuous PSRAM stress writer: 1,890 frames / 378,048 rows, zero late
refills and zero spurious interrupts, with no reset. The owner confirmed stable
moving bars. The later RGB222 runtime comparison exposed noisy text and 114
late refills during an alphabet-print capture. The board was restored to the
standard PSRAM/FabGL profile. Keep FabGL as default until RGB222 and
arbitrary-drawing sustained-load checks pass. See
[PSRAM scanout benchmark results](test-results/psram-dma-framebuffer-benchmark-2026-11-14.md).

**Frame-slot alignment follow-up (2026-11-14):** with stale-row skipping
enabled, both RGB222 and the non-diagnostic QL/32 runtime profile reported zero
late refills, but the runtime image still rolled vertically. The DMA descriptor
table restarts at ring slot zero after every 200 logical rows, while a 128-row
ring advances that slot phase by 72 rows per frame. Changed the ring to 100
rows (50-row refill groups), which divides the frame height and saves 8,960
bytes of internal DMA memory. The non-diagnostic runtime profile rebuilt and
ran with zero late refills and zero spurious interrupts across more than 1,200
frames. The owner confirmed the screen no longer rolls and the console color
is green, but the glyphs are noisy/shifted. A full `GUPDATE` did not clear the
corruption. The packed-4 DMA runtime and an RGB222 DMA comparison both showed
the same noisy alphabet. The experimental FabGL double-buffer profile was
also noisy, while the standard WROVER/PSRAM FabGL profile rendered the same
alphabet cleanly. A direct scanout one-pixel edge diagnostic (vertical stripes,
horizontal stripes, and an unaligned checker patch) was clean, with zero late
refills and zero spurious interrupts across the captured interval. This shows
that raw scanout and pixel ordering work when the framebuffer is written
directly. The BASIC edge fixture initially failed to parse because it used
`NEXT` instead of QL/32's `END FOR`; the corrected fixture then ran without a
VM error. The owner saw horizontal and vertical stripes plus a grid-like
checker patch while text remained noisy. A separate A/B fixture printed into
graphics channel #1 and the console; both text paths were noisy. The mixed
raw-frame glyph control was visually noisy, so it was replaced with the
owner's requested triangle-only profile: black framebuffer with one filled
right triangle and a 45-degree diagonal. Its counters stayed clean. A temporary
pixel-pair reversal made four-pixel groups wrong and was removed; the owner
confirmed the display returned to the first triangle shape. A host test shows
packed-4 span fills and individual pixel writes produce identical triangle
bytes. The owner then supplied the exact left-to-right eight-pixel DMA order:
`2,3,0,1,6,7,4,5` (pixel 8 was a typo). Applying this transform in the
triangle-only profile produced a visually correct 45-degree triangle. The
transform is now shared by all DMA scanout rows, and the packed-4 runtime
profile builds and flashes successfully. A host regression for two consecutive
eight-pixel groups passes. Late-refill count reached 73 across captures. The
owner confirmed the REPL alphabet and digits now render cleanly. The existing
mixed drawing fixture completed with correctly drawn, stable rectangles,
points, lines, and circle. During that workload late-refill count rose from 230
to 430 and spurious IRQs from 2 to 3, with no reset. An 8-second attractor
worker ran without reset but showed occasional flicker; late refills rose from
574 to 645 and spurious IRQs from 3 to 4. The worker was killed cleanly. A
three-pass 20-line automatic-versus-batched PRINT benchmark completed; counters
rose from 962 to 1,043 late refills and 5 to 6 spurious IRQs. VGA review of
the PRINT output is pending. Investigate these scanout timing events before
heavier benchmarks. Keep standard FabGL as the recovery/default until
sustained DMA scanout passes.

**Refill-depth comparison (2026-11-14):** an opt-in 200-row line ring keeps
50-row refill batches and increases lead from 50 to 150 rows. It allocates with
75,056 bytes of internal DMA use and 134,040 bytes free afterward. After 35
initial late events, the idle count stayed stable for 10 seconds. The owner
confirmed the attractor was stable on this profile. The print benchmark still
produced vertical flicker/jumps while updating, despite zero spurious IRQs.
Thus more depth helps sustained animation but does not solve rapid partial
console updates. A 400-row one-block attempt did not start scanout because its
128 KB buffer exceeded the 110,580-byte largest contiguous DMA block; the
current descriptor chain also covers only one frame. The 400-row profile was
removed and the device restored to the working 200-row image. Next, batch
help/pager output by page and reduce repeated full-frame PSRAM copies during
partial console updates.

**Priority and partial-copy follow-up (2026-11-14):** the same ring-200 PRINT
benchmark added 119 late refills at priority 4 and priority 7. The owner saw
stable/readable VGA in both runs, so higher task priority did not address the
intermittent update flicker. A dirty-region catch-up prototype reduced the
benchmark's additional late-refill count to 17, but the owner observed VGA
artifacts or a reset. That prototype was reverted and the previous priority-7
firmware restored. Repeated HELP still showed minor glitching. Treat safe frame
publication as a prerequisite before another delta-copy attempt; first profile
console updates and the triple-buffer handoff under repeated HELP/PRINT.
The first scripted HELP repeat was invalid because opening pySerial asserted
DTR and reset the ESP32. With DTR/RTS low before opening, `PAGER ON`, `HELP`,
and `Q` completed without a reboot. Late refills rose from 80 to 108 during
the HELP page and to 115 while quitting; spurious IRQs stayed at zero. HELP
targets graphics channel #1 and is absent from the serial listing. The
corrected paged-run visual review is pending. A later `PAGER OFF` capture
showed 137 cumulative late refills and 5 spurious IRQs, indicating the issue
is not limited to HELP paging.

**Paged HELP batching follow-up (2026-11-14):** coalesced the pager's forced
full update with its first page write. Host graphics tests passed and the
ring-200 priority-4 build flashed. With DTR/RTS low before opening serial, a
quiet two-second scanout kept 35 late refills; enabling the pager added 20,
the HELP page added 4, and dismissing it added 7. IRQs stayed at zero. The
owner still saw minor horizontally moving lines during the page, so one fewer
publication did not eliminate the disturbance. The pager was restored to OFF.
Continue tracing DMA refill deadlines during partial framebuffer publication.

**Programmable palette command (2026-11-14):** implemented
`PALETTE start,rgb[,rgb...]` and `RGB(red,green,blue)` on the retained
indexed-canvas service. RGB values are packed `RRGGBB` integers, palette
updates are display-wide, and indexed
canvas pixels remain unchanged. The command observes `GUPDATE` and frame-skip
settings, and `GPALETTE$` reads the current mapping. Linux graphics tests passed;
the Olimex double-buffer profile built and flashed. Serial REPL checks reported
palette size 16, round-tripped `#123456`, `#ABCDEF`, and `#654321`, confirmed
that a palette edit made under `GUPDATE 0` could be read before the explicit
flush, and rejected a range beyond the active palette. VGA color appearance
was not evaluated as part of this functionality check. This QL/32 command is
informed by SuperBASIC `PALETTE_QL`; per-window palettes and automatic timer
cycling remain deferred. See
[programmable palette results](test-results/graphics-palette-2026-11-14.md).

**Cursor-based line drawing (2026-11-14):** extended `LINE` with the
SuperBASIC-style cursor subset `LINE x,y`, `LINE TO x,y`, and
`LINE x,y TO x2,y2`, while retaining `LINE x1,y1,x2,y2`. `GX()` and `GY()`
return the current cursor; state is independent for each session and graphics
channel. The Linux graphics test executable passed. The Olimex profile rebuilt
and flashed; serial REPL checks observed cursor positions `(2,3)`, `(5,6)`,
`(11,12)`, and `(3,1)` across the new and existing forms. VGA appearance was
not assessed. This implements the basic `LINE` behavior documented by
SuperBASIC; polyline lists remain unsupported. See
[cursor-line results](test-results/graphics-line-cursor-2026-11-14.md).

**Full-height PSRAM scanout qualification (2026-11-15):** added an opt-in
400x300 packed-4 scanout profile using the PSRAM framebuffer and internal DMA
line ring. The initial 100-row ring rolled/lost sync during live refill, while
the same 400x300 modeline and descriptor chain stayed synced with refill
interrupts disabled. A 150-row ring on core 0 then starved IDLE0 and triggered
the task watchdog; moving refill to core 1 alone still rolled. Optimized packed
row encoding with a precomputed 16-bit pair LUT and emitted the required
`2,3,0,1,6,7,4,5` pixel order in one pass. High-resolution diagnostics now
avoid the moving-source shift so the test isolates refill throughput; palette
rotation and the PSRAM stress writer remain active. The owner confirmed stable
sync with the animated bars visible. Serial counters then recorded 5,195
frames / 1,558,725 rows, 20,783 EOFs, zero late refills, zero spurious
interrupts, and 513 MB of PSRAM stress writes. This qualifies the 400x300
packed-4 scanout diagnostic with a 150-row ring, 75-row refill group, and core-1
worker. It does not yet integrate full-height scanout with the BASIC window
manager. A follow-up 640x480 packed-4 test used a 120-row ring and core-1
worker. The owner confirmed stable/readable VGA, including after increasing the
ring from 96 to 120 rows. Counters still rose from 1,325 to 2,036 late-refill
events across 4,736 to 7,336 EOFs, and three spurious interrupts were recorded;
visual stability is a pass, but timing telemetry remains open before calling
640x480 qualified. PSRAM stress writes were active during both captures. Keep
the standard FabGL runtime as the default/recovery profile; the DMA runtime
profile remains opt-in.

**640x480 QL/32 runtime profile (2026-11-15):** connected the build-selected DMA
mode to the platform's fixed display-mode service and initialized the runtime
window manager, console, and `PsramFramePresenter` at the scanout dimensions.
The `olimex_esp32_sbc_fabgl_psram_dma_display_640x480_packed4` profile built
and flashed. Startup allocated the expected 460,800-byte PSRAM framebuffer
triple-buffer set and 90,620 bytes of internal DMA data, leaving 116,496 bytes
of the internal DMA heap. Serial checks confirmed `MODE LIST` reports 640x480
and `MODE 0,0` preserves/reports the same geometry. The owner confirmed the
channel-1 diagonal, circle, and console text were correct with stable sync.
During drawing and console updates, telemetry showed zero late refills; the
spurious-interrupt count rose to 22 without visible artifacts. This is a
successful runtime visual/functionality check, with spurious IRQ accounting
still open. The DMA runtime profile exposes only its build-selected mode today;
switching between modelines at runtime remains future work. The standard
FabGL profile remains the default/recovery build.

**400x300 QL/32 runtime profile (2026-11-15):** added and flashed the matching
`olimex_esp32_sbc_fabgl_psram_dma_display_400x300_packed4` profile using the
150-row internal ring and core-1 refill worker. `MODE LIST` reported 400x300;
`MODE 0,0` retained 400x300. The owner confirmed the channel-1 diagonal and
circle plus console text rendered correctly with stable sync. Serial counters
showed zero late refills and zero spurious interrupts across the capture. The
connected board was left running this 400x300 QL/32 runtime profile for full
height use; standard FabGL remains the configured default build.

**640x400 QL/32 runtime profile (2026-11-15):** added the opt-in
`olimex_esp32_sbc_fabgl_psram_dma_display_640x400_packed4` profile using the
FabGL 640x400 70 Hz modeline, packed-4 PSRAM framebuffer, 100-row internal DMA
ring, and core-1 refill worker. Build and flash succeeded. Serial boot reported
640x400 at 70.09 Hz, 384,000 bytes for the three PSRAM framebuffers, and
75,948 bytes for the internal DMA chain with 131,248 bytes of DMA heap
remaining. A 12-second runtime capture recorded about 7,800 frames, zero late
refills, and 30 spurious-IRQ counts; the interrupt counter incremented slowly
while the presenter continued to clone frames. The owner later confirmed that
this 640x400 image was stable and readable. The previous 400x300 runtime
profile had passed the owner’s visual check before this profile was flashed.

The fixed DMA profiles now report the same mode IDs as the FabGL VGA presets:
0 for 320x200, 2 for 400x300, 3 for 640x400, and 4 for 640x480. Each
profile accepts only its own ID until live modeline switching is implemented.
The prior serial checks using `MODE 0,0` on the high-resolution profiles
preceded this correction.

Scanout allocation now releases partially allocated framebuffer, line-ring,
blanking, descriptor, and stress buffers when allocation or memory capability
validation fails. The interrupt is allocated before workers, and failures to
create either worker release the interrupt and allocated buffers; a refill
worker waiting for its first notification is deleted if stress-task creation
fails. The 640x400 firmware builds with this cleanup. A coordinated shutdown of
active workers and the interrupt is still needed for live modeline changes.
Startup failure paths have not yet been fault-injected on hardware.

The scanout now has a cooperative stop operation: it rejects a staged frame,
disables and frees the interrupt on its allocating core, stops the stream,
waits for workers to leave their buffers, then frees the allocations. A
640x480 diagnostic stopped and restarted the same 160-row configuration;
serial counters then reached 603 frames with zero late refills and unexpected
interrupts. DMA heap before allocation was 251,320 bytes initially and
251,028 bytes immediately after stop, while heap remaining after each start
was 130,232 bytes. This is one no-stress cycle, not a leak soak or a live mode
change. The stop currently waits synchronously and must be adapted to preserve
scheduler responsiveness before exposing it through `MODE`. Geometry changes,
rollback, concurrent sessions, stress, and visual verification remain open.
The 640x480 QL/32 runtime was rebuilt and reflashed after this diagnostic.

Shutdown has since been split into request and nonblocking poll operations;
the synchronous wrapper is used only by isolated diagnostics. Scanout geometry
can be changed only while stopped and is validated against the preset table.
The hardware diagnostic rejected a geometry change while running and an
unsupported size, then alternated 640x480 → 400x300 → 640x480 → 400x300.
All three stop/restart cycles completed; the DMA heap before each subsequent
allocation was 251,020 bytes and the final 400x300 scanout reached 605 frames
with zero late refills and unexpected interrupts. This validates the scanout
lifecycle only: `MODE`, the window manager, concurrent sessions, rollback,
stress, and monitor appearance were not exercised by that diagnostic.
The diagnostic was subsequently repeated with four-second holds between
changes; the owner confirmed the monitor regained stable sync after each
transition. This remains a scanout-only check, without BASIC `MODE`, window
manager integration, active drawing, or concurrent PSRAM stress.
An expanded no-stress diagnostic then cycled 640x480 → 400x300 → 640x480 →
320x240 → 320x200 → 640x400 → 640x480. All starts and nonblocking shutdowns
completed; free DMA heap before each restart was 251,020 bytes, and the final
640x480 mode reached 483 frames with zero late refills and unexpected IRQs.
The owner has been asked to check monitor sync across the expanded sequence.
A host graphics regression also confirms that two sessions' retained drawing
survives a deferred presentation and display resize. This does not yet wire
the scanout transition to BASIC `MODE`.

The scanout's width and height are now instance fields initialized from the
boot profile, preparing the encoder and presenter for runtime geometry. The
640x400 profile was rebuilt and flashed; an idle serial capture reached
1,367 frames with zero late refills and nine spurious IRQ counts. Visual
qualification, the timing impact under active drawing, runtime geometry
changes, and safe shutdown/restart are still outstanding.

Ring and refill sizes are now instance state with validation that the ring
fits its maximum capacity, divides the frame height, and consists of whole
refill groups. A host-tested geometry table records candidate defaults for the
five VGA sizes; the boot profiles still select their existing ring sizes and
the table does not yet expose runtime mode switching. The refactored 640x400
image built and flashed; serial idle counters reached 1,294 frames with zero
late refills and six spurious IRQ counts. Active-drawing and monitor checks
remain necessary, and 320x240 has not yet been qualified with this ring.

The EOF interrupt handler now samples status once and acknowledges that
sample before processing it. Previously it read status again at the end,
which could acknowledge a newly arrived event that it had not processed.
Additional counters distinguish empty interrupts from other status bits. The
updated 640x400 runtime built and flashed; an idle serial capture reached
1,452 frames with zero late refills and zero unexpected or empty interrupts.
This short idle run is encouraging but does not yet explain the earlier
interrupt counts or qualify active drawing and the 640x480 profile.

On the same 640x400 image, the FFS `graphics-partial-draw-test.bas` worker
reported both `PARTIAL_DRAW_START` and `PARTIAL_DRAW_DONE`. During the captured
run, the counters advanced from 4,944 to 8,830 frames with zero late refills,
empty IRQs, or unexpected IRQs; frame clone counts increased from 126 to 350.
The fixture took more than 35 seconds across the serial observation windows,
so this is a scanout-counter and completion check, not a performance pass.
The monitor image was not visually assessed in this run.

The updated 640x480 runtime also built and flashed, and the same bounded
fixture reported `PARTIAL_DRAW_START` and `PARTIAL_DRAW_DONE`. Serial samples
through frame 7,484 recorded zero late refills and zero empty or unexpected
interrupts while framebuffer clones rose from 19 to 226. The run took over
100 seconds, so performance remains an open concern. This fixture exercises a
320x200 portion of the larger display, and no visual check was made for this
build; it does not establish sustained full-frame stability. The previous
640x480 diagnostic with concurrent PSRAM stress remains a separate open
qualification case.

The 640x480 packed-4 diagnostic was rebuilt with the single-sample interrupt
handler and rerun with its PSRAM stress writer active. Late refills increased
from 1,418 to 4,504 while 101,449,728 additional stress bytes were written;
empty and unexpected interrupt counts stayed at zero. The worker refilled
fewer rows than were consumed, so the high-contention case still fails its
deadline and cannot be qualified from the clean runtime drawing counters.
The 640x480 QL/32 runtime profile was reflashed after this diagnostic.

An aligned four-byte packed framebuffer read reduced observed missed refill
events from approximately 27.5% to 21.5% of EOF events in short separate
stress captures, but did not eliminate the deadline failures. The comparison
is diagnostic rather than a controlled benchmark; visual pixel correctness
and active workload behavior after this encoder change still need checking.
Supported geometry now requires widths divisible by eight to preserve the
aligned row-load invariant. The updated 640x480 runtime was rebuilt and flashed.
An extracted encoder helper passed 65,536 host input combinations but the
subsequent runtime image repeatedly restarted during startup on hardware.
The helper extraction and its host-only test were removed; the inline aligned
encoder was restored. After flashing that version the 640x480 REPL started
and ran through 1,635 idle frames with zero late refills or unexpected IRQs.
The exact cause of the restart is not yet proven.

A 640x480 diagnostic with a 160-row ring and 40-row refill groups allocated
and reported scanout startup, but serial output stopped partway through the
startup message. No stress counters were obtained. That diagnostic also used
the subsequently reverted encoder helper, so the ring itself cannot yet be
identified as the cause. The 120-row QL/32 runtime has been restored.
Repeating the 160-row diagnostic with the restored inline encoder produced the
same truncated startup output and no counters over 16 seconds. This rules out
the helper extraction as a necessary cause of that diagnostic failure; worker
scheduling and interrupt cadence remain hypotheses. The 120-row runtime was
reflashed and reached the REPL with zero late refills through frame 995.
A follow-up 160-row, 40-row-group diagnostic with the stress writer disabled
reached 1,382 frames with zero late refills and zero unexpected interrupts.
Thus the 160-row scanout and palette cycling can run without the competing
writer; the stress-enabled diagnostic's loss of serial output depends on the
combined workload or scheduling. This does not establish visual stability.
The 120-row 640x480 QL/32 runtime was reflashed after the comparison.
At refill priority 1, the same 160-row diagnostic with stress enabled continued
reporting: between frames 421 and 1,321, late refills rose from 2,834 to 8,986
over 10,797 additional EOF events, while the stress writer advanced by about
81 MB. Unexpected IRQs stayed at zero. This supports core starvation as an
explanation for the priority-4 diagnostic's missing output, but priority 1
missed roughly 57% of refill deadlines and is not an acceptable fix. The
120-row priority-4 QL/32 runtime was reflashed after the experiment.

A separate 640x480 diagnostic combined adjacent 16-bit pixel pairs into
32-bit DMA writes and recorded approximately 23% late EOF events under stress,
comparable to the prior aligned-read capture (about 22%). This short test
showed no meaningful improvement, so the experimental word-store path was
removed and the 120-row QL/32 runtime was rebuilt and reflashed.

A diagnostic compiled with GCC's `dupldst` PSRAM cache workaround instead of
the profile's `memw` strategy also failed under the 640x480 stress writer:
late refills increased by 1,578 over 5,896 EOF events (about 27%), with no
unexpected interrupts. This is comparable to the `memw` stress result and
does not justify changing the normal profile. The 120-row `memw` QL/32 runtime
was rebuilt and reflashed after the comparison.

**Backlog — console prompt with `GUPDATE SKIP,1`:** the owner reports that
repeated Enter presses create new console lines but intermittently omit the
`>` prompt when frame skipping is enabled; with `GUPDATE SKIP,0`, each new line
shows the prompt. This has not been reproduced or traced yet. Keep it queued as
a separate issue, with a possible link to screen refresh/presentation, and
retest after the active VGA refresh investigation. No firmware change was made
for this report.

**High-resolution responsiveness investigation (2026-11-16):** runtime DMA
telemetry had been printing a large status block every second from the main
loop. It is now opt-in under `QL32_OLIMEX_DMA_STATUS_TEST`, so ordinary runtime
and mode-test firmware do not block REPL/scheduler work for diagnostic output.
`PsramFramePresenter` also writes converted row segments directly, packing two
adjacent Indexed4 pixels into one byte rather than read/modify/writing each
pixel separately. The framebuffer unit test covers packed nibbles, odd segment
offsets, preserved neighboring pixels, RGB222 mapping, and range rejection.
The updated dynamic-mode build compiled and flashed.

A 10-frame drawing fixture at 240 MHz measured 1.404 s at 400x300, 2.408 s at
640x400, and 2.911 s at 640x480 when the refill worker was moved to core 0.
Although the fixture completed, the owner observed the 640x480 display blink
and lose sync. The core-0 experiment was rejected and its build profile removed.
With the known core-1 profile restored, the same fixture took 2.107 s at
400x300 but did not produce a completion marker within 25 seconds at 640x400.
The device was reflashed to the core-1 profile and rebooted; the owner confirmed
stable sync but continues to report a slow REPL. A second candidate using
priority 1 and a 160-row ring also blinked/lost sync at idle, so both scheduling
experiments were removed. The measurements indicate severe app/refill
contention on core 1 at high resolution, while the tested core-0 placement
damages VGA stability. The row-segment change has not yet been isolated in an
A/B timing comparison.

**FabGL high-resolution allocation (2026-11-16):** FabGL stores VGA viewport
pixels as one byte per pixel in internal DMA-capable memory. A complete
640x400 viewport therefore needs 256,000 bytes and 640x480 needs 307,200 bytes,
before DMA descriptors and runtime allocations. Its allocator can keep a
shorter viewport when it cannot allocate all requested rows. QL/32 previously
accepted any nonempty viewport, which made a partially allocated mode appear
to have succeeded. Mode changes now verify the actual FabGL width and height
against the selected preset and roll back to the previous mode on a short
allocation. The firmware was flashed over `/dev/ttyUSB0` on 2026-11-16.
Serial testing showed `MODE 3,0` allocated only 640x210 of 640x400 and
`MODE 4,0` allocated only 640x206 of 640x480. Both commands raised the new
allocation error, and `MODE(0)`/`MODE(1)` stayed at 0/0 after each rollback.
The owner confirmed VGA returned to the stable 320x200 REPL after both tests.

The full-resolution path should move the scanout source into PSRAM and retain
only the DMA line ring in internal RAM. The existing experimental path does
this with three packed-4 PSRAM frames (about 384,000 bytes at 640x400 and
460,800 bytes at 640x480) plus a 120-row internal ring (76,800 bytes at either
mode). This avoids FabGL's 256/307 KiB internal viewport, but the larger-mode
runtime still needs stable update/refill behavior. Reducing to two PSRAM frames
could save another 128/153.6 KiB, at the cost of blocking frame production until
the inactive frame becomes available; validate that only after the three-frame
path is visually qualified.

To reduce the cost of small REPL redraws without changing scanout scheduling,
the presenter now tells the triple-buffer owner the changed rectangle. Instead
of cloning a complete PSRAM frame before every small text update, it repairs
only the stale rectangle in the recycled frame. Full-frame updates still copy
the full frame. The owner confirmed stable VGA and faster REPL input on this
build.

HELP remained slow, so graphics text output now batches glyphs into runs, uses
packed-byte writes for the default font on even-width Indexed4 canvases, and
defers multiple line scrolls until the output string is complete. The scroll
path skips text that would be scrolled off before it could remain visible.
Host graphics tests pass, including a comparison against the prior per-glyph
draw-and-scroll behavior across wrapping and multiple scrolls. On the flashed
Olimex, three consecutive serial `HELP` calls returned to the prompt in 1.404,
1.502, and 1.603 seconds; serial timing does not measure monitor response or
confirm visible glyph quality. The owner confirmed the latest build is stable
and readable on VGA and that repeated HELP output is clearly faster.

**Mode qualification and stress follow-up (2026-11-16):** the functional
mode record confirms FabGL modes 0–2 (320x200, 320x240, 400x300) and all five
resolutions through the switchable PSRAM DMA runtime. Modes 3 and 4 passed
full-window drawing, console scrolling, and a ten-frame workload with stable
VGA. New 640x400 and 640x480 scrolling-source-gradient stress builds looked
stable and smooth, but neither scripted capture nor the interactive monitor
returned profiler lines. Startup geometry and refill/stress counters therefore
remain unverified. A switchable-runtime flash after the stress tests showed
blank/no-sync output; the stable 320x200 FabGL profile was restored. See the
[mode qualification record](test-results/olimex-vga-functional-modes-2026-11-16.md)
and [low-level comparison report](test-results/agon-vga-low-level-comparison-2026-11-16.md).

**Scheduler contention follow-up (2026-11-17):** an Olimex serial trace found
that continuously printing worker tasks delay other graphics programs
severely. With one tight `PRINT` worker active, Mandelbrot advanced one row in
78.958 seconds; suspending the worker returned row time to about 0.66 seconds.
Three workers produced no next-row marker within 120 seconds. This is a
separate runtime scheduling/presentation issue from the unresolved DMA refill
qualification. Record tick and presentation durations before changing the
DMA worker or default graphics profile. See the
[worker contention report](test-results/olimex-mandelbrot-worker-starvation-2026-11-17.md).

**High-resolution telemetry follow-up (2026-11-17):** opt-in runtime profiles
at 640x400 and 640x480 collect detailed refill counters in memory and print
one report when the diagnostic profile receives `DMADIAG` after a test,
avoiding periodic serial traffic.
Both builds passed. Idle scanout captures showed zero late refills, unexpected
interrupts, and active-slot overwrites; 640x400 also logged its full startup
geometry. The 640x480 run reported 480 rows per frame and passed the full-size
graphics/text smoke test. `DMADIAG` was verified over serial and reported zero
late refills, unexpected interrupts, and active-slot overwrites. Stress
profiles at both sizes showed stable moving
gradients by owner observation, but emitted no serial telemetry. See the
[high-resolution telemetry report](test-results/olimex-dma-high-resolution-telemetry-2026-11-17.md).

**640x480 responsiveness probe (2026-11-17):** moving the Arduino loop to
core 0 improved CPU-only BASIC performance (14.562 s, then 14.670 s for the
240 MHz recurrence in two variants). Pinning the EOF interrupt and refill
worker together on core 1 still missed 1,457 refill deadlines in 9,345 EOF
events under drawing load; the owner observed whole-console flashing and an
unstable attractor image. The stable core-1 runtime profile was reflashed
and counters returned to zero late refills. A deeper 160-row ring on the same
split-core placement was worse before the attractor started: 8,781 late
refills in 38,451 EOF events, with 440 rows of maximum backlog. It was rejected
without running the attractor. The standard 120-row profile then ran the
attractor across two snapshots separated by nearly 4 minutes 53 seconds, with
zero late refills across 286,600 EOF events.
The stable same-core profile outperformed both split-core attempts for scanout
reliability. Shared PSRAM contention is one hypothesis, not yet measured.
Do not repeat core pinning or ring-depth changes alone. A future design needs
bounded display updates and proof that the scanout deadline holds under full
drawing load. See the
[split-core probe report](test-results/olimex-dma-split-core-performance-2026-11-17.md).
The owner-present replay confirmed a stable but very slow attractor; the
draw-only benchmark showed a second channel-2 console and slight flashing. The
benchmark did not complete before the standard 640x480 profile was restored.
The owner confirmed VGA stable/readable after restore. See the
[visual replay report](test-results/olimex-vga-replay-640x480-2026-11-17.md).

## Deferred work

- Recheck switchable-runtime recovery after a diagnostic image and verify that
  the serial diagnostic output works at high resolutions.
- Improve deferred telemetry for 640x400/640x480 stress profiles, whose monitor
  visuals passed but serial captures returned no bytes; investigate per-row
  refill deadlines under PSRAM write contention. Idle runtime counters pass.
- Compressed on-disk bitmap formats and advanced sprite effects.
- PSRAM-to-PSRAM DMA experiments unless profiling identifies a real bottleneck.
- Removing the FabGL fallback before the target-local path has a sustained
  hardware record.
