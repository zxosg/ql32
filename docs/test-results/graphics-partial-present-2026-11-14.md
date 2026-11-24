# Olimex double-buffer partial presentation — 2026-11-14

## Change

The FabGL presenter previously rejected partial regions when double buffering
was enabled. `WindowedService` then recomposed and presented the full screen
for each small update, including each BASIC `PRINT` statement.

For a partial update, the presenter writes the changed region into the hidden
buffer, swaps once at the frame boundary, and writes the same region into the
newly hidden buffer. This keeps both buffers synchronized while allowing the
next update to remain partial. A full-frame update writes once and swaps once;
the buffers remain unsynchronized until a later partial request needs them. In
that case, the presenter asks `WindowedService` for a full-frame redraw, then
copies it into both buffers. This keeps repeated full-frame animation fast.
The portable graphics service and presenter interface remain unchanged.

## Verification

- Host graphics, scheduler, and Olimex framebuffer tests passed.
- A new host test confirms partial updates change only requested pixels, map
  them to FabGL's raw scanline byte order, swap once, and leave both buffers
  identical. A partial update is rejected until the first full frame has
  synchronized both buffers; `WindowedService` then falls back to a full
  presentation. The test also simulates a second-buffer write failure and
  verifies the presenter rejects later partial updates until a full recovery.
- The Olimex PSRAM double-buffer profile built and flashed on the connected
  ESP32-WROVER board.
- The existing 20-line `GUPDATE 1` PRINT benchmark ran three times at 240 MHz.
  On the final flashed image, host timestamps on the serial marker lines
  measured 0.289, 0.283, and 0.283 s. The median is about 3.0× faster than the
  0.855 s result measured immediately before this change. The earlier
  pre-optimization baseline was 1.708 s.
- The `GUPDATE 0` plus final `GUPDATE` batches measured 0.154, 0.155, and
  0.155 s when every full frame was synchronized. The presenter was then
  refined to synchronize only when a subsequent partial update needs both
  buffers; on the final image the same batches measured 0.108 s each, close to
  the previous 0.096 s result.
- The saved shape workload also completed on the final image: 4,000 filled
  rectangles in 3.054 s and 2,000 filled circles in 1.776 s. These runs use
  batched `GUPDATE 0` drawing and are a smoke test for full-frame updates, not a
  measure of partial-present speed.
- The 100-frame attractor completed at 240 MHz in 21.596 s, within the range
  of the previously measured 19.8–21.4 s runs. It uses a full-frame update each
  frame and therefore does not measure the partial-present speedup.
- A saved `GUPDATE 1` partial-drawing test then drew 80 filled rectangles, 80
  pixels, 40 lines, and a filled circle in 3.121 s. It completed without a VM
  error, and the owner confirmed the VGA image was stable and correct.
- The 40-line scrolling PRINT workload completed in 2.188, 2.193, and 2.192 s
  (2.192 s median). VGA remained stable and readable. This is slower than the
  earlier 1.706 s result. Staging the dirty region so it is composed only once
  before copying to both buffers did not change the timing materially; the
  scroll-heavy path needs more profiling.
- The presenter now declines a partial update when synchronizing that region
  would write more pixels than a single full-frame update. Aligned staged rows
  are reordered into FabGL byte layout once and copied to each buffer with
  `memcpy`; the host regression checks both scanline ordering and buffer sync.
  On the Olimex board, three
  repeat 40-line scrolling PRINT runs took 1.455, 1.456, and 1.454 s (1.455 s
  median), down from the prior 2.192 s median and 1.706 s earlier baseline.
  The owner confirmed VGA remained stable and readable. This result suggests
  the full-frame fallback for large dirty regions helps this scroll workload.
- A three-pass 20-line no-scroll `GUPDATE 1` PRINT control took 0.401, 0.351,
  and 0.351 s on the same flashed image (0.351 s median). That remains faster
  than the earlier 0.855 s result, though slower than the 0.283 s median before
  the large-region fallback. The owner confirmed the latest VGA output stayed
  stable and readable during both workloads.
- `FFS:/startup.bas` now gives the console channel #0 two text rows (16 px) and
  places graphics channels #1/#2 above it. After rebooting with this startup
  file, the owner confirmed there is no overlap.

All board-side workloads completed and returned to the REPL. The owner
confirmed the repeated PRINT benchmark was stable and readable, and the mixed
partial-drawing image was stable and correct. See the
[render benchmark](graphics-render-benchmark-2026-11-14.md) for isolated
framebuffer operation timings.

## Remaining work

Profile the scroll-heavy path, which remains slower than its earlier result
despite stable output. Full-frame batching remains close to its previous
timing; consider a faster synchronized full-frame copy only if later
measurements show it is needed.
