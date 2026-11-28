# Language and runtime gaps found while building The Grand Tour

Running log. Each entry: gap, workaround, cost, suggested change, priority (H/M/L).

| # | Gap | Workaround | Cost | Suggested change | Prio |
| --- | --- | --- | --- | --- | --- |
| 1 | `PRIORITY`, `SUSPEND`, `RESUME`, `SLICE` are shell-only (not BASIC statements) | Scene 4 shows task speed differences with different `PAUSE` pacing instead of weights | Cannot demonstrate weighted scheduling from a program; some honesty lines in the caption | Allow `PRIORITY id,w` / `SUSPEND` / `RESUME` as statements (supervisor tasks only) | M |
| 2 | A worker's default window is a half-width slot (CYD 160x240 at x=160), not the screen; `GWIDTH()` before `WINDOW` is not the screen size | Parse `WxH` out of `SYSINFO$(3)` in the layout library | About 10 lines of string parsing in every task that includes the library (duplicated code per task) | `DISPLAYW()/DISPLAYH()` (and usable-area) functions, or numeric `SYSINFO(selector)` | H |
| 3 | `CAPABILITY("audio")` is `-1` (unknown) on CYD/Tenstar; `SYSINFO$(6)` lists no audio; `SYSINFO$(5)` shows no `sdc` | Probe by attempting `MUSIC PLAY`/`OPEN` under `TRAP` and checking `ERR` | Extra TRAP wrappers and a failure path per scene | Make capability queries truthful (`audio`, `sdc`, `touch`, `ps2`) and list them in features | H |
| 4 | Serial arrow keys are not decoded by `INKEY` (`ESC [ A` arrives as 27, 91, 65) | Use letter keys (`N`, `P`, `M`, `Q`, space) | None for the tour; serial users cannot use arrows | Decode ANSI cursor sequences into `KKeyUp` etc. in the serial input path | L |
| 5 | Workers take no arguments (`TASK path` only) | Derive everything from run-time queries; the director communicates nothing to scenes | Every scene recomputes layout | `TASK path, arg$` plus `ARG$()` | M |
