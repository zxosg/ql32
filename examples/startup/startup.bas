REM QL/32 boot splash, one source for every board (CYD, Olimex, LILYGO, Tenstar).
REM Layout comes only from run-time queries: window size, font metrics, SYSINFO$.
REM Install: copy to FFS:/startup.bas (Olimex also reads SDC:/startup.bas first).
REM Font handles: 1 small, 2 normal, 3 large. SYSINFO$ 0 version, 2 platform.
GUPDATE 0
PAPER 0
INK 7
CLS
w = GWIDTH()
h = GHEIGHT()
DIM g$(6)
REM 5x7 block letters Q L 3 2, drawn scaled to the window width.
g$(0) = " ### #     ###  ### "
g$(1) = "#   ##    #   ##   #"
g$(2) = "#   ##        #    #"
g$(3) = "#   ##      ##    # "
g$(4) = "# # ##        #  #  "
g$(5) = "#  ###    #   # #   "
g$(6) = " ######### ### #####"
REM Four 5-column letters with a one-column gap: 23 units wide, 7 tall.
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
FOR pass = 0 TO 1
  IF pass = 0 THEN
    INK 2
    ox = k / 2
  ELSE
    INK 6
    ox = 0
  END IF
  oy = ox
  FOR r = 0 TO 6
    FOR c = 0 TO 19
      IF MID$(g$(r), c + 1, 1) = "#" THEN
        x = left + (INT(c / 5) * 6 + c MOD 5) * k + ox
        RECT x, top + r * k + oy, k, k
      END IF
    END FOR c
  END FOR r
END FOR pass
GFONT f, 0
INK 7
y = top + titleH + k * 2
v$ = "Version " + SYSINFO$(0)
m$ = "Free memory " + STR$(INT(FREE(0) / 1024)) + "K"
p$ = SYSINFO$(2)
TEXT INT((w - LEN(v$) * cw) / 2), y, v$
TEXT INT((w - LEN(m$) * cw) / 2), y + lh, m$
TEXT INT((w - LEN(p$) * cw) / 2), y + lh * 2, p$
GUPDATE 1
GUPDATE
REM The finished window stays on screen as a frozen image and is reclaimed
REM automatically when memory is needed. Type NEW to drop this program's source.
