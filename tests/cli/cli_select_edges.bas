x = 5
SELect ON x
= 0 TO 5
PRINT "inclusive and first"
= 5
PRINT "duplicate must not run"
END SELect
SELect ON x
= 6 TO 8
PRINT "out of range"
END SELect
SELect ON 5
= 5
  SELect ON 2
  = 2
    PRINT "nested select"
  = REMAINDER
    PRINT "inner remainder must not run"
  END SELect
= REMAINDER
  PRINT "outer remainder must not run"
END SELect
SELect ON 0
= REMAINDER
  PRINT "remainder fallback"
END SELect
PRINT "no match falls through"
