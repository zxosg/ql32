# Tenstar TS-ESP32-S3 full test, 2026-11-23

Board: `ql32_tenstar_esp32s3` on /dev/ttyACM0 (135x240 ST7789, 4 MB flash, PSRAM, no SD, no audio, no touch).
Source: origin/main 7f7ff05 plus docs. Olimex and CYD untouched.

## Built / host-tested
- Built: 1,649,581 B flash / 50,704 B RAM.
- Host: CTest 89/89.

## Verified on hardware

| Area | Result |
|---|---|
| Boot, REPL | pass |
| Golden CLI corpus (31 cases, `tests/cli`) | 28 pass: 17 line-mode, 11 numbered-program + RUN. See differences below |
| DIM / OOM | 60,000 and 150,000 numeric elements read back; `DIM s$(2000)` ok; `DIM c(450000)` -> "array exceeds available memory"; `DIM d(1000001)` -> "array allocation limit exceeded" (graceful) |
| Heap baseline | FREE(0) idle ~2,372,700, largest block 2,064,372 |
| Canvas / worker fixtures (`examples/memory-tests`) | mandelbrot, soak, graphics, combined, load, debugger, channel workers run and report sane `MEM|` lines; total heap recovers within ~1 KB after KILL |
| TASK + KILL loop (6x array worker) | total heap recovers (~-0.6 KB); largest block drops once by 32,768 B (2,064,372 -> 2,031,604) on the first cycle, then stays constant |
| File `#f AT` seek | LEN=16, `#f AT 10` -> FPOS 10, read "ABCDEF": pass |
| File commands (MKDIR/COPY/MOVE/CAT/DELETE/RMDIR) | pass (earlier note) |
| MIRROR to file, LIST, close removes mirror, to null, DELETE #dest | pass |
| MIRROR limits | self-mirror, 5th mirror ("at most 4"), chain/cycle all rejected with clear errors |
| MIRROR graphics to window | pass, owner confirmed visually |
| YMODEM upload 146 KB | 3.6-5 s, repeated overwrite ok (3/3 earlier) |
| YMODEM on empty FS, 600 KB file | correctly fails ("channel write failed"), no partial file left |
| Bad/nonexistent directory path on FFS | accepted as flat SPIFFS name (expected, no directories) |
| DEBUG n / TRACE ON / STEP / TRACE OFF / KILL | accepted on a looping task; task removed (needs literal ID) |
| TFT window drawing (RECT, CIRCLE, LINE, TEXT) | owner confirmed visually |

## Findings
1. **Disk-full YMODEM can report success with a truncated file** (new bug, not fixed). With ~300 KB free, uploading 600 KB gave
   "Transfer received successfully" and left a 246-252 KB file (reproduced 3x with a 146 KB file already stored). On an empty FS
   the same upload fails correctly and cleans up. Likely a short/partial write not detected by the receive path; probably
   not Tenstar-specific (shared runtime/SPIFFS code). Needs a size check against the declared YMODEM length on close.
2. **`tools/ql32-memory-test.py` fails on Tenstar** at the "recover within 2048 B" check on the largest free block
   (-32,768 B after the first worker; total heap recovers). The CYD passes all 5 cases. It is a one-time PSRAM fragmentation step,
   not a growing leak; the check is a harness limit that should compare total free, or allow one block step, on PSRAM boards.
3. `canvas-resize-workers.bas` fails with "path names a directory: FFS:/arrwrk.bas": a fixture dependency, not investigated.
4. Golden differences are not Tenstar-specific: `print_separators` (serial endpoint separators/columns, same on Olimex);
   `block_if` is interactive-only (same on host in numbered mode).
5. Leftover tasks from the second KILL of an already-reaped id stay in TASKS in WAITING_INPUT until reset.

## N/A
- Touch: board has none (`touch` false in platform capabilities).
- MUSIC/SOUND: no audio hardware.
- SDC: no SD slot.
- MODE 0/1/2: "display mode service unavailable" (TFT driver, no mode service); expected.
- COPY from console/serial input: not supported by the spec (`COPY` takes files or file `#channel` sources); `COPY #0` returned a prompt with no copy.
- Remote debugger GUI (`tools/ql32_remote_debugger`): not run (needs GUI); serial DEBUG/TRACE/STEP checked instead.
