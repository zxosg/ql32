INCLUDE "ql32.inc"
INCLUDE "boards/esp32_2432s028/ql32-hardware.inc"

device = OPEN(KDeviceSPI, KHwSpiStorage, KHwGpioD0, KHwSpiClockHz, 0)
tx = ALLOC(2)
rx = ALLOC(2)
POKE tx, 159, 0
XFER #device, tx, 2, rx, 2
PRINT "Received: "; PEEK(rx); ","; PEEK(rx + 1)

DEALLOC tx
DEALLOC rx
CLOSE device
