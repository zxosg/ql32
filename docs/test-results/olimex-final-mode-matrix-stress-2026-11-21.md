# Olimex final mode matrix, MODE 0 fps and multi-task stress, 2026-11-21

Owner present, standing flash permission, `/dev/ttyUSB0`, app partition only.
Serial BASIC output goes to the VGA console; picture checks are owner-observed.
BEEP/MUSIC is not on this branch, so audio is not in the mix.

## Code changes (this step)
- Faster aligned encoder, chunked present row write and cheaper console compose are unconditional in every
  PSRAM-DMA env (only `QL32_OLIMEX_LEGACY_ENCODER` and `PRESWRITE 0` select the old paths). No env change needed.
- No-PSRAM `olimex_esp32_sbc_fabgl`: `DisplayModes::setMode` now preflights FabGL's viewport allocation
  (`include/ql32/platform/fabgl_vga/mode_memory.hpp`, only when no SPIRAM) and throws a BASIC error instead of FabGL `abort()`.
- New env `..._640x400_present_profile` (standard placement + `PRESDIAG`).
- New `examples/stress/*.bas` workers and fps probes.

## Built / host-tested / hardware
- Built: `olimex_esp32_sbc_fabgl`, `_psram`, `_graphics_profile`, `..._640x400_present_profile`, `..._640x480_present_profile`.
- Host-tested: `testVgaModeMemoryPreflight` (no-PSRAM 45 KB / 25 KB free refuses 320x240 and 400x300; shrinking allowed); full CTest 80/83 with the 3 known failures (scheduler, channel, hardware).
- Hardware: below.

## (a) Scanout paths (code and measurements)
FabGL modes 0-2 (`DisplayModes` -> `VGAController::setResolution`) scan out through FabGL's I2S DMA from a viewport
allocated line by line in internal DMA-capable RAM (`MALLOC_CAP_DMA`; the no-PSRAM abort logs "allocated 400x58,
required 400x300"). The CPU only draws into that buffer; there is no refill worker, so late refills do not exist
there. 640x400/640x480 use `PsramDmaScanout`: PSRAM frame, CPU refill task + ISR on core 1 into an internal line ring
(late-refill counter applies).

## (b) Mode matrix (owner-observed picture, attractor then PRINT)
| Mode / env | Boot | Mode switch | Attractor | Stability | Late refills | Presents/30 s |
|---|---|---|---|---|---|---|
| 320x200 FabGL `graphics_profile` | ok | ok | animating, 20 frames 4.4 s | stable (stress run) | n/a (I2S DMA) | ~4.5 fps |
| 320x240 | ok | ok | 20 frames 4.8 s | stable | n/a | ~4.2 fps |
| 400x300 | ok | ok from fresh boot; after tasks ran, "allocation incomplete 400x236" (graceful error, rolled back) | 20 frames 6.0 s | stable | n/a | ~3.3 fps |
| no-PSRAM `olimex_esp32_sbc_fabgl` MODE 1/2 | ok | **refused**: "not enough memory for VGA mode 320x240: needs 76800 bytes, 45440 free"; no reboot | - | stays 320x200 | n/a | - |
| plain `_psram` env MODE 2 | ok | graceful "allocation incomplete 400x228" (use `graphics_profile`) | - | - | n/a | - |
| 640x400 `..._640x400_present_profile` | ok | - | owner: stable, animating | stable | 0 of 25,804 (and 0 in repeat) | 104 requests, 25 full, full present ~0.38 s |
| 640x480 `..._640x480_present_profile` | ok | - | owner: stable, animating | stable | 0 of 22,069 | 85 requests, 23 full, 0.50 s |

640x400 had never been re-measured; it is faster than 640x480 as expected (9.4 vs 11.8 s presenting per 30 s).

## MODE 0 "1 FPS" investigation (user report)
Bounded probes (20 frames, host wall time, `examples/stress/fps-*.bas`), `graphics_profile`:
| Load | MODE 0 | MODE 1 | MODE 2 |
|---|---|---|---|
| draw loop (CLS+RECT+GUPDATE), alone | 20 frames <=1.4 s (>14 fps) | 2.0 s | 3.0 s |
| attractor alone | 4.4 s = 4.5 fps | 4.8 s | 6.0 s |
| attractor + loop task | 7.0 s = 2.9 fps | - | - |
| attractor + PRINT task | 28.2 s = 0.7 fps | - | - |
| attractor + PRINT + churn + loop | 35.6 s = 0.6 fps | - | 59.7 s = 0.3 fps |

Finding: MODE 0 alone is not slow (the fastest of the three). About 1 fps appears only when a scrolling PRINT task shares the
scheduler: each PRINT line invalidates and presents the console region synchronously inside the printing task's slice,
so the presenter (not BASIC arithmetic) dominates time and starves the attractor. It is not a MODE 0-specific draw/vsync
bug, so no mode-local fix was made. Candidate fix (not done): rate-limit/coalesce console presents on the FabGL
presenter (the `WindowedService` coalescing exists but is only wired in the Olimex DMA diagnostic envs) and make
`PRINT` output batching per slice. fps before = fps after (no change).

## Multi-task stress (attractor + PRINT loop + string/array churn + sin/cos loop; 3-4 min each)
| Run | Heap (min free) | VM errors/reboots | Late refills | Fairness (progress reports) | Presents |
|---|---|---|---|---|---|
| FabGL MODE 2 (`graphics_profile`, 180 s) | 4,031,475 (flat) | none | n/a | print 25,000 lines, churn 250, loop 46: all progress, no blocking | not counted |
| 640x480 standard (240 s) | 3,289,559 (flat) | none | 0 of 126,413 | print 1,900 lines, churn 10, loop 2: all progress but slow | 82 calls, 9 full |
| 640x400 standard (180 s) | 3,457,823 (flat) | none | 0 (parse partial) | print 1,900, churn 10, loop 2 | 82 calls |

No watchdog, brownout, out-of-memory error or lost sync was observed on serial; heap did not drift. The standard
640x400/640x480 placement is stable but heavily unfair under load (a PRINT task dominates; compute tasks reached only their
first progress report), consistent with the speed findings. MODE 2 under the same load is fair. Window/UI activity was not
included (no scripted UI workload). Stress-run picture stability was not separately observed by the owner; the final
640x400 and 640x480 attractor runs were confirmed stable and animating.

## Final state
Board left on standard `..._640x480_present_profile`; owner confirmed the picture stable.
