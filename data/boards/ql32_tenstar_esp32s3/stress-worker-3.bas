' Persistent string and arithmetic workload in the right 80-pixel panel.
WINDOW 160,0,80,95
PAPER 4
INK 0
CLS
TEXT 2,2,"WORKER 3"
serial_probe = OPEN(2)
cycles = 0
REPEAT work3
  FOR sample = 1 TO 72
    sample_text$ = LEFT$("MULTITASKING",sample MOD 12 + 1)
    text_length = LEN(sample_text$)
  END FOR sample
  cycles = cycles + 1
  IF cycles MOD 10 = 0 THEN
    CLS
    TEXT 2,2,"WORKER 3"
    PRINT cycles
    PRINT #serial_probe,"STRESS|W3|";cycles
  END IF
END REPEAT work3
