' Draw-only companion for attractor-benchmark.bas.
' Keep frame, row, and point counts identical; omit trig calculations.
CONST KOptMathDouble = 0
CONST KOptMathSingle = 1
CONST KEspCpuFreq80 = 80
CONST KEspCpuFreq160 = 160
CONST KEspCpuFreq240 = 240
OPTION KOptMathSingle
OPTION KEspCpuFreq80

benchmarkChannel = OPEN(2)
PRINT #benchmarkChannel,"ATTRACTOR_DRAW_START"
PRINT #benchmarkChannel,"ATTRACTOR_CPUFREQ=";CPUFREQ()

rowStep = 4
sampleStep = 8
screenWidth = GWIDTH()
screenHeight = GHEIGHT()
PAPER 0
FOR frame = 1 TO 100
  GUPDATE 0
  CLS
  c = 1
  FOR i = 1 TO 50 STEP rowStep
    inkIndex = (c - 1) MOD 7 + 1
    INK inkIndex
    FOR j = 1 TO 50 STEP sampleStep
      px = (i + j * 7) MOD screenWidth
      py = (i * 3 + j * 5) MOD screenHeight
      PLOT px, py
    END FOR j
    c = c + 1
    IF c > 15 THEN
      c = 1
    END IF
  END FOR i
  GUPDATE
END FOR frame

PRINT #benchmarkChannel,"ATTRACTOR_DRAW_DONE"
