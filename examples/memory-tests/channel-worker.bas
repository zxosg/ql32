REM Hold an open SD file channel while the supervisor measures heap use.
serial_probe = OPEN(2)
file = OPEN("FFS:/ql32_memory_probe.tmp", "WRITE")
WRITE file, "QL/32 memory test"
PRINT #serial_probe, "MEM|channel|active|"; FREE(0); ","; FREE(1); ","; FREE(2)
CLOSE file
PRINT #serial_probe, "MEM|channel|released|"; FREE(0); ","; FREE(1); ","; FREE(2)
REPEAT keepAlive
  n = n + 1
END REPEAT keepAlive
