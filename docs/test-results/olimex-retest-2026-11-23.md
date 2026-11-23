# Olimex retest after CYD changes — 2026-11-23

Source: origin/main `b12d4f5` (PRs up to #19). Board: Olimex ESP32-SBC (PSRAM) on
`/dev/ttyUSB0`, flashed with `olimex_esp32_sbc_retro`. Raw data:
[olimex-retest-2026-11-23.json](olimex-retest-2026-11-23.json) (from
`tools/ql32-olimex-bytecode-memory.py`).

## Status

| Item | Built | Host-tested | Verified on hardware |
| --- | --- | --- | --- |
| Linux preset `linux` | yes | CTest 88/88 pass | n/a |
| `olimex_esp32_sbc_retro` | yes | n/a | yes (below) |
| `olimex_esp32_sbc_fabgl_psram` | yes | n/a | not flashed |
| `esp32_2432s028_spiffs`, `ql32_tenstar_esp32s3`, `lilygo_vga32` | yes | n/a | not flashed |
| `esp32s3_tft` | not rebuilt (known pre-existing failure) | | |

The CYD changes did not break host tests or any Olimex/other build.

## Firmware size (flash / static RAM, bytes)

| Env | Flash | RAM |
| --- | ---: | ---: |
| olimex_esp32_sbc_retro | 1,959,105 | 57,172 |
| olimex_esp32_sbc_fabgl_psram | 1,962,249 | 55,060 |
| esp32_2432s028_spiffs | 1,800,545 | 52,172 |
| ql32_tenstar_esp32s3 | 1,648,625 | 50,704 |
| lilygo_vga32 | 1,766,485 | 54,280 |

Olimex retro grew 93,528 B flash vs 2026-11-22 (1,865,577), and the CYD env
175,664 B (1,624,881); the growth comes from the owner's CYD-side and other merged
changes since then.

## Hardware (olimex_esp32_sbc_retro)

- Flash OK, boots to BASIC REPL, REPL commands respond.
- Golden programs: 16/17 pass. `abbreviations` fails with "undefined procedure or
  function" at line 70, identical to the 2026-11-22 run (harness artifact, not new).
- MODE 0/1/2 idle switching, 12 cycles: no errors. With a task running, MODE 2
  reports "DMA mode change failed; previous mode restored" (as in the 11-22 run;
  modes 0/1 succeed).
- DIM: 60k/150k/450k numeric elements allocate and read back; 1,000,001 gives
  "array allocation limit exceeded"; 2000-element string array OK. Identical to the
  11-22 results.
- Sound smoke: `MUSIC TEST` runs (late=0, maxGap ~11.6-12.2 ms), `BEEP 200,880`,
  `VOLUME 128`, `MUSIC STOP/STATUS` raise no errors. Owner heard VTX (Bosconia1) and YM (MELANNEW) playback and confirmed music OK (2026-11-23).
- File/seek: WRITE 16 bytes, reopen UPDATE, `FLEN`=16, `#f AT 10` -> `FPOS`=10, read
  "ABCDEF", `#f AT 0` -> `FPOS`=0, delete OK.

## Heap baseline (FREE(0) total, bytes)

| Measure | 2026-11-22 | Now |
| --- | ---: | ---: |
| Idle free | 4,152,047 | 4,152,275 |
| 1,200-statement program: consumed (idle - in-program free) | 271,652 | 269,256 |
| Free after kill | 4,151,855 | 4,152,023 |

No regression; no leak after task kill.

## Unverified

VGA picture was not visually checked; `olimex_esp32_sbc_fabgl_psram`
and the non-Olimex envs were built but not flashed.
