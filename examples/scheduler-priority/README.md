# CYD priority and scheduler slicing demo

Save the three worker programs to the root of the SD card as
`worker-1.bas`, `worker-2.bas`, and `worker-3.bas`. Enter the `OPEN`, `PRINT`,
and `CLOSE` lines for one file at a time in the USB serial REPL. Each `PRINT
#file, "..."` writes one BASIC source line to that file.

The workers create three 106/107-pixel-wide colored windows at the top of the
320x240 display. Each is 176 pixels high. The CYD serial console begins at
pixel row 176 and uses the bottom 64 pixels, so the windows do not cover it.
Task 1 prints its task number, loop count, and free memory. Tasks 2 and 3 print
their task number and loop count.

After saving all three files, start them separately from the REPL:

```text
SPAWN "worker-1.bas"
SPAWN "worker-2.bas"
SPAWN "worker-3.bas"
SLICE 32
PRIORITY <task-1-id>,8
PRIORITY <task-2-id>,4
PRIORITY <task-3-id>,1
TASKS
```

Replace each placeholder with the ID reported by that worker's `SPAWN`
command. Task 1 gets eight times the scheduler turns of task 3, while task 2
gets four times the turns of task 3. The shorter slice makes switching between
workers more frequent. All three loops continue running, so the display shows
the relative scheduling rates. To stop them, issue `CANCEL <id>` for each
worker from the supervisor REPL.
