REM Hold a BASIC array and VM session while the supervisor measures heap use.
serial_probe = OPEN(2)
DIM values(511)
FOR i = 0 TO 511
  values(i) = i
END FOR i
PRINT #serial_probe, "MEM|array|active|"; FREE(0); ","; FREE(1); ","; FREE(2)
REPEAT keepAlive
  i = i + 1
END REPEAT keepAlive
