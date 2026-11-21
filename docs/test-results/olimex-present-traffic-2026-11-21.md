# Olimex VGA present PSRAM traffic, 2026-11-21

Standard placement (ISR + refill on core 1), 640x480, attractor, 30 s, coalescing off (`PRESRATE 1 0`).

## Changes
- `writeMappedRowChunked` (framebuffer.hpp): packs pairs into a 128-byte internal chunk and `memcpy`s to PSRAM. Runtime A/B with serial `PRESWRITE 0|1` (diagnostic `QL32_OLIMEX_PRESENT_PROFILE` builds). Default is chunked.
- `VgaConsole::composePaletteRegionRow`: per-cell glyph lookup instead of per-pixel division and glyph lookup (same output).

## Built / host-tested / hardware
- Built: `..._640x480_present_coalesce_test`, `..._present_profile`.
- Host-tested: new `testChunkedMappedRowMatchesPerByte` (odd/even x and count, chunk boundaries) passes; CTest 80/83, only the 3 known failures (scheduler, channel, hardware). The console rewrite has no host test (lives in the board main).
- Hardware (640x480 only; 640x400 not re-measured):

| Variant | requests | presenting s | provider s | write s | late | refill us/row |
|---|---|---|---|---|---|---|
| per-byte write (x2) | 102 | 11.91 / 11.86 | 6.76 / 6.68 | 3.44 / 3.49 | 0 | 28.1 |
| chunked write (x2) | 102 | 12.20 / 12.15 | 6.74 / 6.67 | 3.73 / 3.77 | 0 | 28.1 |
| chunked + cheaper console | 102 | 11.63 / 11.71 | 6.22 / 6.25 | 3.71 / 3.70 | 0 | 28.1 |

Full present remains ~0.50 s. Final state: standard `present_profile` reflashed, picture confirmed stable by owner.

## Findings
- The write cost is not per-byte `memw` stores: chunked bulk stores are ~8% slower (probably contention with the refill worker's PSRAM reads, not instruction count).
- Console compose rewrite saves ~0.5 s of 30 s (provider -7%); the rest of composition (window loop, canvas PSRAM reads, std::function per row) dominates.
- VM requests stay ~102/30 s: the VM is CPU/PSRAM-starved by the refill worker (~80% of core 1), so cheaper presenting gives only a few percent.

## Next
Remaining levers are shared-PSRAM contention (refill at 28 us/row) and composition's PSRAM canvas reads. Suggest an `ENCBENCH`-style compose microbenchmark, or caching composed rows for unchanged windows, before further changes. Chunked write is kept as default but gives no gain; revert if preferred.
