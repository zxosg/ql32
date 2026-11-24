' GUPDATE 1 PRINT benchmark that scrolls the text canvas several times.
CONST KOptMathSingle = 1
CONST KEspCpuFreq240 = 240
OPTION KOptMathSingle
OPTION KEspCpuFreq240

benchmarkChannel = OPEN(2)
GUPDATE 1
PAPER 0
INK 7
CLS
PRINT #benchmarkChannel,"PRINT_SCROLL_START"
FOR lineNo = 1 TO 40
  PRINT "SCROLL LINE ";lineNo
END FOR lineNo
PRINT #benchmarkChannel,"PRINT_SCROLL_DONE"
