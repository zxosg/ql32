' Text scroll benchmark for the packed retained canvas.
' Capture host elapsed time between SCROLL_START and SCROLL_DONE.
CONST KOptMathSingle = 1
CONST KEspCpuFreq240 = 240
OPTION KOptMathSingle
OPTION KEspCpuFreq240

benchmarkChannel = OPEN(2)
PRINT #benchmarkChannel,"SCROLL_START"
PRINT #benchmarkChannel,"SCROLL_CPUFREQ=";CPUFREQ()
PAPER 0
INK 7
GUPDATE 0
CLS
FOR index = 1 TO 1000
  PRINT "SCROLL LINE ";index
END FOR index
GUPDATE
PRINT #benchmarkChannel,"SCROLL_DONE"
