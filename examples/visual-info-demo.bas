INCLUDE "ql32.inc"

' PRINT color chain: commas after the text are regular PRINT separators.
CLS
PRINT INK 7, PAPER 0, "QL/32 visual output demo"
PRINT INK 2, PAPER 7, "PRINT: ink 2, paper 7"
PRINT INK 4, PAPER 0, "PRINT: ink 4, paper 0"
PRINT INK 1, PAPER 6, "PRINT: ink 1, paper 6"
PRINT INK 7, PAPER 0, ""
PAUSE 1500

' IPRINT is non-blocking; allow each four-second toast to finish.
IPRINT INK 2, PAPER 7, FLASH KInfoFlashOff, "IPRINT steady", 4, KDlgPosTopLeft
PAUSE 4200
IPRINT INK 3, PAPER 6, FLASH KInfoFlashText, "IPRINT text blink", 4, KDlgPosTopRight
PAUSE 4200
IPRINT INK 1, PAPER 7, FLASH KInfoFlashPaperInk, "IPRINT color swap", 4, KDlgPosBottomRight
PAUSE 4200

' BUSY appears after its three-second delay and stays until BUSY OFF.
BUSY INK 2, PAPER 7, FLASH KInfoFlashOff, "BUSY steady", KDlgPosTopLeft
PAUSE 6500
BUSY OFF
BUSY INK 3, PAPER 6, FLASH KInfoFlashText, "BUSY text blink", KDlgPosTopRight
PAUSE 6500
BUSY OFF
BUSY INK 1, PAPER 7, FLASH KInfoFlashPaperInk, "BUSY color swap", KDlgPosBottomRight
PAUSE 6500
BUSY OFF

PRINT INK 7, PAPER 0, "Visual output demo finished"
