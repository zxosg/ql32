FOR n% = 2, 4 TO 10 STEP 2, 12, 15 TO 9 STEP -2
  PRINT n%
END FOR n%
FOR i = 1 TO 2
  FOR j = 1 TO 2
    PRINT i; j
  END FOR j
END FOR i
FOR skipped = 5 TO 1 STEP 1
  PRINT "wrong"
END FOR skipped
FOR early = 1 TO 3
  PRINT early
  NEXT early
  PRINT "late"
END FOR early
FOR jumps = 2, 4 TO 6 STEP 2, 9
  PRINT jumps
  NEXT jumps
  PRINT "after"
END FOR jumps
