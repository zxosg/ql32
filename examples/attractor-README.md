# Animated attractor

`attractor.bas` ports the supplied p5.js recurrence to QL/32. It keeps the
stateful `x`, `v`, and `t` updates and centers/scales the pattern to the active
graphics window. `rowStep=4` and `sampleStep=8` reduce draw calls on ESP32; set
them to `2` and `1` for the source sketch's full point density. `SIN()` and
`COS()` use radians; `INT()` maps positions to pixel coordinates.
The demo selects `OPTION KOptMathDouble` and `OPTION KEspCpuFreq240` in its
module preamble. On the tested Olimex WROVER, DOUBLE and SINGLE had nearly
identical render times at 240 MHz, so the demo keeps DOUBLE precision. The
clock option changes the ESP32's device-wide CPU frequency; it is ignored by
Linux and macOS builds. `CPUFREQ()` reports the current clock in MHz on ESP32
and returns zero on host builds. See the
[performance results](../docs/test-results/attractor-performance-2026-11-12.md).

The p5 sketch uses a custom 16-entry RGB565 table and 3-pixel filled circles.
QL/32 uses `PLOT` to draw one pixel and cycles through the seven non-black QL
colors. The recurrence and movement are preserved; the palette and point size
are approximated.

To run it on Linux, build the host target and launch:

```sh
./build/linux/ql32 examples/attractor.bas
```

The Linux graphics presenter writes the composed image to `ql32-screen.ppm`.
On an ESP32 board, copy `attractor.bas` to the available `SDC:` or `FFS:` drive.
At the supervisor prompt, set `SLICE 2048` and start it as a worker with
`TASK "FFS:/attractor.bas"` (or use the matching `SDC:` path). The larger slice
makes the frame loop smoother while the supervisor remains available. The
task ID is printed on the display; stop it with `KILL id`.
