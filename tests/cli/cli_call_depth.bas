DEFine FuNction Descend(depth)
  IF depth = 0 THEN
    RETurn 0
  END IF
  RETurn Descend(depth - 1)
END DEFine Descend
PRINT Descend(300)
