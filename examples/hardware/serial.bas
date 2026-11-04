INCLUDE "ql32.inc"
INCLUDE "boards/esp32_2432s028/ql32-hardware.inc"

uart = OPEN(KDeviceSerial, KHwUart1, 9600, KSerial8N1, KFlowNone)
WRITE uart, "PING"
reply$ = READ$(uart)
PRINT "Reply: "; reply$
CLOSE uart
