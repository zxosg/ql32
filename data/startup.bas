' OPEN(4) allocates the right pane as the first opened window channel, #2.

' Two top graphics panes with a console across the bottom.
screenWidth = INT(GWIDTH())
screenHeight = INT(GHEIGHT())
consoleHeight = INT(screenHeight / 5)
panelHeight = screenHeight - consoleHeight
leftWidth = INT(screenWidth / 2)
rightWidth = screenWidth - leftWidth

' Channel #2 is allocated as a graphics window for the right pane.
rightChannel = OPEN(4)

' Left pane: blue paper with yellow ink.
WINDOW #1,0,0,leftWidth,panelHeight
PAPER #1,1
INK #1,6
CHANNEL #1
CLS

' Right pane: red paper with yellow ink.
WINDOW #2,leftWidth,0,rightWidth,panelHeight
PAPER #2,2
INK #2,6
CHANNEL #2
CLS

' Console: green text on black, occupying the bottom fifth of the display.
PAPER #0,0
INK #0,4
WINDOW #0,0,panelHeight,screenWidth,consoleHeight
CHANNEL #0
CLS
CHANNEL #1

PRINT #0,"QL/32 BASIC"
PRINT #0,"Version 0.1.0"
PRINT #0,"Free memory total: ";FREE(0)
PRINT #0,"Largest free block: ";FREE(1)
PRINT #0,"Free fragments: ";FREE(2)
