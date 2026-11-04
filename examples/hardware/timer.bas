INCLUDE "ql32.inc"

timer = OPEN(KDeviceTimer, 100, KTimerPeriodic)
ticks = 0
REPeat main
    elapsed = IN(timer)
    ticks = ticks + elapsed
    IF ticks >= 10 THEN
        EXIT main
    END IF
END REPeat main
PRINT "Ticks: "; ticks
CLOSE timer
