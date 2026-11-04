REM Combine array, file-channel, and graphics allocations in one worker.
serial_probe = OPEN(2)
DIM values(511)
FOR i = 0 TO 511
  values(i) = i
END FOR i
file = OPEN("FFS:/ql32_memory_probe.tmp", "WRITE")
WRITE file, "QL/32 combined memory test"
WINDOW 0,0,160,120
PAPER 0
INK 7
CLS
RECT 0,0,159,119
PLOT 80,60
PRINT #serial_probe, "MEM|combined|active|"; FREE(0); ","; FREE(1); ","; FREE(2)
CLOSE file
CLOSEWINDOW
PRINT #serial_probe, "MEM|combined|released|"; FREE(0); ","; FREE(1); ","; FREE(2)
REPEAT keepAlive
  i = i + 1
END REPEAT keepAlive
