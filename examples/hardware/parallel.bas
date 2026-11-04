INCLUDE "ql32.inc"

' Replace the numeric pins with eight free GPIOs on the selected board.
' OPEN order is bit 7 through bit 0.
port = OPEN(KDeviceParallel, 4, 16, 17, 22, 27, 32, 33, 34, KPinMode, KPinInputOutput)
OUT port, 170
OUT port, KPinMode, KPinOutput
OUT port, KPinMode, KPinInput
sample = IN(port)
PRINT "Port sample: "; sample
CLOSE port
