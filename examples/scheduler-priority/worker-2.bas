REM Priority scheduler demo: task 2, medium priority, red paper.
WINDOW 106,0,107,176
PAPER 2
INK 7
CLS
RECT 1,1,105,174
PRINT "TASK 2"
n = 0
REPEAT loop
  PRINT "2 "; n
  n = n + 1
END REPEAT loop
