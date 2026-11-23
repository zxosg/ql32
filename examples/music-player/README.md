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
