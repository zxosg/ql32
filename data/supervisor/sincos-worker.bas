' Small continuous sin/cos animation for task viewport testing.
' Each worker owns a 64x64 window positioned relative to its task origin.
WINDOW 0,0,64,64
PAPER 0
CLS
frame = 0
REPEAT animate
  phase = frame * 0.08
  INK 7
  CIRCLE 32,32,29
  FOR angle = 0 TO 6.28 STEP 0.20
    radius = 12 + INT((SIN(angle * 3 + phase) + 1) * 7)
    px = 32 + INT(COS(angle + phase) * radius)
    py = 32 + INT(SIN(angle + phase) * radius)
    shade = 1 + INT((COS(angle * 2 - phase) + 1) * 3)
    INK shade
    PLOT px,py
  END FOR angle
  frame = frame + 1
  IF frame MOD 8 = 0 THEN
    CLS
  END IF
END REPEAT animate
