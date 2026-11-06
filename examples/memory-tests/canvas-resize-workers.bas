REM Exercise three full-screen canvas allocations and releases on the CYD.
serial_probe = OPEN(2)
array_worker = TASK("FFS:/arrwrk.bas")
channel_worker = TASK("FFS:/chanwrk.bas")
PRINT #serial_probe, "WORKERS|started|"; array_worker; ","; channel_worker; "|"; FREE(0); ","; FREE(1)
FOR cycle = 1 TO 3
  PRINT #serial_probe, "MBFULL|start|"; cycle; "|"; FREE(0); ","; FREE(1)
  GUPDATE 0
  WINDOW #1,0,0,160,120
  WINDOW #1,0,0,320,240
  PAPER #1,0
  INK #1,7
  CLS #1
  GUPDATE 1
  IF cycle = 1 THEN
    FOR py = 0 TO 239
      cy = (120 - py) / 90
      shade = -1
      runStart = 0
      FOR px = 0 TO 319
        cx = (px - 160) / 110 - 0.5
        zx = 0
        zy = 0
        iter = 0
        FOR it = 1 TO 8
          IF zx * zx + zy * zy <= 4 THEN
            temp = zx * zx - zy * zy + cx
            zy = 2 * zx * zy + cy
            zx = temp
            iter = iter + 1
          END IF
        END FOR it
        pixelShade = 0
        IF iter < 8 THEN
          pixelShade = iter MOD 7 + 1
        END IF
        IF px = 0 THEN
          shade = pixelShade
          runStart = 0
        ELSE
          IF pixelShade <> shade THEN
            INK #1,shade
            LINE #1,runStart,py,px-1,py
            shade = pixelShade
            runStart = px
          END IF
        END IF
      END FOR px
      INK #1,shade
      LINE #1,runStart,py,319,py
      PRINT #0, "MBFULL ROW "; py
    END FOR py
  END IF
  PRINT #serial_probe, "MBFULL|rendered|"; cycle
  PRINT #serial_probe, "MBFULL|shrink|"; cycle; "|"; FREE(0); ","; FREE(1)
  GUPDATE 0
  WINDOW #1,0,0,160,192
  CLS #1
  GUPDATE 1
  PRINT #serial_probe, "MBFULL|released|"; cycle; "|"; FREE(0); ","; FREE(1)
END FOR cycle
PRINT #serial_probe, "MBFULL|done|"; FREE(0); ","; FREE(1)
PRINT #serial_probe, "WORKERS|finished|"; array_worker; ","; channel_worker
