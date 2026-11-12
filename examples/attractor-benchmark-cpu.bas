' CPU/math-only companion for attractor-benchmark.bas.
' Keep loop counts and recurrence identical; omit VGA operations.
CONST KOptMathDouble = 0
CONST KOptMathSingle = 1
CONST KEspCpuFreq80 = 80
CONST KEspCpuFreq160 = 160
CONST KEspCpuFreq240 = 240
OPTION KOptMathSingle
OPTION KEspCpuFreq80

benchmarkChannel = OPEN(2)
PRINT #benchmarkChannel,"ATTRACTOR_CPU_START"
PRINT #benchmarkChannel,"ATTRACTOR_CPUFREQ=";CPUFREQ()

x = 0
v = 0
t = 0
r = 6.283185307179586 / 25
cx = 160
cy = 100
sc = 50
rowStep = 4
sampleStep = 8

FOR frame = 1 TO 100
  c = 1
  FOR i = 1 TO 50 STEP rowStep
    rowAngle = r * i
    FOR j = 1 TO 50 STEP sampleStep
      angleV = i + v
      angleX = rowAngle + x
      u = SIN(angleV) + SIN(angleX)
      v = COS(angleV) + COS(angleX)
      x = u + t
      px = INT(cx + u * sc)
      py = INT(cy + v * sc)
    END FOR j
    c = c + 1
  END FOR i
  t = t + 0.005
END FOR frame

PRINT #benchmarkChannel,"ATTRACTOR_CHECKSUM=";x+v+t+px+py
PRINT #benchmarkChannel,"ATTRACTOR_CPU_DONE"
