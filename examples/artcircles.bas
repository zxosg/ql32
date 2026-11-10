WINDOW 0,0,160,120
PAPER 0
INK 7
CLS
GUPDATE 0
OVER 2

cx = 80
cy = 60
s = 60/1905

RESTORE
FOR f = 1 TO 6
  READ x,y,r
  x = x*s
  y = y*s
  r = r*s

  CIRCLE cx+x,cy+y,r,1

  IF f > 1 THEN
    CIRCLE cx+x,cy-y+1,r,1
    IF f > 2 THEN
      CIRCLE cx-x+1,cy+y,r,1
      CIRCLE cx-x+1,cy-y+1,r,1
    END IF
  END IF
END FOR f

OVER 0
GUPDATE 1
GUPDATE

DATA 0,0,210
DATA 0,-240,230
DATA -100,-465,250
DATA -200,-740,300
DATA -300,-1100,350
DATA -400,-1500,400