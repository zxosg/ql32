# QL/32 release-readiness checklist

Status: analysis, no code changes. Memory optimisation work is closed
(see `compact-bytecode-plan.md`, sections 1-19). The 60K largest-block gate was
not met; the full-screen dirplayer on the CYD starts with about 139-141K free
and 39-41K largest, and plays with drums at about 75K / 11K.

## 1. Build status on current main

Built with `pio run -e <env>` (sizes from the PlatformIO summary).

| Env | Result | Flash used | RAM (static) |
|---|---|---|---|
| `esp32_2432s028_spiffs` (CYD, preferred) | OK | 1,903,473 B (63.1% of 3.0 MB app) | 60,652 B (18.5%) |
| `esp32_2432s028` (CYD, SD-only) | OK | 1,866,117 B (45.9%) | 58,012 B (17.7%) |
| `olimex_esp32_sbc_fabgl` | OK | 2,021,305 B (57.1%) | 61,076 B (18.6%) |
| `olimex_esp32_sbc_fabgl_psram` | OK | 2,062,429 B (58.3%) | 61,156 B (18.7%) |
| `olimex_esp32_sbc_retro` | OK | 2,062,473 B (58.3%) | 63,348 B (19.3%) |
| `lilygo_vga32` | OK | 1,856,897 B (45.7%) | 55,888 B (17.1%) |
| `ql32_tenstar_esp32s3` | OK | 1,738,545 B (49.1%) | 52,288 B (16.0%) |
| `esp32s3_tft` | **FAILS** (`std::string_view`: env lacks `-std=gnu++17`) | - | - |

Host: ctest 92/92 in the standard, sanitized, compact-only and dual-run builds.

Findings:
- `esp32s3_tft` is broken (missing `-std=gnu++17`/`build_unflags`). Fix it or
  delete it; do not ship it as a supported env.
- Many experimental Olimex `*_psram_dma_*` envs exist in `platformio.ini`; decide
  which are release targets and mark the rest experimental.
- There is no CI (`.github/workflows` absent); builds are only gated by hand.

## 2. Feature matrix per board

| Feature | Gate | CYD spiffs | Olimex / LilyGO | Tenstar S3 | Recommendation |
|---|---|---|---|---|---|
| Compact bytecode (`QL32_COMPACT_CODE`) | env flag, only in CYD spiffs env | ON | OFF | OFF | Keep OFF until a hardware smoke run passes on each board. It is semantics-neutral (dual-run tested) but those boards have never run it. Safe candidate for Tenstar (PSRAM, little pressure) only after a smoke test. |
| ProgramStore spill to `FFS:.q32-run-N` | part of compact path | ON | OFF | OFF | Needs a writable FFS; keep CYD-only. Olimex has SD only. |
| Relocation slab / bump `operator new` | `ARDUINO` + compact, `QL32_NO_RELOCATE` disables | ON | not active (compact off) | not active | Leave off. Host tests never exercise it, so it is device-only risk. |
| Drum budget 8192 with memory fallback | `esp32_cyd/main.cpp` ~820 | ON | n/a (CYD music path) | n/a | CYD-only code. |
| Debug bar (`DEBUGBAR`) | CYD main | ON | n/a | n/a | Decide the shipped default (see section 7). |
| Canvas reclaim after run | CYD/TFT path | ON | FabGL path differs | not exercised | CYD-only. |
| `QL32_HEAP_DIAGNOSTICS`, `HEAP FREEMAP/LIVEMAP` | env flag | ON | OFF | OFF | Diagnostics; keep out of release builds except CYD where it already is. |
| `NETTIME$` / WiFi | `platform/esp32/network.cpp` | ON | built in Olimex retro/Tenstar src filters | built in | Untested off CYD. WiFi pins about 16.5K that never returns. |

PSRAM boards (Olimex WROVER, Tenstar S3): internal-heap pressure is much lower
there. Routing canvas and arrays to PSRAM (plan section 18, item 4) is
worthwhile only if a PSRAM board actually shows memory failures. Do not
pre-emptively add it.

## 3. Never tested on Olimex / Tenstar hardware

Everything validated on-device in the memory programme was CYD only. Unverified:
FabGL display output and double buffering, PSRAM scanout envs, audio (YM/AY,
digi drums, DAC), SD card on Olimex, PS/2 keyboard, Tenstar ST7789 135x240
text console and USB-CDC REPL, SPIFFS on Tenstar (512 KB partition), WiFi and
`NETTIME$`, TASK workers and memory behaviour with the new compact VM.

Hardware smoke test (per board):
1. Boot to the REPL; `HEAP STATS` idle free/largest recorded.
2. `PRINT`, scrolling, colours, graphics (`examples/graphics.bas`, `hello.bas`), `GUPDATE` / double-buffer toggle, no tearing/flicker.
3. Keyboard input (PS/2 on Olimex/LilyGO, USB serial on Tenstar).
4. Storage: SD (`DIR`, `SAVE`, `LOAD`, `COPY`, `DELETE`), FFS on Tenstar.
5. `examples/music-player/dirplayer.bas` or a music example; play a song with sound.
6. `TASK` worker start/stop; `examples/memory-tests`.
7. `NETTIME$` (only if the board's policy enables WiFi).
8. 30 minute soak: no reset, `HEAP STATS` stable.
9. Golden script equivalent (`tools/ql32-golden-device.py`) where the board supports it.

## 4. Docs, versioning and files

- Version: `CMakeLists.txt` says `0.1.0`; no tags, no `CHANGELOG`, no release notes. **Decision needed**: version number and tag naming (suggest `v0.2.0`).
- `README.md` (340 lines) lists targets as VGA32, Olimex and CYD; it omits Tenstar, the compact-bytecode/memory features and `NETTIME$`. Add a supported-boards table and a "known limits" section.
- `PROJECT-HANDOFF.md` (591 lines) is milestone history, likely stale after the memory programme; refresh or mark as historical.
- Stale or experimental plan docs: `plans_PHASE3-compact-vm.md` (odd name), `bytecode-32b-repack-analysis-2026-11-22.md`, Agon/DMA investigations, `olimex-vga-graphics-plan.md`. Mark status headers (done/superseded/experimental) and add an index in `docs/plans/`.
- Repo-root clutter: `memory-test-report.json`, `ql32-screen.ppm`, `ql32-graphics-session-1.svg`, `_dbg`. Move or ignore.
- Examples: many benchmark `.bas` files; pick a curated set for the release. `examples/cyd_startup.bas`: confirm it has no WiFi SSID or credentials, and ship a startup template with sync disabled.
- Document `NETTIME$`/`DISNET` and the WiFi memory trade-off (spec `QL32-NET-001.md` already has the row).

## 5. Open risks from earlier reviews

| Risk | State | Action |
|---|---|---|
| Compact-only tests: ring-of-8 wrap, borrowed extras vs truncate, nested EVAL/TRAP rollback, STOP/CONTINUE | partially covered | Add tests (host, no hardware). |
| Spill fault injection (OOM, damaged/missing file, FFS write failure) | not covered | Add fault-injection hooks and host tests. |
| Spill restore reads whole file into one contiguous string | open | Stream it transactionally. |
| Leftover `.q32-run-N` recovery files after reset mid-run | seen on device | Startup scan: report or remove stale files safely; document. |
| Spill latency and flash wear over repeated RUNs | not measured | Measure on CYD. |
| WiFi pins about 16.5K after `DISNET` | measured, documented | Document; no fix. |
| Bump slab untested on host | open | Add a host build defining `QL32_BUMP_NEW`. |
| Largest-block gate unmet (43K vs 60K) | accepted | Document as a known limit; full-screen dirplayer works but is tight. |

## 6. Release artifacts

- Per env: `firmware.bin` plus bootloader and partition table (`.pio/build/<env>/`) and the flash command or a merged binary (`esptool merge_bin`). Suggested set: `esp32_2432s028_spiffs`, `olimex_esp32_sbc_fabgl`, `lilygo_vga32`, `ql32_tenstar_esp32s3`.
- SHA256 sums file.
- Release notes: highlights, memory numbers, known limits, per-board status (tested / build-only).
- Tag `vX.Y.Z` on main after the hardware gate; GitHub release with the binaries.
- Boards that are build-only should be labelled "experimental / untested" in the notes.

## 7. Prioritised to-do list

### A. Can do without hardware
1. Fix or remove the `esp32s3_tft` env; label experimental Olimex envs.
2. Add the missing compact-only tests (ring-of-8, extras, EVAL/TRAP, STOP/CONTINUE) and spill fault-injection tests.
3. Stream the spill restore; add a stale recovery-file scan.
4. Add a host build with `QL32_BUMP_NEW`.
5. Add CI (host ctest matrix and the pio builds of the four release envs).
6. README update (boards table incl. Tenstar, features, limits), refresh or archive `PROJECT-HANDOFF.md`, status headers on plan docs, clean repo root, curate examples, write `CHANGELOG.md` and release notes.
7. Script release artifact packaging and checksums.

### B. Needs your hardware
1. Olimex and LilyGO smoke test (section 3), FabGL display, double buffer, audio, SD, PS/2.
2. Tenstar smoke test: ST7789, USB-CDC, SPIFFS (512 KB), PSRAM heap numbers, audio if present.
3. CYD: spill latency and flash wear measurement; final regression on the tagged build.
4. Optional: first on-device run of `QL32_COMPACT_CODE` on Olimex/Tenstar.

### C. Needs your decisions
1. Version number and tag naming; changelog format.
2. Enable `QL32_COMPACT_CODE` on Olimex/Tenstar for the release, or keep CYD-only (recommended: CYD-only).
3. Which boards ship as supported versus experimental.
4. WiFi/NTP policy for shipped startup files (recommended: off by default, documented).
5. Debug bar default for the released CYD firmware.
6. Whether to spend effort on PSRAM routing for Olimex WROVER and Tenstar (recommended: only if hardware tests show failures).
