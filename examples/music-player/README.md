# QL/32 music player (v1, Olimex, MODE 2)

`musicplayer.bas` browses a playlist, plays YM/VTX/PT3/SQT/WAV files with
`MUSIC PLAY`, and offers Pause/Resume, Volume up/down and Stop. It needs
MODE 2 (400x300) and a single session (no worker tasks).

BASIC programs cannot run `DIR`, so the file list is a playlist file,
`FFS:/music.lst`, generated from the REPL (design gap G2, see
`docs/plans/music-player-app-design-2026-11-23.md`).

1. Generate the list: `tools/ql32-music-scan.py /dev/ttyUSB0`
   (scans `SDC:/music` if present, else `SDC:/`, plus `FFS:/`). By hand:
   `p=OPEN(2)`, `f=OPEN("FFS:/music.lst","WRITE")`, `PRINT #f,"@SDC:/"`,
   `DIR #f SDC:/*.ym` (repeat per pattern), `CLOSE f`. Lines starting with
   `@` set the folder for following names.
2. Upload: `tools/ql32_ymodem.py send PORT 115200 musicplayer.bas --device-path FFS:/musicplayer.bas`
3. Run: `MODE 2`, then `TASK "FFS:/musicplayer.bas"`.

Limits: 200 entries, flat list, no subfolders, no error trapping (a bad
file ends the program), pause state is tracked by the program, not read
from the player. Do not type REPL commands on the serial console while it
runs; they feed the menus.

# Directory player (`dirplayer.bas`)

Lists songs straight from a directory channel (no playlist file): tries
`SDC:/music/`, `SDC:/`, then `FFS:/`; opens `OPEN(14, dir, "*", 2)` (sorted,
no folders), keeps `.vtx .pt3 .sqt .wav .ym` (max 120). Pixel UI sized from
`GWIDTH()/GHEIGHT()`, 6x8 font. One-row scrolling uses `GCOPY`.

Keys (numeric `INKEY(300)` codes; PR #28 `INKEY$` names are not on main):
Up/Down move, Enter play, Space pause/resume, `s` stop, `+`/`-` volume
(step 16), `q`/Esc quit. Status shows state, volume bar, song count, item
number, title being played, and play errors (trapped with `TRAP`/`ERR$`).

Host: works with `build/macos/ql32 dirplayer.bas` in a terminal, but there
is no audio, so Enter reports "Service unavailable". CYD: **does not
compile on current main** (token vector needs more RAM than is free, see
`docs/notes/dirplayer-workarounds.md`). Workarounds and language gaps are
listed there.
