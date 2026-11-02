REM Animated attractor ported from the supplied p5.js sketch.
REM Uses the target's full graphics window and its QL eight-color palette.
REM PLOT draws one pixel in the current ink color.
REM Set rowStep=2 and sampleStep=1 for the source sketch's full point density.

CONST KOptMathSingle = 1
CONST KEspCpuFreq240 = 240
OPTION KOptMathSingle
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
    FOR j = 1 TO 50 STEP sampleStep
      REM j controls sample count; the recurrence intentionally advances each point.
      u = SIN(i + v) + SIN(r * i + x)
      v = COS(i + v) + COS(r * i + x)
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
