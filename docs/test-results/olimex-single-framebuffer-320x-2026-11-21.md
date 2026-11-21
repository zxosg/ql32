# Olimex single-framebuffer 320x240 and 320x200 (2026-11-21)

QL/32-specific (no SMSQ/E claim). Same `QL32_OLIMEX_SINGLE_FRAMEBUFFER` packed4
path as 400x300: one PSRAM framebuffer written directly, scanned by the
unchanged refill worker (core 1, priority 4, same interrupt level). Tearing
allowed. VM/graphics layer unchanged.

## Status

| Item | State |
|---|---|
| `cmake --preset linux` + full CTest 85/85 (single-fb RMW tests now run 400x300, 320x240, 320x200; geometry and env-config tests extended) | host-tested |
| Envs `..._display_{320x240,320x200}_single_fb[_status]`, `..._test_{320x240,320x200}_sf_{static,checker,gradient,random,fullscreen,scroll}`, `olimex_esp32_sbc_fabgl_psram` | built |
| Stress patterns, 11 min soaks, runtime BASIC with palette animation, 20x RUN/Ctrl-C | verified on hardware (serial counters) |
| VGA image observation | owner saw cycling lines/circles on the runtime build (one mode confirmed in chat); static-pattern and per-mode visual sign-off not formally recorded |

## Geometry

| Mode | Modeline | Framebuffer | Ring | Refill group | Internal DMA |
|---|---|---|---|---|---|
| 320x240 | QVGA_320x240_60Hz, scan-count 2 | 38,400 B | 120 rows | 60 | 51,328 B |
| 320x200 | VGA_320x200_70Hz, scan-count 2 | 32,000 B | 100 rows | 50 | 43,056 B |
| 400x300 (baseline) | VGA_400x300_60Hz | 60,000 B | 150 rows | 75 | - |

Geometry from `scanoutGeometry()`; both divide the frame height. No change to
core pinning, task priority, interrupt level or timing.

## Finding: diagnostic shift forced the slow path (fixed)

First runs of the diagnostic envs gave late refills (320x240 checker: 7641 late
in 60 s; 320x200: 5280 late in 75 s; refill-total saturated the worker,
~130 us/row vs 34 us at 400x300). Cause: `refillRow` applies a moving
horizontal shift in diagnostic mode for any `width_ == 320`, forcing the
per-pixel path. This is a test pattern, not part of the runtime display
(diagnostic off). Fix: single-fb never applies the shift
(`QL32_OLIMEX_SINGLE_FRAMEBUFFER` joins the zero-shift condition). No other
change was needed; the default geometry needed no adjustment. Pre-fix logs
(`*.slowpath`) were from the diagnostic path only.

## Hardware (Olimex ESP32-SBC-FabGL WROVER, refill profile, PSRAM stress worker + palette rotation, 60-75 s each)

| Mode | Pattern | frames | late | spurious | refill max us | row max us | backlog max |
|---|---|---|---|---|---|---|---|
| 320x240 | checker | 3609 | 0 | 0 | 3219 | 114 | 60 |
| 320x240 | gradient | 3609 | 0 | 0 | 3200 | 99 | 60 |
| 320x240 | random | 3608 | 0 | 0 | 2044 | 601 | 60 |
| 320x240 | fullscreen | 3609 | 0 | 0 | 3293 | 118 | 60 |
| 320x240 | scroll | 3608 | 0 | 0 | 2443 | 86 | 60 |
| 320x200 | checker | 4217 | 0 | 0 | 2672 | 114 | 50 |
| 320x200 | gradient | 4216 | 0 | 0 | 2703 | 248 | 50 |
| 320x200 | random | 4216 | 0 | 0 | 1828 | 712 | 50 |
| 320x200 | fullscreen | 4217 | 0 | 0 | 2653 | 121 | 50 |
| 320x200 | scroll | 4216 | 0 | 0 | 1393 | 91 | 50 |
| 400x300 baseline (checker, same session) | | 3621 | 0 | 0 | 4952 | 605 | 75 |

Static envs (`_sf_static`, no refill worker) print no counters; they only show
that the modeline/descriptor chain starts (frames=0 by design).

### Soak (checker + stress + palette, ~11 min)
| Mode | frames | late | spurious | palette applies | refill max us | resets | free internal heap |
|---|---|---|---|---|---|---|---|
| 320x240 | 37874 | 0 | 0 | 1258 | 3225 | 0 | constant 230,144 |
| 320x200 | 44256 | 0 | 0 | 1259 | 2683 | 0 | constant 238,416 |

### Runtime env (`_single_fb_status`), BASIC
12 rounds of LINE/RECT/CIRCLE with `PALROT 1,14,1`, then 20x `RUN` + Ctrl-C,
then `DMADIAG`: 320x240 frames=3833, 320x200 frames=4480, both late=0,
spurious=0, active-writers=0, no reset; palette applies 181. Source PSRAM
38,400 / 32,000 bytes. The first script attempts used `NEXT` and a wrong
`PALROT` form; the language needs `END FOR var` and `PALROT start,count,steps`.

### Not verified
Soaks beyond ~11 min, 3-way concurrent producers, per-mode formal VGA
sign-off for static patterns, 320x200 at the 70 Hz modeline under a long BASIC
soak.

## Runtime MODE and colour analysis

Today `MODE 0..4` on the FabGL path calls FabGL `setResolution`; the DMA
builds switch via `SwitchableDisplayModes` (stop, reconfigure, restart). The
single-fb ring macros are compile-time.

1. **Now (implemented): compile-time env.** One single-fb env per resolution
   (400x300, 320x240, 320x200). MODE is locked to the build geometry. The
   qualified FabGL env stays the multi-mode build and the default.
2. **Proposal: runtime single-fb family.** Take ring/group from
   `scanoutGeometry()` at runtime (table already exists; ring buffers are
   allocated in `allocateAndBuild`) so one single-fb build exposes MODE 0
   (320x200), 1 (320x240), 2 (400x300) through `SwitchableDisplayModes`
   (cheap: one small framebuffer; scanout restart per switch). MODE 3/4
   (640x400/480) stay unavailable (timing) and return a clear error.
3. **FabGL fallback** coexists as a separate build only: FabGL and the custom
   I2S scanout cannot both own the VGA pins/I2S, so no in-session switch
   between backends.
4. **Colour limit:** packed4 means 16 simultaneous palette entries (RGB222
   from 64), global palette applied at the frame boundary. Fades/rotation are
   cheap; programs assuming more than 16 simultaneous colours or direct RGB
   bitmaps need index mapping/quantisation. MODE info should report the colour
   count per mode. Smaller framebuffers (32/38 KB vs 60 KB) free PSRAM.

## Recommendation
Ready to push for both modes as opt-in envs; default env unchanged.

## Phase 2: runtime-switchable single-framebuffer build

Env `olimex_esp32_sbc_fabgl_psram_dma_display_single_fb_multimode` (and `_status`) combines
`QL32_OLIMEX_SINGLE_FRAMEBUFFER` with the existing `QL32_OLIMEX_DMA_DYNAMIC_MODE_TEST`
switcher. It boots in 400x300; `MODE 0/1/2` selects 320x200 / 320x240 / 400x300 by
stopping scanout, freeing the framebuffer and restarting with that mode's own ring/group.
`MODE 3/4` are rejected ("unsupported Olimex DMA mode id").

- Built: yes (`_status` env). Host-tested: yes (CTest 87/87, new single-fb switchable-list test).
- Verified on hardware (automated serial run, no VGA observation yet): 24 switches
  (0/1/2/1/0/2 x4) with CLS + CIRCLE after each; DMA heap before allocation stayed
  free~203400 / largest=98292 across all switches; final DMADIAG: 506 frames,
  late-refills=0, spurious-IRQ=0, active-writers=0. MODE 3/4 rejected, prompt still responsive.
- Not yet done: owner VGA observation, per-mode stress/soak on this build (deferred to the
  end of all phases per owner request).
