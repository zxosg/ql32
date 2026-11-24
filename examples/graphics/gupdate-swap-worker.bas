' Low-noise worker for the FabGL swap/scheduler benchmark.
' Start two copies while gupdate-swap-benchmark.bas is running.
probe = OPEN(2)
iterations = 0
PRINT #probe,"SWAPWORK_START"
REPEAT work
  iterations = iterations + 1
  IF iterations MOD 250 = 0 THEN
    PRINT #probe,"SWAPWORK_PROGRESS ";iterations
  END IF
  PAUSE 20
END REPEAT work
