' Compare automatic PRINT updates with batched canvas updates.
' Twenty lines fit on the 320x200 text surface, avoiding scroll work.
' Capture each interval between its serial start/done markers.
CONST KOptMathSingle = 1
CONST KEspCpuFreq240 = 240
OPTION KOptMathSingle
OPTION KEspCpuFreq240

benchmarkChannel = OPEN(2)
PRINT #benchmarkChannel,"PRINT_BENCH_CPUFREQ=";CPUFREQ()

FOR pass = 1 TO 3
  GUPDATE 1
  PAPER 0
  INK 7
  CLS
  PRINT #benchmarkChannel,"PRINT_AUTO_START";pass
  FOR lineNo = 1 TO 20
    PRINT "PRINT LINE ";lineNo
  END FOR lineNo
  PRINT #benchmarkChannel,"PRINT_AUTO_DONE";pass

  GUPDATE 0
  PAPER 0
  INK 7
  CLS
  PRINT #benchmarkChannel,"PRINT_BATCH_START";pass
  FOR lineNo = 1 TO 20
    PRINT "PRINT LINE ";lineNo
  END FOR lineNo
  GUPDATE
  PRINT #benchmarkChannel,"PRINT_BATCH_DONE";pass
END FOR pass
