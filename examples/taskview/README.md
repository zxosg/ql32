# TASKVIEW offset bounce demo

Copy both BASIC files to the CYD's `FFS:/` root (or change the drive prefix to
`SDC:/` when using an SD card). Load and run the supervisor driver:

```text
LOAD "FFS:/bounce.bas"
RUN
```

The worker draws a 320×240 sine/cosine surface sampled every four pixels. The
driver starts it asynchronously and updates its view in a bounded loop.
`PAUSE 100` keeps each one-pixel pan step visible; the 1200-step pass runs for
about two minutes on the CYD. The
160×120 viewport is anchored at `(80,60)`; `offsetX` moves between `0` and
`-160`, and `offsetY` moves between `0` and `-120`, so the crop bounces across
the four canvas edges.
After the demo loop ends, the worker remains available at its own prompt.

`KILL id` stops the worker; use the ID assigned by `TASK` in the driver.
