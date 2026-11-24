# Sound capabilities and music formats plan (proposal, 2026-11-20)

Status: plan only. Nothing here is implemented. No hardware needed to read it.

## 1. Current stack (inventory)

| Layer | Where | Notes |
|---|---|---|
| Language | `src/language/interpreter.cpp` | `MUSIC PLAY/PAUSE/RESUME/STOP/STATUS/TEST`, `BEEP ms,hz[,wave]`, `VOLUME 0..255` (QL/32-specific) |
| VM | `src/vm/interpreter.cpp` | ops call `runtime::MusicService`; BEEP waits like `PAUSE` (only the caller suspends); no service -> "audio service unavailable" |
| Runtime interface | `include/ql32/runtime/music.hpp` | `play, stop, pause, resume, state, testTone, beep, setVolume, volume, stopSession` |
| Portable core | `include/ql32/music`, `src/music` | VTX parser (LH5 decoder), `Ay38912` renderer, `VtxPlayer`, `ToneGenerator` (5 waveforms), `pcmToDac16`, `applyVolume` |
| Olimex adapter | `src/platform/olimex_vga/music.cpp` | task `ql32_music` (prio 3, 6 KiB), I2S0 built-in DAC, GPIO25, 22050 Hz mono, 4x256 DMA, one owning session, tone mixed over player, global volume |
| Boards | `capabilities.hpp` | only Olimex declares `Audio`. LilyGO VGA32 exposes GPIO25 audio but has no service. CYD/S3/P4/Tenstar: none |
| Docs | SRS-001 rule 3, LANG-001 (BEEP/VOLUME), language reference, Olimex README, `ay-vtx-olimex.md` | no SRS row for audio formats |

Limits today: one stream plus one tone; mono; 8-bit DAC (about 48 dB); VTX only (AY/YM register dumps); whole file loaded (<=64 KiB); no sample playback; no effects mixing from other sessions (second session gets `ResourceBusy`).

## 2. Candidate formats

Cost is for ESP32 (240 MHz, about 300 KB usable heap without PSRAM, 4 MB+ with PSRAM) at 22.05 kHz mono.

| Format | Value | Effort | CPU / RAM | Licensing / test data |
|---|---|---|---|---|
| WAV PCM 8/16-bit, mono/stereo, streamed | High: effects, speech, any tool can make it | S | trivial CPU; stream from storage with 4-8 KiB buffer; resample (linear) when rate differs | self-generated test files, no concerns |
| IMA/MS ADPCM WAV | Medium: 4x smaller samples | S-M | low CPU | self-made |
| PT3/PT2 (ZX tracker AY) | High: large AY scene, reuses `Ay38912` | M | low CPU; module 5-30 KB | many tunes have unclear licences; use own or explicitly free modules |
| PSG / YM (unpacked/LHA) | Medium: AY register dumps, easy | S | low | tunes: check per file; YM is already near VTX |
| STC/SQT/ASC (more ZX AY trackers) | Low-Medium | M each | low | as PT3 |
| MOD (4-channel Amiga) | Medium-High: huge scene | M-L | mixing 4 voices ok (about 15-25% of one core); sample RAM 20-200 KB, so PSRAM preferred | many MODs unclear; use CC0/own |
| MIDI SMF + software synth | Medium | L | needs soundfont/wavetable; heavy RAM | GM soundfonts licensing |
| SID (C64 6581) | Medium: fan base | L | accurate emulation is CPU heavy; needs 6502 core | HVSC licences vary |
| MP3/OGG/FLAC | Low on this DAC: library cost large, 8-bit output wastes quality | L | MP3 needs 100+ KB RAM and noticeable CPU | decoder licences (minimp3 is CC0) |

Recommendation: WAV first, then PT3 (+PSG), then MOD (PSRAM boards). Defer MIDI, SID, MP3.

## 3. Candidate sound capabilities

Labels: QL/32 = project-specific, no compatibility claim. SMSQ/E = only if a reference is cited and conformance-tested. Today no SMSQ/E sound command is implemented; SMSQ/E `BEEP` has a different signature (QL sound-chip units, gradients), so a compatible `BEEP` would need its own reference-based work.

1. Mixer core (QL/32): N voices (suggest 4-8) with per-voice volume, then master `VOLUME`; replaces the single player+tone path. Portable, host-testable.
2. Sample playback (QL/32): `SOUND PLAY path$ [,loop]` or `SFX`, on top of WAV; background music plus effects at once.
3. Multi-session ownership policy (QL/32): decide between exclusive owner (now) and shared mixer with per-session voice quota, stopped on session end.
4. Envelopes/ADSR for `BEEP` (QL/32): optional extra arguments or `ENVELOPE` command.
5. Non-blocking beep (QL/32): `BEEP ... ,ASYNC` or `SOUND` queue, so programs need not wait.
6. SMSQ/E-style `BEEP` pitch/gradient subset (SMSQ/E-referenced): only with a cited reference and conformance tests; low priority.
7. Other boards: `Audio` capability for LilyGO VGA32 (same GPIO25 DAC, small adapter reuse); I2S amplifier boards (CYD speaker, S3/P4 codec) need new adapters in `src/platform/*`, with hardware.

## 4. Ranked proposal

| # | Item | Scope | Portable / host-testable | Needs hardware listening | Sessions |
|---|---|---|---|---|---|
| 1 | Mixer core + voice model | `src/music/mixer`, refactor Olimex adapter to use it, no new syntax | all logic; tests for mixing, saturation, voice lifetime | short regression listen | 1 |
| 2 | WAV streaming + `SOUND PLAY` | WAV parser, resampler, streaming reader; new command; limits documented | parser, resampler, mixing, errors | yes: 8/16-bit, rates, loop | 1-2 |
| 3 | LilyGO VGA32 audio adapter | reuse DAC path; capability bit | no | yes (board needed) | 1 |
| 4 | PT3 (+PSG) player on `Ay38912` | module decoder, `MUSIC PLAY` by extension | decoder and register streams | yes: compare with reference output | 2 |
| 5 | Async BEEP / envelopes | syntax + VM + generator | all | short listen | 1 |
| 6 | MOD player (PSRAM only) | 4-channel mixer user, memory gating | pattern decoding, mixing | yes, CPU load under VGA | 2 |
| 7 | I2S amplifier adapters for other boards | new `src/platform/*` | no | yes | 1 per board |
| 8 | MIDI/SID/MP3 | evaluate again after 1-6 | - | - | later |

Cross-cutting rules: portable cores in `src/music` and `include/ql32/music`, interface in `ql32/runtime`; only adapters in `src/platform`; each session owns its voices and `stopSession` releases them; a UI-blocking command suspends only its session; each format change adds host tests plus a QL32-LANG-001/SRS update; every platform claim stays labeled QL/32-specific unless a reference exists. Keep the Olimex audio task off VGA's core timing: the load test showed 0 late blocks with 1 voice plus tone, so re-measure `maxGap`/late blocks after each mixer or format change (CPU budget, not only correctness).

## 5. Decisions needed from the owner

1. Scope: pick items from section 4 (suggested first cut: 1 + 2).
2. Voice count and whether several sessions may play at once.
3. Whether to add an SRS audio requirements row and a stable `SOUND` command name before implementation.
4. Test data policy: self-generated or explicitly free files only.
