# QL/32 graphical showcase: plan

Status: **plan only, nothing implemented.** Sources surveyed: README, `docs/reference/QL32-Language-Reference.md`,
QL32-LANG-001/002, QL32-UI-001, `examples/`, `docs/notes/dirplayer-workarounds.md`,
`docs/test-results/{tenstar-hardware,scheduler-batching}-2026-11-28.md`, `examples/startup/`.
Numbers marked *(est.)* are estimates that Phase 0 must replace with measurements.

## 1. Concept: "QL/32: The Grand Tour"

A self-running, roughly five minute tour of a 32-bit BASIC computer. A **director** program owns a thin caption
strip and starts one or more **scene tasks** that draw into tiled windows. Each scene is a real worker task, so
the tour is itself the multitasking demo: the closing scene runs every effect at once with music, then reports
`FREE()` before and after to show nothing leaked.

Narrative: *boot, then see what is under the hood*. Pixels and palette, text, maths, multitasking, sound,
files, language, UI, finale. Each scene has a one-line caption naming the feature ("PALROT: 16 colours, zero
pixel writes").

Modes:

| Mode | How | Behaviour |
| --- | --- | --- |
| Autorun | `RUN "showcase.bas"`, or copy to `FFS:/startup.bas` (not the default; the splash stays default) | Scenes play in order, loop until a key. |
| Interactive | any key during autorun | `SPACE` pause, `N`/`P` next/previous, `M` scene menu (`MENU`), `Q` quit and restore the console. CYD touch: left/right third of the screen = previous/next. |
| Test | `showcase.bas` with a test flag read from a file or constant | Short scene durations; exits with a result line on `OPEN(2)`. For host/device smoke tests. |

## 2. Capabilities survey (what QL/32 can do, and where the demo uses it)

| Area | Capability | Source of truth |
| --- | --- | --- |
| Graphics | `PLOT LINE RECT CIRCLE TEXT CLS`, `INK/PAPER/OVER` (set, clear, XOR), `GCOPY` with OR/XOR/AND-NOT, `BLIT` of QIMG indexed-4 bitmaps with a transparent index, `GUPDATE 0/1/SKIP` batching | Language Reference "Graphics and display" |
| Palette | indexed palette, 8 QL colours by default; `PALETTE`, `PALROT`, `PALSHIFT`, `PALFADE`, `RGB`, `GPALETTE$`, `GPALETTESIZE()` (16 entries on some profiles; always query) | UI-001, LANG-001 s11 |
| Fonts | built-in handles 1 small, 2 normal, 3 large, 4 title, bold/underline styles, `LOADFONT` (Spleen 5x8, 6x12, 8x16, 12x24 in `data/fonts`), `GETFONT` metrics, `FONT`/`GFONT` | `ql32.inc`, `data/fonts` |
| Windows | retained canvases, `WINDOW` move/resize, `TASKVIEW` crop/pan, `TILE`, `WINDOWMODE ISOLATED/SHARED`, `MODE`, `ROTATE` (CYD) | Language Reference |
| Multitasking | `TASK`, `KILL`, `PAUSE`, `TASKVIEW`; slice and weighted priorities (`SLICE`, `PRIORITY`, `SUSPEND`, `RESUME`); non-blocking `INPUT`/`ALERT`/`MENU` (only the calling session waits) | README, LANG-001 s11 |
| Sound | `BEEP` (sine/square/triangle/saw), `VOLUME`, `MUSIC PLAY/PAUSE/STOP` for VTX/YM, readback `MUSIC STATE/POS/LEN/TITLE$` | Language Reference |
| Files | `OPEN` read/write/append/update, directory channels, `DIR`/`CAT` with wildcards, `COPY`/`MOVE`/`DELETE`/`MKDIR`/`RMDIR` with `FORCE`, `FFS:` and `SDC:` | README, LANG-002 |
| Language | procedures/functions with locals and recursion, `SELect`, `REPeat WHILE/UNTIL`, `FOR`, `DATA/READ/RESTORE`, `CONST`, `INCLUDE`, `TRAP`/`RAISE`, `ERR$`, `EVAL`, `INKEY(timeout)`, labels | LANG-001/002 |
| Numbers and strings | double precision by default, `OPTION KOptMathSingle` per module, `DIV/MOD`, bit ops (`BAND ROL SRA ...`), `HEX$/BIN$`, `LEFT$ MID$ INSTR REPT$ UPPER$ STR$ VAL`, `RND`, `TICKS()` (64-bit counter) | Language Reference |
| System | `SYSINFO$`, `CAPABILITY`, `FREE()`, `CPUFREQ()`, `OPTION KEspCpuFreq*`, debug bar, `MIRROR` (duplicate output to another window/serial) | Language Reference |
| UI | `ALERT`, `CONFIRM`, `IPRINT`/`BUSY` toasts, `DINIT/DIALOG`, `MINIT/MENU` | Language Reference |
| Input | serial keyboard (CYD, Tenstar), PS/2 (VGA boards), CYD touch channel (`OPEN(13)`) | Language Reference |
| I/O | GPIO, ADC, I2C, SPI, serial, timer, capacitive touch channels | HW-001 |
| Network | `GETNET`, `NETSTAT$`, `NETSIGNAL`, `NETTIME$` (SNTP), `DISNET` | CHANGELOG, NET-001 |

## 3. Scenes

Default tour (autorun) is scenes 0-9, about 20-30 s each. Scenes marked *opt* are off unless enabled or
supported (checked with `CAPABILITY`/`SYSINFO$`).

| # | Scene | What is on screen | Features exercised |
| --- | --- | --- | --- |
| 0 | **Boot** | The existing splash layout (title in big blocks, version, platform, free memory), then the caption strip appears | `SYSINFO$`, `FREE`, `CPUFREQ`, `GETFONT`, run-time centring (reuse `examples/startup/startup.bas` logic) |
| 1 | **Colour without pixels** | A plasma drawn once, then animated purely by `PALROT`; fade to white/black with `PALFADE`; a gradient bar built with `PALETTE`/`RGB` | palette ops, `GUPDATE`, `GPALETTESIZE` adaptation |
| 2 | **Type** | Every built-in font and style; the Spleen fonts loaded with `LOADFONT`; a smooth text scroller using `GCOPY` plus an XOR rubber-band cursor | `FONT/GFONT`, `LOADFONT`, `GETFONT`, `GCOPY`, `OVER 2` |
| 3 | **Numbers** | Two tasks run the same attractor, one `OPTION KOptMathSingle`, one default double, side by side with their `TICKS()` frame times; a live bit-twiddling readout (`ROL`, `HEX$`, `BIN$`) | `OPTION`, `TICKS`, bit ops, `SIN/COS`, `TASK` |
| 4 | **Many things at once** | Tiles: Mandelbrot, sine/cosine surface with `TASKVIEW` panning, bouncing balls, attractor. A text tile lists task state. The director raises one task's priority and the tile visibly speeds up | `TASK`, `TASKVIEW`, `PRIORITY`/`SLICE` (see Phase 0 check), `WINDOWMODE`, `KILL` |
| 5 | **Sound** | Waveform shapes drawn while `BEEP` plays sine, square, triangle, saw; then the tune starts with a title/position bar from `MUSIC TITLE$/POS/LEN` and an animated equaliser that is driven by song position (decorative, not a spectrum) | `BEEP`, `VOLUME`, `MUSIC`, readback, `INKEY` |
| 6 | **Sprites without sprites** | Parallax scrolling landscape with `GCOPY`, animated sprites using `BLIT` with a transparent index (or procedural bitmaps), collision flicker-free via XOR | `BLIT/LOADIMAGE`, `GCOPY` ops, `OVER`, `GUPDATE` |
| 7 | **Files** | In a sandbox directory: create files, `CAT` with wildcards, `COPY` with a progress bar, `COPY`/`MOVE` `FFS:` to `SDC:` when an SD card is present, refuse-then-`FORCE` overwrite, wildcard `DELETE`, `RMDIR FORCE`; everything is cleaned up | file ops, `OPEN`, directory channels, `MKDIR`, `TRAP` |
| 8 | **Language** | Animated Tower of Hanoi (recursive procedure with locals), an `EVAL`-driven plotter, `TRAP`/`RAISE` catching a deliberate error and printing `ERR$`, `SELect` on strings, `DATA` driven pixel-art | procedures, recursion, `TRAP`, `EVAL`, `SELect`, `DATA/READ` |
| 9 | **UI does not block** | While scene-4 tiles keep running, an `ALERT`/`CONFIRM`/`MENU`/`DIALOG` sequence appears (auto-dismissed by the director with a typed answer in autorun, real input when interactive); `IPRINT`/`BUSY` toasts | `ALERT`, `CONFIRM`, `DIALOG`, `MENU`, `IPRINT`, `BUSY`, session-only blocking |
| 10 | **Finale** | Everything at once: 4 effect tiles, music, scrolling credits. Prints `FREE` before/after after all tiles are killed | all of the above, leak check |
| opt A | Touch paint (CYD) | drawing with the touch channel, XOR cursor | `OPEN(13)`, `INPUT #touch` |
| opt B | Rotate (CYD) | `ROTATE 90`/`0` while a task animates | `ROTATE`, `GROTATION` |
| opt C | Mirror | `MIRROR #0 TO window` so serial/console text also appears in a tile | `MIRROR` |
| opt D | Network clock | Wi-Fi to the open network, `NETTIME$` clock face (needs credentials, pins about 16 KB on CYD) | `GETNET`, `NETTIME$`, `NETSIGNAL` |
| opt E | Hardware I/O | ADC noise scope or timer channel on a configured pin (needs wiring or board support) | `OPEN(KDevice...)` |
| opt F | Video modes (VGA only) | `MODE LIST`, switching resolution | `MODE` |

## 4. Concurrency and music

* The director is the only long-lived foreground program. It uses a caption window (about 320x24 on the CYD,
  roughly 3.8 KB at 4 bits per pixel) and never allocates a full-screen canvas.
* Each scene task starts with `WINDOW` sized to its tile (see section 5), then `TASKVIEW` places it. Workers use
  `WINDOWMODE ISOLATED` unless a scene needs to share the display.
* Music is started by the director (`MUSIC PLAY`), before the scene tasks allocate, because starting needs a free
  contiguous block (8 KB admission plus 4 KB for song loads) and the CYD largest block shrinks as tiles open. The
  scene order therefore starts music in scene 5 and keeps at most 3 tiles open while it plays on the CYD; the
  finale uses 4 tiles only where the budget in section 6 allows.
* Tune choice: `examples/music/LyraII1.vtx` (878 bytes) and `Bosconia1.vtx`. Avoid YM songs with large drum samples
  (the CYD drops drums beyond 4096 samples) and avoid first-play transcoding delays by pre-playing once at install
  time or by choosing VTX.
* Pacing uses `INKEY(timeout)` as the frame sleep so the director responds to keys while only its own session waits.
* Hard rule from the language session: a blocking-looking call suspends only its own task, so the director can run a
  dialog while tiles animate.

## 5. Layout across platforms

Reuse the splash approach: no hard-coded coordinates. A small library, `examples/showcase/lib/layout.bas`,
provides functions only (about 30 lines), included into the director and scenes:

* `ScreenW()`, `ScreenH()` from `GWIDTH()/GHEIGHT()` taken before the first `WINDOW` call (verify in Phase 0 that a
  fresh task's default window reports the usable area, which excludes the console strip).
* `Cols()`, `Rows()` and `TileW()`, `TileH()`, `TileX(i)`, `TileY(i)`: rule of thumb: usable width below 260 px gives
  1x2 tiles, below 500 px gives 2x2, above gives 3x2. Tile size is derived from the screen, so every worker computes
  the same value and the director passes no arguments (`TASK` takes a path only).
* `CentreX(text$)`, `Large$`/font selection from `GETFONT` metrics, as in `startup.bas`.
* `Pal(n)` gives `n` palette entries, degrading to the 8-entry QL set when `GPALETTESIZE()` is small.

Resolution classes to support and test:

| Platform | Default graphics area | Tiles | Notes |
| --- | --- | --- | --- |
| CYD ESP32-2432S028 | 320x240 less the 3-row console, about 320x216 | 2x2 of about 160x108 | hardware-verified platform; touch, serial keys |
| Tenstar ESP32-S3 | 240x135 less the 40 px console strip, about 240x95 | 1x2 of about 120x95 | not all scenes fit; audio unverified, gate with `CAPABILITY` |
| Olimex / LILYGO VGA | 320x200 default; 400x300, 640x400/480 modes | 2x2 up to 3x2 | not hardware-verified; PS/2 keys |
| Host (macOS/Linux) | any | any | CI and layout tests |

Every scene must first check what it needs and skip itself with a caption ("not available on this board") rather than fail.

## 6. Memory budget

Measured anchors (CYD): idle 176 KB free / 110 KB largest; each extra worker costs about 10-11 KB (6 tasks:
116 KB / 106 KB); full-screen canvas 38.4 KB (4 bpp); 160x120 tile 9.6 KB; full-screen dirplayer left 38 KB free
but only 8 KB largest while music played (later 11 KB); a 25.6 KB canvas version is what made it fit; Wi-Fi/NTP pins about
16 KB; the retained program of the 407-line dirplayer was about 83 KB before compact bytecode. Tenstar: 297 KB / 270 KB internal idle,
2 MB PSRAM (only allocations of 16 KB or more land there; canvases of 11 KB do not).

| Item (CYD) | KB *(est.)* |
| --- | --- |
| idle | 176 |
| director program and caption window | 25 (about 200 lines, plus 4 KB window) |
| 3 tiles (scene tasks incl. their own programs, 160x108 canvases) | 35-45 |
| music (VTX stream, buffers, task stacks) | 10-15 |
| network, if enabled | 16 |
| **left, finale with 4 tiles and music** | **about 60-80, largest about 40** |

Rules: scene source files stay below 120 lines each (INCLUDEd code is **duplicated into every task that includes
it**, so the layout library is tiny and function-only), tile canvases stay at or below 160x120, no full-screen canvas,
`GUPDATE 0` batching instead of buffers, arrays small. Scenes allocate on start and `KILL`/exit frees everything; the finale checks that `FREE(0)`
and `FREE` largest return to within 1 KB of the start. Tenstar uses the same sources with `TileW` computed from its smaller area and
has ample room. All numbers above must be confirmed in Phase 0 and tracked through the debug bar and `HEAP STATS` (`failedalloc` must stay 0).

## 7. File layout and size

Single source of truth in `examples/showcase/`, copied unchanged to every board (same rule as the splash: only run-time queries differ):

```
examples/showcase/
  showcase.bas          director: caption strip, scene runner, keys, menu     ~220 lines  ~8 KB
  lib/layout.bas        INCLUDEd helpers                                       ~30 lines   ~1 KB
  scenes/s00-boot.bas ... s10-finale.bas   one task per scene               ~60-120 lines each, ~3 KB each
  assets/ (optional)    a few QIMG sprites                                     <= 8 KB total
  README.md             install per board, keys, how to add a scene
```

Total about 45-55 KB of source on `FFS:`; music from `examples/music` (about 1-3 KB for VTX). Packaging: `tools/` script copies the folder
to the release zip (no per-board forks), install steps in the README (YMODEM upload per board, as for the splash).
Modules vs INCLUDE: scenes are separate files because `TASK` takes a path; INCLUDE only for the tiny layout library.

## 8. Phased implementation

Each phase ends with a smoke test and its own PR.

| Phase | Work | Smoke test | Effort |
| --- | --- | --- | --- |
| 0 Spike | Measure director + 3 tasks + music on the CYD and Tenstar; verify `GWIDTH` before `WINDOW`, `PRIORITY/SUSPEND` callable from BASIC (LANG-001 s11.8 lists them as shell commands; if not, scene 4 changes tasks' own `PAUSE` rate instead), `INKEY` on serial, `MIRROR` | numbers in a test-result doc; go/no-go | 0.5 day |
| 1 Framework | `layout.bas`, director, caption strip, scene runner, keys/menu, test mode, scenes 0 and 10 stubs | host layout test at 6 resolutions; host test compiles every showcase file; CYD `@SCREEN` | 1-1.5 days |
| 2 Visual scenes | 1, 2, 3, 6 | host headless run of each scene for N ticks with no error; CYD screenshots; heap returns | 2 days |
| 3 Multitasking and UI | 4, 9, opt C | CYD 1-4 tiles, `failedalloc=0`, stress shape like the 1-6 task table; dialog while tiles animate | 1.5 days |
| 4 Sound and finale | 5, 10 | CYD `late=0`, `failedalloc=0`, `musicmaxgap` reported with LyraII1 and Bosconia1; 30 min soak, free/largest before = after | 1.5 days |
| 5 Files and language | 7, 8 | host test with temp directory; CYD with and without SD; sandbox directory removed | 1 day |
| 6 Optional and release | opt A-F gated by capability, Tenstar pass, README, CHANGELOG, release pack, golden 16/16 regression | per-board screenshot set under `docs/test-results/` | 1-1.5 days |

Total about 8-10 days of focused work. Standard gates apply (host ctest plus sanitized, four `pio` builds, hardware gate on the CYD; Tenstar
for layout; Olimex/LILYGO build-only).

## 9. Risks

* **Memory** (largest block on the CYD, especially with music and 4 tiles): mitigated by 160x120 tiles, tiny library, music first, Phase 0 measurement, per-scene tile cap.
* **Scheduler fairness and speed**: the CYD VM is about 4x slower than the Tenstar on integer loops; scenes must be tuned by frame-time, not iterations, and use `PALROT` and `GCOPY` rather than per-pixel loops.
* **`PRIORITY/SUSPEND/RESUME` may be shell-only**: scene 4 falls back to differently paced tasks.
* **INCLUDE duplicates code per task**: keep the library tiny, or inline per scene.
* **First music play can take seconds to minutes on the CYD (transcode)**: use VTX and pre-play in the install step.
* **Dialogs need input**: autorun auto-answers via the director; interactive mode depends on serial/touch/PS/2 availability.
* **Unverified boards** (Olimex, LILYGO, Tenstar audio and keys): host tests for layout; label them clearly in the README.
* **Flash wear and storage**: the files scene uses a sandbox directory and removes it; never `FORMAT`.
* **Maintenance**: any language change could break a scene; the host test that compiles and runs every scene is part of CI.

## 10. What cannot be shown, and why

* **A true audio spectrum/VU meter**: there is no register or sample readback; the equaliser is decorative and driven by song position.
* **Hardware sprites or scrolling**: none exist; the demo emulates them with `BLIT`/`GCOPY`/XOR.
* **True colour**: the display is palette indexed (8 default entries, 16 on some profiles); palette tricks are shown instead.
* **64-bit integers**: there is no 64-bit integer type. Numbers are double (or single with `OPTION`); only `TICKS()` is exact to 2^53. The demo shows single versus double and the 64-bit tick counter instead.
* **The debugger** (`DEBUG`, `BREAK`, `STEP`, `WATCH`): shell commands for a terminal, not callable from BASIC. Shown as a README screenshot/transcript, not live.
* **Hardware I/O (GPIO, I2C, SPI, ADC)**: needs wiring and pin maps; optional scene only. Host drivers are virtual.
* **Network**: needs Wi-Fi credentials; only status and SNTP time exist (no HTTP/TLS client), and it pins about 16 KB on the CYD; optional.
* **Touch, `ROTATE`**: CYD only. **PS/2 keyboard and `MODE` switching**: VGA boards only.
* **SMSQ/E or QL emulation features** (pointer environment, QDOS sprites, windows managers): QL/32 is not an emulator.
* **Board-specific verification**: Olimex, LILYGO and Tenstar results are layout-tested only, audio unverified on Tenstar.

## 11. Decisions for the product owner

1. **Concept**: (a) **guided tour + scene menu (recommended)**, (b) pure continuous demoscene show with no captions, (c) interactive launcher only (no autorun).
2. **Scope**: core scenes 0-10 only, or include optional scenes A-F (touch, rotate, mirror, network, hardware, modes)?
3. **Music**: bundled tiny VTX tunes from `examples/music` (recommended) or user-supplied songs on SD?
4. **Assets**: procedural bitmaps only (no licensing, smaller) or add a few generated QIMG sprites (needs a small converter tool)?
5. **Autorun**: opt-in via `showcase.bas` (recommended; splash stays the default `startup.bas`) or an alternative `startup.bas`?
6. **Name**: "QL/32: The Grand Tour" or another.
