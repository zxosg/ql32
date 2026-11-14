' Packed-canvas fill benchmark for the Olimex FabGL VGA profile.
' Capture host elapsed time between each *_START and *_DONE marker.
CONST KOptMathSingle = 1
CONST KEspCpuFreq240 = 240
OPTION KOptMathSingle
OPTION KEspCpuFreq240

benchmarkChannel = OPEN(2)
PRINT #benchmarkChannel,"GRAPHICS_RECT_START"
PRINT #benchmarkChannel,"GRAPHICS_CPUFREQ=";CPUFREQ()
PAPER 0
INK 7
GUPDATE 0
CLS
FOR index = 1 TO 4000
  x = (index * 37) MOD 250
  y = (index * 23) MOD 150
  RECT x,y,64,32,1
END FOR index
GUPDATE
PRINT #benchmarkChannel,"GRAPHICS_RECT_DONE"

PRINT #benchmarkChannel,"GRAPHICS_CIRCLE_START"
GUPDATE 0
CLS
FOR index = 1 TO 2000
  x = (index * 41) MOD 296 + 12
  y = (index * 19) MOD 176 + 12
  CIRCLE x,y,12,1
END FOR index
GUPDATE
PRINT #benchmarkChannel,"GRAPHICS_CIRCLE_DONE"

PAPER 1
CLS
INK 7
