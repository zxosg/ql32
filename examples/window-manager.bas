REM Draw first, then move and resize the retained canvas.
PAPER 0
INK 7
CLS
TEXT 8,8,"Retained canvas"
INK 2
RECT 20,25,140,90
LINE 20,25,160,115
CIRCLE 90,70,30
WINDOW 160,100,320,240
PRINT "Window origin X:"; GORIGINX()
PRINT "Window origin Y:"; GORIGINY()
INK 5
TEXT 8,150,"Moved to (160,100)"
