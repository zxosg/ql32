' Experimental attractor benchmark using sine/cosine sum identities.
' This reduces four trig calls per point to three; rounding can change the
' exact chaotic trajectory, so compare the displayed pattern as well as speed.
CONST KOptMathDouble = 0
CONST KOptMathSingle = 1
CONST KEspCpuFreq240 = 240
OPTION KOptMathSingle
OPTION KEspCpuFreq240

benchmarkChannel = OPEN(2)
PRINT #benchmarkChannel,"ATTRACTOR_ID_START"
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
      halfSum = (angleV + angleX) / 2
      halfDiff = (angleV - angleX) / 2
      halfSumSin = SIN(halfSum)
      halfSumCos = COS(halfSum)
      halfDiffCos = COS(halfDiff)
      u = 2 * halfSumSin * halfDiffCos
      v = 2 * halfSumCos * halfDiffCos
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

PRINT #benchmarkChannel,"ATTRACTOR_ID_DONE"
