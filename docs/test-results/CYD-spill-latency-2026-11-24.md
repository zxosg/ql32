# CYD program-spill latency and flash wear

Board: ESP32-2432S028 (no PSRAM), `esp32_2432s028_spiffs`, compact bytecode on.
Measured with a `-DQL32_SPILL_TIMING` build (timings printed to the UART with `esp_rom_printf`; the instrumentation is compiled out otherwise).
Programs are long-running (end with `PAUSE 4000`) because a spill only happens while a program keeps running after compile.

## Results

| Program (listing text) | FFS: write+verify (RUN start delay) | FFS: restore | SDC: write+verify | SDC: restore |
|---|---|---|---|---|
| 3 lines (69 B) | 1.5-1.65 s | 80-120 ms | 0.38 s | 29 ms |
| 60 lines (859 B) | 1.75 s | 82 ms | 0.40 s | 32 ms |
| 250 lines (3.9 KB) | 2.0 s | 86 ms | 0.52 s | 41 ms |
| dirplayer.bas (8.7 KB) | 2.4 s | not measured separately | 0.47 s | - |

* The delay is dominated by SPIFFS file creation (temporary file, rename, sidecar), not by data volume. RUN start is delayed by exactly this time because the spill runs before execution starts.
* Restore happens on the first command that needs the listing after RUN (LIST, EDIT, SAVE, a numbered-line edit); later commands are free. It is a streaming, hash-verified read.
* Adopting an identical leftover recovery file (same program re-run after a reset) skips the write: 0.6 s for dirplayer, which is the hash pass only.

## Flash wear per RUN (FFS:)

One RUN that spills performs: 1 temporary-file create + rename (listing), 1 sidecar create, then 2 deletes when the program ends or is restored. Payload is the listing text plus a ~80 B sidecar, i.e. 1-3 SPIFFS pages, with GC amplification roughly 2-3x. Order of magnitude: about 6-12 sector erases per RUN. A 475 KB SPIFFS volume has ~115 sectors with wear levelling and ~100 K erase cycles each, so about 10^6 RUNs of budget; 100 spilled RUNs per day would take decades. Wear is not a concern; latency is.

## Findings and fixes

1. **Bug (fixed):** an orphan `*.meta` sidecar (no recovery file) made the slot look occupied. After 16 orphans every RUN probed all 16 slots (~3.2 s, two times per RUN) and then silently did not spill, so large programs hit ALLOCFAIL again. Orphans are now replaced, and the discard order is sidecar first so an interrupted discard leaves a LOAD-able recovery file instead of an orphan. Test: `testSpillIgnoresOrphanSidecars`.
2. **Mitigation (implemented):** `AdmissionPolicy::programSpillMinBytes` (CYD default 4096, override `-DQL32_PROGRAM_SPILL_MIN_BYTES=n`). Programs with less listing text stay in RAM, so small programs start instantly; the saving for them is only a few KB. Test: `testSpillSkipsProgramsBelowMinimumSize`.
3. **Option:** `-DQL32_PROGRAM_SPILL_PATH=\"SDC:.q32-run\"` spills to the SD card: ~4-5x faster RUN start (0.4-0.5 s) and restore, and no SPIFFS wear. Default stays `FFS:` because it needs no card.

Remaining cost for programs >= 4 KB on FFS: 2-2.4 s added to RUN start. It is the price of the memory headroom that lets dirplayer-size programs run at all.

## Update: per-RUN volume choice (SDC: preferred, FFS: fallback)

`AdmissionPolicy::programSpillPreferredPath` (CYD: `SDC:.q32-run`) is tried first; any failure (no card, write-protected, full, verify error) cleans up and falls back to `programSpillPath` (`FFS:.q32-run`). Adoption scans both volumes; the boot message lists recovery files on both. A card removed before restore is reported as a missing recovery file (the file stays on the card; reinsert and `LOAD "SDC:.q32-run-N"`).

Measured on the CYD (3 GB card), dirplayer.bas (8.7 KB), RUN-start delay: **0.59 s with card** (was 2.4 s on FFS:), restore 78 ms. Two pitfalls found while measuring, both avoided: `SD.usedBytes()` walks the FAT and costs about 1.95 s, so the card's free space is not queried (a full card fails the write and falls back); and each probe of a missing SPIFFS slot costs ~200 ms, so the adoption scan stops at the first missing slot. Not tested on hardware: physically removing or write-protecting the card (covered by host fakes).
