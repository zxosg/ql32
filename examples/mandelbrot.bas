REM Small 120x84 Mandelbrot, drawn as horizontal color runs for CYD.
DIM row%(119)
WINDOW 0,0,240,176
PAPER 0
INK 7
CLS
PRINT "MANDELBROT 120x84"
FOR py = 0 TO 83
  cy = (42 - py) / 32
  FOR px = 0 TO 119
    cx = (px - 60) / 33 - 0.5
    zx = 0
    zy = 0
    iter = 0
    FOR it = 1 TO 24
      IF zx * zx + zy * zy <= 4 THEN
        temp = zx * zx - zy * zy + cx
        zy = 2 * zx * zy + cy
        zx = temp
        iter = iter + 1
      END IF
    END FOR it
    shade = 0
    IF iter < 24 THEN
      shade = iter MOD 7 + 1
    END IF
    row%(px) = shade
  END FOR px
  x = 0
  REPEAT runs
    IF x >= 120 THEN
      EXIT runs
    END IF
    shade = row%(x)
    runStart = x
    REPEAT extend
      x = x + 1
      IF x >= 120 THEN
        EXIT extend
      END IF
      IF row%(x) <> shade THEN
        EXIT extend
      END IF
    END REPEAT extend
    INK shade
    LINE runStart * 2, py * 2 + 8, x * 2 - 1, py * 2 + 8
    LINE runStart * 2, py * 2 + 9, x * 2 - 1, py * 2 + 9
  END REPEAT runs
END FOR py
PRINT #0, "Mandelbrot complete"
