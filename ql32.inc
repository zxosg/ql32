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
' FREE() heap memory selectors
CONST KFreeTotal = 0
CONST KFreeContiguous = 1
CONST KFreeFragments = 2
' TASK start flags and TASKRESULT selectors
CONST KTaskWait = 1
CONST KTaskResultStatus = 1
CONST KTaskResultChannels = 2
CONST KTaskResultErrorCode = 3
CONST KTaskResultErrorDetail = 4
CONST KTaskResultKind = 5
CONST KTaskResultPriority = 6
CONST KTaskResultResumePriority = 7
