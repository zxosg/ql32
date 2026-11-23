REM QL/32 music player v1 (Olimex, MODE 2, single task, no extra WINDOWs).
REM Reads the playlist FFS:/music.lst written by tools/ql32-music-scan.py
REM (see README.md); BASIC programs cannot run DIR themselves.
REM Run:  TASK "FFS:/musicplayer.bas", KTaskWait
DIM path$(200)
DIM name$(200)
DIM kind(200)
DIM drv(200)
DIM idx(200)
count = 0
vol = 128
state = 0
now$ = "-"
page = 0
VOLUME vol
IF MODE(KModeCurrent) <> 2 THEN
  ALERT "Music player", "Needs MODE 2 (400x300). Type MODE 2 first."
  STOP
END IF
LoadList
IF count = 0 THEN
  ALERT "Music player", "FFS:/music.lst is empty. Run tools/ql32-music-scan.py first."
  STOP
END IF
REPeat disks
  d = PickDisk()
  IF d = 0 THEN
    EXIT disks
  END IF
  Browse d
END REPeat disks
IF state <> 0 THEN
  MUSIC STOP
END IF
PAPER 0
INK 7
CLS
PRINT "Bye"

DEFine PROCedure LoadList
  LOCal f, line$, prefix$, full$, ext$, k
  f = OPEN("FFS:/music.lst", "APPEND")
  CLOSE f
  f = OPEN("FFS:/music.lst", "READ")
  prefix$ = ""
  count = 0
  REPeat reading
    IF EOF(f) <> 0 THEN
      EXIT reading
    END IF
    line$ = READ$(f)
    IF LEN(line$) > 0 THEN
      IF CODE(RIGHT$(line$, 1)) = 13 THEN
        line$ = LEFT$(line$, LEN(line$) - 1)
      END IF
    END IF
    IF LEN(line$) > 0 THEN
      IF LEFT$(line$, 1) = "@" THEN
        prefix$ = MID$(line$, 2, LEN(line$) - 1)
      ELSE
        IF RIGHT$(line$, 1) <> "/" AND count < 200 THEN
          full$ = prefix$ + line$
          ext$ = UPPER$(RIGHT$(line$, 4))
          k = 0
          IF ext$ = ".WAV" THEN
            k = 2
          END IF
          IF ext$ = ".VTX" OR ext$ = ".SQT" OR ext$ = ".PT3" OR RIGHT$(ext$, 3) = ".YM" THEN
            k = 1
          END IF
          IF k > 0 THEN
            count = count + 1
            path$(count) = full$
            name$(count) = line$
            kind(count) = k
            drv(count) = 1
            IF UPPER$(LEFT$(full$, 4)) = "FFS:" THEN
              drv(count) = 2
            END IF
          END IF
        END IF
      END IF
    END IF
  END REPeat reading
  CLOSE f
END DEFine LoadList

DEFine FuNction DriveCount(d)
  LOCal i, n
  n = 0
  FOR i = 1 TO count
    IF drv(i) = d THEN
      n = n + 1
    END IF
  END FOR i
  RETurn n
END DEFine DriveCount

DEFine FuNction PickDisk()
  LOCal m, c, n1, n2
  REPeat picking
    Screen
    n1 = DriveCount(1)
    n2 = DriveCount(2)
    m = MINIT("Select disk")
    MCARD m, 1, "SDC: card (" + STR$(n1) + " files)", ""
    MCARD m, 2, "FFS: flash (" + STR$(n2) + " files)", ""
    IF state <> 0 THEN
      MCARD m, 3, "Now playing controls", ""
    END IF
    MCARD m, 9, "Quit", ""
    IF n1 = 0 THEN
      MENABLE m, 1, 0
    END IF
    IF n2 = 0 THEN
      MENABLE m, 2, 0
    END IF
    c = MENU(m)
    IF c = 3 THEN
      Controls
    ELSE
      EXIT picking
    END IF
  END REPeat picking
  IF c = 9 THEN
    RETurn 0
  END IF
  RETurn c
END DEFine PickDisk

DEFine PROCedure Browse(d)
  LOCal n, i, pages, first, last, m, c, k, title$
  n = 0
  FOR i = 1 TO count
    IF drv(i) = d THEN
      n = n + 1
      idx(n) = i
    END IF
  END FOR i
  pages = INT((n + 7) / 8)
  page = 0
  REPeat browsing
    Screen
    first = page * 8 + 1
    last = first + 7
    IF last > n THEN
      last = n
    END IF
    title$ = "Page " + STR$(page + 1) + "/" + STR$(pages)
    m = MINIT(title$)
    FOR k = first TO last
      MCARD m, k - first + 1, name$(idx(k)), ""
    END FOR k
    IF page < pages - 1 THEN
      MCARD m, 100, "Next page", ""
    END IF
    IF page > 0 THEN
      MCARD m, 101, "Previous page", ""
    END IF
    IF state <> 0 THEN
      MCARD m, 102, "Now playing controls", ""
    END IF
    MCARD m, 103, "Change disk", ""
    c = MENU(m)
    IF c = 0 OR c = 103 THEN
      EXIT browsing
    END IF
    IF c = 100 THEN
      page = page + 1
    END IF
    IF c = 101 THEN
      page = page - 1
    END IF
    IF c = 102 THEN
      Controls
    END IF
    IF c >= 1 AND c <= 8 THEN
      Play idx(first + c - 1)
      Controls
    END IF
  END REPeat browsing
END DEFine Browse

DEFine PROCedure Play(i)
  LOCal p$
  IF state <> 0 THEN
    MUSIC STOP
    state = 0
  END IF
  p$ = path$(i)
  now$ = name$(i)
  IF kind(i) = 2 THEN
    MUSIC WAV p$
  ELSE
    MUSIC PLAY p$
  END IF
  state = 1
END DEFine Play

DEFine PROCedure Controls
  LOCal m, c
  REPeat ctl
    Screen
    m = MINIT("Now playing")
    IF state = 2 THEN
      MCARD m, 1, "Resume", ""
    ELSE
      MCARD m, 1, "Pause", ""
    END IF
    MCARD m, 2, "Volume up", ""
    MCARD m, 3, "Volume down", ""
    MCARD m, 4, "Stop", ""
    MCARD m, 5, "Back to list", ""
    IF state = 0 THEN
      MENABLE m, 1, 0
      MENABLE m, 4, 0
    END IF
    c = MENU(m)
    IF c = 0 OR c = 5 THEN
      EXIT ctl
    END IF
    IF c = 1 AND state = 1 THEN
      MUSIC PAUSE
      state = 2
    ELSE
      IF c = 1 AND state = 2 THEN
        MUSIC RESUME
        state = 1
      END IF
    END IF
    IF c = 2 THEN
      vol = vol + 16
      IF vol > 255 THEN
        vol = 255
      END IF
      VOLUME vol
    END IF
    IF c = 3 THEN
      vol = vol - 16
      IF vol < 0 THEN
        vol = 0
      END IF
      VOLUME vol
    END IF
    IF c = 4 AND state <> 0 THEN
      MUSIC STOP
      state = 0
      EXIT ctl
    END IF
  END REPeat ctl
END DEFine Controls

DEFine PROCedure Screen
  LOCal s$
  PAPER 0
  INK 7
  CLS
  INK 6
  AT 1, 1
  PRINT "QL/32 MUSIC PLAYER"
  INK 7
  AT 3, 1
  PRINT "Song  : "; now$
  s$ = "stopped"
  IF state = 1 THEN
    s$ = "playing"
  END IF
  IF state = 2 THEN
    s$ = "paused"
  END IF
  AT 4, 1
  PRINT "State : "; s$
  AT 5, 1
  PRINT "Volume: "; vol; "  ["; REPT$("#", INT(vol / 16)); REPT$("-", 16 - INT(vol / 16)); "]"
END DEFine Screen
