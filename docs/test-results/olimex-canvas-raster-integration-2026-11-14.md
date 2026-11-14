# Olimex retained-canvas raster integration — 2026-11-14

## Change

`WindowedService` and `WindowManager` now accept an optional platform-neutral
`CanvasRasterOps` interface. The Olimex adapter applies its packed 4-bit
framebuffer primitives directly to retained channel canvases. It accelerates
clear, pixel writes, in-bounds lines, filled rectangles, in-bounds outline and
filled circles, and vertical scroll. Unsupported depths, odd-width packed
canvases, clipped outline circles/lines, and invert-mode shapes use the existing
portable `IndexedCanvas` implementation. FabGL remains the active presenter
and fallback for VGA scanout.

## Verification

- Host tests compare accelerated and portable canvas bytes for clear, clipped
  fill, in-bounds and clipped lines, outline circles, clear-over mode, invert
  fallback, and scroll. A `WindowedService` test confirms BASIC graphics calls
  produce identical retained pixels with and without the Olimex adapter.
- The host test also checks that odd-width packed canvases do not expose a
  false row stride and safely use the portable implementation.
- The Olimex ESP32-WROVER double-buffer profile built and flashed successfully.
  The board ran at 240 MHz with the existing `FFS:/startup.bas` layout.
- The 80-rectangle/80-pixel/40-line/circle partial-draw fixture took 2.860 s,
  compared with 3.121 s before raster integration. The owner confirmed the
  shapes were correct and VGA remained stable.
- The 4,000 filled-rectangle workload took 2.909 s, compared with 3.054 s
  before integration. The 2,000 filled-circle workload took 1.756 s, compared
  with 1.776 s; filled circles currently use the portable fallback.
- The 40-line scrolling PRINT test remained at 1.454–1.455 s, effectively
  unchanged from the 1.455 s pre-integration median. The 20-line no-scroll
  `GUPDATE 1` PRINT passes measured 0.401 s each; this integration does not
  accelerate text rasterization or presentation.

The owner confirmed stable, correct output on the latest drawing fixture.
The host CMake suite could not be invoked because CMake is absent in the
environment; affected host tests were compiled and run directly.

## Follow-up

Profile PRINT composition and presentation separately before changing its text
path. Filled-circle acceleration was subsequently enabled after byte-exact
comparison; see the results below.

## Filled-circle acceleration — 2026-11-14

The packed-4 adapter now accelerates filled circles, including circles clipped
at canvas edges. Invert mode and unsupported canvas layouts still use the
portable fallback. Host tests compare the packed output byte-for-byte for
centered, clipped, zero-radius, and clear-over circles.

The 2,000-circle `GUPDATE 0` benchmark completed three times on the flashed
Olimex double-buffer profile in 1.403 s each, compared with the earlier 1.756 s
portable-path result (about 20% faster). The same runs completed the 4,000
rectangle control in 2.405–2.505 s; there is no matched control for that
measurement. No VM errors occurred. The owner’s visual check of the current
VGA output confirmed stable, correct shapes, including the filled circle.
Separate visual checks also confirmed stable scrolling text and a stable,
correct 100-frame attractor animation/final pattern at 240 MHz. That attractor
run completed in 17.224 s without a VM error, and the board returned to the
normal REPL.
