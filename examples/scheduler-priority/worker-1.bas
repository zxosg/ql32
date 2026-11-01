REM Priority scheduler demo: task 1, highest priority, blue paper.
WINDOW 0,0,106,176
PAPER 1
INK 7
CLS
RECT 1,1,104,174
PRINT "TASK 1"
n = 0
REPEAT loop
  PRINT "1 "; n; " "; FREE()
  n = n + 1
END REPEAT loop
