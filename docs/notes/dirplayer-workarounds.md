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
