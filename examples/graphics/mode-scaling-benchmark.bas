' Compare full-frame drawing cost across VGA display modes.
' Capture elapsed time between each MODE_DRAW_START and DONE marker.
CONST KOptMathSingle = 1
CONST KEspCpuFreq240 = 240
OPTION KOptMathSingle
OPTION KEspCpuFreq240

benchmarkChannel = OPEN(2)
screenWidth = GWIDTH()
screenHeight = GHEIGHT()
PRINT #benchmarkChannel,"MODE_DRAW_SIZE=";screenWidth;"x";screenHeight
PRINT #benchmarkChannel,"MODE_DRAW_CPUFREQ=";CPUFREQ()

PAPER 0
INK 7
PRINT #benchmarkChannel,"MODE_DRAW_START"
FOR frame = 1 TO 10
  GUPDATE 0
  CLS
  FOR i = 1 TO 50 STEP 4
    inkIndex = (i MOD 7) + 1
    INK inkIndex
    FOR j = 1 TO 50 STEP 8
      px = (i + j * 7) MOD screenWidth
      py = (i * 3 + j * 5) MOD screenHeight
      PLOT px, py
    END FOR j
  END FOR i
  GUPDATE
END FOR frame
PRINT #benchmarkChannel,"MODE_DRAW_DONE"
