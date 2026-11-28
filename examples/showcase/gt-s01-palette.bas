REM Scene 1: palette cycling. Concentric squares use indices 1-15; PALROT/PALFADE animate them.
INCLUDE "gt-layout.bas"
w = DispW()
h = StageH()
WINDOW 0, 0, w, h
GUPDATE 0
PAPER 0
CLS
FOR i = 0 TO 14
  INK 1 + i
  RECT INT(i * w / 32), INT(i * h / 32), w - INT(i * w / 16), h - INT(i * h / 16)
END FOR i
GUPDATE 1
GUPDATE
FOR n = 1 TO 100000
  PALROT 1, 15, 1
  IF n MOD 120 = 0 THEN PALFADE 1, 15, 0, 0.3
  PAUSE 40
END FOR n
