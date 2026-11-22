# Documentation audit, 2026-11-22

Baseline: origin/main `fb57269`. Inventory derived from `src/language/interpreter.cpp`
(keyword/builtin/shell tables), `src/vm`, `src/platform/*`, `platformio.ini` and `tools/`.
Docs-only change; no test reads docs. Linux preset `linux` build + CTest run anyway: 88/88 passed.

## What was missing or stale

- `MUSIC WAV` (and PLAY/TEST/STOP/PAUSE/RESUME/STATUS) had only a prose mention in LANG; no syntax summary, no API entry.
  No SOUND/PLAY statements exist; `BEEP`, `VOLUME` were undocumented in LANG.
- No SEEK keyword exists; seeking is `#c AT offset` (plus `FPOS`/`FLEN`). Not documented as such.
- Dialog/menu/network commands (DTEXT, DEDIT, DINT, DFLOAT, DCHOICE, DCHECK, DBUTTONS, DGET, MENABLE, GETNET, DISNET, NETSTAT$) absent from LANG.
- Debugger/shell commands (BREAK, CLEARBREAK, WATCH, UNWATCH, TRACE, STATS, DEBUGBAR, WINDOWMODE) absent from LANG.
- Runtime `MODE 0/1/2` on `olimex_esp32_sbc_retro`: PLAT and handoff still said MODE is compile-time.
- Bytecode compaction (40 B `Instruction`, narrow 16-bit fields, compile error on overflow): not in VM/LANG.
- DIM limits (non-negative bound ≤ 2147483647, 1,000,000 total elements, ESP32 free-block preflight, 8 B/numeric element).
- `esp32_2432s028_spiffs_debug` env, `olimex-psram`/`olimex-retro` build-target options, Tenstar build, and tools
  (`bytecode_footprint.cpp`, `ql32-olimex-bytecode-memory.py`, `run-vm-benchmarks.py`, `upload-olimex-fs.sh`, `convert_spleen_bdf.py`).
- Handoff statements ("local only", 83/84 tests) were historical; SRS had no entries for the above.

## What was added

- LANG §11 "Built-in reference index" (11.1–11.9) with syntax, limits and QL/32-specific labels.
- API: "Audio services (QL/32-specific)" section.
- PLAT: debug env row, corrected MODE note. VM: bytecode compaction. SRS: 2026-11-22 status block.
- README: build targets/notes. PROJECT-HANDOFF: refreshed date, corrected stale notes, new "Where everything is documented" section.
- No SMSQ/E compatibility is claimed for any new entry.

## Remains unclear / suspected issues (not changed)

- `tools/build-target.sh cyd` builds SD-only `esp32_2432s028`, contrary to the `esp32_2432s028_spiffs` preference; Tenstar has no option.
- `upload-olimex-fs.sh` targets `olimex_esp32_sbc_fabgl`, not the retro env.
- `ql32-olimex-bytecode-memory.py` hardcodes `/dev/ttyUSB0`.
- Stale counts: LANG "33-case suite", SRS "65/65", handoff CTest 83/84/88 (current: 88).
- `KDlgNoDrag` is accepted but has no effect.
- Argument forms for CLS/FLASH/BLIT in §11 come from the arity table, not verified against VM behavior.
- §11 dialog/menu syntax is taken from UI-001 and the arity table; `DIAGNOSTICS ON|RESET` and `TRACE ON` from README.
