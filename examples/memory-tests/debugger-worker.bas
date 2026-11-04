REM Keep a running task available for DEBUG attach, inspect, step, and continue.
serial_probe = OPEN(2)
DIM values(255)
n = 0
PRINT #serial_probe, "MEM|debugger|active|"; FREE(0); ","; FREE(1); ","; FREE(2)
REPEAT keepAlive
  n = n + 1
  values(n MOD 256) = n
END REPEAT keepAlive
