# dirplayer.bas: workarounds and language/runtime gaps

Format: wanted / limitation / did instead / suggested improvement.

## Blocker: app does not run on the CYD (esp32_2432s028_spiffs, main 5152917)
- Wanted: a ~300-line BASIC app (620 instructions, 4 KB source).
- Limitation: `RUN` fails with `ALLOCFAIL ... out of memory`. The lexer holds
  every token of the whole source in one `std::vector<Token>`; a Token is
  ~112 B on ESP32 and the vector doubles. `LOAD` also keeps ~75 KB.
  Measured: programs above ~170-330 instructions (~300 tokens) fail even with
  142 KB free at idle. Deque storage (tried and reverted) is not enough:
  the heap is then exhausted (free=168 B), i.e. total token memory is the limit.
- Did instead: shrank the app 1460 -> 978 -> 821 -> 694 -> 620 instructions
  (dropped folder navigation, drive key, Home/End, scroll bar, playing marker,
  PgUp/PgDn, unsorted fallback). Still too big. Verified on host only.
- Suggest: streaming lexer/parser (no whole-file token list), compact Token
  (no std::string, interned text), free source/tokens after compile, and
  `LOAD` not retaining a second copy.

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
6. `INKEY$` special-key names are not on main (PR #28): numeric INKEY codes.
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
