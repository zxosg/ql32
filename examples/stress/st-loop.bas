REM Stress worker: arithmetic loop heavy task. Reports progress on serial (OPEN(2)).
serial_probe = OPEN(2)
n = 0
acc = 0
REPeat spin
  FOR i = 1 TO 1000
    acc = acc + SIN(i) * COS(i)
  END FOR i
  n = n + 1
  IF n MOD 2 = 0 THEN
    PRINT #serial_probe, "PROG|loop|"; n; "|"; FREE(0)
  END IF
END REPeat spin
