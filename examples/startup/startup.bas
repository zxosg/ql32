REM QL/32 boot splash for the CYD: copy to FFS:/startup.bas to run at boot.
REM Large QL32 title, then version, free memory and platform in the small font.
REM Font handles: 1 small, 4 title. Selectors: SYSINFO$ 0 version, 2 platform.
GUPDATE 0
PAPER 0
INK 7
CLS
w = GWIDTH()
h = GHEIGHT()
GFONT 4, 1
tw = GETFONT(0, 4) * 4
y0 = h / 2 - 40
INK 2
x = (w - tw) / 2
TEXT x + 2, y0 + 2, "QL32"
INK 6
TEXT x, y0, "QL32"
GFONT 1, 0
INK 7
v$ = "Version " + SYSINFO$(0)
x = (w - LEN(v$) * 6) / 2
TEXT x, y0 + 44, v$
m$ = "Free memory " + STR$(INT(FREE(0) / 1024)) + "K"
x = (w - LEN(m$) * 6) / 2
TEXT x, y0 + 56, m$
p$ = SYSINFO$(2)
x = (w - LEN(p$) * 6) / 2
TEXT x, y0 + 68, p$
GUPDATE 1
GUPDATE
REM The finished window stays on screen as a frozen image and is reclaimed
REM automatically when memory is needed. Type NEW to drop this program's source.
