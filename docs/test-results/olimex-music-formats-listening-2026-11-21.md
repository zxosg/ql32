# Olimex music formats listening session, 2026-11-21

Board: Olimex ESP32-SBC-FabGL, env `olimex_esp32_sbc_fabgl_psram`, firmware at commit `1d6ef01`. Files on `SDC:/`. Owner listened; results below are by ear. Host tests (ctest 82/82) are separate.

## Bugs found while listening (all fixed)
- AY envelope step rate 8x too slow; noise rate 2x too fast (commit `a698aad`).
- Volume table saturated levels 13-15; replaced with the standard AY-3-8910 relative table (`a698aad`).
- VTX R13=0xFF means "shape not written"; it was treated as shape 15 (`1d6ef01`).
- Channel mix was bipolar (ring-modulated tone x envelope); now unipolar with a DC blocker (`1d6ef01`).

Behavioral reference: vitamin-caig/zxtune (LGPL-3.0). No code copied; implemented independently.

## Results (final build)
| Item | Result |
|---|---|
| WAV vacation, theme (long, streamed; maxGap ~11.6 ms, late=0) | OK |
| PT3 Melancholy, TheDome, Peculiar | OK (re-checked after `a698aad`) |
| PT3 Walker (after `1d6ef01`) | OK, clean AY |
| VTX z00m_unreal (HW-envelope bass) | OK, clear and loud |
| VTX Bosconia1 | OK |
| SQT Enigma | OK |
| YM ZYNAPS1 (YM2, uncompressed) | OK; PCM hits +/-16448 (clip limit) |
| YM buzz, rapido1 (LHA) | OK |

Serial: late=0 and maxGap ~11.6 ms for every item.

## Not done
- VTX LyraII9, SQT Zabreh, YM BITOFF2/led1 not heard on the final build.
- BEEP/VOLUME 32/128/255 recheck.
- olivierh59500/ym-player review (license not yet checked).
- YM peaks reach the clip limit; the cause is unconfirmed (digi-drums vs mix gain).

## VGA load test (added)
`FFS:/attractor.bas` was loaded and run while VTX z00m_unreal played. The owner reported the attractor animating and the music clean (no dropouts). Serial music diagnostics were not logged after RUN, so late/maxGap under load are unconfirmed; the result is by ear only.
