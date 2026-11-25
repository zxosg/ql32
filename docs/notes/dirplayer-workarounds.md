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
1. `INSTR` missing (spec text mentions it): looped over characters instead.
2. `MIN`/`MAX` missing: explicit `IF`. Suggest adding.
3. Reserved words as names (`sel`, `mode`, `step`, `Select`, `Stop`, `Pause`,
   `Play`) give cryptic errors: renamed. Suggest listing them in the spec and
   a clearer error.
4. `TEXT (expr), y, t$` rejected (command args without parentheses): compute
   into a variable first.
5. `INCLUDE "ql32.inc"` needs the file on FFS: and costs compile RAM: numeric
   literals for key codes and options. Suggest built-in named constants.
6. (Resolved) `INKEY$` names now used.
7. No playback state/volume/metadata readback, no end-of-song event:
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
13. `SELect ON` is numeric only: key dispatch is a long IF chain (costs
    tokens). Single-line `IF c THEN stmt` unsupported. Suggest string SELect.
14. Number-to-string concatenation needs `STR$()`. Suggest implicit.
15. No clock: "Time" counts `INKEY$(250)` timeouts, approximate. Suggest
    `TICKS()`/`TIMER`.
