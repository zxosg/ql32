# Olimex single-framebuffer retro mode, 400x300 packed4 (2026-11-21)

Mode: `QL32_OLIMEX_SINGLE_FRAMEBUFFER` (QL/32-specific; no SMSQ/E claim).
One 60,000-byte PSRAM framebuffer is written directly by graphics and scanned
continuously by the unchanged refill worker into the internal DMA ring.
Tearing is allowed. DMA geometry (150-row ring, 75-row groups), refill core,
interrupt level and timing are unchanged from the 400x300 baseline.

## Status

| Item | State |
|---|---|
| Linux `cmake --preset linux` build + full CTest (84/84, incl. new `ql32_olimex_single_framebuffer_tests`) | host-tested |
| `olimex_esp32_sbc_fabgl_psram_dma_display_400x300_single_fb`, `..._single_fb_status`, `olimex_esp32_sbc_fabgl_psram_dma_test_400x300_single_fb`, `olimex_esp32_sbc_fabgl_psram` | built |
| Diagnostic 30 s run, runtime BASIC drawing run (below) | verified on hardware (short runs only) |
| Long soak, other stress patterns of section 10 (scroll/random/checker as separate profiles), session-cancel loop, multi-core concurrent producers | **not yet run** |

## Changes

- `PsramDmaScanout` (mode only): `kFramebufferCount = 1`; `beginFrameUpdate`
  returns the live framebuffer, `publishFrameUpdate`/`cancelFrameUpdate` are
  trivial; the refill worker's frame swap/stale-damage block is compiled out.
  Palette exchange and apply at row 0 (frame boundary) are unchanged.
  ISR untouched. Default (triple-buffer) path is unchanged for all other envs.
- `packed4_rmw.hpp`: packed-4 edge-nibble read-modify-write goes through
  `storePackedNibble`, which in this mode takes a tiny spin flag for one byte
  update (ESP32 S32C1I does not work on PSRAM). Whole-byte stores never lock;
  the refill worker and ISR never lock; no lock spans a primitive.
  All nibble RMW sites in `framebuffer.hpp` use it.
- Memory ordering: refill only reads bytes; byte stores are atomic; both cores
  share the same PSRAM cache, so visibility needs no extra fence.
- Lifetime: framebuffer is allocated in `begin()` and freed only after the
  scanout workers have stopped; `beginFrameUpdate` returns null once stop is
  requested. A presenter write in flight concurrently with `end()` is not
  guarded (same as before); not exercised.

## Hardware measurements (Olimex ESP32-SBC-FabGL WROVER, serial)

| Run | frames | late refills | spurious IRQ | palette applies | clones |
|---|---|---|---|---|---|
| Diagnostic env, 30 s, PSRAM stress worker (139 MB written) + palette rotation, 100x400 ring | 1811 | 0 | 0 | 59 | 0 |
| Runtime env, BASIC 12 rounds (RECT/LINE/PLOT/CIRCLE, CLS, 40-line text scroll, `GUPDATE 1`), ~26 s, 150-row ring | 2397 | 0 | 0 | 1 | 0 |

Source PSRAM reported 60,000 bytes (runtime) vs 3x before. Visual: bar
animation ran with stable sync (user observation). Late-refill baseline was 0;
refill duration/backlog counters were not captured (profile build not used).
Note the diagnostic env uses the 100-row default ring, not the ring150 geometry.

## Unified architecture / next modes

The VM/graphics layer is unchanged: it presents through
`FramePresenter` to `PsramFramePresenter`, which now writes straight into the
single framebuffer. 320x200 and 320x240 can adopt this by building with the
same flag, but today they run on the FabGL qualified path (double-buffer
profiles) and need a PSRAM-DMA geometry (ring/refill group dividing the frame
height) plus a hardware late-refill qualification run each. Suggested
follow-up: 320x240 then 320x200, then collapsing the triple-buffer path for
modes below 640x400. 640x400/640x480 remain exceptions (timing).

## Follow-up hardening (same day)

### Built
Envs `..._psram_dma_test_400x300_sf_ring150` (ring150 geometry, refill on
core 1, `QL32_OLIMEX_DMA_REFILL_PROFILE`, status every 30 s) and
`..._sf_{checker,gradient,random,fullscreen,scroll}` (`QL32_SF_STRESS_PATTERN`
1..5: a producer inside the PSRAM stress task writes the framebuffer through
begin/publish, with the PSRAM stress buffer and palette rotation active).
`olimex_esp32_sbc_fabgl_psram` was rebuilt and flashed at the end.

### Writer guard (presenter write racing `end()`)
Single-fb `beginFrameUpdate` counts `activeWriters_`; publish/cancel release
it. `end()` stops the workers, then `releaseAllocatedBuffers()` waits up to 200
ticks for zero writers and, if one is still active, logs and leaks the
framebuffer rather than freeing it. Writers starting after the stop request get
null. Rule: every begin must be paired with publish or cancel
(`PsramFramePresenter` does). Not host-tested (scanout is ESP-only).

### Hardware: ring150 + refill profile, 60 s per pattern (PSRAM stress + palette rotation)
| Pattern | frames | late | spurious | refill max us | row max us | backlog max rows |
|---|---|---|---|---|---|---|
| checker | 3621 | 0 | 0 | 4972 | 663 | 75 |
| gradient | 3621 | 0 | 0 | 5143 | 389 | 75 |
| random | 3621 | 0 | 0 | 2624 | 436 | 75 |
| fullscreen | 3621 | 0 | 0 | 5092 | 140 | 75 |
| scroll | 3621 | 0 | 0 | 3751 | 130 | 75 |

Refill batches are 75 rows, so backlog 75 is the normal group size. Same 0
late refills as the baseline.

### Hardware: soak, ring150, checker + stress + palette, ~11 min
frames=39811 (last snapshot), 0 late, 0 spurious, refill max 5019 us, row max
376 us, 1315 palette applies, free internal heap constant (205,552 B), no reset.

### Hardware: session cancel/restart (runtime `single_fb_status`)
40 x (`RUN`, Ctrl-C after 1.5 s mid-draw), then DMADIAG: no reset or panic,
0 late, 0 spurious, active-writers=0, free internal heap stable (~139 KB).
MODE switching does not apply: single-fb is locked to the build geometry.

### Hardware: BASIC palette animation
`PALETTE`/`PALROT` during drawing: palette-applies rose with 0 late refills. A
smooth 14-colour `PALROT` ramp over a line fan was confirmed smooth on VGA by
the owner. A first script mixing `PALROT`, `PALETTE` and `PALFADE` per
iteration flickered; that was the script, not the display. Finding: an
over-long serial input line (a 14-value `PALETTE`) left the REPL unresponsive
until reset (not investigated; unrelated to scanout). The closing DMADIAG of
the final 40 s ramp run was not captured.

### Recommendation
Ready to push for 400x300. Not verified: soaks beyond 11 min, counters during
sustained BASIC palette animation, the REPL long-line hang. 320x240 needs a
PSRAM-DMA geometry (ring/group dividing 240 rows), a hardware late-refill run,
and leaving the FabGL qualified path; 320x200 is next.
