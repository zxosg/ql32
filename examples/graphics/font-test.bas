REM Print every printable ASCII character in each built-in font and style.
REM Run from the same drive as ql32.inc. Metrics and progress go to serial.
INCLUDE "ql32.inc"
serial_probe = OPEN(2)
DIM names$(3)
names$(0) = "SMALL"
names$(1) = "NORMAL"
names$(2) = "LARGE"
names$(3) = "TITLE"

FOR f = 0 TO 3
  handle = KFontSmall + f
  FOR style = 0 TO 3
    FONT handle,style
    CLS
    PRINT names$(f);" style ";style
    PRINT "w=";GETFONT(KFontMetricWidth,handle);" h=";GETFONT(KFontMetricHeight,handle)
    line$ = ""
    FOR c = 32 TO 126
      line$ = line$ + CHR$(c)
    END FOR c
    PRINT line$
    PRINT #serial_probe,"FONT|";names$(f);"|";style;"|";GETFONT(KFontMetricWidth,handle);"x";GETFONT(KFontMetricHeight,handle);"|chars=";GETFONT(KFontMetricCharacterCount,handle);"|free=";FREE(0)
    PAUSE 150
  END FOR style
END FOR f
FONT KFontDefault,0
PRINT "FONT TEST DONE"
PRINT #serial_probe,"FONT|DONE"
