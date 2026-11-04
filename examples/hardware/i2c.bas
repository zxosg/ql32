INCLUDE "ql32.inc"
INCLUDE "boards/esp32_2432s028/ql32-hardware.inc"

' Example target: MPU-6050 address 104, WHO_AM_I register 117.
sensor = OPEN(KDeviceI2C, KHwI2c0, 104, KHwI2cClockHz)
identity = IN(sensor, 117)
PRINT "WHO_AM_I: "; identity

tx = ALLOC(6)
rx = ALLOC(6)
POKE tx, 107, 0, 0, 0, 0, 0
status = IOWRITE(sensor, 59, tx)
count = IOREAD(sensor, 59, rx)
PRINT "Status/count: "; status; "/"; count

DEALLOC tx
DEALLOC rx
CLOSE sensor
