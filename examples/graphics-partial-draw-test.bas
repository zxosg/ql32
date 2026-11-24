' Exercise automatic partial VGA updates for graphics primitives.
' Run on the Olimex 320x200 double-buffer profile and watch for flicker,
' artifacts, or sync loss during the bounded drawing sequence.
CONST KOptMathSingle = 1
CONST KEspCpuFreq240 = 240
OPTION KOptMathSingle
OPTION KEspCpuFreq240

benchmarkChannel = OPEN(2)
GUPDATE 1
PAPER 0
INK 7
CLS
PRINT #benchmarkChannel,"PARTIAL_DRAW_START"

FOR index = 1 TO 80
  inkIndex = (index MOD 7) + 1
  INK inkIndex
  x = (index * 37) MOD 296
  y = (index * 23) MOD 184
  RECT x,y,20,12,1
END FOR index

INK 6
FOR index = 1 TO 80
  x = (index * 53) MOD 320
  y = (index * 29) MOD 200
  PLOT x,y
END FOR index

INK 3
FOR index = 1 TO 40
  x1 = (index * 17) MOD 320
  y1 = (index * 31) MOD 200
  x2 = (x1 + 48) MOD 320
  y2 = (y1 + 24) MOD 200
  LINE x1,y1,x2,y2
END FOR index

INK 2
CIRCLE 160,100,36,1
PRINT #benchmarkChannel,"PARTIAL_DRAW_DONE"
