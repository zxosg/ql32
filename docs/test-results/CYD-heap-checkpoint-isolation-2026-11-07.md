# CYD heap checkpoint isolation — 2026-11-07

## Method

Rebooted the CYD to clear the supervisor REPL state, captured `HEAP` maps at an idle prompt, after the first `PRINT FREE()`, with one `FFS:/sincos64-grid.bas` worker active, after cancelling it, and after reaping its terminal task record. FFS was not written or formatted. Full dumps were taken only at quiet checkpoints or with one worker active.

## Findings

- The boot-idle map had a single 112,960 B free block in heap region `0x3ffe4350`.
- The first `PRINT FREE()` added a live 38,412 B block at `0x3ffcd15c`. The BASIC interpreter's default output channel is 1; that output goes through `WindowedService::writeText`, which creates a 4-bit graphics window at the screen-sized default bounds. At 320×240, the payload is exactly 38,400 B; the remaining 12 B are allocator overhead. The full-screen canvas is owned by the persistent supervisor session, so it remains after worker cleanup. This is expected session-owned graphics memory, not a worker leak.
- The 8 KiB emergency reserve appeared as an 8,204 B allocated block at boot. After `KILL`, the original reserve block became free and an 8,204 B block was allocated elsewhere. This matches `EmergencyReserveLease` releasing and restoring the reserve during task removal; its relocation can change the largest-block figure without indicating a leak.
- The one-worker map showed temporary 468 B blocks and other worker allocations. They were freed by cancellation/reap; the `0x3ffe4350` region returned to its original single 112,960 B free block. In this isolated run, worker state did not leave residual allocations in that region.
- Earlier seven-worker maps still showed persistent 80 B, 652 B, and 80 B live blocks in that region after reaping. This isolated run did not reproduce them, so those blocks are likely tied to the repeated-run harness or its supervisor command state, but their owner is not yet proven.

## Debug information

A separate `esp32_2432s028_spiffs_debug` PlatformIO environment builds with `-Og -g3 -ggdb`; its ELF contains DWARF symbols. The bundled ESP-IDF configuration sets `CONFIG_HEAP_TRACING_OFF`, so the current heap dumps do not include allocation call stacks. Debug symbols alone cannot identify those three blocks; call-site tracing needs an ESP-IDF build with heap tracing enabled or another allocation hook.

The debug-symbol firmware was flashed to the CYD. It booted to the BASIC REPL, and a read-only `DIR #heap_serial FFS:/` listing confirmed `sincos64-grid.bas` and the other FFS files remained present.

Raw map capture: [cyd-heap-checkpoint-isolation-2026-11-07.log](cyd-heap-checkpoint-isolation-2026-11-07.log)

## Seven-worker minimal-harness follow-up

Repeated the seven-worker burst on the debug-symbol firmware using `PRINT #heap_serial,TASK(...)` to collect IDs, without per-worker REPL variables or `TASKRESULT` status queries. After reaping, total free heap was 212,372 B against a 214,044 B baseline; the largest block was 57,332 B. The same three notable live allocations remained across heap regions: 80 B, 652 B, and 80 B. This shows they do not depend on the earlier `task_probe`/status-query variables.

The 652 B allocation is now strongly attributed to `WindowManager::windows_`: the debug ELF reports `sizeof(WindowManager::Entry) == 80`, and 652 B equals an eight-entry backing buffer (8 × 80 B) plus the 12 B allocator header. Seven worker windows grow `windows_` to capacity eight; `WindowManager::closeSession` erases the entries, but `std::vector::erase` retains that backing buffer. This is scheduler/graphics high-water bookkeeping retained after worker cleanup, not live canvas pixels.

The two 80 B allocations are consistent with retained hash bucket arrays in graphics session metadata (`WindowedService::sessionWindows_` and a related map), but their exact owners still need direct capacity instrumentation. Full checkpoint log: [cyd-heap-minimal-harness-2026-11-07.log](cyd-heap-minimal-harness-2026-11-07.log)

## Graphics bookkeeping fix and device verification

Changed the per-window arena and window collections from vectors to node-based lists, so each `CanvasArena::Block` and `WindowManager::Entry` node is returned on release. Changed task-keyed graphics metadata from `unordered_map` to `map`, which frees task entries instead of retaining peak bucket arrays. Frozen-window IDs also use a list. No task/window cap was added.

Added a host regression covering four bursts of 12 windows, task viewports, discard, and fresh window creation. The full 64-test CTest suite passes. Both the normal CYD/FFS release profile and the debug-symbol profile build; the debug-symbol image was flashed to the CYD and exercised with the same FFS fixture.

Three repeated seven-worker launch/cancel/reap runs produced identical readings:

| Checkpoint | Total free | Largest block |
|---|---:|---:|
| Baseline | 214,056 B | 110,580 B |
| Seven workers active | 36,768 B | 21,492 B |
| Cancelled, before reap | 211,860 B | 94,196 B |
| All records reaped | 213,424 B | 110,580 B |

After reaping, the largest block returned to the exact baseline each time. The remaining 632 B total-free difference is small REPL/test-session state; it no longer splits the 110,580 B block. The prior 652 B window-vector, 204 B canvas-block-vector, and 80 B bucket-array residuals are gone from the post-reap maps. The three device runs are [run 0](cyd-heap-lifetime-fix-2026-11-07-run-0.log), [run 1](cyd-heap-lifetime-fix-2026-11-07-run-1.log), and [run 2](cyd-heap-lifetime-fix-2026-11-07-run-2.log).

The final debug-symbol firmware was reflashed after the frozen-window cleanup adjustment and passed the seven-worker cycle with the same measurements (214,056 / 110,580 B baseline; 213,424 / 110,580 B after reap). FFS loading succeeded. Final raw capture: [cyd-heap-lifetime-fix-2026-11-07-final.log](cyd-heap-lifetime-fix-2026-11-07-final.log).
