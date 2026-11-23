# QL/32 project handoff

Use this file to continue work as any coding agent or human contributor. Read
it together with [`AGENTS.md`](AGENTS.md) before changing the project. The
specifications and dated reports in `docs/test-results` are the project
record; refresh this handoff after meaningful implementation or
hardware-acceptance changes. Last refreshed 2026-11-22 (documentation audit:
[report](docs/test-results/documentation-audit-2026-11-22.md)).

## Project and architecture

QL/32 is a portable, native BASIC computer inspired by SMSQ/E SuperBASIC and
Psion-style application interfaces. Linux and macOS are host targets. ESP32
targets include LILYGO FabGL VGA32, Olimex ESP32-SBC-FabGL, ESP32-2432S028
(CYD), and the TENSTAR TS-ESP32-S3. Keep the language,
VM, runtime, system, and UI layers independent of board libraries. Session
resources belong to their BASIC session, and UI waits suspend only that
session.

## Repository state (2026-11-21)

The previously local integration reached `origin/main` at `289f0c8`.
The owner approved publishing the MODE fixes (`c562551`), followed by the
videoram analysis (`6377b41`, cherry-picked from `76a932f`), and this handoff
refresh as a fast-forward to `origin/main`. Check the live remote tip before
continuing. The integration branch
`jan-pal-branch-integration` merged these branches (merge order; all clean
except doc conflicts):
`jan-pal-olimex-sound-audit` (sound, YM/AY/PT3/VTX/SQT/WAV, `BEEP`/`VOLUME`,
stale-test fixes), `jan-pal-handoff-refresh`, `jan-pal-basic-file-seek`,
`jan-pal-cyd-memory-audit`, `jan-pal-automatic-potato` (Olimex VGA work).
`jan-pal-fix-ctest-failures` duplicates the sound branch's `49887c0` and was
skipped. Check Git and the remote rather than assuming the source branches
or this historical integration state are unchanged.

Status 2026-11-22: `origin/main` contains the integration, the retro default and
the bytecode compaction; the "local only" and 83/84-test statements in this
section and under Next work are historical (current host suite: see the audit
report).

A user music patch (modified `music.hpp`, `music.cpp`, `interpreter.cpp`,
`bytecode.hpp`, `music_tests.cpp`) may exist **uncommitted in the main
checkout** (`/home/osg/GitHub/QL32`). It overlaps files the sound work
rewrote; reconcile by hand and do not discard it.

Native Linux: `cmake --preset linux`, `cmake --build build/linux`,
`ctest --test-dir build/linux` (there is no `linux` test preset) passes
83/83 at the `289f0c8` baseline and 84/84 with the MODE fixes, including the
new FabGL display-mode adapter regression target. The subsequent videoram
analysis and publication handoff changes are documentation-only.

Terms below: **built** = firmware/host target compiles; **host-tested** =
covered by Linux CTest (fakes, no board); **hardware-verified** = observed on
a board with the owner present.

### Built, host-tested, hardware-verified

| Item | Built | Host-tested | Hardware-verified |
| --- | --- | --- | --- |
| Olimex DAC output fix (I2S `STAND_MSB`) | yes | `dac_format`/tone tests | yes, by ear ([report](docs/test-results/olimex-sound-2026-11-20.md)); DMA 640x480 sound and long-run playback not verified |
| `BEEP ms,hz[,waveform]`, `VOLUME 0..255` (QL/32-specific, default 128; not SMSQ/E-compatible) | yes | yes | yes, by ear: 5 waveforms, VOLUME 32/128/255, under attractor load |
| `MUSIC WAV` (QL/32-specific streaming PCM) | yes | yes | yes: vacation, theme streamed, late=0, maxGap ~11.6 ms |
| PT3, VTX (256 KiB/512 KiB limits), SQT players (QL/32-specific) | yes | yes (example modules audible and loop) | yes, by ear: PT3 Melancholy/TheDome/Peculiar/Walker, VTX z00m_unreal/Bosconia1/LyraII9, SQT Enigma/Zabreh |
| AY core fixes: envelope rate, standard relative volume table, VTX R13=0xFF "not written", unipolar mix with DC blocker, ultrasonic-period averaging | yes | yes (`testAyUltrasonicTone`) | yes, by ear (listening report) |
| YM2/3/3b/5/6 (incl. LHA) with SID, digi-drum, sync-buzzer | yes | yes (`testYmEffects`) | yes, by ear: ZYNAPS1, buzz, jess2, rapido1, ND-Loader, ND-Toxygene, VirtualEscape1/2, BITOFF2, led1 |
| YM sinus-SID (approximation) and YM2 Mad Max drums | yes | synthetic only | **no** (no example files; Mad Max drum samples not embedded, muted unless `YmSong::setDrumBank`) |
| YMT1/YMT2/MIX1 | no | no | no; stage 6 skipped by owner decision |
| File `#ch AT`, `FPOS`, `FLEN`, `OPEN(.., "UPDATE")` (QL/32-specific) | yes (Linux, ESP32 adapters) | yes, against fake filesystems | **no** (CYD FFS/SD untouched) |
| Bytecode memory compaction: numeric arrays as 8 B doubles (`DIM` preflight uses the real element size, so larger arrays now fit), `Instruction` 256 -> 40 B on x86-64 (pooled text pointer, packed flags, narrow fields); behavior unchanged ([report](docs/test-results/bytecode-memory-2026-11-22.md)) | yes, all four PlatformIO envs | yes (CTest 88/88, `bytecode_layout_tests`) | Olimex: **verified on hardware**; CYD not verified |
| CYD memory phase A: `DIM` rejects arrays above the largest free heap block, task canvas sized to the task viewport, Wi-Fi disconnect powers the radio down | yes | yes (`runtime_tests`, `graphics_tests`, `esp32_network_tests`) | **no** (no CYD run after the change) |
| PRINT slice charge (48 instructions per finished PRINT line), FabGL present coalescing, clear fixed-mode "mode N is unavailable" error, no-PSRAM mode preflight | yes | yes (`testPrintLinesAreChargedToSliceBudget`, `testVgaModeMemoryPreflight`, fixed-mode tests) | yes for fps/stress on `graphics_profile` and the `MODE 0,0` message on the 640x480 profile; see below |
| Olimex 640x480/640x400 present telemetry and experiment envs | yes | `ql32_olimex_present_stats_tests` and others | yes, counters and pictures |

`BEEP`/`VOLUME` and the music formats are specified in
[QL32-LANG-001](docs/specification/QL32-LANG-001.md), the Olimex
[board README](boards/olimex_esp32_sbc_fabgl/README.md) and the language
reference. SMSQ/E has its own `BEEP` with different parameters; QL/32's is
QL/32-specific. The sound work used vitamin-caig/zxtune (LGPL) and
arnaud-carre/StSound (MIT) as behavioral references only; no code was copied.

### Olimex VGA findings (2026-11-17 to 2026-11-21)

Hardware-verified with the owner present; reports are under
`docs/test-results/olimex-*-2026-11-2*.md`. Summary:

- MODE reliability follow-up on the qualified plain PSRAM build: FabGL
  actually exposes presets 0-4, not just 0-2; this does not promise that
  their internal DMA framebuffers fit. Normal startup on the tested board
  supports single-buffer modes 0/1, while 2-4 lack allocation capacity.
  Free PSRAM is not usable for FabGL DMA scanout. Runtime `MODE 0,1`
  reproduced heap corruption from partial double-buffer row allocation.
  Growth/option changes now probe complete internal row-pool allocation
  after releasing the old viewport, with rollback on refusal; rollback
  failure is explicitly reported. Shrinking 1 -> 0 previously failed with
  "composed region exceeds VGA canvas geometry" because console callbacks
  merged old deferred damage; coalescing now clips merged damage to the
  current display. Owner confirmed distinct, stable 320x200 and 320x240
  labeled pictures and a usable final MODE 0 recovery prompt. Final serial
  matrix: 41 attempts with matching state/geometry checkpoints; MODE 1
  additionally refuses internal-memory allocation with music plus four
  workers active, leaving MODE 0 and workers intact. Workers/audio stopped,
  serial released. No timing, ring-depth or core-pinning changes. See the
  [MODE reliability report](docs/test-results/olimex-vga-mode-reliability-2026-11-21.md).
- Presentation, not scanout, is the cost on standard placement: full 640x480
  present about 0.50 s (640x400 about 0.38 s) after the aligned encoder, cheaper
  console compose and chunked row write; about 85-100 present requests per
  30 s. Late refills: 0.
- Rejected speed experiments (parked, diagnostic envs only): split-core
  placement (VM on core 0; lost sync, 14-27% late refills, flicker), present
  coalescing on the DMA path (no effect on VM requests), per-frame byte budget,
  IRAM refill placement, small-batch and bulk-copy refill variants (no gain).
  PSRAM store traffic was not the bottleneck (refill uses about 80% of core 1).
- Standard placement (`..._640x480_present_profile`, `..._640x400_present_profile`)
  is the supported 640x400/640x480 path. The custom PSRAM-DMA scanout stays
  experimental; FabGL is the qualified path.
- Mode matrix: FabGL MODE 0-2 (320x200/320x240/400x300, `graphics_profile` or
  `_psram` envs) scan out from internal DMA RAM; MODE 2 needs a fresh heap
  (after other tasks it fails gracefully with "allocation incomplete" and
  rolls back). The no-PSRAM build refuses MODE 1/2 with a BASIC error. The
  fixed-mode 640x400/640x480 builds accept only their own mode id.
- Standard-profile "about 1 fps": an attractor alone presents about 3 times per
  second with a 0.4 s full present, so about 1 fps is the measured baseline;
  with the 4-task stress it drops to about 0.4 fps and the standard placement is
  unfair under load. Not a regression. Workers like `examples/stress/*.bas`
  never exit: `KILL` them or reset the board to regain the prompt.
- Scheduler fairness: PRINT lines are charged 48 instructions to the VM slice
  budget; MODE 0 attractor + PRINT 28.2 s -> 12.6 s per 20 frames, MODE 2 stress
  mix fair. The standard 640x480/640x400 stress mix was re-run before the fix
  (slow, no errors, late 0, heap flat); only the `MODE 0,0` message was re-checked
  after it. Fixed-mode builds are unaffected by present coalescing.
- Multi-task stress (3-4 min): no watchdog, brownout, out-of-memory or heap
  drift; late refills 0.
- amiga500-esp32 review ([note](docs/test-results/olimex-amiga500-esp32-review-2026-11-21.md),
  read-only, MIT, SPI TFT not VGA): nothing to import. The only candidate is a
  per-row dirty compare inside full presents, to be A/B tested with `PRESDIAG`
  counters; it may cost more PSRAM bandwidth than it saves.
- Details: [investigation notes](docs/plans/olimex-vga-640x480-dma-investigation.md).

### Videoram scanout follow-up (2026-11-21)

The [design analysis](docs/plans/videoram-scanout-design.md) compares shared
videoram with the existing PSRAM-to-internal-DMA-ring implementation; no
runtime code, pinning, refill priority or timing was changed. The
[hardware report](docs/test-results/videoram-scanout-analysis-2026-11-21.md)
records built diagnostic profiles and owner-observed short runs: static
400x300 was stable, and active packed-4 400x300 had stable palette cycling
with zero late refills under PSRAM stress. The base 640x400 packed-4 run had
about 14% late refills and localized marker flicker; the gradient looked
stable but lacked usable refill telemetry. Neither result qualifies general
shared-videoram runtime use or production 640x400 scanout.

The experiment restored qualified PSRAM FabGL, with a responsive REPL and
owner-confirmed stable picture, and released serial. That restoration was
built from the videoram branch's pre-MODE-fix baseline; it does not establish
that the newly published MODE fixes are currently installed on the board.
Keep FabGL as the default; further scanout changes require separate,
telemetry-backed testing.

## Olimex single-framebuffer 320x240 / 320x200 (2026-11-21)

Same flag, envs `..._psram_dma_display_{320x240,320x200}_single_fb[_status]`
and `..._test_{mode}_sf_{static,checker,gradient,random,fullscreen,scroll}`.
Geometry 120/60 (320x240) and 100/50 (320x200), no core/IRQ/timing change.
A diagnostic-only horizontal shift on 320-wide modes caused late refills and is
now disabled in single-fb. Hardware: all patterns, 11 min soaks and runtime
BASIC palette animation + 20 cancel cycles gave 0 late refills. MODE was
compile-time per env at that point; it later became runtime-selectable
(`MODE 0/1/2`) in `olimex_esp32_sbc_retro` (see Olimex PlatformIO
environments). The family and colour limit are analysed in
`docs/test-results/olimex-single-framebuffer-320x-2026-11-21.md`. Default env
unchanged; not pushed.

## Olimex single-framebuffer mode (2026-11-21)

`QL32_OLIMEX_SINGLE_FRAMEBUFFER` (env `..._psram_dma_display_400x300_single_fb`,
plus `_single_fb_status` with `DMADIAG` and a `_test_400x300_single_fb`
diagnostic) uses one 60 KB PSRAM framebuffer drawn directly and scanned
continuously; tearing is allowed, no publish/clone/stale repair. Packed-4
nibble RMW is serialised by `packed4_rmw.hpp`. Hardware (ring150, refill
profile): five stress patterns, an 11 min soak, a 40x RUN/Ctrl-C loop and BASIC
palette animation all showed 0 late refills; a presenter write racing `end()`
is guarded by an active-writer count. Not verified: soaks beyond 11 min and a
REPL hang on very long input lines. 320x200/320x240 are the planned
next adopters; 640x400/480 are exceptions. See
[results](docs/test-results/olimex-single-framebuffer-2026-11-21.md).

## Olimex FFS write hold (2026-11-23)
FFS (SPIFFS) flash writes stall the cache and used to blink the VGA. Writes are bracketed by `BulkWriteScope` (`runtime/bulk_write_gate.hpp`); the scanout keeps an internal-RAM snapshot of the real picture, replayed by an IRAM ISR, with the IPRINT toast (yellow frame, black, red blinking text, top-right) drawn into it. Solid-colour fallback if internal heap margin < 48 KB. SDC and FabGL paths untouched. DMADIAG shows `held-groups`/`snapshot-groups`. Lesson: ISR cost must stay low (heavy ISR broke YMODEM). Open: first-upload-after-reset flake, rare release race. See [note](docs/test-results/olimex-ffs-hold-2026-11-23.md).

## Olimex PlatformIO environments

- **Default (owner-approved 2026-11-21):** `olimex_esp32_sbc_retro` (and
  `olimex_esp32_sbc_retro_status` with `DMADIAG`) is the default WROVER/PSRAM
  firmware: single-framebuffer packed4 retro modes with runtime `MODE 0/1/2` =
  320x200 / 320x240 / 400x300 (boots 400x300; MODE 3/4 rejected; 16-entry
  RGB222 palette). It is in `default_envs`.
- **FabGL (separate build, cannot coexist in one image):**
  `olimex_esp32_sbc_fabgl_psram` is the qualified FabGL WROVER firmware and the
  route to 640x400/640x480 and the FabGL fallback. Flash it explicitly with
  `pio run -e olimex_esp32_sbc_fabgl_psram -t upload`; flash the retro default
  with `pio run -e olimex_esp32_sbc_retro -t upload`.
  `olimex_esp32_sbc_fabgl` is the non-PSRAM base profile.
- **Experimental `*_psram_dma_*`:** custom PSRAM-framebuffer scanout with an
  internal DMA line ring, extending the PSRAM env. Families:
  `..._psram_dma_test*` (synthetic scanout/pattern tests, packed4, ring and
  size variants), `..._dma_framebuffer_benchmark*`, `..._psram_dma_display*`
  (runtime QL/32 on the DMA scanout: 320x200 default, `400x300`, `640x400`,
  `640x480` packed4, `*_profile` with `DMADIAG`, `*_split_cores_test` and
  `ring160` variants) and `..._dynamic_mode_test`.
- **Present profile (VGA branch only):**
  `olimex_esp32_sbc_fabgl_psram_dma_display_640x480_present_profile` adds
  `PRESRESET`/`PRESDIAG` presenter counters to the standard 120-row ring with
  BASIC, ISR and refill on core 1. It is the **stable standard 640x480
  diagnostic profile** and the one to reflash after any experiment.
- **Split / budget / IRAM (VGA branch only), all diagnostic and flickering:**
  `..._640x480_present_split_cores_test` (BASIC on core 0, ISR/refill on
  core 1), `..._present_split_budget_test` (adds `PRESBUDGET <bytes/frame>`)
  and `..._present_split_iram_test` (refill hot path in IRAM).
- Flash the application partition only for experiments; FFS and SDC contents
  must stay untouched. Prefer the `esp32_2432s028_spiffs` profile for CYD.

The latest implementation includes:

- M1 BASIC core: expressions, variables, structured `IF`, flexible block and
  inline `FOR`, `REPeat`, procedures/functions/locals, arrays, block
  `SELect ON`, abbreviations and canonical `LIST`; numbered programs and a
  persistent host REPL.
- Host storage and session-owned channels, `LOAD`/`SAVE`/`DIR`, `OPEN`,
  line and exact-byte file I/O, and asynchronous UI requests. `ALERT`,
  `CONFIRM`, dialogs and menus suspend/resume only their calling session.
  Optional `PAGER ON` pagination covers generated listings; graphics adapters
  show the input prompt on screen. Submit an empty line, `L`, or `Q` to advance,
  step, or stop the listing.
- Retained indexed graphics windows, `PLOT`, filled/outline primitives,
  window movement/resizing, CYD rotation
  and palette queries, Linux PPM output, and CYD/VGA32 graphics adapters.
  `WINDOW #channel,x,y,w,h` now selects independent per-session channel
  surfaces, including a graphics surface for console channel `#0`; `PRINT #0`
  remains console output. CYD and FabGL console overlays follow `#0` bounds.
  `CLS` accepts no arguments, `#channel`, `ink,paper`, or
  `#channel,ink,paper`; color arguments persist on that graphics surface.
  Graphics channels support
  Spleen 5×8, 6×12, 8×16, and 12×24 via shared session font handles, separate
  `FONT`/`GFONT` styles, `UNLOADFONT`, and `GETFONT`; QFNT v2 reads glyphs from
  storage on demand. The CYD received and visually rendered all four files over
  YMODEM on 2026-11-10; cross-board visual checks and heap measurements remain
  open. Classic console text remains unchanged. The classic glyph set covers printable
  ASCII `0x20`–`0x7E`, verified by VGA capture. CYD users have
  confirmed compositor movement, resizing/clipping and rotations; the four
  `CLS` forms ran without syntax or VM errors, though overlapping windows
  prevented visual isolation of clear colors. The attractor's `PLOT` output was
  captured and visually confirmed on 2026-11-07.
- TENSTAR TS-ESP32-S3 target with ST7789 240×135 landscape graphics, USB CDC
  REPL, 4 MiB flash layout, QSPI PSRAM, and 512 KiB SPIFFS. `startup.bas`,
  graphics/memory tests, three continuous worker programs, and a Mandelbrot
  workload are uploaded from `data/boards/ql32_tenstar_esp32s3/`. Sensors are
  intentionally deferred.
- Cooperative weighted session scheduling and an embedded persistent REPL.
  Current supervisor/BASIC task APIs use `TASK`, `KILL`, `TASKRESULT`,
  `TASKCHANNELS`, `LOGON`, `LOGOFF`, `SUSPEND`, `RESUME`, `SLICE`,
  and `PRIORITY`. `TASK` has command and function forms, optional
  `KTaskWait`, and task status/error queries `ERR`, `ERR$(code)`,
  `ERRX$`.
- M6 Wi-Fi station calls: `GETNET`, `DISNET`, `NETSTAT$()`,
  `NETADDR$()`, and `NETSIGNAL()`; recursive source `INCLUDE` with path,
  cycle, depth, size and module-preamble checks.
- Recent language helpers: UTF-8 `LEFT$`, `RIGHT$`, `MID$`; `SIN`,
  `COS`, `INT`; unsigned 32-bit `BAND`, `BOR`, `BXOR`, `BNOT`,
  `ROL`, `ROR`, `SRA`, `SRL`, `SLA`, `SLL`; `&H`/`&B` literals and
  `HEX$`/`BIN$`; session-owned `RND()` overloads and `RANDOMIZE seed`.
  Graphics refresh batching uses `GUPDATE`.

The 2026-11-14/15 functionality milestone added QL/32-specific QIMG v2
indexed-4 images (`LOADIMAGE`, transparent `BLIT`, `UNLOADIMAGE`) with an
embedded 16-color RGB palette and nearest-color mapping to the active display
palette. The Python remote debugger now includes image load, resize, preview,
QIMG export, and YMODEM upload. Earlier physical image checks used v1 fixtures;
the Olimex PSRAM build was flashed on 2026-11-15 and passed the QIMG v2
YMODEM/LOAD/BLIT/UNLOAD fixture with a VGA capture. Host checks and the desktop
GUI dependency limitation are recorded in
`docs/test-results/qimg-v2-implementation-2026-11-15.md`.
The milestone also includes program-controlled
`PALETTE`/`RGB`/`PALROT`/`PALSHIFT`/`PALFADE`, portable
`SYSINFO$`/`CAPABILITY` profile introspection, supervisor `TILE`, and menu item
enablement via `MENABLE`. Olimex serial checks covered the profile APIs and
palette transforms. The three-window TILE layout and visibly dimmed disabled
menu item were verified on VGA. See the SRS backlog and dated test reports.
The Olimex debugger serial receiver now detects raw `@QL32DBG/...` frames
before the graphical terminal/keyboard decoding path; a direct `tasks.list`
request returned valid JSON after flashing. The PS/2 Break event is already
queued, but a scheduler tick and synchronous BLIT can delay handling it. Fast
Break within long drawing operations remains open; see
`docs/test-results/debugger-serial-input-2026-11-15.md`.
Olimex profile configuration now has CTest coverage for the GPIO assignments,
capabilities, PlatformIO SPIFFS flags, and non-overlapping 512 KiB FFS
partition. The checks passed directly with `g++` and Python on 2026-11-16;
CMake is not installed in this environment, so CTest registration could not be
exercised here.
The VGA console also now refreshes its cached screen dimensions when the
window manager resizes for a display mode change. This fixes clipping the
console to the original 320×200 geometry on larger/taller modes. Olimex PSRAM
firmware builds with the fix; physical mode cycling still needs device
verification.
Olimex startup graphics now persist after `FFS:/startup.bas` returns to its
supervisor REPL. The VM/scheduler were closing the program's `OPEN(4)` window
channel and tearing down the supervisor graphics session; both now follow the
interactive session lifetime. The startup source uses the returned handle
instead of assuming `#2`. Host scheduler regression and on-device serial
checks passed; VGA visual confirmation is pending. See
`docs/test-results/startup-window-lifetime-2026-11-15.md`.
On 2026-11-20 the native Linux build (`cmake --preset linux`, then
`cmake --build build/linux` and `ctest --test-dir build/linux`; there is no
`linux` test preset) passed 82/82 on the sound branch (83/83 on the integration branch) after three stale tests
(scheduler raise ids, channel routing, hardware UART) were updated for
program-owned channels and per-interpreter graphics session ids. `BEEP` and
`VOLUME` are summarized in the state table above.
On 2026-11-08 the suite passed (65/65).
The scheduler pager assertion was stale: its expected text used lowercase
`Enter=page`, while the current prompt is `Enter=Page, L=Line, Q=Quit`. The test
was updated to the prompt contract, preserving the assertion that only the
pager's session suspends while another task continues. CYD and Tenstar firmware
builds and Tenstar graphics regression coverage also passed; detailed board
results are in [the 2026-11-08 acceptance report](docs/test-results/board-acceptance-performance-2026-11-08.md).

On 2026-11-05, the Olimex firmware was rebuilt and flashed. Board checks
verified the full printable ASCII glyph set in a 320x200 VGA capture, paged
HELP resumption after `Q`, and rendered Mandelbrot output. The Mandelbrot demo
now uses an 80x56 streaming renderer on the default graphics surface. Its
`WINDOW` resize statement raised `bad_alloc` on this board, even at 80x64, so
the demo avoids resizing. The runtime, scheduler, graphics, and graphics UI
host suites passed when compiled directly with `g++`; CMake was unavailable, so
this was not a full CTest run at that time; the full native Linux suite later
passed 64/64 on 2026-11-06.

## Decisions to preserve

- SMSQ/E SuperBASIC is the core language reference; compatibility claims need
  a cited reference and conformance test. Label other behavior QL/32-specific.
- SuperBASIC mixed-case spellings show mandatory uppercase and optional
  lowercase letters. Keywords are case-insensitive and recognized
  abbreviations list in canonical form.
- `ELSE IF` is a nested `IF`, not an `ELSEIF` token.
- `FOR` accepts mixed comma-separated values and inclusive stepped ranges,
  in block and inline forms.
- String quotes are doubled. Comments accept `REMark`, `REM`, and apostrophe.
- Scalars pass by value; array parameters pass by reference. Numeric arrays
  have zero-based inclusive bounds; dynamic UTF-8 string arrays are QL/32
  behavior. `RUN` clears session variables.
- Commands/procedures are statement calls without parentheses; functions
  require parentheses in expressions. See
  [ADR-002](docs/decisions/ADR-002-language-profile.md) and
  [QL32-LANG-001](docs/specification/QL32-LANG-001.md).

## File random access

- `#ch AT offset` is the only seek syntax (absolute, zero-based; no SEEK
  statement). `FPOS(ch)`/`FLEN(ch)` return position/length; relative and
  from-end positioning use arithmetic. `OPEN(path, "UPDATE")` is read/write,
  non-truncating, existing file only. Offsets beyond length raise error 5.
- Implemented via `ChannelService::position()/length()` and
  `ChannelMode::Update` in Linux, Arduino SD, ESP-IDF SD, and drive-router
  adapters. Status: built and host-tested only (`ql32_channel_tests`,
  `ql32_esp32_channels_tests`); not hardware-verified (CYD FFS/SD untouched).

## Milestone and hardware status

- Use the target-by-target matrix and ranked open work in
  [QL32-SRS-001](docs/specification/QL32-SRS-001.md) as the current milestone
  record. Earlier hardware measurements and detailed profile notes remain in
  [QL32-PLAT-001](docs/specification/QL32-PLAT-001.md).
- CYD acceptance already includes the TFT/serial REPL, BASIC execution, SD and
  SPIFFS storage checks, rotation/compositor behavior, 1/4/8-bit rotation heap
  stability, Wi-Fi station calls, multi-window workers, scheduling weights,
  slice changes, suspend/resume, and worker cancellation. The attractor's
  `PLOT` output was captured and visually confirmed on 2026-11-07. CYD serial
  `SCREEN` capture now
  streams a reconstructed compositor and console snapshot as an 8-bit indexed
  BMP over YMODEM without a full-frame allocation; a 320×240 capture was
  received from hardware. `TASKVIEW` offset cropping is complete and visually
  confirmed on the CYD: a 160×120 sine/cosine crop bounced across the task
  canvas with `PAUSE 100`. The demo restores automatic graphics updates after
  its batched draw; syntax and semantics are documented in
  [QL32-LANG-001](docs/specification/QL32-LANG-001.md). The CYD SPIFFS firmware
  also completed an automated
  25-run worker suite with cleanup and reboot recovery; cumulative free-heap
  drift before reboot was 5,796 bytes, with task-result retention not isolated.
- Olimex Rev B has 512 KiB internal FFS alongside external SDC. Both mounts,
  BASIC REPL arithmetic/heap queries, dual-drive YMODEM, remote debugger file
  listing, worker launch, breakpoint/EVAL/watch/step/diagnostics, and worker
  cancellation were verified on 2026-11-05. VGA screen capture, ASCII font
  output, pager interaction, and Mandelbrot output were also verified. Physical
  PS/2 key input/shortcuts, clock, line UI, and Wi-Fi still need board-level
  verification.
- Tenstar stress run: with `DEBUGBAR ON` and `SLICE 64`, three continuous
  workers and Mandelbrot all remained `RUNNABLE` together. After closing them,
  the task list contained only the supervisor and free heap recovered by about
  65 KiB; the largest free block stayed at 2,064,372 bytes. The user observed
  a few blinking pixels under concurrent drawing and confirmed they stopped
  after workers closed. Mandelbrot itself remained runnable after a further
  28 seconds alone and was cancelled. The bounded Mandelbrot workload
  completed once in 33.929 seconds; later attempts did not complete within the
  observation window, so that workload has no reliable median. The finite
  attractor benchmark now runs as a task and completed once in 26.681 seconds
  at its requested 80 MHz/SINGLE setting; actual CPU frequency is not read back
  yet. Display
  color blocks and all four rotations were visually confirmed after fixing the
  rotation callback ordering. The 2026-11-08 YMODEM investigation found that
  the 1,029-byte STX frame exceeded the default 256-byte USB CDC RX queue;
  Tenstar now configures a 2,048-byte queue. An unpaced benchmark upload passed
  after the fix, and the temporary file was deleted. Debug bar was left enabled.
- A CYD `WINDOW #0` resize returned out-of-memory under the tested workload.
- CYD memory constraint: three concurrent workers plus Mandelbrot can fail
  during compiler preparation. CYD saturation is now characterized: two
  full-screen 4-bit canvases fit the arena, ten animated 64×64 workers ran,
  and the next worker failed safely while the supervisor and `SAVE` remained
  usable. Repeated cleanup showed no monotonic heap loss, though the largest
  block can remain reduced after reaping a ten-worker OOM pass. The separate
  25-run suite returned total free to within 60 B of baseline after reboot;
  the saturation pass itself was not measured across reboot. The 320×240
  eight-iteration render exceeded five minutes with array and channel
  workers active, so that combination is outside the interactive envelope.
  Root cause of the variable post-saturation fragmentation/high-water cost is
  not isolated. See [CYD test results](docs/test-results/CYD-open-points-2026-11-07.md).
  A separate v2 cleanup suite passed 25 worker runs: pre-reboot total-free drift
  was 1,816 B within the 2,048 B tolerance, the largest block stayed at
  110,580 B, and rebooted baseline was within 60 B. The flashed image revision
  was unverified, and task-record checkpoint deltas do not establish an exact
  retained-record cost; see [the v2 report](docs/test-results/CYD-memory-2026-11-07.md).

See [QL32-SRS-001](docs/specification/QL32-SRS-001.md),
[QL32-PLAT-001](docs/specification/QL32-PLAT-001.md), and
[QL32-NET-001](docs/specification/QL32-NET-001.md) for acceptance details.

## Sound (2026-11)

`music::Mixer` (include/ql32/music/mixer.hpp) sums a Music and an Effect slot;
`MUSIC WAV`, `MUSIC PLAY` (VTX, YM, PT3, SQT, detected from content), `MUSIC TEST`,
`BEEP` and `VOLUME` run on the Olimex I2S DAC (GPIO25); all are QL/32-specific.
Status per format is in the state table above and in
[the listening report](docs/test-results/olimex-music-formats-listening-2026-11-21.md).
Limits: PT3 has no TurboSound; VTX stereo is mixed to mono; YM songs are held
unpacked in RAM (PSRAM boards only; transcoding for SRAM-only boards waits on a
StorageService binary write handle and owner approval); YMT1/YMT2/MIX1 unsupported.
YM effects were audited against StSound and fixed ([plan](docs/plans/ym-effects-plan.md)):
stages 1-5 done, stage 6 (YMT/MIX) skipped by user decision. Heard on hardware:
SID, digi-drums (YM5/YM6) and sync-buzzer. **Unverified:** sinus-SID (StSound has no
equivalent, 16-step approximation kept) and YM2 Mad Max drums (no sample data, no
example file). Serial under music-only load: late=0, maxGap about 11.6 ms; late/maxGap
under VGA load were not logged (by ear only: attractor plus VTX clean).

## Next work

1. **Review and push:** the integration branch `jan-pal-branch-integration` is
   local only. Reconcile the owner's uncommitted music patch in the main
   checkout with it by hand, then push when the owner approves. Re-run the
   Linux ctest (83/83) after any reconciliation.
2. **Hardware verification of host-only work:** flash the CYD
   (`esp32_2432s028_spiffs`) and test file `AT`/`FPOS`/`FLEN`/`UPDATE` on FFS and
   SD, and the CYD memory phase A changes (`DIM` preflight, task canvas size,
   Wi-Fi power-down) including heap checkpoints. Verify sinus-SID and YM2 Mad Max
   drums only if suitable files exist; log late/maxGap during VGA load.
3. **Olimex VGA:** reflash the board to the standard
   `..._640x480_present_profile` and confirm the picture, then re-run the
   640x480/640x400 stress mix with the PRINT slice charge. Optional experiments:
   per-row dirty compare in full presents (see the amiga500 review), console
   present coalescing for the standard placement. Keep standard placement for
   640x400/640x480; DMA 640x480 sound and long-run playback are unverified.
4. **Board acceptance:** repeat broader Olimex/LILYGO acceptance. The Olimex
   serial port was accessible with host-device permissions on 2026-11-16; the
   latest high-resolution allocation guard passed serial rollback checks and
   the owner confirmed stable VGA recovery. The latest
   Olimex QIMG, TILE, and MENABLE checks passed on 2026-11-15; LILYGO was not
   tested in this round. Tenstar Wi-Fi remains unverified. Other not-run items
   are recorded in the board reports.
5. **Language, UI, and image candidates:** define scope before implementing
   string splitting, indirect procedure calls, network channels, or cascading
   menus. `LOADIMAGE` now reads QIMG v2 with an embedded 16-color RGB palette;
   v1 compatibility is not required before release. The Python debugger has an
   image converter tab for load, preview, resize, export, and YMODEM upload.
   QIMG v2 passed on Olimex; launch the desktop GUI with its dependencies
   installed and exercise image conversion. See
   `docs/plans/bitmap-format-study.md`.
6. **System profile accuracy:** build ID currently reports `unknown`; validate
   capability flags against exact board builds, including LilyGo.
7. **Shared display-buffer design:** define task ownership, overlap, visibility,
   and mode-change semantics before implementation. Retained per-session
   windows remain the default.
8. **Parked graphics work:** the custom PSRAM-DMA scanout loses sync on some
   high-transition patterns; keep FabGL as the qualified path. The optimized
   framebuffer primitives are not yet the backing store for BASIC drawing,
   and the full Phase 6 benchmark matrix remains open. The `GUPDATE SKIP,1`
   missing-prompt report stays in the issue backlog. PS/2 Break is queued by
   FabGL but is only handled between scheduler ticks; long BLIT operations need
   cooperative Break polling to improve response time without slowing every
   VM slice. FabGL 640x400/640x480 needs 256/307 KiB of internal DMA viewport
   memory and may silently shorten the viewport. The firmware now rejects and
   rolls back partial allocations; serial checks measured only 640x210 and
   640x206 respectively, with successful return to 320x200 and stable VGA.
   Full-height modes need the PSRAM framebuffer plus internal DMA line ring
   path to be stable under live text/drawing updates. See
   `docs/olimex-vga-graphics-plan.md`.

## Recently completed

- **BASIC random numbers (2026-11-10):** Implemented per-session `RND()`,
  `RND(value)`, `RND(low,high)`, and `RANDOMIZE seed`. Seeds are finite
  nonnegative 32-bit integers; unequal bounds form a half-open range, while
  equal bounds return that value. Added runtime coverage for range validation,
  repeatable reseeding, and session isolation. Linux runtime and language
  conformance tests pass; see [QL32-LANG-001](docs/specification/QL32-LANG-001.md).

- **REPL program editing and line deletion (2026-11-08):** The numbered-line
  REPL editor has been implemented and fixed. `DELETE lineNumber1,...` is also
  implemented and validates the remaining program after deletion. These are
  no longer backlog items; see [QL32-LANG-001](docs/specification/QL32-LANG-001.md)
  and [QL32-API-001](docs/specification/QL32-API-001.md).

- **32-bit shifts (2026-11-08):** Added `SRA`, `SRL`, `SLA`, and `SLL` with
  32-bit fill semantics, saturation for counts of 32 or greater, and errors
  for negative/fractional counts. The host runtime tests passed. CYD firmware
  built and flashed; on-device examples yielded `C0000000`, `40000000`, `2`,
  and `1`, large-count results matched the specified fill behavior, and
  invalid counts raised VM errors. See
  [QL32-LANG-001](docs/specification/QL32-LANG-001.md) and
  [QL32-API-001](docs/specification/QL32-API-001.md).

- **Supervisor diagnostics and task window modes (2026-11-08):** Added the
  CYD `DEBUGBAR ON|OFF` overlay for heap totals, largest free block, allocation
  count, active tasks, and wall-clock time. Added supervisor-only
  `WINDOWMODE [ISOLATED|SHARED]`; isolated mode tiles active task viewports and
  shared mode restores full-display viewports. Linux tests pass, and the CYD
  was flashed and checked with three live sin/cos workers, the visible debug
  bar, mode switching, and cleanup. See
  [QL32-API-001](docs/specification/QL32-API-001.md) for command behavior.

## Where everything is documented (2026-11-22 audit)

- **Every BASIC statement, function, shell and debugger command:**
  [QL32-LANG-001 section 11](docs/specification/QL32-LANG-001.md), derived from
  the keyword/built-in tables in `src/language/interpreter.cpp`. Detailed
  semantics: LANG sections 2-9, [API](docs/specification/QL32-API-001.md),
  [UI](docs/specification/QL32-UI-001.md).
- **Audio:** `MUSIC PLAY|WAV|TEST|STOP|PAUSE|RESUME|STATUS`, `BEEP`, `VOLUME`;
  Olimex only. There is no `SEEK` keyword: random access is `#ch AT offset`
  with `FPOS`/`FLEN`. No `SOUND` or `PLAY` statement exists.
- **Limits:** `DIM` total 1,000,000 elements per array on every target plus an
  ESP32 largest-free-block check (8 B per numeric element); `SLICE` 1..65536;
  `BEEP` 1..600000 ms and 20..10000 Hz; `VOLUME` 0..255; binary `#c IN/OUT`
  8192 B; breakpoint lines 1..32767; compiled-program fields are 16-bit (line,
  column, call arguments at most 65535).
- **PlatformIO envs:** default set is `lilygo_vga32`, `esp32_2432s028_spiffs`,
  `olimex_esp32_sbc_fabgl`, `olimex_esp32_sbc_retro`. Others: `esp32_2432s028`
  (SD-only CYD), `esp32_2432s028_spiffs_debug` (DWARF symbols for decoding
  Xtensa PCs), `ql32_tenstar_esp32s3`, `esp32s3_tft` (backlog placeholder), and
  the Olimex PSRAM/FabGL/DMA experiment families above;
  see [QL32-PLAT-001](docs/specification/QL32-PLAT-001.md).
- **Tools (`tools/`):** `build-target.sh` (`linux|mac|vga32|olimex|olimex-psram|
  olimex-retro|cyd`), `ql32-ymodem.py`/`ql32_ymodem.py` (file transfer, BMP
  capture), `ql32_remote_debugger/` (PC debugger/file/image GUI),
  `ql32-memory-test.py` (board memory report), `ql32-olimex-bytecode-memory.py`
  (Olimex bytecode memory measurement over `/dev/ttyUSB0`),
  `bytecode_footprint.cpp` (host CSV of compiled footprint),
  `run-vm-benchmarks.py` (VM timing of `benchmarks/*.bas` and Mandelbrot),
  `upload-olimex-fs.sh` (stage and upload startup files to the Olimex SPIFFS),
  `convert_spleen_bdf.py` with `spleen-source/` (font conversion). Note that
  `build-target.sh cyd` builds the SD-only `esp32_2432s028`; use
  `pio run -e esp32_2432s028_spiffs` for the preferred CYD profile.

## Starting a new session

Read `PROJECT-HANDOFF.md` and `AGENTS.md`, inspect local branch and remote
status (`git branch -vv`, `git worktree list`, and `git status` in the main
checkout for the uncommitted music patch), and continue the first unfinished
item under “Next work”. Update this file when that item or milestone state
changes.
