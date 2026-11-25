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
`SDC:/music/`, `SDC:/`, then `FFS:/`; opens `OPEN(14, dir, "*", 3)` (folders
first, sorted; falls back to unsorted when the 14 KB sort buffer is not
available), keeps `.vtx .pt3 .sqt .wav .ym` (max 120). Folders show as
`[name]`, `../` goes up. Long names are truncated with `~`. One-row scrolling
uses `GCOPY`; bigger jumps redraw.

Keys (`INKEY$` names): Up/Down, PgUp/PgDn move; Enter plays a song or opens a
folder; Backspace goes up; Space pause/resume; `s` stop; `+`/Right and
`-`/Left volume; `d` switch SDC:/FFS:; `q` quit. Status area: state, volume
bar, song count, entry n/N, now playing, approximate time (counted from
`INKEY$` timeouts: no clock exists), and trapped errors.

Host: no audio, Enter reports "Service unavailable". CYD: compile admission
needs 56 B per token (about 2150 tokens free at idle), so the app was
trimmed to fit (no scroll bar, Home/End, sort/filter toggles, playing-row
marker). Playing knuckle.ym and z00m.vtx from the app fails with allocation
errors (heap fragmentation). See `docs/notes/dirplayer-workarounds.md`.
