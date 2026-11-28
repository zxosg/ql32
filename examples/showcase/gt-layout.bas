REM The Grand Tour layout helpers. INCLUDEd into every task, so keep this tiny.
REM Display size comes from SYSINFO$(3) ("name WxH format") because a task's own window is not the screen.
DEFine FuNction DispW()
  LOCal d$, x
  d$ = MID$(SYSINFO$(3), INSTR(SYSINFO$(3), " ") + 1) + " "
  x = INSTR(d$, "x")
  IF x < 2 THEN RETurn 320
  RETurn VAL(LEFT$(d$, x - 1))
END DEFine DispW

DEFine FuNction DispH()
  LOCal d$, x
  d$ = MID$(SYSINFO$(3), INSTR(SYSINFO$(3), " ") + 1) + " "
  x = INSTR(d$, "x")
  IF x < 2 THEN RETurn 240
  RETurn VAL(MID$(d$, x + 1, INSTR(d$, " ") - x - 1))
END DEFine DispH

DEFine FuNction Cols()
  IF DispW() < 500 THEN RETurn 2
  RETurn 3
END DEFine Cols

DEFine FuNction TileW()
  RETurn INT(DispW() / Cols())
END DEFine TileW

DEFine FuNction TileH()
  RETurn INT(TileW() * 9 / 16)
END DEFine TileH

DEFine FuNction StageH()
  RETurn TileH() * 2
END DEFine StageH
