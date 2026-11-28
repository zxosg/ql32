REM The Grand Tour layout helpers. INCLUDEd into every task, so keep this tiny.
REM Display size comes from CAPABILITY("display.width"/"display.height") because a task's own window is not the screen.
DEFine FuNction DispW()
  RETurn CAPABILITY("display.width")
END DEFine DispW

DEFine FuNction DispH()
  RETurn CAPABILITY("display.height")
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
