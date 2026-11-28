REM QL/32: The Grand Tour - director. Keys: SPACE/N next, P previous, Q quit.
REM Test mode: autoSecs > 0 advances scenes hands-free; holdScene = N stops with scene N on screen.
REM Upload every gt-*.bas file next to this one; run with: LOAD "FFS:/showcase.bas" then RUN.
INCLUDE "gt-layout.bas"
INCLUDE "gt-title.bas"
DIM t(5)
w = GWIDTH()
h = GHEIGHT()
scenes = 3
autoSecs = 0
holdScene = -1
ticks = 0
scene = 0
REPeat tour
  IF scene = 0 THEN
    Title w, h, "THE GRAND TOUR", "v" + SYSINFO$(0), "space = next, Q = quit"
  END IF
  IF scene = 1 THEN
    GUPDATE 1
    PAPER 0
    CLS
    tw = TileW()
    th = TileH()
    IF th * 2 > h THEN th = INT(h / 2)
    n = Cols() * 2
    FOR i = 0 TO n - 1
      t(i) = TASK("FFS:/gt-tile.bas")
    END FOR i
    FOR i = 0 TO n - 1
      TASKVIEW t(i), (i MOD Cols()) * tw, INT(i / Cols()) * th, tw, th
    END FOR i
  END IF
  IF scene = 2 THEN
    Title w, h, "THE END", "free " + STR$(FREE(0)) + " largest " + STR$(FREE(1)), SYSINFO$(2)
  END IF
  IF scene = holdScene THEN STOP
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
