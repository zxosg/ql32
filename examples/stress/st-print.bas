REM Stress worker: scrolling PRINT loop. Reports progress on serial (OPEN(2)).
serial_probe = OPEN(2)
n = 0
REPeat printLoop
  n = n + 1
  PRINT "stress print line "; n; " ........................"
  IF n MOD 100 = 0 THEN
    PRINT #serial_probe, "PROG|print|"; n; "|"; FREE(0)
  END IF
END REPeat printLoop
