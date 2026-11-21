# Olimex fast modes 320x200-400x300 and 640x400/640x480 fallback: functional matrix + speed analysis - 2026-11-20

Board: Olimex ESP32-SBC-FabGL, /dev/ttyUSB0, owner present. Commit base 1bf61b7.
Serial BASIC output goes to the VGA console, not serial, so picture checks
are owner-observed; serial shows errors/aborts and telemetry only.
BEEP is not in this branch (sound session work), so "sound while drawing"
was not tested.

## Modes and envs discovered
| Mode | Env | Path | Status before this run |
|---|---|---|---|
| 0/1/2 = 320x200 / 320x240 / 400x300 (`MODE n,0`) | `olimex_esp32_sbc_fabgl_graphics_profile` (PSRAM, FabGL double buffer) | FabGL | qualified (2026-11-16 functional modes report) |
| same | `olimex_esp32_sbc_fabgl` (no PSRAM) | FabGL | mode 0 baseline; 1/2 not tested before |
| 400x300, 320x200, 320x240 | `..._psram_dma_display_400x300_packed4`, `..._dma_display`, `..._dma_dynamic_mode_test` | PSRAM-DMA | parked/experimental, not re-run here |
| 640x400 | `..._psram_dma_display_640x400_profile` | PSRAM-DMA, standard placement | experimental |
| 640x480 | `..._640x480_present_profile` | PSRAM-DMA, standard placement | known stable baseline |

## Test matrix and results
| Mode / env | Boot | Picture stability | PRINT scroll | Graphics (circle/line) | Attractor | Window/UI | Sound | Heap |
|---|---|---|---|---|---|---|---|---|
| 320x200 FabGL PSRAM | ok | stable, readable | ok | ok | not run | not run | n/a | not read |
| 320x240 FabGL PSRAM | ok | stable, readable | ok | ok | not run | not run | n/a | not read |
| 400x300 FabGL PSRAM | ok | stable, readable | ok | ok | drawing, stable (25 s, no VM error) | not run | n/a | not read |
| 320x200 FabGL no-PSRAM | ok | not observed | ok (no serial error) | ok | not run | not run | n/a | boot: 25,196 B free, largest 14,836 B |
| 320x240 / 400x300 FabGL no-PSRAM | ok | **abort() on `MODE 1,0` / `MODE 2,0`** | - | - | `VM error: out of memory` (task start) | - | n/a | 25 KB free |
| 640x400 DMA standard | ok | stable, readable | ok | ok | drawing, stable, late-refills 0 | not run | n/a | PSRAM 3.7 MB free at boot |
| 640x480 DMA standard (present_profile) | ok | stable, readable, slow | ok | ok | drawing, stable, late-refills 0, almost no animation | not run | n/a | PSRAM 3.6 MB free |

## Finding: no-PSRAM profile cannot run modes 1 and 2
Env `olimex_esp32_sbc_fabgl` has about 25 KB free heap at boot. `MODE 2,0`
logs "allocated 400x58, required 400x300", then `abort() was called` on core 1
and the board reboots (same for `MODE 1,0`: 320x73 of 320x240). `TASK "FFS:/attractor.bas"`
on this build also failed with `VM error: out of memory` (after the MODE 2
attempt; the board had rebooted to mode 0 with the same 25 KB heap). This is a memory-budget
limit of the no-PSRAM profile, expected, but the abort is a robustness defect:
the FabGL mode adapter should refuse the mode and stay in mode 0 (the
2026-11-16 report states it rejects short viewports; here FabGL aborts first).
The PSRAM profile at the same modes is fine, so it is not a regression of the
PSRAM profile.

## Speed analysis (read-only; user: "everything very slow", attractor almost no animation)
Baseline on standard 640x480, attractor, owner-confirmed: PRESDIAG over about
40 s: 3 full presents (full=3), 12.7 s total in the presenter, 4.2 s each
(begin-us 0.63 s each = frame clone; rows 3.6 s each = 7.5 ms/row: provider
4.8 ms + PSRAM write 2.7 ms). That is about 0.07-0.08 full frames/s. Earlier
split-core run: 84 frames/30 s (2.8 fps). No attractor fps data exists for
640x400 standard or FabGL 400x300 (not measured; propose measuring).

Where the VM time goes (code: `presentRegion` runs synchronously in the VM/
graphics call, `windowed_service.cpp` calls it after every graphics op):
1. **Refill worker consumes about one whole core.** Standard 640x480 needs
   28,800 rows/s and costs about 33.9 us/row, so about 98% duty on core 1,
   where QL/32's Arduino loop (VM, presenter) also runs at lower priority
   (refill priority 4). The VM gets only leftovers. This explains 7.5 ms/row
   presenter cost versus 0.17 ms/row on the split build (which put VM on a free
   core) and "program executes in a fraction of CPU time". The split build
   then cannot keep up since it needs 38 us x 28.8k = about 110% of a core.
2. **Per-row refill cost is unexplained.** 80 loop iterations of an aligned
   load plus four LUT lookups should be about 5-8 us at 240 MHz; measured
   34-38 us even with IRAM and bulk copy (steps 5-6). Either the timing
   includes per-row overhead (micros() x2, barrier, `lineReadySequence_`
   accesses, uncached PSRAM-backed members) or preemption by ISR/bus
   contention. An isolated encoder microbenchmark would decide this.
3. **Composition dominates inside presentRegion** (provider 66-80% of row
   time), then PSRAM write (12-35%); one full-frame clone per present
   (`beginFrameUpdate`, 0.63 s).
4. **Full-frame presents**: the attractor presents full 640x480 frames, no
   dirty-rect benefit; PRINT presents small regions (cheap).

## Ranked options (not implemented)
1. **Measure the encoder in isolation** (diagnostic env: time N encodes of a
   row without ISR/other traffic, plus refill duty). Cheap, decides whether
   the 34 us is real compute or overhead. Expected: if overhead, a 3-5x
   refill cut gives the VM most of a core back (largest gain, 3-5x).
2. **Present coalescing/rate limit to frame boundary**: skip intermediate
   presents, present at most every N ms; for attractor-like loops reduces
   presenter share from 34% to near the set cap. Expected 2-3x BASIC speed.
3. **Async lower-priority presenter task** so the VM never blocks on a
   present; combined with 2. Expected larger VM speed, but needs session and
   snapshot ownership design.
4. **Dirty-rect only / span copy composition**: helps PRINT and small
   shapes; little for full-frame attractor. 1.3-2x on composition.
5. **VM on core 0** (split placement): already measured; fast (30x) but late
   refills 16-60%, so only viable after option 1 cuts refill below about
   25 us/row.
Recommend trying 1 first (no behaviour change, one flash, tells which of 2/5
is worth it), then 2.

## Status
- Built: graphics_profile, olimex_esp32_sbc_fabgl, 640x400_profile, standard present_profile.
- Host-tested: no code change; ctest unchanged (80/83, 3 known failures).
- Hardware-verified: matrix above. Board left on standard present_profile
  (reset after the runs); owner confirmed stable, readable (slow) picture.
