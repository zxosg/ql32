REM Full-screen 2D sine/cosine surface for TASKVIEW panning.
WINDOW 0,0,320,240
PAPER 0
INK 0
CLS
GUPDATE 0
FOR py = 0 TO 236 STEP 4
  FOR px = 0 TO 316 STEP 4
    z = SIN(px * 0.05) * COS(py * 0.05)
    INK 1 + INT((z + 1) * 3)
    PLOT px,py
  END FOR px
END FOR py
GUPDATE 1
GUPDATE
