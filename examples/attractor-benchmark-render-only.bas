' Draw into the retained canvas for 100 frames and present only once.
' Compare this with attractor-benchmark-draw.bas, which presents every frame.
CONST KOptMathSingle = 1
CONST KEspCpuFreq240 = 240
OPTION KOptMathSingle
OPTION KEspCpuFreq240

MODE 0,0
benchmarkChannel = OPEN(2)
PRINT #benchmarkChannel,"ATTRACTOR_RENDER_START"
PRINT #benchmarkChannel,"ATTRACTOR_CPUFREQ=";CPUFREQ()

rowStep = 4
sampleStep = 8
screenWidth = GWIDTH()
screenHeight = GHEIGHT()
PAPER 0
GUPDATE 0
FOR frame = 1 TO 100
  CLS
  c = 1
  FOR i = 1 TO 50 STEP rowStep
    inkIndex = (c - 1) MOD 7 + 1
    INK inkIndex
    FOR j = 1 TO 50 STEP sampleStep
      px = (i + j * 7 + frame) MOD screenWidth
      py = (i * 3 + j * 5 + frame) MOD screenHeight
      PLOT px, py
    END FOR j
    c = c + 1
    IF c > 15 THEN
      c = 1
    END IF
  END FOR i
END FOR frame
GUPDATE

PRINT #benchmarkChannel,"ATTRACTOR_RENDER_DONE"
