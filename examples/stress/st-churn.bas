REM Stress worker: string and array churn. Reports progress on serial (OPEN(2)).
serial_probe = OPEN(2)
DIM vals(255)
n = 0
REPeat churnLoop
  n = n + 1
  s$ = ""
  FOR i = 1 TO 20
    s$ = s$ + "x" + STR$(i)
  END FOR i
  FOR i = 0 TO 255
    vals(i) = i * n
  END FOR i
  IF n MOD 10 = 0 THEN
    PRINT #serial_probe, "PROG|churn|"; n; "|"; FREE(0); "|"; LEN(s$)
  END IF
END REPeat churnLoop
