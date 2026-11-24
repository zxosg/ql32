' Finite attractor benchmark using the density settings from attractor.bas.
' Change the two OPTION declarations for each clock/precision configuration.
' Serial markers include the measured CPU clock for each run.
CONST KOptMathDouble = 0
CONST KOptMathSingle = 1
CONST KEspCpuFreq80 = 80
CONST KEspCpuFreq160 = 160
CONST KEspCpuFreq240 = 240
OPTION KOptMathSingle
OPTION KEspCpuFreq80

benchmarkChannel = OPEN(2)
PRINT #benchmarkChannel,"ATTRACTOR_START"
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
END FOR frame

PRINT #benchmarkChannel,"ATTRACTOR_DONE"
