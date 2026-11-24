# Olimex retro default image (Phase 3/4) - 2026-11-21

`olimex_esp32_sbc_retro` (+ `_retro_status` with `DMADIAG`) is now the default Olimex
WROVER/PSRAM image (in `default_envs`): single-framebuffer packed4 PSRAM-DMA,
runtime `MODE 0/1/2` = 320x200 / 320x240 / 400x300, boots 400x300, 16-entry RGB222
palette, MODE 3/4 rejected. It aliases `..._psram_dma_display_single_fb_multimode`.
FabGL stays a separate image (`olimex_esp32_sbc_fabgl_psram`, 640x400/640x480, FabGL
fallback); the two backends use the same VGA pins/I2S and cannot share one build, so
there is no in-session backend switch. Flash commands are in README.md and PROJECT-HANDOFF.md.

## Status
- Host-tested: Linux preset + CTest 87/87.
- Built (PlatformIO, SUCCESS): `olimex_esp32_sbc_retro`, `olimex_esp32_sbc_retro_status`,
  `olimex_esp32_sbc_fabgl_psram`, `olimex_esp32_sbc_fabgl`, `lilygo_vga32`,
  `esp32_2432s028_spiffs` (CYD), `ql32_tenstar_esp32s3`.
- `esp32s3_tft`: UNVERIFIED / pre-existing. The legacy env has no `-std=gnu++17`
  (fails on `std::string_view`, later hangs in the compiler); identical failure on the
  pre-change tree. Not touched by this work.

## Hardware (retro_status env, serial + owner VGA)
Load program: lines/rects/circles plus PALROT animation, per mode, 110 s each
(counters reset at each mode restart):

| Mode | frames | late refills | spurious IRQ |
| --- | --- | --- | --- |
| 320x200 | 8038 | 0 | 0 |
| 320x240 | 6879 | 0 | 0 |
| 400x300 | 6902 | 0 | 0 |

- MODE switch loop under load (BASIC program: `MODE n MOD 3`, CLS, drawing, PALROT; 60
  switches): DMA heap before allocation alternated 194068/194072 free, largest 98292 (no
  leak/fragmentation); final scanout 0 late refills, 0 spurious IRQ, no VM errors.
- Soak at 400x300 with the load program, ~12 min (44890 frames): 0 late refills,
  0 spurious IRQ, 520 palette applies.
- Owner observed VGA during the runs: stable, no flicker/tearing/blanking.
- Not run on this build: the synthetic PSRAM stress worker (diagnostic-mode only); it
  was qualified per mode in the 320x results file. Soaks at 320x200/320x240 on the
  multimode build were not repeated (qualified in the single-mode builds, ~11 min each).
- Note: BASIC output is VGA-only; over serial only echo and diagnostics appear. A running
  program blocks queued commands (e.g. DMADIAG) until it ends, so use finite programs.
