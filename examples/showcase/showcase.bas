REM QL/32: The Grand Tour - director. Keys: SPACE/N next, P previous, Q quit.
REM Test mode: autoSecs > 0 advances scenes hands-free; holdScene = N stops with scene N on screen.
REM Upload every gt-*.bas file next to this one; run with: LOAD "FFS:/showcase.bas" then RUN.
INCLUDE "gt-layout.bas"
INCLUDE "gt-title.bas"
DIM t(5)
w = GWIDTH()
h = GHEIGHT()
scenes = 6
autoSecs = 0
holdScene = -1
ticks = 0
scene = 0
REPeat tour
  IF scene = 0 THEN
    Title w, h, "THE GRAND TOUR", "v" + SYSINFO$(0), "space = next, Q = quit"
  END IF
  IF scene >= 1 AND scene <= 4 THEN
    PAPER 0
    CLS
    sh = StageH()
    IF sh > h - 14 THEN sh = h - 14
    IF scene = 1 THEN
      t(0) = TASK("FFS:/gt-s01-palette.bas")
      cap$ = "PALROT / PALFADE: 16 colours"
    END IF
    IF scene = 2 THEN
      t(0) = TASK("FFS:/gt-s02-fonts.bas")
      cap$ = "4 fonts + GCOPY scroller"
    END IF
    IF scene = 3 THEN
      t(0) = TASK("FFS:/gt-s03-double.bas")
      t(1) = TASK("FFS:/gt-s03-single.bas")
      cap$ = "OPTION math: 64 vs 32 bit"
    END IF
    IF scene = 4 THEN
      t(0) = TASK("FFS:/gt-s06-parallax.bas")
      cap$ = "GCOPY parallax, no sprites"
    END IF
    GFONT 1, 0
    INK 7
    TEXT 4, sh + 2, cap$
    GUPDATE
    IF scene = 3 THEN
      TASKVIEW t(0), 0, 0, TileW(), sh
      TASKVIEW t(1), TileW(), 0, TileW(), sh
    ELSE
      TASKVIEW t(0), 0, 0, w, sh
    END IF
  END IF
  IF scene = 5 THEN
    Title w, h, "THE END", "free " + STR$(FREE(0)) + " largest " + STR$(FREE(1)), SYSINFO$(2)
  END IF
  IF scene = holdScene THEN
    PAUSE 6000
    STOP
  END IF
  ticks = 0
  REPeat wait
    k = 0
    IF autoSecs > 0 THEN
      PAUSE 200
    ELSE
      k = INKEY(200)
    END IF
    ticks = ticks + 1
    IF autoSecs > 0 AND ticks >= autoSecs * 5 THEN k = 32
    IF k = 32 OR k = 110 OR k = 78 THEN
      scene = scene + 1
      EXIT wait
    END IF
    IF k = 112 OR k = 80 THEN
      scene = scene - 1
      EXIT wait
    END IF
    IF k = 113 OR k = 81 THEN
      scene = scenes
      EXIT wait
    END IF
  END REPeat wait
  FOR i = 0 TO 5
    IF t(i) > 0 THEN KILL t(i)
    t(i) = 0
  END FOR i
  IF scene < 0 THEN scene = 0
  IF scene >= scenes THEN EXIT tour
END REPeat tour
CLS
PRINT "Grand Tour finished"
