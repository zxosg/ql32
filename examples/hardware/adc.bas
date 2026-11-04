INCLUDE "ql32.inc"
INCLUDE "boards/esp32_2432s028/ql32-hardware.inc"

sensor = OPEN(KDeviceADC, KHwGpioD0, KAdcResolution12)
counts = IN(sensor)
PRINT "ADC counts: "; counts
CLOSE sensor
