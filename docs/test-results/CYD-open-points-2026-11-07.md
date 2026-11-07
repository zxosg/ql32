# CYD open-point follow-up

- **Date:** 2026-11-07 (Europe/Prague)
- **Board:** ESP32-2432S028 CYD
- **Serial port:** `/dev/cu.usbserial-11440`
- **Firmware profile:** `esp32_2432s028_spiffs`
- **Source head:** `3967283` plus the local `FREE()` change described below
- **Firmware build:** 51,048 B static RAM; 1,489,753 B flash

## `FREE()` output

The no-argument form now returns `total,largest-block,free-block-count` as a
CSV string. Numeric selector behavior remains unchanged. On CYD the serial
output was `225040,110580,14`; separate calls returned `225040`, `110580`, and
`14`. Host tests verify the formatted tuple and the numeric selector forms.
Platforms without fragmentation metrics return empty CSV fields in those
positions.

## Task API and priority checks

- Function form with `KTaskWait` ran a worker to `CHILD DONE` and resumed the
  supervisor. Command form with `KTaskWait` did the same.
- Function and command forms without `KTaskWait` both started workers. An idle
  worker entered focus with `LOGON`; `LOGOFF` returned to the supervisor.
- A child dividing by zero caused both child and waiting supervisor to report
  the runtime error. `TASKRESULT(2,1)` returned `FAILED`, selector 3 returned
  error code `7`, and selector 4 returned `line 1, column 8: division by zero`.
- `PRIORITY 3,12`, `SUSPEND 3`, and `PRIORITY 3,0` reported the worker as
  `P=0 DISABLED (no saved resume priority)`. `RESUME 3` then returned
  `session is not suspended`, confirming the zero priority replaced the
  saved resume baseline.

## Worker cleanup and heap retention

The existing five-by-five CYD memory suite passed. The first run, which issued
only one `KILL` per active worker, ended 8,420 B below its idle total-free
baseline; the largest block remained 110,580 B. The reset baseline recovered
to within 60 B. Inspection showed that `TASKS` omits cancelled tasks while
`GETTASK(0)` still counts them and `TASKRESULT` can still read their status.
The memory runner therefore now reads the cancelled result, then issues a
second `KILL` to remove that terminal result before measuring cleanup.

With explicit reaping, all five workload groups passed. The largest block
remained 110,580 B. Post-cycle total-free readings fluctuated by at most about
2.4 KiB below the initial idle baseline without monotonic growth; each case's
post-run drift was between -932 B and +16 B. After reboot, total free was
222,436 B versus the 222,496 B initial median, and the largest block matched.
After cancellation, `GETTASK(0)` reported two tasks; after the second `KILL`,
it reported one. Reaping recovered roughly 0.1–0.7 KiB per task in the sampled
cases. This identifies retained cancelled-task records as a substantial part
of the earlier cumulative drift; it does not account for all allocator
variation.

## Canvas resize soak

The 100-cycle, warmed full-screen grow/shrink soak completed. Every post-cycle
sample was `195748,110580,17`; completion was `194844,110580,18`. During
320x240 replacement, free total dropped from 195,620 B to 157,204 B while both
buffers were live, a 38,416 B decrease, and returned to 195,620 B after the
swap/release. The largest block stayed 110,580 B throughout this solo soak.

## Full-screen render with two workers

After reboot, two workers started successfully. During the 320x240 canvas
replacement, the observed samples were:

| Phase | Free total | Largest block |
| --- | ---: | ---: |
| Before replacement | 158,648 B | 110,580 B |
| Both buffers live | 120,232 B | 73,716 B |
| After swap | 129,848 B | 73,716 B |

The array and channel workers remained active at the next reported checkpoints.
The BASIC 320x240, 8-iteration render had not completed after five minutes, so
the board was reset. This is a practical performance failure for this combined
fixture at the tested settings; it does not establish an allocation failure.

## Sin/cos worker memory saturation and recovery

The unchanged repository `data/supervisor/sincos-worker.bas` was run until its
window limit. Four workers (IDs 2–5) remained runnable; the fifth and sixth
started as sessions but could not create windows and reported
`window manager window limit reached`. This reached the configured four-window
limit before exhausting heap.

To exercise the canvas byte limit, a temporary test copy changed only the
worker's `WINDOW` from 64x64 to 320x240. Each 4-bit full-screen canvas is
38,400 B, and the CYD arena is capped at 76,800 B. Two workers ran; the third
reported `window manager canvas memory limit reached` while free heap was still
109,320 B and the largest block was 73,716 B. Thus the configured canvas cap,
not ESP32 heap exhaustion, stopped this workload.

| Checkpoint | Free total | Largest block |
| --- | ---: | ---: |
| Baseline after upload | 224,820 B | 110,580 B |
| One full-screen worker | 166,944 B | 110,580 B |
| Two full-screen workers | 110,392 B | 73,716 B |
| Third worker rejected | 109,320 B | 73,716 B |
| After cancel and reap | 145,780 B | 73,716 B |
| After opening and closing a recovery window | 222,520 B | 110,580 B |

The two full-screen windows remain frozen for display after task cancellation;
creating and closing a new graphic window discards those frozen canvases. A
second saturation/cleanup cycle also rejected its third worker and restored the
largest block to 110,580 B. To check the apparent 820 B difference between
those first two cycles, a five-cycle follow-up reused the same three REPL
variables on each cycle. Its baseline was 224,992 B free with a 110,580 B
largest block. All five cycles again ran two full-screen workers and rejected
the third. After cancellation, result reaping, and opening/closing a small
recovery window, free total ranged from 222,040 to 222,560 B (2,432–2,952 B
below baseline); the largest block returned to 110,580 B every time. The
residual varied instead of growing monotonically, so the run found no
accumulating heap loss across five cycles. The free-block count rose from 14 to
23–27, indicating increased fragmentation, while the largest block recovered.
The temporary full-screen worker file was deleted from CYD SPIFFS after the
test.

## Dynamic task/window allocation and recovery-reserve check

- **Date:** 2026-11-07
- **Firmware profile:** `esp32_2432s028_spiffs`
- **Port:** `/dev/cu.usbserial-11440`
- **Emergency reserve:** 8,192 B held by the ESP32 memory service
- **Build footprint:** 51,056 B static RAM; 1,490,633 B flash

After `DEBUGBAR ON`, the idle sample was 215,632 B free with a 110,580 B
largest block. Ten `sincos-worker.bas` sessions (IDs 2–11) ran at once, proving
that window creation no longer stops at the previous four-window limit. At
that point `FREE()` reported 13,400 B total and a 10,740 B largest block.
Replacing `resource-recovery-smoke.bas` with `SAVE` succeeded while all ten
workers remained active.

A twelfth worker session (ID 12) was created but failed during execution as
heap pressure rose. The board stayed up and the supervisor remained at its
prompt. `SAVE` still succeeded after the failed task. Killing/reaping ID 12
and then IDs 2–11 returned the task list to the supervisor only. Final `FREE()`
was 191,404 B total with a 31,732 B largest block; the supervisor remained
responsive. The total did not return to the idle checkpoint during this run,
and the largest block remained smaller, so allocator fragmentation and frozen
window storage still need a separate recovery characterization.

The first pre-fix saturation pass reset the board while the platform loop
copied a vector of `TaskInfo` records for diagnostics. The decoded backtrace
ended in `SessionScheduler::tasks()` called from `advanceScheduler()`: this
diagnostic allocation was outside the VM OOM handler. Embedded polling now
uses allocation-free task-status, active-count, waiting-UI, and error-visiting
APIs. A separate SAVE attempt also exposed SPIFFS's short path limit because
the temporary name appended a suffix to the destination; temporary and backup
files now use short sibling names. The final hardware pass confirmed both
corrections.

The CYD firmware built and flashed successfully. Olimex VGA and Tenstar ESP32-S3
firmware profiles also built successfully. The Olimex physical saturation test
remains open.

A final repeat after adding allocation-free OOM text reporting again ran ten
workers. At ten active workers `FREE()` was 13,684 B total and 11,252 B largest
block. The next worker failed and printed `BASIC session 12 VM error: out of
memory`; the board and supervisor stayed responsive. SAVE succeeded again,
then all workers were killed/reaped. `TASKS` showed only the supervisor and
`FREE()` returned 191,652 B total with a 15,860 B largest block. The temporary
SAVE file was deleted from SPIFFS after verification. The lower largest-block
recovery than the earlier pass reinforces that fragmentation still needs a
separate soak/cleanup study.

## PLOT and SCREEN capture

The updated attractor was uploaded and started as a task, and a separate
default-window smoke test ran `WINDOW`, `PLOT`, `TEXT`, and `GUPDATE` without
VM errors. Those earlier `SCREEN`/YMODEM captures were valid 320x240 BMPs, but
all pixel indices were zero, including after static text, point drawing, and a
filled 320x192 rectangle. That result did not reproduce in the follow-up
foreground test below.

To check the capture path with known live graphics, three repository
`sincos-worker.bas` tasks were started and assigned 106x120 task viewports.
`TASKS #2` showed all three runnable immediately before `SCREEN`. The capture
was a 320x240 indexed BMP with eight used palette indices and 1,156 nonzero
pixels, visibly containing the workers' circles and viewport text. This proves
that `SCREEN`/YMODEM can capture live task canvases. The full six-worker demo
did not fit during BASIC program preparation on this CYD, so this check started
three workers directly from the REPL.

Reopening the serial port for a later status query showed a fresh boot banner
and uptime under one second. Thus serial reconnects can reset this board and
discard the running fixture; run status and capture checks through one open
serial connection when comparing a live display with its BMP snapshot.

A second same-session check drew `CIRCLE 160,120,50` and
`TEXT 8,8,"SCREEN TEST"` in the foreground 320x240 window. The resulting
capture contained 438 nonzero pixels and showed both the text and circle.

The repository `examples/attractor.bas` was then uploaded unchanged and started
as a worker. Three captures were taken while it ran. Excluding palette index 7
used by console text, the captures contained 13, 69, and 50 plotted pixels
from palette indices 1 through 6. The matching source sets `INK inkIndex` and
executes `PLOT px, py` for each recurrence point. This verifies the attractor's
`PLOT` path on the CYD. The first capture caught a mostly blank animation frame;
later captures showed the colored points. The user also confirmed that the
points were visible on the display.

The diagnostic issued `TASKS #2` and `PRINT #2,...` without opening channel 2,
so the foreground session displayed `channel handle is not open for this
session` errors in the captured overlay. Those errors came from the test
commands, not the attractor worker.

## Memory/concurrency conclusion

The measured operating envelope for the tested CYD SPIFFS firmware is:

- A 320×240 4-bit canvas uses 38,400 bytes. The configured 76,800-byte canvas
  arena therefore admits two full-screen canvases; a third is rejected by the
  canvas budget while more than 109 KiB of general free heap remained. Treat
  two as the full-screen canvas limit for this profile.
- Ten animated 64×64 workers were created and remained active near saturation
  (`FREE()` reported 13,400–13,684 bytes total and 10,740–11,252 bytes in the
  largest block). The next worker failed during execution. The supervisor
  remained responsive and `SAVE` still worked. Treat ten such workers as a
  demonstrated ceiling, not a safe target when other workloads are active.
- A 320×240, eight-iteration Mandelbrot render did not finish within five
  minutes while the array and channel workers also ran. That combination is
  outside the practical rendering envelope. As a conservative operating
  policy, keep one compute-heavy full-screen render active at a time and reduce
  other worker load when response time matters. The test does not establish a
  frame-rate guarantee for a lone renderer.
- Ordinary five-by-five worker cleanup and the five-cycle 64×64 saturation
  follow-up did not show monotonic total-heap loss. Small-cycle recovery
  returned the largest block to 110,580 bytes each time, while total-free
  readings varied by about 2.4–3.0 KiB below baseline and the free-block count
  rose. The ten-worker OOM pass recovered neither the original total-free
  value nor largest block after task reaping; the largest block varied
  substantially between passes. The separate 25-run cleanup suite returned
  to within 60 bytes of its baseline after reboot. This is consistent with
  allocation high-water retention and heap fragmentation, but the current
  measurements do not isolate their individual contributions or prove a leak.
  The ten-worker saturation pass itself was not followed by a same-run reboot
  measurement.

OOM recovery is verified: the rejected task reports an error, the board does
not reset, and the supervisor can save and reap tasks. The separate 25-run
cleanup suite also passed reboot recovery. No fixed task-count limit is
justified; the practical limits are workload-specific and should be checked
with `FREE(0)` and `FREE(1)` before starting another graphics workload. These
results close the CYD memory/concurrency characterization item at the
operating-envelope level; the residual post-saturation fragmentation remains
documented as a limitation.

## Follow-up

1. Keep future attractor captures in the same serial session as launch. Its
   `PLOT` output is now verified; repeated captures avoid mistaking a blank
   animation frame for a graphics failure.
2. Use the repository sin/cos workers for subsequent quick graphics and
   viewport checks; do not repeat the slow Mandelbrot fixture for this screen
   investigation.
3. Keep the memory runner's cancelled-result read and second `KILL`; otherwise
   `TASKS` alone gives a false impression that cancelled task records were
   released.
