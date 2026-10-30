PRINT Square(4)
PRINT Factorial(5)
DEFine FuNction Square(value)
  LOCal result
  result=value*value
  RETurn result
END DEFine Square
DEFine FuNction Factorial(value)
  IF value <= 1 THEN
    RETurn 1
  END IF
  RETurn value*Factorial(value-1)
END DEFine Factorial
