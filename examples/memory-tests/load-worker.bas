REM Hold a retained graphics canvas and keep drawing until killed.
WINDOW 0,0,160,120
PAPER 0
INK 7
CLS
REPEAT spin
  n = (n + 1) MOD 120
  LINE 0,n,159,119 - n
END REPEAT spin
