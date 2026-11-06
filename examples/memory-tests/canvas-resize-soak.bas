REM Warm the allocator, then repeat a full-screen canvas grow/shrink 100 times.
serial_probe = OPEN(2)
GUPDATE 0
WINDOW #1,0,0,160,120
FOR warmup = 1 TO 5
  WINDOW #1,0,0,320,240
  WINDOW #1,0,0,160,120
END FOR warmup
PRINT #serial_probe, "RESIZESOAK|baseline|"; FREE(0); ","; FREE(1); ","; FREE(2)
FOR cycle = 1 TO 100
  WINDOW #1,0,0,320,240
  WINDOW #1,0,0,160,120
  PRINT #serial_probe, "RESIZESOAK|cycle|"; cycle; "|"; FREE(0); ","; FREE(1); ","; FREE(2)
END FOR cycle
GUPDATE 1
PRINT #serial_probe, "RESIZESOAK|done|"; FREE(0); ","; FREE(1); ","; FREE(2)
