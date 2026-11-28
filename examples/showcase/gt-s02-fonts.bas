REM Scene 2: the four built-in fonts and a GCOPY pixel scroller.
INCLUDE "gt-layout.bas"
w = DispW()
h = StageH()
WINDOW 0, 0, w, h
GUPDATE 0
PAPER 0
INK 7
CLS
y = 2
FOR f = 4 TO 1 STEP -1
  GFONT f, 0
  INK 1 + f
  TEXT 4, y, "Font " + STR$(f) + ": QL/32"
  y = y + GETFONT(3, f) + 2
END FOR f
GFONT 3, 1
cw = GETFONT(6, 3)
lh = GETFONT(3, 3)
sy = h - lh - 2
msg$ = "   *** QL/32 SCROLLS PIXELS WITH GCOPY ***   "
GUPDATE 1
GUPDATE
INK 6
n = 0
FOR frame = 1 TO 100000
  GCOPY 2, sy, w - 2, lh, 0, sy, 0, 1
  IF frame MOD INT(cw / 2) = 0 THEN
    n = n + 1
    TEXT w - cw, sy, MID$(msg$, n MOD LEN(msg$) + 1, 1)
  END IF
  PAUSE 20
END FOR frame
