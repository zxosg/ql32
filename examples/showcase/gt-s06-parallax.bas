REM Scene 6: three-layer parallax built from procedural columns, scrolled with GCOPY.
INCLUDE "gt-layout.bas"
w = DispW()
h = StageH()
WINDOW 0, 0, w, h
GUPDATE 0
PAPER 0
CLS
bh = INT(h / 3)
t = 0
FOR frame = 1 TO 100000
  t = t + 1
  GCOPY 1, 0, w - 1, bh, 0, 0, 0, 1
  GCOPY 2, bh, w - 2, bh, 0, bh, 0, 1
  GCOPY 4, bh * 2, w - 4, h - bh * 2, 0, bh * 2, 0, 1
  INK 0
  LINE w - 1, 0, w - 1, bh - 1
  IF INT(RND(7)) = 0 THEN
    INK 7
    PLOT w - 1, INT(RND(bh))
  END IF
  FOR k = 0 TO 1
    INK 0
    LINE w - 2 + k, bh, w - 2 + k, bh * 2 - 1
    hill = 6 + INT((SIN((t * 2 + k) / 17) + 1) * (bh - 12) / 2)
    INK 2
    LINE w - 2 + k, bh * 2 - hill, w - 2 + k, bh * 2 - 1
  END FOR k
  FOR k = 0 TO 3
    INK 6
    IF INT((t * 4 + k) / 12) MOD 2 = 0 THEN INK 5
    LINE w - 4 + k, bh * 2, w - 4 + k, h - 1
  END FOR k
  GUPDATE
  PAUSE 1
END FOR frame
