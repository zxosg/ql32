# Bytecode memory compaction — 2026-11-22

Goal: reduce memory used by compiled programs and arrays (CYD is memory
bound). Behavior is unchanged; speed was not a goal and was not measured.

## Status labels

| Item | Built | Host-tested | Hardware-verified |
| --- | --- | --- | --- |
| Numeric arrays as 8 B doubles | yes (4 PlatformIO envs) | yes (CTest 88/88) | **no** |
| Compact `Instruction` (shared text, side metadata, `uint32` fields) | yes (4 PlatformIO envs) | yes (CTest 88/88) | **no** |

PlatformIO builds all succeeded (flash/RAM use after the change):
`esp32_2432s028_spiffs` 1,624,881 B flash / 51,584 B static RAM;
`olimex_esp32_sbc_retro` 1,865,577 / 57,028; `ql32_tenstar_esp32s3`
1,585,597 / 50,584; `lilygo_vga32` 1,702,905 / 54,160. No baseline
firmware was built for comparison; static sizes are not expected to change
meaningfully (the saving is heap). No board was flashed or touched, so no CYD
heap measurement exists.

## Host measurements (Linux x86-64, `ql32_bytecode_footprint`)

Corpus: 94 programs from `examples/` and `tests/cli/` that compile
(6,183 instructions). Raw per-program data:
[baseline](bytecode-memory-baseline-2026-11-22.csv),
[after](bytecode-memory-after-2026-11-22.csv). Counts are of the
compiled instruction record plus out-of-line text/metadata, excluding
allocator overhead and deque block slack.

| Measure | Before | After |
| --- | ---: | ---: |
| `sizeof(Instruction)` | 256 B | 104 B |
| Bytecode total, whole corpus | 1,592,012 B | 769,596 B (-52%) |
| Largest program (`mandelbrot-benchmark.bas`, 306 instr.) | 78,864 B | 35,298 B |
| `sizeof(Value)` (variables, stack, string array element source) | 40 B | 40 B |
| Numeric array element | 40 B | 8 B |
| String array element | 40 B | 32 B (`std::string`) |

Both sizes are x86-64 figures. On ESP32 (32-bit) `size_t`/pointers are
narrower, so the `uint32` and pointer savings are smaller than here; the
relative saving there is still expected to be substantial (shared-text and
side-metadata moves) but is **not measured**.

## What changed

- `RuntimeArray` stores `std::vector<double>` or `std::vector<std::string>`.
  A 5,000-element numeric array drops from about 200 KB to 40 KB.
- `DIM` preflight now compares against the real element size, so arrays up to
  roughly five times larger fit before "array exceeds available memory". The
  1,000,000-element cap and error texts are unchanged. This is an observable
  side effect (more programs succeed); no program that worked before fails.
- `Instruction`: names and string literals are shared allocations deduplicated
  per compile; a string literal lives in `name` (flag `textLiteral`); rare
  metadata (FOR/SELECT items, call parameters, INPUT variables, jump scopes,
  owner definitions...) moved into a lazily allocated side record;
  indices/counts are `uint32_t`; enums are one byte.

## Not done (candidates)

- `line`/`column`/`basicLine`/`sourceLineTarget` as 16-bit, packed bool flags
  (about 20 B more per instruction; truncation risk on huge sources).
- Sharing call-site `parameters` with the definition's list.
- Slot-indexed variables and locals (`unordered_map<string, Value>` per session
  and frame) — larger change, needs separate approval.
- Compact numeric `%` arrays (4 B) — would need identical range-check semantics.
- On-device CYD measurement (`DEBUGBAR`, `tools/ql32-memory-test.py`) after
  the owner flashes a build.
