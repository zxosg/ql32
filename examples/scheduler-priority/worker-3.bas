REM Priority scheduler demo: task 3, lowest priority, green paper.
WINDOW 213,0,107,176
PAPER 4
INK 0
CLS
RECT 1,1,105,174
PRINT "TASK 3"
n = 0
REPEAT loop
  PRINT "3 "; n
  n = n + 1
END REPEAT loop
