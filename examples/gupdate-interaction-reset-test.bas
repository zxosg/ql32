' Verify automatic-update restoration before UI input and on console return.
PAPER 0
INK 7
CLS
PRINT "GUPDATE reset test"
PRINT #0,"GUPDATE_RESET|START"

GUPDATE 0
GUPDATE SKIP,3
PAPER 2
CLS
PRINT "This frame is pending until ALERT."
PRINT #0,"GUPDATE_RESET|BEFORE_ALERT"
ALERT "GUPDATE reset", "Press Enter to continue"
PRINT "Updates should now be automatic."
PLOT 30,40
PRINT #0,"GUPDATE_RESET|AFTER_ALERT"

GUPDATE 0
GUPDATE SKIP,3
PAPER 4
CLS
PRINT "This frame is pending until keyboard INPUT."
INPUT name$
PRINT "Input received: ";name$
PLOT 60,40
PRINT #0,"GUPDATE_RESET|AFTER_INPUT"

PRINT "Program ending; console updates should be normal."
PRINT #0,"GUPDATE_RESET|DONE"
