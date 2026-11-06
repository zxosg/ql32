' One-shot 64x64 indexed-color field. Set the task viewport before starting.
' The task origin shifts the phase, giving each grid tile a distinct pattern.
' For a full-height 3x2 grid, first enter WINDOW #0,239,0,1,135 in the REPL.
WINDOW 0,0,64,64
PAPER 0
CLS
phase = GORIGINX() * 0.031 + GORIGINY() * 0.047
FOR y = 0 TO 63
  runStart = 0
  oldShade = -1
  FOR x = 0 TO 63
    wave = SIN(x * 0.19634954 + phase) + COS(y * 0.19634954 - phase * 0.8) + SIN((x + y) * 0.09817477 + phase * 1.3) * COS((x - y) * 0.13089969 - phase)
    shade = INT((wave + 3) * 7 / 6)
    IF shade < 0 THEN
      shade = 0
    END IF
    IF shade > 7 THEN
      shade = 7
    END IF
    IF oldShade = -1 THEN
      oldShade = shade
      runStart = x
    ELSE
      IF shade <> oldShade THEN
        INK oldShade
        LINE runStart,y,x - 1,y
        oldShade = shade
        runStart = x
      END IF
    END IF
  END FOR x
  INK oldShade
  LINE runStart,y,63,y
END FOR y
