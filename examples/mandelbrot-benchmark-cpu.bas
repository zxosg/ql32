' CPU-only companion to mandelbrot-benchmark.bas.
' Keep the 80x56, 24-iteration escape calculation; omit VGA drawing.
CONST KOptMathSingle = 1
CONST KEspCpuFreq240 = 240
OPTION KOptMathSingle
OPTION KEspCpuFreq240

benchmarkChannel = OPEN(2)
PRINT #benchmarkChannel,"MANDELBROT_CPUFREQ=";CPUFREQ()
PRINT #benchmarkChannel,"MANDELBROT_CPU_START"

checksum = 0
FOR py = 0 TO 55
  cy = (28 - py) / 21
  FOR px = 0 TO 79
    cx = (px - 40) / 22 - 0.5
    zx = 0
    zy = 0
    iter = 0
    FOR it = 1 TO 24
      IF zx * zx + zy * zy <= 4 THEN
        temp = zx * zx - zy * zy + cx
        zy = 2 * zx * zy + cy
        zx = temp
        iter = iter + 1
      END IF
    END FOR it
    pixelShade = 0
    IF iter < 24 THEN
      pixelShade = iter MOD 7 + 1
    END IF
    checksum = checksum + pixelShade
  END FOR px
END FOR py

PRINT #benchmarkChannel,"MANDELBROT_CHECKSUM=";checksum
PRINT #benchmarkChannel,"MANDELBROT_CPU_DONE"
