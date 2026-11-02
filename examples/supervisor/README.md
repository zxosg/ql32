# Embedded supervisor demo

Copy this directory to the SD card so the three worker files are available at
`supervisor/worker-1.bas`, `supervisor/worker-2.bas`, and
`supervisor/worker-3.bas`. Start the three workers from the embedded REPL:

```text
TASK "supervisor/worker-1.bas"
TASK "supervisor/worker-2.bas"
TASK "supervisor/worker-3.bas"
TASKS
TASKRESULT 2
```

The supervisor can control responsiveness and relative run frequency. `SLICE`
sets the bytecode instructions per scheduler turn (default 128); smaller
slices switch tasks sooner but increase scheduling overhead. `PRIORITY` sets a
task's scheduling weight. For example, if the three workers were assigned IDs
2, 3, and 4:

```text
SLICE 16
PRIORITY 2,16
PRIORITY 3,4
PRIORITY 4,1
TASKS
```

The first task receives about four times as many scheduler turns as the second
and sixteen times as many as the third while all three remain runnable. Use
`SUSPEND id` to pause a task without cancelling it; its interpreter state and
channels stay allocated. The scheduler remembers the task's current priority.
`RESUME id` restores that priority and continues the task on future scheduler
turns. `PRIORITY id,0` instead sets the priority baseline directly and clears
the saved resume weight. A later `RESUME` will not restore the old value; set a
positive priority to enable the task again. `TASKS` shows saved resume weights
and writes to the current print channel (initially `#1`); use `TASKS #channel`
to redirect a listing. Log off before suspending the currently attached task.
Use the actual IDs printed by `TASK` or `TASKS`.
Use `TASK "path.bas", KTaskWait` when the current task must wait for its child;
the child takes the active console focus while it runs. Use
`TASKRESULT(id, KTaskResultStatus)` or one of the other `KTaskResult...`
selectors in BASIC expressions.

Each worker creates a retained window in one third of the 320x240 display,
opens a per-session log file, and prints 250 numbers. The scheduler advances
workers according to their priority weights. Let them run for several seconds,
then cancel them one at a time using the IDs printed by `TASK`:

```text
KILL 2
KILL 3
KILL 4
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
its startup file finishes. `KILL` cannot cancel the currently attached
session; `LOGOFF` first.
