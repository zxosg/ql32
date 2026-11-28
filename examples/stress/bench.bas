o = OPEN(2)
PRINT #o,"BENCH|cpufreq|";CPUFREQ()
t0 = TICKS()
c = 0
FOR py = 0 TO 27
  cy = (14 - py) / 10
  FOR px = 0 TO 39
    cx = (px - 20) / 11 - 0.5
    zx = 0
    zy = 0
    iter = 0
    FOR it = 1 TO 24
      IF zx * zx + zy * zy <= 4 THEN
        t = zx * zx - zy * zy + cx
        zy = 2 * zx * zy + cy
        zx = t
        iter = iter + 1
      END IF
    END FOR it
    c = c + iter
  END FOR px
END FOR py
PRINT #o,"BENCH|mandel40x28|";TICKS()-t0;"|ms|chk";c
t0 = TICKS()
s = 0
FOR i = 1 TO 100000
  s = s + i MOD 7
END FOR i
PRINT #o,"BENCH|intloop100k|";TICKS()-t0;"|ms|";s
t0 = TICKS()
DIM a(2000)
FOR k = 1 TO 20
  FOR i = 0 TO 2000
    a(i) = i * k
  END FOR i
END FOR k
PRINT #o,"BENCH|array40k|";TICKS()-t0;"|ms"
t0 = TICKS()
x$ = ""
FOR i = 1 TO 300
  x$ = x$ + "ab"
END FOR i
FOR i = 1 TO 5000
  q$ = x$(1 TO 20) + STR$(i)
END FOR i
PRINT #o,"BENCH|string5k|";TICKS()-t0;"|ms"
PRINT #o,"BENCH|DONE"
