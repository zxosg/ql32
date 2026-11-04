INCLUDE "ql32.inc"
INCLUDE "boards/esp32_2432s028/ql32-hardware.inc"

button = OPEN(KDeviceGPIO, KHwGpioD0, KPinInput, KPullUp)
led = OPEN(KDeviceGPIO, KHwGpioD1, KPinOutput)

level = IN(button)
PRINT "Pin level: "; level
OUT led, 1

CLOSE button
CLOSE led
