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

## Remaining items (second pass, same build `1d6ef01`)
| Item | Result |
|---|---|
| VTX LyraII9 | OK |
| SQT Zabreh | OK |
| YM BITOFF2, led1 | OK |
| BEEP at VOLUME 32/128/255 | OK, three rising loudness levels |

YM clip check: led1 reached PCM -16.7k..+18.9k, inside int16, so there is no clipping. The earlier +/-16448 is the maximum PSG sum with full volume, not a limiter. DAC bytes stayed within 0x3e00..0xc900, so the 8-bit DAC range is not exceeded.

## ym-player review (olivierh59500/ym-player)
The repository has no LICENSE file; the README badge says BSD-2-Clause and the project describes itself as a Go port of STSound. Licensing is therefore unclear and derived from STSound, so only ideas were considered and no code was copied. The YM effects (SID voice, sinus-SID, digi-drums, sync-buzzer, YMT1) were not compared in code in this pass; that remains an open follow-up.

## VGA load test (added)
`FFS:/attractor.bas` was loaded and run while VTX z00m_unreal played. The owner reported the attractor animating and the music clean (no dropouts). Serial music diagnostics were not logged after RUN, so late/maxGap under load are unconfirmed; the result is by ear only.

Note: `attractor.bas` has no exit and blocks the REPL (serial commands are ignored). The board had to be reset with the EN button, and it re-enumerated as `/dev/ttyUSB0`. Music diagnostics were not logged during the attractor run, so late/maxGap under VGA load remain unconfirmed.

## YM reference: arnaud-carre/StSound
StSound (the original YM replayer, which ym-player ports) is MIT licensed (LICENSE file present). It is the preferred behavioral reference for the YM path (SID voice, sinus-SID, digi-drums, sync-buzzer, Mad Max YM2, YMT1). Not yet compared in code. Implement independently and cite it in comments and docs.
