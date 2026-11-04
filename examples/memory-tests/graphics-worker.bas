REM Allocate and draw into a worker-owned retained graphics canvas.
serial_probe = OPEN(2)
WINDOW 0,0,160,120
PAPER 0
INK 7
CLS
RECT 0,0,159,119
PLOT 80,60
PRINT #serial_probe, "MEM|graphics|active|"; FREE(0); ","; FREE(1); ","; FREE(2)
CLOSEWINDOW
PRINT #serial_probe, "MEM|graphics|released|"; FREE(0); ","; FREE(1); ","; FREE(2)
REPEAT keepAlive
  n = n + 1
END REPEAT keepAlive
