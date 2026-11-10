WINDOW 0,0,160,120
PAPER 0
CLS
GUPDATE 0

np = 8
i = 0
cr = 4
hh = 60
hw = 80
mr = hh-cr*2
piVal = 3.141592653589793

REPeat animation
  OVER 0
  CLS

  INK 7
  CIRCLE hw,hh,hh-2,1

  INK 2
  FOR f = 0 TO np*2-1
    a = (piVal/np)*f
    x = INT(mr*SIN(a)+hw)
    y = INT(mr*COS(a)+hh)
    LINE x,y,hw,hh
  END FOR f

  INK 7
  CIRCLE hw,hh,10,1

  OVER 2
  FOR f = 0 TO np-1
    a = (piVal/np)*f
    r = SIN(i+a)*mr
    x = INT(r*SIN(a)+hw)
    y = INT(r*COS(a)+hh)
    CIRCLE x,y,cr,1
  END FOR f

  OVER 0
  GUPDATE
  PAUSE 1
  i = i+0.03
END REPeat animation