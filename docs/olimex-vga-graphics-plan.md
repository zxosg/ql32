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
- The experiment uses a 320x200, 8-bit indexed color-bar source in PSRAM and
  three internal line buffers plus a DMA descriptor chain.
- Its rows repeat the same pattern, so the line buffers can be prepared once.
  This exercises descriptor output, but does **not** yet prove sustained PSRAM
  reads while scanout is active or while graphics code is writing PSRAM.
- The current Olimex pin map lists one GPIO per RGB channel (GPIO 21, 18, 5),
  which provides eight directly selectable output colors. Confirm the board's
  VGA circuitry and available signals before promising 64 visible colors or a
  16-entry palette chosen from all 64 RGB222 colors.
- Codex reports that the test and fallback profiles compile. Physical signal
  stability and timing have not been checked. The owner plans to build/validate
  when hardware is available.

## Decisions and rules

1. Keep the custom scanout behind its own Olimex PSRAM test/profile until it
   passes physical validation. Do not replace the default FabGL startup yet.
2. First test the target-local scanout already present. Consider a FabGL
   `VGA16Controller` PSRAM extension only if measurements show it is simpler or
   more reliable than maintaining the target-local backend.
3. Separate logical framebuffer formats and drawing from VGA byte encoding.
   The renderer owns indexed pixels; the Olimex scanout adapter translates
   pixels, palette, blanking, and sync into DMA rows.
4. Treat PSRAM as CPU-accessed memory on the original ESP32. Use I2S DMA only
   from internal DMA-capable scanline buffers; do not make PSRAM memory-to-
   memory DMA a requirement.
5. Do not implement higher resolutions until the 320-wide path meets its
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
- Add a second pattern whose rows differ (for example, row number, checkerboard,
  and moving color bars). Refill the internal ring from PSRAM as each buffer is
  released by DMA. This is the first test that proves repeated PSRAM reads at
  scanline rate.
- Instrument DMA EOF/underrun or late-refill events and report them alongside
  measured refresh and pixel clock. Stress concurrent PSRAM writes from the
  CPU while the moving pattern is running.
- Verify ISR/cache safety. If the ESP32 can make PSRAM inaccessible during a
  cache-disabled interval, keep such accesses out of the ISR and use a safe
  producer/staging strategy instead.
- Resolve the color wiring question from the board schematic and hardware.
  Keep the current result labeled as 8-color if only three RGB inputs are
  connected.

**Exit:** stable 320x200 output for an extended run, correct moving/unique-row
pattern, no scanout errors under PSRAM read/write load, measured timing, and a
documented color-depth limit.

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
- Add BASIC syntax only after the native API and semantics are stable. Mark new
  commands QL/32-specific unless a QL/SMSQ/E reference and conformance tests
  establish compatibility.
- Benchmark both framebuffer modes against the current single-buffer FabGL
  path: full clears, 10k lines, 100k random pixels, 1k fills, 1k 64x64 opaque
  and transparent blits, 100 full-width scrolls, and 1k palette updates.
- Report elapsed time, effective copy rate where meaningful, scanout errors,
  internal DMA-capable free heap, PSRAM used, and resolution/refresh.

**Exit:** documented correctness and performance results on the Olimex board;
the default profile changes only if the new path is stable and demonstrably
useful.

## Deferred work

- 640x400 and 640x480 qualification until lower resolutions pass.
- Compressed on-disk bitmap formats and advanced sprite effects.
- PSRAM-to-PSRAM DMA experiments unless profiling identifies a real bottleneck.
- Removing the FabGL fallback before the target-local path has a sustained
  hardware record.
