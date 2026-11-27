# RUN-time memory footprint: measurements and optimisation list

Measured on the CYD (ESP32-2432S028, no PSRAM, debug bar on, no FORMAT) with firmware
`a3d0974` (P13; code identical to `main` 36159bc, later commits are docs). Free /
largest block in KB from `HEAPSTATS` and `FREE()`. dirplayer = `examples/music-player/dirplayer.bas`
(387 stored lines, 7,456 B text, 1,163 instructions, no INCLUDE). Editing can use more
memory than running, so the numbers separate the two workflows.

## 1. Correction to PR #51

The scheduler releases the task's source copy right after the first slice
(`src/vm/scheduler.cpp`, `current.releaseSource()` after `executeSlice`). The
"+8 KB while running" gain claimed for Step A is **not real**. Step A only lowers the
compile peak (the joined copy is 9.7 KB, `listingBytes()`). `compile-on-disk-review.md`
is corrected. Rubber-duck cautions adopted: an artifact loader must validate section
lengths and counts before allocating; the artifact is bound to the include contents
(manifest hashes); and Step B saves the compile peak, not resident RAM, unless code is
paged.

## 2. Measured phases (dirplayer)

| Phase | Free | Largest | Delta / note |
| --- | ---: | ---: | --- |
| Idle prompt | 180.7 | 110.6 | |
| LOAD (end) | 164.7 | 110.6 | -16.0 = `ProgramStore` |
| LOAD (transient low) | 142.5 | | file string + candidate growth: -38 peak |
| Compile (lowest point) | 49.8 | | **-113.8 below the loaded state** |
| After compile + first slice (program `STOP`ped before output) | 107.0 | 49.1 | retained code/VM state: **56.6** |
| RUN start in dirplayer (after DIMs) | 96.0 | 47.1 | -11.0 more: arrays/strings of the app |
| After canvas (`TRAP WINDOW #1,0,0,320,160`) | 65.5 | 18.4 | -30.5 (25.6 canvas + ~5 window overhead) |
| Ready screen | 70.7 | 18.4 | |
| Playing (z00m, measurement variant with extra channel #2) | 44.0 | 4.6 | largest only 0.6 above the 4 KiB play admission |
| Exit (console restored) | 138.8 | 55.3 | largest block stays at half of idle after exit |
| `NEW` after a `STOP`ped run | 180.2 | 110.6 | all program memory returned |

Other measurements:

| Scenario | Result |
| --- | --- |
| 12 edits on a loaded dirplayer | free -0.06 KB (dead text is compacted; no growth) |
| `INCLUDE "ql32.inc"` (5.4 KB, 180 `CONST`s) + `STOP` | retained **20.6**, compile low point 38.4 below start; about 114 B per constant |
| `PRINT 1` (any first output) | free 180.1 -> 141.2: the default console canvas (~39) is allocated and **stays after the program ends** |
| `STOP`, `CONTINUE` | STOP retains exactly the running state (56.6); CONTINUE adds nothing |
| EVAL (`PRINT 1+1` at the prompt) | not isolated: the first output allocates the console canvas, which hides it; needs a run with output already open |

## 3. Attribution of the 56.6 KB retained code/VM state

Directly known: instruction deque 1,163 x 32 B = **37.2 KB** (66%). The other **19.4 KB**
is text pool, extras, function/array tables, VM stacks, frames, scheduler task and
interpreter objects (~2.5 KB); the device cannot split these today. The host
harness gave a similar share (instruction store 46.5 of 66.7 KB). Phase 0 of the
compact plan must add per-category counters (an instrumented allocator on the host
and a debug `MEMSTATS` on the device). ProgramStore: 16,383 B
`retainedBytes()` for 387 lines (text 7,456 B + index 387 x 12 B = 12.1 KB needed, **4.3 KB
capacity waste** from doubling growth); the device LOAD delta of 16.0 KB matches.

## 4. Spilling the editable program during RUN

Gain: up to **16.4 KB free** (two blocks: text and index), no change to the largest block
beyond what the freed holes give. A design that never overwrites the user's file:
1. A "clean" flag (set by LOAD/SAVE, cleared by any edit) and a hash of the listing.
2. RUN with a clean program whose source file still hashes the same: drop the RAM copy, no write.
3. Dirty program: write `FFS:/.q32/recover.bas` atomically, read it back and verify the
   hash; **if the write or verify fails, keep the RAM copy and run as today**.
4. Restore on END, error, STOP, `NEW`/`LOAD` and any command that needs the program (`LIST`,
   `EDIT`, `SAVE`, `RUN`); keep the recovery file until the restore verifies. A hard reset
   leaves the file and the boot banner offers it.

Risks: restore OOM when the app leaves the heap fragmented (a 12 KB text block is needed
unless the store is chunked into 1 KB pages first), a wear cost of one ~10 KB write per
RUN of an edited program, debugger/LIST while stopped needing a quick restore, and extra
states to test (reset mid-run, full FFS, SD removed). Do cheaper items first (section 5).

## 5. Prioritised RUN-time optimisation list (dirplayer, CYD)

| # | Item | Free gain | Largest-block gain | Effort / risk |
| ---: | --- | ---: | --- | --- |
| 1 | Compact bytecode (compact plan, 32 -> ~6 B code + ~2 B positions per instruction, pools unchanged) | +25-30 | +10-20 (1 KB segments instead of ~12-16 KB deque growth) | high / medium, gated by the differential harness |
| 2 | Spill the editable `ProgramStore` during RUN (section 4) | +16.4 (+12.1 if the store is first shrunk) | small | medium / medium |
| 3 | Reduce compile transients (peak 113.8 vs retained 56.6): the 57 KB freed after compile leaves the holes that cap the largest block at 47 KB | 0 | +10-25 (estimate) | medium; first profile which structures (declared set, definitions, deque growth, resolution vectors) |
| 4 | `ProgramStore::shrink_to_fit` after LOAD and large edits, and streamed LOAD (no whole-file string) | +4.3 | LOAD transient -8 | low / low |
| 5 | Compact constants table for INCLUDE (`unordered_map<string,Value>` node per constant is ~114 B; a sorted array with pooled names is ~12 B) | +18 for programs including `ql32.inc` (0 for dirplayer) | many small nodes disappear: fewer holes | low-medium / low |
| 6 | Keep the default console canvas lazy or release it when a program installs its own window | +39 for programs that never print | +? | medium (UI behaviour) |
| 7 | Post-exit largest block (55 KB vs 110 KB idle): find the allocation left in the middle (music IO/worker stacks, console restore) | 0 | up to +55 | low-medium, measure first |
| 8 | dirplayer-side: smaller arrays/strings (11 KB retained by the app at start), canvas height | +3-8 | | low |
| 9 | Step A of the compile-on-disk review (no joined copy) | 0 | compile peak -9.7 | low |
| 10 | `.q32b` artifact (Step B) | 0 resident (peak only, -57 on hit) | | after compact bytecode |

Free-heap gain and largest-block gain differ: items 1, 2, 4, 5 raise free memory; items 3,
7 and the segmented store of item 1 raise the contiguous block that music and canvas need.

## 6. Admission estimate

`compileSource` estimates `60 B x operations` = ~70 KB for dirplayer; the measured
compile low point is 113.8 KB below the loaded state (retained 56.6 + 57 transient). The
estimate is calibrated to retained size, not the peak, so a program of about 1,400
instructions passes the check at idle yet needs ~140 KB. Either calibrate to the peak
(about 98 B per operation measured here) or reduce the transients (item 3).

## 7. Not measured / limits

Per-category device attribution (needs Phase 0 instrumentation); EVAL with open output;
song switches in this run (the P13 soak covers them: 9 plays, failedalloc 0, late 0);
INCLUDE nesting deeper than one file; the playing row uses a measurement variant of
dirplayer with an extra channel #2, so its largest block (4.6) is lower than the real app's
(8.2 in P12).
