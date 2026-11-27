REM QL/32 directory music player: directory channel, GCOPY scrolling, INKEY$.
REM Up/Down/PgUp/PgDn/Home/End move, Enter plays or opens a folder,
REM Backspace goes up, Space pause/resume, S stop, +/- or Left/Right volume,
REM D drive, O toggles sorted/unsorted listing, Q or Esc quit.
REM Songs auto-advance at the end or when a loop repeats; time comes from MUSIC POS()/LEN().
REM See README.md and docs/notes/dirplayer-workarounds.md.
DIM nm$(120)
DIM root$(2)
root$(0) = "SDC:/music/"
root$(1) = "SDC:/"
root$(2) = "FFS:/"
hlp$ = "Ent play Spc pause S stop +- vol D drive Q quit"
now$ = "-"
vol = 128
smode = 3
play$ = ""
GW = 320
GH = 240
SH = 240
TRAP WINDOW #1, 0, 0, GW, GH
LY = 14
SY = GH - 64
rows = INT((SY - LY) / 10)
cols = INT((GW - 8) / 6)
TRAP VOLUME vol
TRAP WINDOW #0, 0, SH - 8, GW, 8
PAPER 0
INK 7
CLS
REPeat find WHILE nsong < 1 AND r < 3
  path$ = root$(r)
  LoadDir
  r = r + 1
END REPeat find
msg$ = hlp$ : shown$ = ""
Redraw
REPeat main WHILE quit = 0
  k$ = INKEY$(250)
  IF k$ = "" THEN
    IF state = 1 THEN
      pos = MUSIC POS()
      IF MUSIC STATE() = 0 OR pos < lastpos THEN
        NextSong
      ELSE
        lastpos = pos
        DrawTime
      END IF
    END IF
  ELSE
    TRAP HandleKey k$
    IF ERR <> 0 THEN
      msg$ = "Error: " + ERR$(ERR)
      DrawStatus
    END IF
  END IF
END REPeat main
TRAP MUSIC STOP
REM Release the 38.4K canvas first so the console window can be re-created.
TRAP WINDOW #1, 0, 0, 8, 8
TRAP WINDOW #0, 0, SH - 64, GW, 64
CLS

DEFine PROCedure LoadDir
  LOCal d, n$, x$, dirf, s
  total = 0
  nsong = 0
  IF RIGHT$(path$, 2) <> ":/" THEN
    nm$(0) = "../"
    total = 1
  END IF
  d = 0
  TRAP d = OPEN(14, path$, "*", smode)
  IF d = 0 AND smode = 3 THEN
    TRAP d = OPEN(14, path$, "*", 1)
  END IF
  IF d = 0 THEN
    msg$ = "Cannot open " + path$ + ": " + ERR$(ERR)
    RETurn
  END IF
  REPeat scan WHILE EOF(d) = 0 AND total < 120
    TRAP n$ = READ$(d)
    IF ERR <> 0 THEN
      EXIT scan
    END IF
    dirf = DIRINFO(d, 1)
    IF LEFT$(n$, 1) <> "." THEN
      IF dirf <> 0 THEN
        nm$(total) = n$ + "/"
        total = total + 1
      ELSE
        x$ = UPPER$(RIGHT$(n$, 4))
        s = (x$ = ".VTX") + (x$ = ".PT3") + (x$ = ".SQT") + (x$ = ".WAV") + (RIGHT$(x$, 3) = ".YM")
        nsong = nsong + s
        IF s = 1 THEN
          nm$(total) = n$
          total = total + 1
        END IF
      END IF
    END IF
  END REPeat scan
  CLOSE d
END DEFine LoadDir

DEFine PROCedure Redraw
  cur = 0
  top = 0
  INK 0
  RECT 0, 0, GW, LY, 1
  INK 6
  TEXT 2, 3, LEFT$("QL/32 " + path$, cols)
  ShowList
  DrawStatus
END DEFine Redraw

DEFine PROCedure FitView
  IF cur > total - 1 THEN
    cur = total - 1
  END IF
  IF cur < 0 THEN
    cur = 0
  END IF
  IF top > total - rows THEN
    top = total - rows
  END IF
  IF top < 0 THEN
    top = 0
  END IF
  IF cur < top THEN
    top = cur
  END IF
  IF cur >= top + rows THEN
    top = cur - rows + 1
  END IF
END DEFine FitView

DEFine PROCedure ShowList
  LOCal i
  FOR i = 0 TO rows - 1
    DrawRow i
  END FOR i
END DEFine ShowList

DEFine PROCedure DrawRow(s)
  LOCal i, y, t$, n$
  y = LY + s * 10
  i = top + s
  INK 0
  RECT 0, y, GW, 10, 1
  IF total = 0 AND s = 0 THEN
    INK 2
    TEXT 2, y + 1, "(no songs: D = other drive)"
    RETurn
  END IF
  IF i >= total THEN
    RETurn
  END IF
  n$ = nm$(i)
  t$ = " " + n$
  IF state > 0 THEN
    IF path$ + n$ = play$ THEN
      t$ = ">" + n$
    END IF
  END IF
  IF LEN(t$) > cols THEN
    t$ = LEFT$(t$, cols - 1) + "~"
  END IF
  INK 7
  IF i = cur THEN
    INK 4
    RECT 0, y, GW, 10, 1
    INK 0
  END IF
  TEXT 2, y + 1, t$
END DEFine DrawRow

DEFine FuNction Mmss$(ms)
  LOCal t
  t = INT(ms / 1000)
  RETurn STR$(INT(t / 60)) + ":" + RIGHT$("0" + STR$(t MOD 60), 2)
END DEFine Mmss$

DEFine PROCedure DrawTime
  LOCal t$, l
  t$ = Mmss$(MUSIC POS())
  l = MUSIC LEN()
  IF l > 0 THEN
    t$ = t$ + " / " + Mmss$(l)
  END IF
  IF t$ = shown$ THEN RETurn
  shown$ = t$
  INK 0
  RECT 0, SY + 33, GW, 10, 1
  INK 7
  TEXT 2, SY + 35, "Time " + t$
END DEFine DrawTime

DEFine PROCedure DrawStatus
  LOCal n
  INK 0
  RECT 0, SY, GW, GH - SY, 1
  INK 7
  shown$ = ""
  n = INT(vol / 16)
  TEXT 2, SY + 3, MID$("STOPPED PLAYING PAUSED  ", state * 8 + 1, 8) + "Vol " + STR$(vol) + " [" + REPT$("#", n) + REPT$("-", 16 - n) + "]"
  TEXT 2, SY + 13, "Songs " + STR$(nsong) + "  Entry " + STR$(cur + 1) + "/" + STR$(total)
  TEXT 2, SY + 23, LEFT$("Now: " + now$, cols)
  DrawTime
  INK 5
  TEXT 2, SY + 45, LEFT$(msg$, cols)
  DrawBar
END DEFine DrawStatus

DEFine PROCedure DrawBar
  LOCal h, y
  INK 0
  RECT GW - 3, LY, 3, rows * 10, 1
  IF total > rows THEN
    h = INT(rows * rows * 10 / total)
    y = INT(top * (rows * 10 - h) / (total - rows))
    INK 2
    RECT GW - 3, LY + y, 3, h, 1
  END IF
END DEFine DrawBar

DEFine PROCedure MoveTo(n)
  LOCal old, d, s
  old = cur
  cur = n
  d = top
  FitView
  IF top = d THEN
    DrawRow old - top
    DrawRow cur - top
  ELSE
    d = top - d
    IF d = 1 OR d = -1 THEN
      top = top - d
      cur = -1
      DrawRow old - top
      top = top + d
      cur = n
      FOR s = 1 TO 2
        GCOPY 0, LY + (d + 1) * 5 / 2, GW, rows * 10 - 5, 0, LY + (1 - d) * 5 / 2, 0, 1
      END FOR s
      DrawRow cur - top
      ELSE
      ShowList
    END IF
  END IF
  DrawStatus
END DEFine MoveTo

DEFine PROCedure Enter
  LOCal n$
  IF total = 0 THEN
    RETurn
  END IF
  n$ = nm$(cur)
  IF n$ = "../" THEN
    GoUp
  ELSE
    IF RIGHT$(n$, 1) = "/" THEN
      path$ = path$ + n$
      LoadDir
      Redraw
    ELSE
      PlayCur
    END IF
  END IF
END DEFine Enter

DEFine PROCedure GoUp
  LOCal i, p$
  IF RIGHT$(path$, 2) = ":/" THEN
    RETurn
  END IF
  p$ = LEFT$(path$, LEN(path$) - 1)
  i = LEN(p$)
  REPeat f WHILE i > 0 AND MID$(p$, i, 1) <> "/"
    i = i - 1
  END REPeat f
  path$ = LEFT$(p$, i)
  LoadDir
  Redraw
END DEFine GoUp

DEFine PROCedure HandleKey(k$)
  msg$ = hlp$
  SELect ON UPPER$(k$)
  = "UP"
    MoveTo cur - 1
  = "DOWN"
    MoveTo cur + 1
  = "PGUP"
    MoveTo cur - rows + 1
  = "PGDN"
    MoveTo cur + rows - 1
  = "HOME"
    MoveTo 0
  = "END"
    MoveTo total - 1
  = "O"
    smode = 4 - smode
    LoadDir
    Redraw
  = "ENTER"
    Enter
  = "BACKSPACE"
    GoUp
  = "+", "RIGHT"
    SetVolume vol + 16
  = "-", "LEFT"
    SetVolume vol - 16
  = " "
    TogglePause
  = "S"
    StopPlay
  = "D"
    path$ = root$(1 + (LEFT$(path$, 3) = "SDC"))
    LoadDir
    Redraw
  = "Q", "ESC"
    quit = 1
  END SELect
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
  lastpos = 0
  pidx = cur
  now$ = nm$(cur)
  p$ = path$ + now$
  play$ = p$
  msg$ = "Loading " + now$ + " ..."
  DrawStatus
  IF UPPER$(RIGHT$(p$, 4)) = ".WAV" THEN
    TRAP MUSIC WAV p$
  ELSE
    TRAP MUSIC PLAY p$
  END IF
  msg$ = hlp$
  IF ERR = 0 THEN
    state = 1
    now$ = MUSIC TITLE$()
    IF now$ = "" THEN
      now$ = nm$(pidx)
    END IF
    ShowList
  ELSE
    now$ = "-"
    play$ = ""
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
  play$ = ""
  ShowList
  DrawStatus
END DEFine StopPlay

DEFine PROCedure NextSong
  LOCal i
  i = pidx + 1
  IF path$ + nm$(pidx) <> play$ THEN
    i = total
  END IF
  REPeat f WHILE i < total AND RIGHT$(nm$(i), 1) = "/"
    i = i + 1
  END REPeat f
  IF i < total THEN
    MoveTo i
    PlayCur
  ELSE
    StopPlay
  END IF
END DEFine NextSong
