' Persistent math and text workload in the left 80-pixel panel.
WINDOW 0,0,80,95
PAPER 1
INK 7
CLS
TEXT 2,2,"WORKER 1"
serial_probe = OPEN(2)
cycles = 0
accumulator = 0
REPEAT work1
  FOR sample = 1 TO 80
    accumulator = accumulator + SIN(sample + cycles) * COS(sample / 3)
  END FOR sample
  cycles = cycles + 1
  IF cycles MOD 10 = 0 THEN
    CLS
    TEXT 2,2,"WORKER 1"
    PRINT cycles
    PRINT #serial_probe,"STRESS|W1|";cycles
  END IF
END REPEAT work1
