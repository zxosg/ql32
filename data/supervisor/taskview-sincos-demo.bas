' Start six sin/cos workers, tile their task canvases, then let the user
' adjust each viewport. Copy this file and sincos-worker.bas to the same drive.
INCLUDE "ql32.inc"
workerTotal = 6
DIM workerIds%(5)

screenWidth = GWIDTH()
screenHeight = GHEIGHT()
tileWidth = INT(screenWidth / 3)
tileHeight = INT(screenHeight / 2)

PRINT #0,"Starting ";workerTotal;" sin/cos workers"
FOR slot = 0 TO workerTotal - 1
  workerIds%(slot) = TASK("sincos-worker.bas")
  originX = (slot MOD 3) * tileWidth
  originY = INT(slot / 3) * tileHeight
  TASKVIEW workerIds%(slot),originX,originY,tileWidth,tileHeight
END FOR slot

' Open the preview after the workers so its small dashboard stays on top.
preview = OPEN(KDeviceWindow)
WINDOW #preview,0,0,80,48
PAPER #preview,0
CLS #preview
FOR slot = 0 TO workerTotal - 1
  taskId = workerIds%(slot)
  originX = TASKRESULT(taskId,KTaskResultCanvasOriginX)
  originY = TASKRESULT(taskId,KTaskResultCanvasOriginY)
  canvasWidth = TASKRESULT(taskId,KTaskResultCanvasWidth)
  canvasHeight = TASKRESULT(taskId,KTaskResultCanvasHeight)
  previewX = 2 + INT(originX * 72 / screenWidth)
  previewY = 2 + INT(originY * 40 / screenHeight)
  previewWidth = INT(canvasWidth * 72 / screenWidth)
  previewHeight = INT(canvasHeight * 40 / screenHeight)
  IF previewWidth < 2 THEN
    previewWidth = 2
  END IF
  IF previewHeight < 2 THEN
    previewHeight = 2
  END IF
  INK #preview,slot + 1
  RECT #preview,previewX,previewY,previewWidth,previewHeight
  IF slot = 0 THEN
    TEXT #preview,previewX + 2,previewY + 2,"1"
  END IF
  IF slot = 1 THEN
    TEXT #preview,previewX + 2,previewY + 2,"2"
  END IF
  IF slot = 2 THEN
    TEXT #preview,previewX + 2,previewY + 2,"3"
  END IF
  IF slot = 3 THEN
    TEXT #preview,previewX + 2,previewY + 2,"4"
  END IF
  IF slot = 4 THEN
    TEXT #preview,previewX + 2,previewY + 2,"5"
  END IF
  IF slot = 5 THEN
    TEXT #preview,previewX + 2,previewY + 2,"6"
  END IF
END FOR slot

' Count live non-supervisor tasks from the task table; terminal task records
' remain enumerable until killed, so exclude their status explicitly.
taskRecords = GETTASK(KTaskCount)
runningWorkers = 0
FOR position = 0 TO taskRecords - 1
  taskId = GETTASK(KTaskHandle,position)
  kind$ = TASKRESULT(taskId,KTaskResultKind)
  state$ = TASKRESULT(taskId,KTaskResultStatus)
  IF kind$ <> "SUPERVISOR" THEN
    IF state$ <> "COMPLETED" THEN
      IF state$ <> "FAILED" THEN
        IF state$ <> "CANCELLED" THEN
          runningWorkers = runningWorkers + 1
        END IF
      END IF
    END IF
  END IF
END FOR position
PRINT #0,"Running worker tasks: ";runningWorkers
PRINT #0,"Enter 1 to move a task, 0 to keep its current tile."

FOR slot = 0 TO workerTotal - 1
  taskId = workerIds%(slot)
  originX = TASKRESULT(taskId,KTaskResultCanvasOriginX)
  originY = TASKRESULT(taskId,KTaskResultCanvasOriginY)
  canvasWidth = TASKRESULT(taskId,KTaskResultCanvasWidth)
  canvasHeight = TASKRESULT(taskId,KTaskResultCanvasHeight)
  PRINT #0,"Task ID ";taskId;"  preview #";slot + 1;"  ";originX;",";originY;"  ";canvasWidth;"x";canvasHeight
  INPUT #0,"Move this task? 1/0: ",#0,moveTask
  IF moveTask = 1 THEN
    INPUT #0,"New origin X: ",#0,originX
    INPUT #0,"New origin Y: ",#0,originY
    TASKVIEW taskId,originX,originY,tileWidth,tileHeight
    CLS #preview
    FOR previewSlot = 0 TO workerTotal - 1
      previewTaskId = workerIds%(previewSlot)
      previewOriginX = TASKRESULT(previewTaskId,KTaskResultCanvasOriginX)
      previewOriginY = TASKRESULT(previewTaskId,KTaskResultCanvasOriginY)
      previewCanvasWidth = TASKRESULT(previewTaskId,KTaskResultCanvasWidth)
      previewCanvasHeight = TASKRESULT(previewTaskId,KTaskResultCanvasHeight)
      previewX = 2 + INT(previewOriginX * 72 / screenWidth)
      previewY = 2 + INT(previewOriginY * 40 / screenHeight)
      previewWidth = INT(previewCanvasWidth * 72 / screenWidth)
      previewHeight = INT(previewCanvasHeight * 40 / screenHeight)
      IF previewWidth < 2 THEN
        previewWidth = 2
      END IF
      IF previewHeight < 2 THEN
        previewHeight = 2
      END IF
      INK #preview,previewSlot + 1
      RECT #preview,previewX,previewY,previewWidth,previewHeight
      IF previewSlot = 0 THEN
        TEXT #preview,previewX + 2,previewY + 2,"1"
      END IF
      IF previewSlot = 1 THEN
        TEXT #preview,previewX + 2,previewY + 2,"2"
      END IF
      IF previewSlot = 2 THEN
        TEXT #preview,previewX + 2,previewY + 2,"3"
      END IF
      IF previewSlot = 3 THEN
        TEXT #preview,previewX + 2,previewY + 2,"4"
      END IF
      IF previewSlot = 4 THEN
        TEXT #preview,previewX + 2,previewY + 2,"5"
      END IF
      IF previewSlot = 5 THEN
        TEXT #preview,previewX + 2,previewY + 2,"6"
      END IF
    END FOR previewSlot
  END IF
END FOR slot

' Report the final active worker count after all viewport changes.
taskRecords = GETTASK(KTaskCount)
runningWorkers = 0
FOR position = 0 TO taskRecords - 1
  taskId = GETTASK(KTaskHandle,position)
  kind$ = TASKRESULT(taskId,KTaskResultKind)
  state$ = TASKRESULT(taskId,KTaskResultStatus)
  IF kind$ <> "SUPERVISOR" THEN
    IF state$ <> "COMPLETED" THEN
      IF state$ <> "FAILED" THEN
        IF state$ <> "CANCELLED" THEN
          runningWorkers = runningWorkers + 1
        END IF
      END IF
    END IF
  END IF
END FOR position
PRINT #0,"Viewport test complete; running workers: ";runningWorkers
