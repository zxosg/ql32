# dirplayer.bas: workarounds and language/runtime gaps

Format: wanted / limitation / did instead / suggested improvement.

## Memory (CYD, esp32_2432s028_spiffs, main e7fbb9b)
- Wanted: full app (scroll bar, Home/End, filter/sort toggles, playing
  marker, IsSong function, ESC): 2787 tokens / 1255 instructions.
- Limitation: compile admission needs `tokens * 56 B` free; idle has ~120 KB,
  so max ~2150 tokens ("not enough memory for program of 2787 tokens").
  Lowering the estimate to 8 B crashes (ALLOCFAIL), so it is real.
- Did instead: cut those features (~25%). Now fits and runs.
- Suggest: compact tokens / streaming parser so 56 B/token drops sharply.
- Sorted `OPEN` needs a 14 KB block; fails during playback: unsorted
  fallback (mode 1) added.
- Per-song test from the app on a fresh board (7 songs on FFS:): 5 play;
  knuckle.ym fails (ALLOCFAIL 1752 B, largest 1588, reported "Invalid path")
  and z00m.vtx fails (ALLOCFAIL 8192 B, reported "Input/output error").
  From the bare REPL both play (largest block 94196, failedalloc=0).
  So the loaded app fragments the heap. Suggest correct error text for
  allocation failures and a pre-reserved decoder buffer.
- Heap (FREE()/largest): idle in app 58/25.6 KB; playing 26/5-6 KB; after
  MUSIC STOP 41.5/15.9 KB. HEAP STATS after quit: free=110764 largest=21492
  min=18028 failedalloc=5 lastfail=8192.
- `HEAP STATS` is REPL-only; while an app runs, serial keys go to the app.

## Language gaps and workarounds
1. (Resolved: `INSTR(h$, n$[, start])`) `INSTR` missing (spec text mentions it): looped over characters instead.
2. (Resolved: `MIN`, `MAX`, `CLAMP`) `MIN`/`MAX` missing: explicit `IF`. Suggest adding.
3. Reserved words as names (`sel`, `mode`, `step`, `Select`, `Stop`, `Pause`,
   `Play`) give cryptic errors: renamed. Suggest listing them in the spec and
   a clearer error.
4. `TEXT (expr), y, t$` rejected (command args without parentheses): compute
   into a variable first.
5. (Resolved for key constants: `KKey*` built in) `INCLUDE "ql32.inc"` needs the file on FFS: and costs compile RAM: numeric
   literals for key codes and options. Suggest built-in named constants.
6. (Resolved) `INKEY$` names now used.
7. (Resolved: `MUSIC STATE()/POS()/LEN()/VOLUME()/TITLE$()`; `STATE()=0` marks song end) No playback state/volume/metadata readback, no end-of-song event:
   app tracks state itself, no auto-advance, title = file name. Suggest
   `MUSIC STATE()`, `VOLUME()`, `MUSIC POS()`/finished flag.
8. Directory pattern allows one wildcard: extension filter done in BASIC.
   Suggest multi-extension patterns (`*.{ym,pt3}`).
9. Functions cannot be called as statements and procedures return nothing:
   PROCedures plus globals.
10. No text metrics: assumes the 6x8 font. `TEXT` draws glyph pixels only, so
    each row is cleared with `RECT` first. Suggest opaque text / `TEXTBG`.
11. Directory channels: 2 per session; leaked handles give error 18 until
    reset. A sorted open costs ~8.7 KB temporary heap; 256 entries/14 KiB cap.
12. CYD: `TASK "FFS:/x.bas"` fails (114 KB graphics canvas); serial `PRINT`
    goes to the screen, use `OPEN(2)`. Host: no audio, `MUSIC PLAY`/`VOLUME`
    raise "Service unavailable" (trapped).
13. (Resolved: string `SELect ON` with `=`, `TO`, wildcards) `SELect ON` was numeric only: key dispatch is a long IF chain (costs
    tokens). (Resolved: single-line `IF c THEN stmt [ELSE stmt]`.) Suggest string SELect.
14. Number-to-string concatenation needs `STR$()`. Suggest implicit.
15. (Resolved: `TICKS()`/`TIMER()`) No clock: "Time" counts `INKEY$(250)` timeouts, approximate. Suggest
    `TICKS()`/`TIMER`.

## P5 memory fixes (stacked on the full dirplayer)

- RUN admission now estimates `60 B x bytecode-producing tokens` (separators and
  brackets excluded) instead of `56 B x all tokens`; the full 2787-token app
  (~1255 instr) is no longer refused at idle.
- The scheduler frees its program-source copy once the program is compiled.
- The LH5 dictionary (YM files) is 16 x 512 B blocks instead of one 8 KiB block.
- `bad_alloc` while opening a song reports `music: not enough memory (heap
  fragmented)` (OutOfMemory) instead of "Invalid path" / "I/O error".
- CYD (idle 144.8 KB free / 94.2 KB largest): app loaded 129.3 KB / 81.9 KB;
  KNUCKLE.YM (correction: this was YMT1, which then failed with "unsupported YM
  variant"; YMT1 support added later, device re-verification pending) and z00m.vtx both play from the app with failedalloc=0
  (min free 18-22 KB while playing; after quit 111 KB / 59.4 KB largest).
- Cut features were restored in P6.

## P6 (sorted directory, restored features)

- Sorted directory listings keep records (8 per page) and names (512 B pages) in
  small pages instead of one 14 KiB block; the 14 KiB total budget and 256-entry
  limit are unchanged. Larger directories are refused by the budget error (the
  app then lists unsorted), never by an allocation failure. Host test: no
  allocation above 1 KiB, 150 entries sorted, 1000 entries bounded.
- `sourceKey` (VTX/YM cache key) hashes in 256 B chunks instead of a 4 KiB block.
- dirplayer restored: Home/End, scroll bar, playing marker (`>`), O (toggle
  sorted/unsorted), Esc quits.

## P7 (static I/O task stack, large directory)

- `ql32_music_io` now uses a 4 KiB static stack (.bss), so it exists regardless of
  heap fragmentation. The earlier "ALLOCFAIL 4096 = I/O task stack" attribution was
  wrong: the ALLOCFAIL 4096-4218 lines seen while playing with the app loaded
  come from a different, handled allocation (heap largest block is ~3 KB during
  playback); playback and `O` toggling continue normally.
- Host test: a storage `bad_alloc` while building the cache maps to
  `ServiceError(OutOfMemory)`.
- CYD, 300 files on FFS: the sorted open is refused by the 256-entry budget, the
  app falls back to unsorted; z00m.vtx plays; HEAP before/after unchanged
  (idle 140.2 KB / 94.2 KB, app loaded 124.8 / 81.9 KB, after quit 111.2 / 69.6 KB).

## P8 (ALLOCFAIL 4096 call site)

- Debug aid: build with `-DQL32_ALLOC_TRACE` (e.g. `PLATFORMIO_BUILD_FLAGS`) to print the
  task name and a backtrace on every failed allocation; decode with `addr2line`.
- The 4 KiB failure is Arduino's `VFSFileImpl` calling `setvbuf` (4096 B stdio buffer,
  caps 0x1800, task loopTask) while opening a file during play start; at that moment
  the largest 8-bit block is ~3.3 KB (36 KB free). Two sites were found: `exists()`
  (fixed: `SdStorage::exists` now uses `stat()` on the mount point, no FILE*) and the
  open of the VTX/YM cache in `openCache`/`openYmCache`. The latter is a handled
  fallback (cache treated as a miss, song is re-transcoded and plays; late=0, no
  audible effect). Removing it needs the stdio buffer to be allocated before the
  play-start buffers (open cache first); a pool cannot be used because the buffer is
  allocated inside newlib.
- Admission: host test `testAdmissionTracksInstructionCountAcrossProgramShapes`
  measures the refusal threshold for numeric/string/function/IF programs:
  52-70 B per instruction (device peak is ~58 B), so the 60 B/operation estimate
  is slightly generous for call-heavy code and ~10% optimistic for string-heavy code.
- CYD sustained check (z00m and KNUCKLE, 3 cold boots, play/switch/up/down cycles):
  late=0, maxGap ~11.6 ms, music stack high-water 1.8 KB free, iostack ~2.3 KB free,
  heap recovers to 110.8 KB / 69.6 KB largest after quit.

## P9 (CYD device test of the full player)

- The CYD text console is a fixed 64 px opaque overlay (y 176..240); the
  status area is placed at `GH - 64` and the console is moved with
  `WINDOW #0` while the player runs.
- Fixed a one-row scroll redraw glitch in `MoveTo` (two highlighted rows).
- Open: foreground run leaves FREE ~44K / largest 6K (canvas + stored
  source fragment the heap), so music start (32K total, 8K contiguous)
  fails with "Out of memory". Under `WINDOWMODE SHARED` + `TASK ...,
  KTaskWait` the heap is 59K / 45K, but serial keys do not reach the task.
  The earlier "plays from the app at 124.8K" claim was not reproducible.

## P10: why dirplayer cannot play in the foreground (CYD)

Measured on the CYD (no PSRAM), dirplayer.bas (1144 instructions):

| Point | Free | Largest block |
| --- | --- | --- |
| Idle | 180.5K | 110.6K |
| After LOAD | 164.7K | 110.6K |
| After RUN starts (before canvas) | 82.9K | 38.9K |
| After the 38.4K canvas | ~43K | 4-7K |

- RUN retains about 83K (compiled bytecode, pool, extras), and the freed
  compile transients leave ~43K of holes that are each under ~5K. The canvas
  then takes the only large block.
- Playback needs contiguous blocks (6 KiB music worker stack, 4 KiB VFS file
  buffers, 2 KiB LH5 tables, 8 KiB admission). With the app loaded the largest
  block is 2-4.6K, so play fails with "Out of memory" for every song type.
- Shrinking the contiguous admission to 2K only moves the failure to the 6144 B
  worker stack; a static .bss stack lets the app start but the VFS 4096 B
  `setvbuf` buffer and LH5 vectors then fail. These were not shipped.
- Shipped: calls to one procedure now share a single immutable parameter and
  local-name list and `ownerDefinition` is a pooled `SharedText` (about 1.3K
  saved on the device at program start, ~3K on host).
- Real fix needs a bounded pre-reserved music arena or smaller retained code
  (Call extras side table, 24 B instructions), or a smaller dirplayer canvas.

## P11: foreground playback (smaller canvas, chunked drums)

Root cause of the foreground playback OOM: the default channel #1 canvas is
full-screen (38.4 KB) and, with the retained program (~83 KB), left ~44 KB free
but only a 4–7 KB largest block, below the 8 KB contiguous music-start admission.

Changes:
- `dirplayer.bas` opens `TRAP WINDOW #1, 0, 0, 320, 160` before any output, so the
  canvas is 25.6 KB. The list shows 8 rows (was 14); the status block is unchanged.
  Idle debug bar: FREE 64K / MAX 20K (was 44K / 7K).
- YM digi-drums are stored in 128-sample blocks (`DrumSamples`) instead of one
  vector, and the device keeps at most 4096 drum samples (larger drums, e.g.
  KNUCKLE.YM's 6810-sample drum, play silently); no 13 KB contiguous allocations.

Measured on the CYD (debug bar ON, dirplayer in the foreground): KNUCKLE.YM,
z00m.vtx and LyraII1.vtx play with late=0 and no ALLOCFAIL during playback;
playing heap about FREE 38K / MAX 8K; auto-advance on POS wrap (c12→c14→c17),
the `>` playing marker and MUSIC readback (title, time/length) work. The only
failed allocation left is the 10 KB console-window restore when the app exits.

Still open: a compact (OPL-style) bytecode would cut the ~83 KB retained program
to roughly a third and is the next lever if more features are restored.

## P12: compile-only metadata released

`ownerDefinition`, `jumpScopes` and `arrayArgumentCandidates` are read only
while compiling; they are now dropped after resolution and the per-instruction
extras record is freed when nothing the VM needs remains. `emitCall` no longer
allocates an extras record for calls without candidates or owner.
`dirplayer.bas` also shrinks the canvas before restoring the console window at
exit (removes the 10 KB ALLOCFAIL).

CYD, dirplayer, `FREE()` (free, largest) by phase, before -> after:
- idle prompt 180.7K/110.6K (unchanged); after LOAD 164.7K/110.6K (unchanged)
- RUN start 88.9K/45.0K -> 96.7K/47.1K (+7.8K)
- after canvas 58.3K/19.4K -> 66.2K/22.5K
- playing z00m 36.8K/5.9K -> 44.6K/8.2K (largest +2.3K, now above the 8K admission)
- exit: failedalloc 1 -> 0

## P13: music diagnostics and cache policy

- Whole-run audio counters (`musicBlocks`, `musicLateBlocks`, `musicWriteErrors`, `musicMaxGapUs`) live in the ESP32 diagnostics. They appear in HEAPSTATS (`musicblocks|musiclate|musicwriteerr|musicmaxgap`), as ` LATE:n` on the CYD debug bar once non-zero, and as a serial `music: total ...` line when they grow after the first 10 s.
- The YM cache key now includes the drum sample budget, so caches written under another policy are rebuilt. Cache header byte 5 bit 2 (value 4) marks drums dropped; the `MUSIC TITLE$` readback appends ` [no drums]`.
- Song loads while audio runs now also require a 4 KiB contiguous block (`musicPlayContiguousBytes`).
- Host soak: repeated switching between two YM songs (cache miss then hits) and rebuild on a stale-key cache.
- Hardware gate (golden 16/16, CYD soak) is still pending for this change.
