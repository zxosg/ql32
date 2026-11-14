' High-frequency pixel-order fixture for direct VGA framebuffer paths.
' Black/green 1-pixel vertical and horizontal edges expose pixel shuffling.

CONST KOptMathSingle = 1
OPTION KOptMathSingle

GUPDATE 0
WINDOW #1,0,0,320,176
PAPER #1,0
INK #1,0
CLS #1

' Left half: alternating vertical pixels.
INK #1,4
FOR x = 0 TO 158 STEP 2
  LINE #1,x,0,x,175
END FOR x

' Right half: alternating horizontal rows.
FOR y = 0 TO 174 STEP 2
  LINE #1,160,y,319,y
END FOR y

' One-pixel checker patch; edges are deliberately not aligned to four pixels.
FOR y = 32 TO 95
  FOR x = 193 TO 256
    IF (x + y) MOD 2 = 0 THEN
      PLOT #1,x,y
    END IF
  END FOR x
END FOR y

GUPDATE
PRINT "PIXEL EDGE TEST DONE"
