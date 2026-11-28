# Scheduler batching (time-budgeted VM slices per loop())

`loop()` now runs `tickBatch()` (include/ql32/vm/tick_batch.hpp): several 128-instruction slices per
loop, up to 6 ms, stopping when idle or a UI-waiting task exists. `delay(1)` only when nothing ran or
every 32 ms; otherwise `yield()`. Applied to CYD, Tenstar and FabGL (FabGL: built only, not hardware verified).

`examples/stress/bench.bas`, milliseconds (lower is better):

| Workload | Tenstar before | Tenstar after | CYD before | CYD after |
|---|---|---|---|---|
| mandel40x28 | 9283 | 5138 | 15783 | 8881 |
| intloop100k | 10959 | 3185 | 16408 | 13129 |
| array40k | 3762 | 1175 | 5633 | 4257 |
| string5k | 6446 | 6229 | 9167 | 8368 |

Gates (with batching):
- Golden 16/16 on CYD and Tenstar.
- CYD stress 1-6 tasks: idle 173K/110.6K, 6 tasks 116K/106K, back to 172.6K/110.6K, failedalloc=0, 60 s soak stable.
- Tenstar stress 1-6 tasks: failedalloc=0, internal heap recovers after KILL.
- CYD music (debug bar on): KNUCKLE.YM and LyraII1.vtx alone, and with mandelbrot / sincos grid in
  parallel: musiclate=0, musicwriteerr=0, failedalloc=0, musicmaxgap 11.7 ms (11664 us alone, 11707 us loaded).
