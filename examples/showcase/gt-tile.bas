REM Framework check tile: border, size and free memory.
INCLUDE "gt-layout.bas"
w = TileW()
h = TileH()
WINDOW 0, 0, w, h
PAPER 0
INK 4
CLS
RECT 0, 0, w, h
INK 7
TEXT 4, 4, STR$(w) + "x" + STR$(h)
FOR n = 1 TO 100000
  INK 1 + n MOD 6
  CIRCLE INT(w / 2), INT(h / 2), 4 + n MOD INT(h / 3)
  PAUSE 50
END FOR n
