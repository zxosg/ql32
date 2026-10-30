FOR i = 1 TO 4
  PRINT i
  IF i = 2 THEN
    EXIT i
  END IF
END FOR i
PRINT "after"
FOR outer = 1 TO 3
  FOR inner = 1 TO 3
    EXIT outer
  END FOR inner
  PRINT "wrong"
END FOR outer
PRINT "outer"
