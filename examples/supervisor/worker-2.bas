REM Supervisor demo: middle display column.
WINDOW 106,0,107,240
PAPER 0
INK 5
CLS
PRINT "TASK 2"
log = OPEN("task-2.log", "APPEND")
WRITE log, "worker 2 started"
FOR n = 1 TO 250
  PRINT n
END FOR n
PRINT "READY"
