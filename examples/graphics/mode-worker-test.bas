REM Run as a worker while the supervisor changes display modes.
serial_probe = OPEN(2)
PRINT #serial_probe,"MODE_WORKER_START"
n = 0
REPEAT drawing
  INK 1 + (n MOD 7)
  PLOT n MOD 80,(n * 3) MOD 80
  n = n + 1
  IF n MOD 25 = 0 THEN PRINT #serial_probe,"MODE_WORKER_PROGRESS ";n
  PAUSE 20
END REPEAT drawing
