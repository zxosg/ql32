REM Launch a full-screen sine/cosine worker and pan a half-screen crop.
workerId = TASK("FFS:/sincos2d.bas")
offsetX = 0
offsetY = 0
directionX = -1
directionY = -1
FOR frame = 1 TO 1200
  TASKVIEW workerId,80,60,160,120,offsetX,offsetY
  PAUSE 100
  offsetX = offsetX + directionX
  offsetY = offsetY + directionY
  IF offsetX <= -160 THEN
    offsetX = -160
    directionX = 1
  END IF
  IF offsetX >= 0 THEN
    offsetX = 0
    directionX = -1
  END IF
  IF offsetY <= -120 THEN
    offsetY = -120
    directionY = 1
  END IF
  IF offsetY >= 0 THEN
    offsetY = 0
    directionY = -1
  END IF
END FOR frame
