dialogHandle = DINIT("Profile")
DTEXT dialogHandle, "Edit the profile"
DEDIT dialogHandle, "Name", "Ada"
DINT dialogHandle, "Age", 36, 0, 150
DFLOAT dialogHandle, "Score", 1.5, 0, 10
DCHOICE dialogHandle, "Color", 2, "Red|Green|Blue"
DCHECK dialogHandle, "Active", 1
DBUTTONS dialogHandle, "Apply", "Cancel"

IF DIALOG(dialogHandle) THEN
  PRINT "Name: "; DGET$(dialogHandle, 2)
  PRINT "Age: "; DGET(dialogHandle, 3)
  PRINT "Score: "; DGET(dialogHandle, 4)
  PRINT "Color option: "; DGET(dialogHandle, 5)
  PRINT "Active: "; DGET(dialogHandle, 6)
ELSE
  PRINT "Dialog cancelled"
END IF

menuHandle = MINIT("Main menu")
MCARD menuHandle, 10, "Open", "O"
MCARD menuHandle, 20, "Save", "S"
MCARD menuHandle, 90, "Exit", "X"
command = MENU(menuHandle)
PRINT "Selected command: "; command
