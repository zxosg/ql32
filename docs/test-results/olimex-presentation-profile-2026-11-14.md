# Olimex presentation stage profile — 2026-11-14

## Instrumentation

The opt-in `olimex_esp32_sbc_fabgl_graphics_profile` PlatformIO environment
adds `GFXPROFILE` counters around the FabGL presenter. It separates retained
row composition, palette conversion, scanline writes, and `swapBuffers()`
duration, while counting full/partial presentations and full-frame fallbacks.
The regular Olimex profile does not enable these timers. `swap_us` includes
time spent inside FabGL's swap call, including any frame-boundary wait; it is
not a measurement of pixel-copy time alone.

## Captured serial profiles

The profiling firmware produced these aggregates on the Olimex 320x200
double-buffer path:

| Workload | Requests | Full / partial / fallback | Rows composed | Compose | Convert | Scanline writes | Swap calls |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 20-line PRINT benchmark: 3 automatic + 3 batched passes | 98 | 14 / 77 / 7 | 4,032 | 505,448 us | 13,161 us | 250,932 us | 886,593 us |
| 40-line scrolling PRINT, repeat A | 71 | 24 / 25 / 22 | 5,216 | 720,868 us | 7,289 us | 357,682 us | 471,080 us |
| 40-line scrolling PRINT, repeat B | 71 | 24 / 25 / 22 | 5,216 | 720,849 us | 7,266 us | 357,729 us | 468,827 us |

The scroll profiles above are the two repeated runs after a warm-up run. Their
stage sums are 1.557 s and 1.555 s, respectively, and the host measured 1.402 s
and 1.403 s between the BASIC start/done markers. Presenter totals include
setup/cleanup drawing outside the BASIC marker interval. The earlier warm-up
included extra requests while loading the file and is excluded. For the PRINT
benchmark, all three automatic 20-line passes measured 0.301 s between serial
markers; the three batched passes measured 0.200 s. Marker timing has roughly
100 ms serial polling granularity. The aggregate PRINT presenter-stage sum is
1.656 s. Stage totals are counters within presenter calls, not end-to-end
workload time.

## Reading the results

- The PRINT benchmark spends most measured presenter time inside
  `swapBuffers()` (about 54% of summed stage time). Composition is next at
  about 31%; scanline writes account for about 15%, and conversion about 1%.
  For this case, further pixel-write tuning has limited headroom. Reducing the
  number of presentation requests is the more promising path, while preserving
  requested update semantics.
- Scrolling spends most measured presenter time composing rows (about 46%),
  with swap calls around 30% and scanline writes around 23%. It also falls
  back to a full-frame update 22 times in 71 requests. The packed-span
  optimization below addresses the measured composition cost; the fallbacks
  remain appropriate for large dirty regions where synchronizing both buffers
  would move more pixels than one full-frame update.
- The profile timers are diagnostic instrumentation. Their overhead has not
  been calibrated, and measurements should be repeated on the same firmware,
  CPU frequency, startup layout, and workload before comparing code changes.

### Per-call swap latency follow-up

The profile now records swap-call count and minimum, maximum, and average
duration. Two consecutive reruns of the 20-line PRINT benchmark at 240 MHz
reported:

| Run | Swap calls | Total swap time | Average | Minimum | Maximum |
| --- | ---: | ---: | ---: | ---: | ---: |
| A | 77 | 731,413 us | 9,498 us | 2,154 us | 11,816 us |
| B | 111 | 989,371 us | 8,913 us | 598 us | 14,591 us |

The request count differs because REPL setup/cleanup and console activity are
included in the profile window, so compare latency distribution rather than
total time or call count. Both runs show a broad, repeatable millisecond-scale
wait rather than rare long-tail stalls. This supports the interpretation that most swap time is FabGL's
synchronous display-service/frame-boundary wait. FabGL's
`Canvas::swapBuffers()` queues a swap primitive and waits for its execution;
the VGA controller performs the buffer swap and notifies the waiting caller.
Consequently, making swap asynchronous or batching implicit `GUPDATE 1`
presentations would change their completion/visibility semantics. The safe
existing batching control remains `GUPDATE 0` followed by bare `GUPDATE`.

## Verification and status

- The normal `olimex_esp32_sbc_fabgl_psram_double_buffer_test` profile built
  and was flashed successfully: 54,764 bytes static RAM and 1,766,477 bytes
  flash image. The board returned to a responsive standard REPL prompt, with
  no profiling output enabled.
- The focused host presenter test compiled and passed, and `git diff --check`
  passed.

## Packed-row composition optimization

Presenter profiling identified row composition as the largest scroll cost.
`WindowManager` now copies packed canvas spans into output rows and full frames
directly: 8-bit canvases use `memcpy`, while packed 4-bit rows unpack two
indices per source byte and handle odd pixel boundaries. The existing one-bit
path remains correct and does not add a platform dependency.

| Workload | Before compose time | After compose time | Other observed result |
| --- | ---: | ---: | --- |
| 20-line PRINT benchmark, 3 automatic + 3 batched passes | 505,448 us | 115,222 us | 98 requests in both captures; aggregate swap time 886,593 us before and 845,886 us after |
| 40-line scrolling PRINT | 720,868 / 720,849 us | 156,951 / 157,005 us | same 71 requests, 24 full, 25 partial, 22 fallbacks in both repeated runs |

The scrolling composition stage fell by about 78% (4.6× faster); the summed
presenter stages for the two repeated runs fell from 1.555–1.557 s to
0.876–0.877 s. BASIC start/done marker intervals after the change were
0.802–0.902 s, compared with 1.402–1.403 s before it. The serial capture has
about 100 ms polling granularity, so the presenter stage counters are the
stronger comparison. The 20-line PRINT composition stage fell by about 77%;
its automatic marker intervals did not show a reliable change because
`swapBuffers()` still dominates.

The normal firmware was rebuilt and flashed after the change. Three normal-
profile 40-line scroll runs completed in 0.801, 0.902, and 0.903 s between
serial markers; the mixed partial-drawing workload completed in 2.806 s, and a
100-frame 240 MHz attractor completed in 18.627 s in the earlier smoke run.
During the latest visual checks, the owner confirmed the mixed shapes were
stable and correct, the scrolling text was stable and correctly laid out, and
the attractor animation and final pattern were stable and correct. A latest
attractor run completed in 17.224 s. All workloads returned completion markers
without VM errors, and the board returned to a responsive REPL.

The host graphics suite, Olimex raster suite, and focused presenter suite passed.
The normal Olimex build reports 54,764 bytes static RAM and 1,766,477 bytes
flash used; the instrumented build reports 54,780 bytes static RAM and
1,767,621 bytes flash used. The currently flashed board is the normal,
non-instrumented profile.
