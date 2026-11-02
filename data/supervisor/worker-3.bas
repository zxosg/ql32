REM Supervisor demo: right display column.
WINDOW 213,0,107,240
PAPER 0
INK 3
CLS
PRINT "TASK 3"
log = OPEN("task-3.log", "APPEND")
WRITE log, "worker 3 started"
FOR n = 1 TO 250
  PRINT n
END FOR n
PRINT "READY"
