REM Block-letter title shared by the boot and finale screens .
DEFine PROCedure Title(w, h, l1$, l2$, l3$)
  LOCal rows$, k, f, cw, lh, titleH, blockH, top, left, pass, ox, oy, r, c, x, y
  rows$ = " ### #     ###  ### " + "#   ##    #   ##   #" + "#   ##        #    #" + "#   ##      ##    # "
  rows$ = rows$ + "# # ##        #  #  " + "#  ###    #   # #   " + " ######### ### #####"
  k = INT(w * 0.55 / 23)
  IF k < 1 THEN k = 1
  IF k > 8 THEN k = 8
  f = 1
  IF w >= 400 THEN f = 2
  IF w >= 640 THEN f = 3
  cw = GETFONT(6, f)
  lh = GETFONT(3, f) + 2
  titleH = 7 * k
  blockH = titleH + k * 2 + 3 * lh
  top = INT((h - blockH) / 2)
  IF top < 0 THEN top = 0
  left = INT((w - 23 * k) / 2)
  GUPDATE 0
  PAPER 0
  INK 7
  CLS
  FOR pass = 0 TO 1
    IF pass = 0 THEN
      INK 2
      ox = INT(k / 2)
    ELSE
      INK 6
      ox = 0
    END IF
    oy = ox
    FOR r = 0 TO 6
      FOR c = 0 TO 19
        IF MID$(rows$, r * 20 + c + 1, 1) = "#" THEN
          x = left + (INT(c / 5) * 6 + c MOD 5) * k + ox
          RECT x, top + r * k + oy, k, k
        END IF
      END FOR c
    END FOR r
  END FOR pass
  GFONT f, 0
  INK 7
  y = top + titleH + k * 2
  TEXT INT((w - LEN(l1$) * cw) / 2), y, l1$
  TEXT INT((w - LEN(l2$) * cw) / 2), y + lh, l2$
  TEXT INT((w - LEN(l3$) * cw) / 2), y + lh * 2, l3$
  GUPDATE 1
  GUPDATE
END DEFine Title
