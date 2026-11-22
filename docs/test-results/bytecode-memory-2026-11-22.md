# Bytecode memory compaction — 2026-11-22

Goal: reduce memory used by compiled programs and arrays (CYD is memory
bound). Behavior is unchanged; speed was not a goal and was not measured.

## Status labels

| Item | Built | Host-tested | Hardware-verified |
| --- | --- | --- | --- |
| Numeric arrays as 8 B doubles | yes (4 PlatformIO envs) | yes (CTest 88/88) | **verified on hardware (Olimex)**; CYD not verified |
| Compact `Instruction` (shared text, side metadata, `uint32` fields) | yes (4 PlatformIO envs) | yes (CTest 88/88) | **verified on hardware (Olimex)**; CYD not verified |

Olimex results are in "Hardware results (Olimex)" below. The CYD itself was
not flashed or measured.

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

## Hardware results (Olimex) — verified on hardware (Olimex)

Board: Olimex ESP32-SBC-FabGL-style retro build (`olimex_esp32_sbc_retro`,
ESP32-D0WD-V3 with PSRAM, MAC 7c:9e:bd:e2:98:b4), default 400x300 mode, USB
serial REPL. Baseline firmware: commit 2051fa9 (built in a temporary worktree,
flash 1,864,721 B). Compaction firmware: commit e7e0ce8 (flash 1,865,577 B).
Both were flashed in turn and driven by the same script
(`tools/ql32-olimex-bytecode-memory.py`) and the same fixtures; raw results:
`bytecode-memory-olimex-baseline-2051fa9.json` and
`bytecode-memory-olimex-compaction-e7e0ce8.json`. Heap values are
`FREE(0)` total / `FREE(1)` largest block (PSRAM included), in bytes.
This is the Olimex only; the CYD has no PSRAM, so its absolute numbers
differ, but the per-object sizes (8 B array elements, smaller instructions)
are the same.

### Idle heap

| | total | largest |
| --- | ---: | ---: |
| 2051fa9 | 4,150,963 | 3,997,684 |
| e7e0ce8 | 4,152,027 | 3,997,684 |

### Program footprint (heap used while the task is alive)

Measured from the REPL against the idle value after reboot, then task reaped.

| Program | 2051fa9 used | e7e0ce8 used | Change |
| --- | ---: | ---: | ---: |
| tiny (empty loop, 2 lines) | 7,220 | 5,560 | −23% |
| 1,200-statement program (`foot_big`, about 1,200 instr.) | 1,243,168 | 697,688 | −44% |
| `examples/memory-tests/array-worker.bas` (512 doubles) | 25,640 | 11,192 | −56% |

Heap returned to idle values after each task was reaped on both builds.

### DIM boundary (inside a task, one `DIM` plus read/write of the last element)

| Test | 2051fa9 | e7e0ce8 |
| --- | --- | --- |
| `DIM a(60000)` | ok, uses 1,920,208 B (readback 5) | ok, uses 480,192 B (readback 5) |
| `DIM a(150000)` | rejected: array exceeds available memory | ok, readback 5 |
| `DIM a(450000)` | rejected: array exceeds available memory | ok, uses 3.6 MB, readback 5 |
| `DIM a(1000001)` | rejected: array allocation limit exceeded | rejected: array allocation limit exceeded |
| `DIM s$(2000)` + write/read | ok (readback q) | ok (readback q) |

The new limit follows the intended side effect (preflight uses the real
8 B element size); the 1,000,000-element cap is unchanged.

### Golden programs on the board

Seventeen `tests/cli` programs (arrays, array_by_reference, block_if,
expression_rules, for_*, functions, language_slice, numeric_portable,
print_separators, procedure_locals, repeat_*, select, select_edges) were
uploaded to `FFS:/`, run as tasks with `CHANNEL #1 TO` a serial channel, and
the serial output captured. Output is identical between 2051fa9 and e7e0ce8
for all 17 (16 completed normally; `abbreviations` did not complete on either
build, identically, as it expects a different environment, so it only shows
no difference, not a pass).

### Load and MODE switching

- Twelve `MODE 0/1/2` switches with a 20,000-element array in the session: no
  errors on either build; DMA heap before allocation stayed stable (free about
  203,000, largest 98,292 on both; the compaction build is about 256 B lower
  in internal free heap, from its slightly larger code); total heap at the
  end 4,151,959 vs 4,150,907 and largest block 3,932,148 on both.
- MODE switching with a running 1,200-statement task: `MODE 0` and `MODE 1`
  succeed and `MODE 2` is refused ("DMA mode change failed; previous mode
  restored") on both builds, identically. This is existing behavior (the
  internal DMA heap is too small with the task running), not a regression.

### Cleanup

Fixtures were deleted from `FFS:/`; the serial port was released; the board was
left running `olimex_esp32_sbc_retro` built from e7e0ce8. Speed was not
measured on hardware.

## Not done (candidates)

- `line`/`column`/`basicLine`/`sourceLineTarget` as 16-bit, packed bool flags
  (about 20 B more per instruction; truncation risk on huge sources).
- Sharing call-site `parameters` with the definition's list.
- Slot-indexed variables and locals (`unordered_map<string, Value>` per session
  and frame) — larger change, needs separate approval.
- Compact numeric `%` arrays (4 B) — would need identical range-check semantics.
- On-device CYD measurement (`DEBUGBAR`, `tools/ql32-memory-test.py`) after
  the owner flashes a build.
