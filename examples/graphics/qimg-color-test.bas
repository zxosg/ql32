' QIMG v2 physical smoke test: embedded palette, odd 17x17 image and transparent border.
CLS 7,2
image% = LOADIMAGE("FFS:/qimg-color-test.qimg")
BLIT image%, 151, 91, 0
GUPDATE
UNLOADIMAGE image%
PRINT #0, "QIMG LOAD/BLIT/UNLOAD PASS"
