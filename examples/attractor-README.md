# Animated attractor

`attractor.bas` ports the supplied p5.js recurrence to QL/32. It keeps the
stateful `x`, `v`, and `t` updates and centers/scales the pattern to the active
graphics window. `rowStep=4` and `sampleStep=8` reduce draw calls on ESP32; set
them to `2` and `1` for the source sketch's full point density. `SIN()` and
`COS()` use radians; `INT()` maps positions to pixel coordinates.

The p5 sketch uses a custom 16-entry RGB565 table and filled circles. QL/32
currently exposes the eight QL colors and outline-only `CIRCLE`, so this port
cycles through the seven non-black QL colors and uses radius-1 circles as small
points. The pattern and movement are preserved; the exact colors and dot fill
are approximated.

To run it on Linux, build the host target and launch:

```sh
./build/linux/ql32 examples/attractor.bas
```

The Linux graphics presenter writes the composed image to `ql32-screen.ppm`.
On an ESP32 board, copy `attractor.bas` to the SD card. At the supervisor
prompt, set `SLICE 2048` and start it as a worker with `TASK "attractor.bas"`.
The larger slice makes the frame loop smoother while the supervisor remains
available. The task ID is printed on the display; stop it with `KILL id`.
