' Exercise combinable DINIT layout and corner options on a graphical display.
INCLUDE "ql32.inc"
passed = 1
probe = OPEN(2)

dialogHandle = DINIT("Centered default")
DTEXT dialogHandle, "This dialog uses the default centered position. Press Enter."
result = DIALOG(dialogHandle)
IF result <> 1 THEN
  passed = 0
END IF
PRINT "Centered default result: "; result

dialogHandle = DINIT("Top left", BOR(BOR(KDlgNoTitle, KDlgHideButtons), KDlgPosTopLeft))
DTEXT dialogHandle, "Press Esc to cancel; no buttons or title are shown."
result = DIALOG(dialogHandle)
IF result <> 0 THEN
  passed = 0
END IF
PRINT "Top left hidden buttons result: "; result

dialogHandle = DINIT("Top right", BOR(BOR(KDlgButRight, KDlgDensePack), KDlgPosTopRight))
DTEXT dialogHandle, "Buttons appear at the right. Press Enter."
DBUTTONS dialogHandle, "Continue", "Cancel"
result = DIALOG(dialogHandle)
IF result <> 1 THEN
  passed = 0
END IF
PRINT "Top right result: "; result

dialogHandle = DINIT("Bottom left", BOR(KDlgNoDrag, KDlgPosBottomLeft))
DTEXT dialogHandle, "Bottom-left position. Press Enter."
result = DIALOG(dialogHandle)
IF result <> 1 THEN
  passed = 0
END IF
PRINT "Bottom left result: "; result

dialogHandle = DINIT("Bottom right", KDlgPosBottomRight)
DTEXT dialogHandle, "Bottom-right position. Press Enter."
result = DIALOG(dialogHandle)
IF result <> 1 THEN
  passed = 0
END IF
PRINT "Bottom right result: "; result

dialogHandle = DINIT("Full screen", BOR(KDlgFillScreen, KDlgNoTitle))
DTEXT dialogHandle, "Full-screen layout. Press Enter."
result = DIALOG(dialogHandle)
IF result <> 1 THEN
  passed = 0
END IF
PRINT "Full screen result: "; result
IF passed THEN
  PRINT "DINIT options test PASS"
ELSE
  PRINT "DINIT options test FAIL"
END IF
PRINT #probe, "DINIT_OPTIONS|";passed
CLOSE probe
