# Music player in QL/32 BASIC — design (Olimex), 2026-11-23

Status: **design only**, no code. Target: `olimex_esp32_sbc_retro` (VGA, PS/2
keyboard, `SDC:` + `FFS:`, GPIO25 DAC audio). Sources checked on origin/main:
`QL32-LANG-001` §8, §11.1, §11.6; `QL32-UI-001` (M2/M4); `QL32-API-001`.
Other boards: the CYD has experimental audio but no keyboard; Tenstar and
LILYGO have no audio, so the player is not meaningful there.

## 1. What the language already gives us

| Need | Existing feature | Notes |
| --- | --- | --- |
| Play | `MUSIC PLAY path$` (VTX, YM2–YM6 raw/LHA, PT3, SQT), `MUSIC WAV path$` | Format chosen by content; WAV 8/16-bit PCM only. |
| Pause / resume / stop | `MUSIC PAUSE`, `MUSIC RESUME`, `MUSIC STOP` | Owner-session only; stop releases the audio. |
| Volume | `VOLUME 0..255` (global, default 128) | Applies to BEEP and MUSIC. |
| Background play | Playback runs in the music service; the BASIC session keeps running or can block in UI calls. | One session owns audio; the player must be that session. |
| Browse | `DIR [#ch,] pattern`, `CAT`, wildcards `*`/`?` in the leaf, `CD`, `MKDIR` | Output goes to a channel; `DIR #f, "SDC:/*.vtx"` can target a file channel. |
| Read lines | `OPEN(KDeviceFile, path, mode)`, `READ$(c)`, `EOF(c)`, `CLOSE` | |
| Text UI | `AT row,col`, `CLS`, `INK/PAPER`, `PRINT`, `TEXT`, `WINDOW` | |
| Menus / input | `MINIT`/`MCARD`/`MENU(m)` (blocking, returns command id, 0 on Esc), `DIALOG`, `INPUT`, `ALERT`, `CONFIRM`, `IPRINT` toast | Keyboard operable. |
| Strings | `LEFT$ MID$ RIGHT$ INSTR`, `DIM a$(n)` | |

## 2. Gaps (each needs owner scope approval before any language change)

| # | Gap | Effect | Workaround today | Smallest proposed addition |
| --- | --- | --- | --- | --- |
| G1 | **No non-blocking key read** (`INKEY$`-style). `INPUT`/`MENU` block the session. | A live "now playing" screen cannot react to a single key (space = pause, +/- = volume). | Drive the Now-playing screen with a blocking `MENU` (Pause / Resume / Vol+ / Vol- / Stop / Back); music keeps playing meanwhile. Costs extra keystrokes (Enter on an item). | `INKEY$(timeoutMs)` returning "" or one key (QL/32-specific; SMSQ/E `INKEY$` is the reference for conformance). |
| G2 | **No directory listing into an array.** `DIR` only prints. | Program must scrape text. | `DIR #f, "FFS:/*.vtx"` into a temp file on `FFS:`, then `READ$` lines (dirs end with `/`). Needs one DIR per extension (leaf-only wildcards, no alternation). | `DIRLIST(pattern$, array$())` or `FILES$(pattern$, n)`; returns the count, sorted, dirs flagged. |
| G3 | **No playback status as a value.** `MUSIC STATUS` prints text. | Cannot detect "song finished" (non-looping formats) or confirm pause. | Track our own state flags; auto-advance impossible. Loop-point formats just loop. | `MUSIC(KMusicState)` function returning 0 stopped / 1 playing / 2 paused (matches the `MODE(selector)` style). |
| G4 | No volume read-back | Player keeps its own `vol`. | Own variable, initial 128. | None needed. |
| G5 | `MENU` item limit / paging not specified | A long file list may not fit one menu. | Page the list in the program (8–10 entries per page plus `Next`/`Prev`). Must be checked on hardware (max MCARD count, label length). | Document the limit; none otherwise. |
| G6 | No song duration / position / title | No progress bar. | Show file name and size only. | Out of scope for v1. |

Without G1–G3 the app is fully buildable (v1, §5), just with a menu-driven
UI and no auto-advance. G1 is the one that most improves usability.

## 3. Display mode and session constraints (MODE 2, 400×300)

The player fixes **MODE 2** (400×300, the retro image's boot mode, so normally
no switch is needed; the program only checks `MODE(KModeCurrent)` and calls
`MODE 2` if it differs, before any `WINDOW`, `MUSIC` or other task starts —
a `MODE` change is refused while worker tasks run, per LANG-001 §11.7/memory
rules). The app therefore runs as **one session with no worker tasks, no
extra `WINDOW`s and no `TASK` calls**: all UI is text on the main screen
channel plus the `MENU`/`ALERT` presenters, and playback runs in the music
service, not in a BASIC task. Consequence: nothing can run alongside it (no
background scanner); the scan is synchronous with a `BUSY`/`IPRINT` toast.

## 3a. Screen layout (Olimex MODE 2)

Text cell size follows the default font; design for ≥ 40 columns × 20 rows.

```
+--------------------------------------+
| QL/32 MUSIC PLAYER        SDC:/music |   row 1  header: drive and path
|--------------------------------------|
|  > ../                               |   rows 3-16: file list page,
|    song1.vtx            48 K         |   highlight = selected row
|    demo.ym              32 K         |   (inverse PAPER/INK)
|    tune.pt3              9 K         |
|    jingle.wav          120 K         |
|                          page 1/3    |
|--------------------------------------|
| Playing: demo.ym   Vol 128 [#####   ]|   row 18: now playing + volume bar
| ENTER play  P pause  +/- vol  S stop |   row 19: key help (changes with state)
+--------------------------------------+
```

v1 (menu driven) replaces the highlight and key help with the menu popups
described next; the header, list and now-playing line are drawn by `AT`/`PRINT`.

## 4. UI flow

1. **Disk select** — `MENU`: `SDC:/`, `FFS:/`, `Quit`. Probe `SDC:` with a
   `CAT`/`DIR` into the temp file; if it fails, grey out the item
   (`MENABLE`) and show `ALERT "No SD card"`.
2. **Scan** — for the current directory build the list: one `DIR #f` for
   directories (all `*` entries ending `/`) plus one per supported extension
   (`*.vtx *.ym *.pt3 *.sqt *.wav`, plus upper-case variants if the
   filesystem is case-sensitive — check on hardware). Entries go into
   `DIM name$(200)`, `kind(200)` (0 dir, 1 file, 2 wav). Show a `BUSY`/`IPRINT`
   "Scanning…" toast for slow SD cards. Cap the list at the array size and
   say so.
3. **File list** — page of ≤ 10 entries as a `MENU` (ids 1–10 = entries,
   100 = Next page, 101 = Prev page, 102 = Up/`..`, 103 = Change disk,
   104 = Now playing, 0/Esc = Quit/back). Directory entries `CD` and rescan.
4. **Play** — file entry → `MUSIC STOP` (release any song), then
   `MUSIC PLAY` or `MUSIC WAV`; errors (bad format, `ResourceBusy`, out of
   memory) are shown with `ALERT` and the list is kept. Set the now-playing
   line and state = playing. Large YM/VTX play immediately on the Olimex (it
   has PSRAM; no cache transcode).
5. **Now playing** — menu: Pause/Resume (label toggles by state), Volume up
   (+16), Volume down (−16), Stop, Back to list, Quit. Volume steps clamp to
   0–255 and update `VOLUME` and the bar. With G1 this becomes a single-key
   loop instead (`INKEY$(250)`), redrawing the bar without a menu.
6. **Quit** — `MUSIC STOP`, `CLOSEWINDOW` if any, delete the temp file,
   `CLS`.

## 5. Program structure (v1, single file, ~250 lines)

```
REM globals: drive$, path$, vol, state, n, page
DIM name$(200), kind(200)
vol = 128 : VOLUME vol : state = 0 : drive$ = "FFS:/"

DEFine PROCedure Main        : loop: SelectDisk / Scan / ListMenu / dispatch
DEFine PROCedure SelectDisk  : MINIT/MCARD/MENU, sets drive$/path$
DEFine PROCedure Scan        : DIR into FFS:/ql32_mp.tmp, then ReadTmp
DEFine PROCedure ReadTmp     : OPEN/READ$/EOF/CLOSE -> name$(), kind(), n
DEFine PROCedure ListMenu    : builds the page menu, returns selected id
DEFine PROCedure Play(i)     : MUSIC STOP, MUSIC PLAY|WAV, state=1, header
DEFine PROCedure NowPlaying  : pause/resume/volume/stop menu
DEFine PROCedure Header, DrawVolume(vol), ShowError(msg$)
```

Conventions: procedures with `LOCal` variables, constants from `ql32.inc`
(`INCLUDE`), tab/column positions in named constants. Memory is trivial
(two 200-element arrays; far below the Olimex heap). Run it as a normal
program from `FFS:` or `SDC:` (`TASK`/`LOAD` + `RUN`); the player's session
owns the audio so no second task may call `MUSIC` while it runs.

## 6. Verification plan

* Host: no audio service on Linux ("music service unavailable") — test the
  scanning and paging logic only, with a stubbed `Play` (golden program).
* Olimex: serial-driven smoke (PLAY/PAUSE/RESUME/STOP/VOLUME sequence and
  `MUSIC STATUS` text) as in the 2026-11-21 listening note; then owner for
  ears and keys (menus, volume steps, SD missing/present).
* Check on hardware: `MODE 2` is already active/accepted with the player running and no tasks; max `MCARD` count and label length (G5), upper/lower
  case wildcard matching on FAT vs SPIFFS, scan time on a large SD.

## 7. Open questions for the owner

1. Approve G1 (`INKEY$(timeoutMs)`) for a single-key UI, or accept the
   menu-driven v1?
2. Approve G2 (`DIRLIST`/`FILES$` into an array) or accept the temp-file
   `DIR #f` workaround?
3. Approve G3 (`MUSIC(KMusicState)` function) for auto-advance/"finished"
   detection, or leave playback un-sequenced (loop/stop only)?
4. Start folder: `SDC:/music` if present, else `FFS:/`?
5. Where to keep the program: `examples/music-player/`, plus an upload fixture
   to `FFS:/`?

## Implementation note (v1)

`DIR`/`CAT` are shell-only and cannot be compiled in a program, so v1 reads a
playlist `FFS:/music.lst` generated from the REPL by `tools/ql32-music-scan.py`.
See `examples/music-player/README.md`.
