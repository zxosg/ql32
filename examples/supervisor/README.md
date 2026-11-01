# Embedded supervisor demo

Copy this directory to the SD card so the three worker files are available at
`supervisor/worker-1.bas`, `supervisor/worker-2.bas`, and
`supervisor/worker-3.bas`. Start the three workers from the embedded REPL:

```text
SPAWN "supervisor/worker-1.bas"
SPAWN "supervisor/worker-2.bas"
SPAWN "supervisor/worker-3.bas"
TASKS
```

Each worker creates a retained window in one third of the 320x240 display,
opens a per-session log file, and prints 250 numbers. The scheduler advances
workers in round-robin slices. Let them run for several seconds, then cancel
them one at a time using the IDs printed by `SPAWN`:

```text
CANCEL 2
CANCEL 3
CANCEL 4
TASKS
```

Task IDs continue increasing, so use the IDs reported by your board. Start all
three again. After each worker prints `READY`, inspect its channel ownership
from the supervisor. For example, if the new IDs are 5, 6, and 7:

```text
TASKCHANNELS 5
TASKCHANNELS 6
TASKCHANNELS 7
```

Each listing includes that task's reserved console/window handles and its open
file handle. Attach to a worker to change its window from that worker's own
session, then return to the supervisor:

```text
LOGON 5
WINDOW 0,0,100,240
LOGOFF
LOGON 6
WINDOW 100,0,110,220
LOGOFF
LOGON 7
WINDOW 210,0,110,200
LOGOFF
```

Use the actual task IDs from `TASKS`. The prompt changes to `task <id>>` while
attached. BASIC statements, variables, and channel commands then operate in
that task. `LOGON` only attaches to a worker that has reached its prompt after
its startup file finishes. `CANCEL` cannot cancel the currently attached
session; `LOGOFF` first.
