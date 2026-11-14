REM Animated attractor ported from the supplied p5.js sketch.
REM Uses the target's full graphics window and its QL eight-color palette.
REM PLOT draws one pixel in the current ink color.
REM Set rowStep=2 and sampleStep=1 for the source sketch's full point density.

CONST KOptMathDouble = 0
CONST KEspCpuFreq240 = 240
OPTION KOptMathDouble
OPTION KEspCpuFreq240

x = 0
v = 0
t = 0
r = 6.283185307179586 / 25
cx = GWIDTH() / 2
cy = GHEIGHT() / 2
sc = GHEIGHT() / 4
rowStep = 4
sampleStep = 8

PAPER 0
REPeat animation
  GUPDATE 0
  CLS
  c = 1
  FOR i = 1 TO 50 STEP rowStep
    inkIndex = (c - 1) MOD 7 + 1
    INK inkIndex
    rowAngle = r * i
    FOR j = 1 TO 50 STEP sampleStep
      REM j controls sample count; the recurrence intentionally advances each point.
      angleV = i + v
      angleX = rowAngle + x
      u = SIN(angleV) + SIN(angleX)
      v = COS(angleV) + COS(angleX)
      x = u + t
      px = INT(cx + u * sc)
      py = INT(cy + v * sc)
      PLOT px, py
    END FOR j
    c = c + 1
    IF c > 15 THEN
      c = 1
    END IF
  END FOR i
  t = t + 0.005
  GUPDATE
END REPeat animation
