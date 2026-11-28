REM Shared body of the math twins. Needs the caller's OPTION line to differ.
DEFine PROCedure MathDemo(label$)
  LOCal s, i, a, y
  WINDOW 0, 0, TileW(), StageH()
  PAPER 0
  INK 7
  CLS
  GFONT 2, 1
  y = GETFONT(3, 2)
  INK 6
  TEXT 4, 2, label$
  INK 7
  GFONT 1, 0
  s = 0
  FOR i = 1 TO 1000
    s = s + 0.1
  END FOR i
  TEXT 4, 2 + y + 2, "0.1 x 1000 = " + STR$(s)
  TEXT 4, 2 + y * 2, "2^0.5 squared - 2 = " + STR$(2 ^ 0.5 * 2 ^ 0.5 - 2)
  TEXT 4, 2 + y * 3 - 4, "1/3 = " + STR$(1 / 3)
  a = 22
  TEXT 4, 2 + y * 4 - 8, "BAND(22,12)=" + STR$(BAND(a, 12)) + " BOR=" + STR$(BOR(a, 12))
  TEXT 4, 2 + y * 5 - 12, "BXOR=" + STR$(BXOR(a, 12)) + " ROL(22,2)=" + STR$(ROL(a, 2))
END DEFine MathDemo
