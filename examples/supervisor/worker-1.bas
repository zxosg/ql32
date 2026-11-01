REM Supervisor demo: left display column.
WINDOW 0,0,106,240
PAPER 0
INK 7
CLS
PRINT "TASK 1"
log = OPEN("task-1.log", "APPEND")
WRITE log, "worker 1 started"
FOR n = 1 TO 250
  PRINT n
END FOR n
PRINT "READY"
