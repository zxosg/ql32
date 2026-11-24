# Review: Makerstown/amiga500-esp32 (read-only, 2026-11-21)

Source: https://github.com/Makerstown/amiga500-esp32 (pushed 2026-07-12). Read-only research; no code copied or changed, no board used.

## Licence
- Repository code: MIT, (c) 2026 Massimiliano (`LICENSE`). Reuse is legal with the copyright notice kept.
- Explicitly excluded: Musashi 68000 core (its own MIT licence) and Commodore ROMs/Workbench (not included).
- QL/32 rule still applies: use as a **behavioural reference**; any adopted idea is written independently and cited. Do not paste code. No FabGL/ESP-IDF types in portable interfaces.

## What it is
An Amiga 500 emulator on ESP32-S3 (N16R8, 8 MB OPI PSRAM) with an **SPI ST7796 TFT (320x480, 80 MHz)** and I2S MAX98357A audio. It is **not VGA**: there is no scanout DMA ring, I2S-parallel video or modeline timing, so nothing there replaces our 640x480 PSRAM scanout. It is the S3, not the classic ESP32 of the Olimex board.

## Reusable ideas (behavioural)
1. **Dirty-check per row (`video.cpp`, `prev_line`/`prev_pal`/`g_row_onscreen`).** A snapshot of the previous row's source (bitplane bytes plus palette) is kept in PSRAM; unchanged rows are not re-sent. Relates directly to our ~1 fps present: our `PsramFramePresenter` already does partial presents, but full-frame repaints (22 of 84 presents) cost ~0.4 s each. A cheap per-row hash/compare in the compositor (portable, in `src/graphics`) could skip unchanged rows within a full present. Hypothesis to test: attractor frames change a small share of rows between presents. Cost: extra PSRAM read per row, so measure first (PSRAM traffic is already our bottleneck).
2. **One row at a time, no full framebuffer in DRAM** (400 KB does not fit). Same constraint as ours; confirms row-streaming composition, which we already do.
3. **Block (not byte) PSRAM reads** noted in `video.cpp` ("molto piu' veloce"): matches our finding that chunked reads help; our chunked write gave no gain, so the read side is the lever to re-check.
4. **Audio ring buffer** (`paula.cpp`: power-of-two ring, producer writes, I2S task pops, silence on underflow, ring allocated from PSRAM to save internal DRAM). A sane model for the sound session's mixer to I2S path; QL/32's own design should keep the ring behind a platform-neutral interface.
5. **Input** (`input.cpp`): plain GPIO active-low debounce; nothing beyond what we have (PS/2 on Olimex).

## Not reusable / risks
- SPI TFT push path, ST7796 init, S3-only OPI PSRAM and the legacy I2S driver: different hardware; no use for Olimex VGA.
- Emulator chipset code (Copper/Denise/Blitter/Paula/CIA) and Musashi: out of scope and Amiga-specific.
- Risk of cargo-culting the dirty-check: on our board PSRAM bandwidth is shared with scanout refill (core 1, 28 us/row, late 0), so an extra snapshot read/write per row may cost more than it saves. Needs a telemetry-backed A/B (present us per full frame, late refills) before adoption.

## Recommendation
Nothing to import. Only candidate worth a test is the per-row dirty compare inside full presents (idea 1), as a diagnostic-env experiment with PRESDIAG counters (rows skipped, extra read us) and an owner-present attractor fps check. Do not touch core pinning, ring depth or modes.
