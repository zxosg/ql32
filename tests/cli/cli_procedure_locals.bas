value=5
Show value
PRINT value
PRINT scratch
DEFine PROCedure Show(parameter)
  LOCal scratch
  scratch=parameter+1
  PRINT scratch
  RETurn
END DEFine Show
PRINT CountMatches(2)
DEFine FuNction CountMatches(wanted)
  LOCal i, n
  n = 0
  FOR i = 1 TO 4
    IF i MOD 2 = 0 THEN
      n = n + wanted
    END IF
  END FOR i
  RETurn n * 10
END DEFine CountMatches
