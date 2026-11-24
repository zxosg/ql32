' Inline experimental sine/cosine sum-identity attractor benchmark.
' Avoids temporary variables used by attractor-benchmark-identity.bas.
' Finite-precision math changes the exact chaotic trajectory.
CONST KOptMathDouble = 0
CONST KOptMathSingle = 1
CONST KEspCpuFreq240 = 240
OPTION KOptMathSingle
OPTION KEspCpuFreq240

benchmarkChannel = OPEN(2)
PRINT #benchmarkChannel,"ATTRACTOR_IN_START"
PRINT #benchmarkChannel,"ATTRACTOR_CPUFREQ=";CPUFREQ()

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
FOR frame = 1 TO 100
  GUPDATE 0
  CLS
  c = 1
  FOR i = 1 TO 50 STEP rowStep
    inkIndex = (c - 1) MOD 7 + 1
    INK inkIndex
    rowAngle = r * i
    FOR j = 1 TO 50 STEP sampleStep
      angleV = i + v
      angleX = rowAngle + x
      u = 2 * SIN((angleV + angleX) / 2) * COS((angleV - angleX) / 2)
      v = 2 * COS((angleV + angleX) / 2) * COS((angleV - angleX) / 2)
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
END FOR frame

PRINT #benchmarkChannel,"ATTRACTOR_IN_DONE"
