' QL/32 common selector constants
CONST KChannelCount = 0
CONST KChannelHandle = 1
CONST KChannelType = 2
' OPTION math precision values
CONST KOptMathDouble = 0
CONST KOptMathSingle = 1
' ESP32 CPU frequency options in MHz; ignored on host targets
CONST KEspCpuFreq80 = 80
CONST KEspCpuFreq160 = 160
CONST KEspCpuFreq240 = 240
' Graphics update modes for GUPDATE
CONST KGupdateOff = 0
CONST KGupdateOn = 1
CONST KGupdateAuto = 1
' OPEN device kinds
CONST KDeviceFile = 1
CONST KDeviceSerial = 2
CONST KDeviceNull = 3
CONST KDeviceWindow = 4
CONST KDeviceGPIO = 5
CONST KDeviceADC = 6
CONST KDeviceSPI = 7
CONST KDeviceI2C = 8
CONST KDeviceParallel = 9
CONST KDeviceIrDA = 10
CONST KDeviceCapTouch = 11
CONST KDeviceTimer = 12
' GPIO modes and pulls
CONST KPinInput = 0
CONST KPinOutput = 1
CONST KPinInputOutput = 2
CONST KPinMode = 100
CONST KPinInitialValue = 101
CONST KPullNone = 0
CONST KPullUp = 1
CONST KPullDown = 2
' ADC resolution and timer modes
CONST KAdcResolution12 = 12
CONST KTimerOneShot = 0
CONST KTimerPeriodic = 1
' Serial configuration
CONST KSerial8N1 = 0
CONST KFlowNone = 0
CONST KFlowRtsCts = 1
CONST KFlowXonXoff = 2
' FREE() heap memory selectors
CONST KFreeTotal = 0
CONST KFreeContiguous = 1
CONST KFreeFragments = 2
' TASK and GETTASK selectors, plus TASKRESULT selectors
CONST KTaskWait = 1
CONST KTaskResultStatus = 1
CONST KTaskResultChannels = 2
CONST KTaskResultErrorCode = 3
CONST KTaskResultErrorDetail = 4
CONST KTaskResultKind = 5
CONST KTaskResultPriority = 6
CONST KTaskResultResumePriority = 7
CONST KTaskResultCanvasOriginX = 8
CONST KTaskResultCanvasOriginY = 9
CONST KTaskResultCanvasWidth = 10
CONST KTaskResultCanvasHeight = 11
CONST KTaskCount = 0
CONST KTaskHandle = 1
