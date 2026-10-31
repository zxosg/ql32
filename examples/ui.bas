dialog = DINIT("Profile")
DTEXT(dialog, "Edit the profile")
DEDIT(dialog, "Name", "Ada")
DINT(dialog, "Age", 36, 0, 150)
DFLOAT(dialog, "Score", 1.5, 0, 10)
DCHOICE(dialog, "Color", 2, "Red|Green|Blue")
DCHECK(dialog, "Active", 1)
DBUTTONS(dialog, "Apply", "Cancel")

IF DIALOG(dialog) THEN
  PRINT "Name: "; DGET$(dialog, 2)
  PRINT "Age: "; DGET(dialog, 3)
  PRINT "Score: "; DGET(dialog, 4)
  PRINT "Color option: "; DGET(dialog, 5)
  PRINT "Active: "; DGET(dialog, 6)
ELSE
  PRINT "Dialog cancelled"
END IF

menu = MINIT("Main menu")
MCARD(menu, 10, "Open", "O")
MCARD(menu, 20, "Save", "S")
MCARD(menu, 90, "Exit", "X")
command = MENU(menu)
PRINT "Selected command: "; command
