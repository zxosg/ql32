notEqual = 1 <> 2
greater = 2 > 1
greaterEqual = 2 >= 1
source$ = "ABCD"
middle$ = source$(2 TO 3)
CHANNEL #0
CHANNEL #0 TO #0
#0 AT 0
#0 IN 1, bytes$
#0 OUT 1, 1
PRINT #0, middle$
