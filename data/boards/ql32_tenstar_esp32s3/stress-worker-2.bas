' Persistent numeric and canvas-clear workload in the middle panel.
WINDOW 80,0,80,95
PAPER 2
INK 7
CLS
TEXT 2,2,"WORKER 2"
serial_probe = OPEN(2)
cycles = 0
accumulator = 1
REPEAT work2
  FOR sample = 1 TO 96
    accumulator = (accumulator + sample * 3) MOD 100003
  END FOR sample
  cycles = cycles + 1
  IF cycles MOD 10 = 0 THEN
    CLS
    TEXT 2,2,"WORKER 2"
    PRINT cycles
    PRINT #serial_probe,"STRESS|W2|";cycles
  END IF
END REPEAT work2
