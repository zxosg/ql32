# YM effects plan (2026-11-21, plan only, awaiting user approval)

Status: **proposed. Nothing implemented, no board use.** QL/32-specific behavior unless a reference is cited.

## Current state (verified in code, `src/music/ym.cpp`, `ym_player.cpp`)
- Parsed: YM2!, YM3!, YM3b (register dumps), YM5!, YM6! (with digi-drum samples). Rejected: YM4, MIX1, YMT1/YMT2.
- Implemented in a first approximation: SID voice (square amplitude gate), digi-drums (nearest-sample, frame-start trigger), sinus-SID (16-step table), sync-buzzer (envelope restart on timer tick).
- YM2 is played as a plain register dump: no Mad Max digi-drums.
- Hardware-listened (OK by ear): ZYNAPS1 (YM2), buzz, BITOFF2, led1, rapido1. No effect-specific verdict on a SID/digi-drum file yet; the first-pass player was never compared with a reference.

## Reference and licensing
- Behavioral reference: `arnaud-carre/StSound` (MIT, LICENSE present). Secondary: `olivierh59500/ym-player` (Go port of StSound; no LICENSE file, README claims BSD-2; ideas only).
- MIT would allow copying with notice, but the project rule is to implement independently. Approach: read the reference for behavior, write tests from the YM file spec and reference observations, write our own code in our structure (portable `src/music`), and add a header comment citing StSound as the behavioral reference. List it in `docs` and in the third-party notes. No source is copied; if any table of constants is taken verbatim (for example Mad Max sample data), copy it only with the MIT notice kept in a separate file.
- Fixed point only, no float on the render path, no heap in render.

## Stages (each: host tests, then listening on the board only after approval)
1. **Audit against StSound (no behavior change).** Write a short comparison table: MFP timer prescaler/count decoding, per-voice effect slot rules, how long an effect runs inside a frame, volume handling for SID/sinus/digi, drum sample rate and looping. Output: list of diffs as test cases. Host-only.
2. **SID voice and sinus-SID.** Align the timer arithmetic (MFP 2457600 Hz, prescaler x count) and the phase, and re-trigger/stop behavior at frame boundaries. Tests: square period for several prescaler/count pairs, sinus table shape, voice stops with the next frame without an effect.
3. **Digi-drums.** Match sample rate, volume scaling (4-bit vs 8-bit samples), and the end-of-sample behavior. Consider linear interpolation for low-rate samples. Tests: the drum plays to its last sample, the correct rate, the voice is restored afterward.
4. **Sync-buzzer.** Envelope restart at timer rate with the shape from R13; test restart period and shape-kept semantics (R13=0xFF).
5. **YM2 Mad Max digi-drums.** YM2 encodes up to 3 hard-coded drum samples through register bits. Needs a small built-in sample table; the data's licensing must be checked before embedding it (otherwise omit and report as unsupported). Tests: with synthetic input.
6. **YMT1/YMT2 and MIX1.** Separate format work: tracker modules with sample-based voices, not register dumps. Larger scope (own parser + player), only if the user wants it. Recommend deferring.
7. **Listening pass (needs user and board).** Files with SID/digi-drums (the user picks), one LHA-wrapped; compare with a real chip/another player; capture serial `late/maxGap`.

## Host tests
In `tests/music_tests.cpp` plus tiny synthetic YM fixtures built in code: period tables for MFP timers, SID/sinus/buzzer waveforms, drum playback and restore, YM2 pattern decoding, loop handling, rejection of malformed headers. All portable, no board.

## RAM / PSRAM impact per board
- Effects state is a few dozen bytes per session. The cost is the unpacked file: YM5/6 register data is 16 bytes x frames plus drum samples; a 5-minute tune at 50 Hz is about 240 KB; ZYNAPS1 (YM2) is 146 KB.
- PSRAM boards (Olimex): fits in PSRAM, as it does today (the whole file unpacked).
- SRAM-only boards (CYD and others): large YM files do not fit. Streaming/transcoding is a separate, user-gated step (needs a StorageService binary write handle) and is **not** part of this plan.
- CPU: effect ticks run per output sample (22050 Hz); expect a small addition (3 voices, integer math) on top of the 4x-oversampled AY core. Measure `maxGap/late` under a VGA load at the listening stage.

## Risks
- Effects depend on exact timer phase, so subjective results may differ from real hardware; the reference comparison and listening are the arbiter.
- Digi-drum rate versus the 8-bit DAC at 22050 Hz can sound grainy; this is an output limit, not a player bug.

## Decisions needed from the user
1. Approve stages 1-5 in this order (audit first)?
2. Is YMT1/YMT2/MIX1 (stage 6) wanted, and when?
3. Which files should be the SID/digi-drum listening set?
