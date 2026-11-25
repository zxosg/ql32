REM QL/32 directory music player: sorted directory channel, GCOPY scrolling.
REM Up/Down/PgUp/PgDn move, Enter plays, Space pause/resume, S stop,
REM +/- volume, Q or Esc quit. See README.md.
REM Kept small on purpose: the CYD compiles at most ~512 instructions.
DIM nm$(120)
DIM root$(2)
root$(0) = "SDC:/music/"
root$(1) = "SDC:/"
root$(2) = "FFS:/"
hlp$ = "ENTER play SPC pause S stop +/- vol Q quit"
msg$ = hlp$
now$ = "-"
vol = 128
GW = GWIDTH()
GH = GHEIGHT()
cols = INT(GW / 6) - 1
SY = GH - 44
rows = INT((SY - 19) / 10)
TRAP VOLUME vol
REPeat find WHILE total < 1 AND r < 3
  LoadDir r
  r = r + 1
END REPeat find
IF total < 1 THEN
  PRINT "No songs found in SDC:/music, SDC:/ or FFS:/"
  STOP
END IF
base$ = root$(r - 1)
PAPER 0
INK 7
CLS
TEXT 2, 2, "QL/32 MUSIC  " + base$
ShowList
DrawStatus
REPeat main WHILE k <> 113 AND k <> 1114113
  k = INKEY(300)
  IF k <> 0 THEN
    HandleKey k
  END IF
END REPeat main
TRAP MUSIC STOP
PAPER 0
CLS
PRINT "Bye"

DEFine PROCedure LoadDir(r)
  LOCal d, n$, x$
  d = 0
  TRAP d = OPEN(14, root$(r), "*", 2)
  IF d = 0 THEN
    RETurn
  END IF
  REPeat scan WHILE EOF(d) = 0 AND total < 120
    TRAP n$ = READ$(d)
    IF ERR <> 0 THEN
      EXIT scan
    END IF
    x$ = UPPER$(RIGHT$(n$, 4))
    IF LEFT$(n$, 1) <> "." AND (x$ = ".VTX" OR x$ = ".PT3" OR x$ = ".SQT" OR x$ = ".WAV" OR RIGHT$(x$, 3) = ".YM") THEN
      nm$(total) = n$
      total = total + 1
    END IF
  END REPeat scan
  CLOSE d
END DEFine LoadDir

DEFine PROCedure ShowList
  LOCal i
  FOR i = 0 TO rows - 1
    DrawRow i
  END FOR i
END DEFine ShowList

DEFine PROCedure DrawRow(s)
  LOCal i, y, t$
  y = 16 + s * 10
  i = top + s
  INK 0
  RECT 0, y, GW, 10, 1
  IF i >= total THEN
    RETurn
  END IF
  INK 7
  IF i = cur THEN
    INK 4
    RECT 0, y, GW, 10, 1
    INK 0
  END IF
  t$ = LEFT$(nm$(i), cols)
  TEXT 2, y + 1, t$
END DEFine DrawRow

DEFine PROCedure DrawStatus
  LOCal n
  INK 0
  RECT 0, SY, GW, GH - SY, 1
  INK 7
  n = INT(vol / 16)
  TEXT 2, SY + 3, MID$("STOPPED PLAYING PAUSED ", state * 8 + 1, 8) + "Vol " + STR$(vol) + " [" + REPT$("#", n) + REPT$("-", 16 - n) + "]"
  TEXT 2, SY + 13, STR$(total) + " songs, item " + STR$(cur + 1)
  TEXT 2, SY + 23, LEFT$("Now: " + now$, cols)
  INK 5
  TEXT 2, SY + 33, LEFT$(msg$, cols)
END DEFine DrawStatus

DEFine PROCedure MoveTo(n)
  LOCal old, d
  old = cur
  cur = n
  IF cur > total - 1 THEN
    cur = total - 1
  END IF
  IF cur < 0 THEN
    cur = 0
  END IF
  msg$ = hlp$
  DrawRow old - top
  d = 0
  IF cur = top + rows THEN
    d = 1
  END IF
  IF cur = top - 1 THEN
    d = -1
  END IF
  IF d <> 0 THEN
    GCOPY 0, 16 + (d + 1) * 5, GW, rows * 10 - 10, 0, 16 + (1 - d) * 5, 0, 1
    top = top + d
    DrawRow cur - top
  ELSE
    IF cur < top OR cur >= top + rows THEN
      top = cur - INT(rows / 2)
      IF top > total - rows THEN
        top = total - rows
      END IF
      IF top < 0 THEN
        top = 0
      END IF
      ShowList
    ELSE
      DrawRow cur - top
    END IF
  END IF
  DrawStatus
END DEFine MoveTo

DEFine PROCedure HandleKey(k)
  IF k = 1114124 OR k = 1114125 THEN
    MoveTo cur + (k - 1114124) * 2 - 1
  END IF
  IF k = 43 OR k = 45 THEN
    SetVolume vol + (44 - k) * 16
  END IF
  IF k = 1114112 THEN
    PlayCur
  END IF
  IF k = 32 THEN
    TogglePause
  END IF
  IF k = 115 THEN
    StopPlay
  END IF
END DEFine HandleKey

DEFine PROCedure SetVolume(v)
  vol = v
  IF vol > 255 THEN
    vol = 255
  END IF
  IF vol < 0 THEN
    vol = 0
  END IF
  TRAP VOLUME vol
  DrawStatus
END DEFine SetVolume

DEFine PROCedure PlayCur
  LOCal p$
  TRAP MUSIC STOP
  state = 0
  now$ = nm$(cur)
  p$ = base$ + now$
  IF UPPER$(RIGHT$(p$, 4)) = ".WAV" THEN
    TRAP MUSIC WAV p$
  ELSE
    TRAP MUSIC PLAY p$
  END IF
  msg$ = hlp$
  IF ERR = 0 THEN
    state = 1
  ELSE
    now$ = "-"
    msg$ = "Play failed: " + ERR$(ERR)
  END IF
  DrawStatus
END DEFine PlayCur

DEFine PROCedure TogglePause
  IF state > 0 THEN
    IF state = 1 THEN
      TRAP MUSIC PAUSE
    ELSE
      TRAP MUSIC RESUME
    END IF
    state = 3 - state
    DrawStatus
  END IF
END DEFine TogglePause

DEFine PROCedure StopPlay
  TRAP MUSIC STOP
  state = 0
  now$ = "-"
  DrawStatus
END DEFine StopPlay
