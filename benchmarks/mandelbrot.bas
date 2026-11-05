inside = 0
FOR py = 0 TO 27
  cy = (14 - py) / 11
  FOR px = 0 TO 39
    cx = (px - 20) / 12 - 0.5
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
    IF iter = 24 THEN
      inside = inside + 1
    END IF
  END FOR px
END FOR py
PRINT inside
