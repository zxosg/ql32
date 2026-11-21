REM Bounded 20-frame fps probe: CLS + RECT + one GUPDATE per frame.
serial_probe = OPEN(2)
PAPER 0
INK 7
PRINT #serial_probe, "FPS|start"
FOR frame = 1 TO 20
  GUPDATE 0
  CLS
  RECT frame, frame, GWIDTH() - frame, GHEIGHT() - frame
  GUPDATE
END FOR frame
PRINT #serial_probe, "FPS|done"
