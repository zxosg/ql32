INCLUDE "ql32.inc"
INCLUDE "boards/esp32_2432s028/ql32-hardware.inc"

pad = OPEN(KDeviceCapTouch, KHwTouch0)
state = IN(pad)
IF state = 1 THEN
    PRINT "Touched"
ELSE
    PRINT "Released"
END IF
CLOSE pad
