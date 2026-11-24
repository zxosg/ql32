' Supervisor TILE smoke test: three retained window channels, then a 2x2 layout.
w1% = OPEN(4)
w2% = OPEN(4)
w3% = OPEN(4)
WINDOW #w1%, 0, 0, 320, 200
WINDOW #w2%, 0, 0, 320, 200
WINDOW #w3%, 0, 0, 320, 200
PAPER #w1%, 1
CLS #w1%
PAPER #w2%, 2
CLS #w2%
PAPER #w3%, 4
CLS #w3%
TILE
PRINT #0, "TILE MULTIWINDOW PASS"
PAUSE 60000
CLOSE w1%
CLOSE w2%
CLOSE w3%
