' CYD touch-channel demo. LOAD this file and enter RUN.
touch = OPEN(13)
win = OPEN(4)
WINDOW #win, 0, 0, 320, 240
INK #win, 2
RECT #win, 8, 8, 304, 224
AT #win, 1, 1
PRINT #win, "Touch inside the frame to test coordinates."
AT #win, 3, 1
PRINT #win, "Waiting for touch..."

REPEAT forever
  INPUT #touch, type%, x%, y%, hit%
  AT #win, 3, 1
  PRINT #win, "TYPE "; type%; " X "; x%; " Y "; y%; " CH "; hit%
  IF type% = 1 THEN
    PLOT #hit%, x%, y%
  END IF
END REPEAT forever
