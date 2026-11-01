REM Sample free heap after each CYD rotation; results stay on the retained canvas.
PAPER 1
INK 7
CLS
PRINT #1,"FREE 0 ";FREE()
ROTATE 90
PRINT #1,"FREE 90 ";FREE()
ROTATE 180
PRINT #1,"FREE 180 ";FREE()
ROTATE 270
PRINT #1,"FREE 270 ";FREE()
ROTATE 0
PRINT #1,"FREE 0 again ";FREE()
