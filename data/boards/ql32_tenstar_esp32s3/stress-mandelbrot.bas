' Concurrent render workload bounded to the 240x95 area above the console.
WINDOW 0,0,240,95
PAPER 0
INK 7
GUPDATE 1
CLS
PRINT "MANDELBROT"
FOR py = 0 TO 55
  cy = (28 - py) / 21
  shade = -1
  runStart = 0
  FOR px = 0 TO 79
    cx = (px - 40) / 22 - 0.5
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
    pixelShade = 0
    IF iter < 24 THEN
      pixelShade = iter MOD 7 + 1
    END IF
    IF px = 0 THEN
      shade = pixelShade
      runStart = 0
    ELSE
      IF pixelShade <> shade THEN
        INK shade
        LINE runStart,py + 8,px - 1,py + 8
        shade = pixelShade
        runStart = px
      END IF
    END IF
  END FOR px
  INK shade
  LINE runStart,py + 8,79,py + 8
END FOR py
GUPDATE
PRINT #0,"Mandelbrot task complete"
