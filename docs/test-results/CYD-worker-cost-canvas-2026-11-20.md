# Per-worker memory cost and canvas admission

## Interpreter/worker cost (bytes of free heap)

Host (malloc-hook, `ql32_worker_cost_probe`, no graphics): launch 2.5-2.7 KB
(Interpreter + Task shell); first slice +2.4 KB (mandel, sincos) or +11 KB
(churn: `DIM(255)` and strings); compile transient peak 17-34 KB.

CYD (`HEAP STATS`, idle 145,556 free / 94,196 largest):

| Program | Delta after launch + first slice | Largest after |
| --- | ---: | ---: |
| churn.bas (no canvas) | 8,668 | 94,196 |
| sincos.bas (canvas) | 44,584 | 55,284 |
| mandel.bas (canvas) | 50,168 | 55,284 |

The Interpreter itself is a few KB; the canvas (38,400 B full screen) dominates.

## Canvas admission (CYD, 8 KB headroom)

Three full-screen `w.bas` workers: free 145 KB -> 103 KB -> 60 KB (largest
17 KB). The 4th-6th now refuse before allocating:
`not enough memory for a 38400 byte canvas: need 46592 bytes free (38400 contiguous), have 56564 free, 17396 largest`.
`TILE` then shrinks the canvases in place: free 56 KB -> 139 KB.
Launch, `TILE`, repeat four times, then `MUSIC PLAY`: all four run and music
starts (137 KB -> 106 KB free, no failed allocations).

## Worker canvases: lazy and tile-sized (follow-up)

A window canvas is allocated on a task's first graphics call, so text-only
workers cost no canvas (churn.bas: 8.7 KB total). In `WINDOWMODE ISOLATED`
new workers get a tile-sized canvas from the start (no full-screen allocate
then shrink). CYD, `w.bas` workers, free heap after each launch from 145 KB idle:

| Workers | Shared (full-screen, then TILE) | Isolated |
| ---: | ---: | ---: |
| 1 | 141 KB | 122 KB |
| 2 | 143 KB | 108 KB |
| 3 | 133 KB | 94 KB |
| 4 | 137 KB | 83 KB |
| 6 | n/a (4th refused untiled) | 64 KB, largest 61 KB |

Shared+TILE ends with more free memory only because its `w.bas` windows are
re-tiled to small canvases; the isolated numbers include the supervisor's
shrink to a tile and run with no full-screen peak, so a 4th/5th/6th worker
never needs a 38 KB block. Music after 6 isolated workers: 32 KB free,
no failed allocations. The 4-workers + music run in isolated mode was skipped
at the user's request.
