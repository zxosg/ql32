count=0
REPeat loop
  count=count+1
  IF count < 3 THEN
    NEXT loop
  END IF
  PRINT "late"
  IF count = 3 THEN
    EXIT loop
  END IF
END REPeat loop
PRINT count
REPeat outer
  REPeat inner
    EXIT outer
  END REPeat inner
  PRINT "wrong"
END REPeat outer
PRINT "done"
