DEFine FuNction Twice(n)
  RETurn n * 2
END DEFine Twice
DEFine PROCedure Boom(n)
  LOCal t
  t = EVAL("Twice(" + STR$(n) + ")")
  PRINT "boom", t
  t = 1 / 0
END DEFine Boom
total = 0
FOR i = 1 TO 40
  total = total + EVAL("Twice(" + STR$(i) + ") + 1")
  TRAP total = total + EVAL("1 / 0")
  IF ERR = 0 THEN PRINT "unexpected success"
  TRAP total = total + EVAL("(")
  TRAP Boom i
  IF i MOD 20 = 0 THEN PRINT i, total, ERR
END FOR i
TRAP x = 1 / 0 : PRINT ERR > 0
PRINT EVAL("Twice(21)")
