' Supervisor MENABLE visual smoke test: the second item must be disabled.
menu% = MINIT("MENABLE visual test")
MCARD menu%, 10, "Enabled item", "E"
MCARD menu%, 20, "Disabled item", "D"
MENABLE menu%, 20, 0
PRINT #0, "MENABLE TEST READY"
selected% = MENU(menu%)
PRINT #0, "MENU RETURNED"
