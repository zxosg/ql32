# Olimex sound output — 2026-11-20

## Setup

Board: Olimex ESP32-SBC-FabGL, env `olimex_esp32_sbc_fabgl_psram`, standard
(non-DMA-test) 120-row profile, serial `/dev/ttyUSB0`. Audio: I2S0 built-in DAC
on GPIO25, 22050 Hz, 256-sample blocks, 4 DMA buffers. VGA scanout stays on I2S1.
Only the application partition was flashed; FFS and SD contents were not erased
(`attractor.bas` and `mandelbrot.bas` were added to the SD card root).

## Symptom and changes

Reported symptom: starting music produced only a blip, with no waveform. Changes
(all uncommitted when this was written):

- I2S communication format `STAND_I2S` → `STAND_MSB`, because the built-in DAC takes
  the upper byte of each 16-bit slot and the I2S one-bit delay shifts the data.
- The `MUSIC TEST` tone no longer reads GPIO25 back through ADC2; an ADC2 read
  disables the DAC on that pin and could itself produce blip-then-silence.
- Music task stack 4 KiB → 6 KiB (the task calls `Serial.printf`).
- Diagnostics every ~1 s for the first ~10 s of playback: PCM and DAC min/max, largest gap
  between `i2s_write` completions, late blocks (gap > 40 ms) and stack headroom.
- `MUSIC TEST` plays a 1 kHz sine (amplitude 6000). The first build used a ±12000
  square wave; the owner heard it "loud, slightly distorted with additional
  frequency", which is expected from a naive square at 22.05 kHz. The sine was clean.
- Portable `include/ql32/music/dac_format.hpp` (PCM → DAC word, sine generator) with a
  host test in `tests/music_tests.cpp`.

Which of the first three changes removed the blip was not isolated; the original
failure was not reproduced on the new firmware.

## Results

| Check | Result |
| --- | --- |
| Build | `pio run -e olimex_esp32_sbc_fabgl_psram` succeeded. |
| Host | `ql32_music_tests` passes. Full CTest: 79 of 82 pass; scheduler, channel and hardware tests fail identically on clean HEAD. No `linux` test preset exists (only a configure preset). |
| Square tone | Loud, slightly distorted (test-signal artifact). |
| Sine tone | Owner: clean. |
| `MUSIC PLAY "SDC:/Bosconia1.vtx"`, idle | Owner: recognizable, clean. Log: `i2s_write` accepted 1024/1024 B, maxGap ≈ 11.62 ms, 0 late blocks. |
| Music + attractor task | Owner: clean, no dropouts. Log: maxGap ≤ 11.66 ms, 0 late blocks (diagnostics cover the first ~10 s only). |
| Music + attractor + Mandelbrot | Log: maxGap ≤ 11.65 ms, 0 late blocks. Owner (reported afterwards): sound was clean and the VGA picture was also OK. |

## Not verified

- Long-duration playback (diagnostics stop after about 10 s).
- Playback with the DMA 640x480 profiles and VGA late-refill telemetry.
- VGA late-refill telemetry during playback (the picture looked fine to the owner).
- Output level and amplifier requirement; loop and end-of-song behaviour; pause/resume by ear.
- Which of the three changes fixed the original blip.

## Next steps

Listen to music while both workloads run; repeat on a 640x480 DMA profile; decide
whether to remove the temporary diagnostics or keep them behind a debug flag; add a
`linux` test preset or correct the handoff's reference to it.
