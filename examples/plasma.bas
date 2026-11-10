WINDOW 0,0,160,120
PAPER 0
CLS
GUPDATE 0

w = 160
h = 120
r = 1
u = 256
rr = 64
colors = GPALETTESIZE()

FOR y = 0 TO h-1
  FOR x = 0 TO w-1
    rr = -rr
    xr = x+r
    c = SIN(xr/28)*64
    c = c-SIN((xr+(u MOD 284))/45)*128
    c = c+SIN((y+rr)/16)*64
    c = INT(c) MOD colors
    IF c < 0 THEN c = c+colors
    INK c
    PLOT x,y
  END FOR x
END FOR y

GUPDATE 1
GUPDATE