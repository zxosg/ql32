# PSRAM framebuffer writes during DMA scanout — 2026-11-14

## Setup

Two opt-in Olimex profiles benchmarked direct operations against a spare PSRAM
framebuffer while I2S DMA continuously read a separate active PSRAM framebuffer
for VGA scanout. The normal FabGL profile was not changed. Both profiles used
320×200 at 240 MHz, the 128-row DMA ring, 64-row refill groups, and the
low-transition moving-bar diagnostic. The benchmark disabled the separate
64 KB stress writer; its framebuffer operations supplied the competing PSRAM
traffic. It left the active scanout frame unchanged.

The initial full RGB222 matrix exceeded the scanout worker's scheduling budget
and triggered the task watchdog after about 11.4 s. A shorter repeatable matrix
was used to capture per-operation times and scanout counters before the
watchdog limit. These are single captures, with iteration counts reduced for
this stress check; compare them with the separate full workload results only
as an estimate.

## Results

| Operation | Iterations | RGB222 | Packed indexed-4 |
|:---|---:|---:|---:|
| Full clear | 10 | 45,140 µs | 3,617 µs |
| 80×40 fill | 200 | 5,458 µs | 5,738 µs |
| Random pixels | 10,000 | 26,539 µs | 11,249 µs |
| General lines | 1,000 | 178,137 µs | 124,510 µs |
| Filled radius-12 circles | 100 | 8,242 µs | 5,338 µs |
| Full-height scroll up/down | 20 pairs | 293,495 µs | 93,488 µs |
| Opaque 64×64 blit | 100 | 32,092 µs | 5,853 µs |
| Transparent 64×64 blit | 100 | 104,397 µs | 48,890 µs |
| Indexed-4 full-frame translation | 100 | — | 493,901 µs |

The RGB222 run reported 149 late refills in 158 EOF events, with no spurious
interrupts. Packed-4 reported zero late refills in 178 EOF events and no
spurious interrupts. Packed-4 therefore handled this bounded workload better
in this run, but it has not passed sustained stability or visual qualification
under general drawing load.

### RGB222 refill backlog follow-up

Instrumentation showed that the RGB222 refill worker could replay rows whose
ring slots had already missed their scanout deadline. Before the fix, one
refill batch grew to 1,664 rows and took 121,440 µs; maximum backlog was also
1,664 rows. The worker now skips obsolete sequence numbers and processes only
rows that can still be presented. In the same bounded RGB222 workload, maximum
batch size fell to 64 rows, maximum batch duration to 46,232 µs, and maximum
backlog to 640 rows. It skipped 1,792 obsolete rows. Late refills fell from
149/158 to 28/158. In a 13-second continuation beyond the previous watchdog
failure point, the late count stayed at 28, the worker continued advancing,
and there was no watchdog reset or unexpected interrupt.

This demonstrates that obsolete-row replay caused severe RGB222 backlog growth
and that dropping expired work prevents that runaway backlog in this bounded
test. RGB222 still has late refills and a maximum batch duration measured in
tens of milliseconds. Packed-4 showed zero late refills in its sustained
counter capture. The owner visually confirmed stable moving bars on packed-4
after the helper change. RGB222 remains unqualified because the heavier
framebuffer write benchmark still records late refills.

The packed-4 profile was rebuilt with the refill-window helper and flashed.
During an approximately 18-second post-benchmark scanout capture it advanced
through about 1,745 frames and 349,056 rows with zero late refills and zero
spurious interrupts. Maximum refill duration was 5,877 µs; maximum batch and
backlog were both 64 rows, with zero obsolete rows skipped. The bounded
benchmark itself ran before serial capture began, so its operation timing line
was not collected on this repeat. The owner later confirmed stable moving bars
for packed-4 with the helper applied.

### Packed-4 live stress follow-up

The standalone packed-4 diagnostic was flashed with PSRAM stress enabled. In
an 8-second serial capture it advanced from frame 541 to frame 1,013 (472
frames and 94,464 logical rows), wrote over 40 MB to the competing PSRAM
stress buffer, and reported zero late refills and zero spurious interrupts.
The last counter showed 40,435,712 stress bytes. The owner confirmed stable
moving bars on VGA with this diagnostic running.
An additional 20-second capture advanced beyond frame 5,600 and 1.13 million
logical rows; late-refill and spurious-interrupt counters remained zero. The
cumulative stress-write counter exceeded 225 MB without a watchdog reset.

### RGB222 live stress with shared refill-window logic

Expired-row skipping was moved out of the benchmark-only conditional so the
normal RGB222 DMA diagnostic uses it too. The profile rebuilt and ran for about
20 seconds with its continuous PSRAM writer: 1,890 frames and 378,048 logical
rows, zero late refills, zero spurious interrupts, and no watchdog reset. VGA
visual review confirmed stable moving bars. This is separate from the heavier
framebuffer benchmark, which previously still showed 28 late refills during
its write workload.

### Frame publication and ring-phase investigation

The packed-4 benchmark now draws three recognizable test frames into the spare
PSRAM framebuffer and publishes each for a frame-boundary swap. The owner saw
the vertical bars move left and a pixelated slanted line, matching the test's
intentional horizontal source scroll and raster line. Counters after the
publish sequence showed zero late refills and zero spurious interrupts; the
maximum refill was 5,970 µs. This checks staged writes, three-buffer publish,
and scanout continuity, but does not qualify the QL/32 runtime image.

The non-diagnostic runtime profile then rolled vertically despite zero late
refills. Inspection found the descriptor table restarts at slot zero every
200 logical output rows, but the 128-row ring's phase advanced by 72 slots each
frame. Changed the ring to 100 rows with 50-row refill groups so its phase
repeats at every frame boundary. This reduces the internal DMA ring allocation
by 8,960 bytes. With this size the runtime counters remained at zero late
refills and zero spurious interrupts across more than 1,200 frames. The owner
confirmed the screen stopped rolling and the console color was green, but the
glyphs remained noisy/shifted after a full `GUPDATE`.

After both experiments, the normal
`olimex_esp32_sbc_fabgl_psram_double_buffer_test` firmware was rebuilt and
flashed. After each restoration, serial returned the standard REPL prompt.
Host framebuffer and canvas raster tests passed, and `git diff --check` passed.

The owner tested `ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789` on three profiles.
The packed-4 and RGB222 DMA runtime images both showed noisy text. The
experimental FabGL double-buffer profile was noisy and blinked occasionally.
The standard `olimex_esp32_sbc_fabgl_psram` profile rendered the same test
cleanly and accepted REPL input. The DMA packed-4 profile was reflashed for
the follow-up fixture tests below; standard FabGL remains the known-good
recovery profile.
This isolates the visible text defect to the experimental buffered/raw
presentation paths. RGB222 runtime stats showed late refills increasing from
37 to 116 during the capture, plus one spurious interrupt, so that profile
also needs separate timing work. Keep FabGL standard as the recovery profile
until direct scanline and DMA text-edge output are corrected.

### Single-pixel edge follow-up

Added `examples/graphics-pixel-edge-test.bas` and uploaded it to `FFS:` over
YMODEM. The fixture draws alternating one-pixel vertical and horizontal
stripes, plus an unaligned checker patch, with updates held until one full
`GUPDATE`. Its first run failed to parse because it used `NEXT` rather than
QL/32's `END FOR`; that corrected source is retained locally and was uploaded
to a new FFS path. It loaded and ran without a VM error. The owner saw the
horizontal and vertical stripes plus a grid-like checker patch, with the font
still noisy. During the run the packed-4 scanout reported five cumulative late
refills and zero spurious interrupts. The raw scanout edge diagnostic bypassing
the runtime presenter was independently confirmed visually clean, with zero
late refills and zero spurious interrupts. This narrows the remaining defect
to text rendering/presentation rather than general pixel rasterization.

An A/B BASIC fixture was then run from `SDC:`. It prints a test string into
graphics channel #1 and another into the console. The owner reported both text
paths noisy. Scanout had zero late refills and one cumulative spurious IRQ in
the captured interval. A direct raw-frame glyph control now draws the same
portable glyph columns directly into the packed-4 scanout buffer, alongside
the edge patterns. Palette rotation was disabled, but the owner still reported
noisy text, so the mixed pattern was ambiguous. At the owner's request, a new
triangle-only profile was added: black framebuffer with one solid 96×96 right
triangle whose hypotenuse is a 45-degree staircase. The triangle-only profile
was built and flashed successfully. Its serial counters showed zero late
refills, zero spurious IRQs, and zero palette changes. The owner reported that
the temporary DMA line-ring pair reversal put four-pixel groups in the wrong
order; the photo showed a comb-like diagonal. That transform was removed and
the no-swap triangle profile reflashed. The owner confirmed the display
returned to the first triangle shape. A host regression now compares the
packed-4 triangle made with `fillRect` spans and individual `writePixel` calls;
both produce identical bytes. The owner clarified the physical left-to-right
order for each eight-pixel group as `2,3,0,1,6,7,4,5` (the earlier mention of
pixel 8 was a typo). The triangle diagnostic was rebuilt and flashed with this
permutation; the owner confirmed the 45-degree triangle now looks correct. The
validated ordering is now applied to every row emitted by the DMA scanout,
rather than only the triangle test. The regular packed-4 DMA runtime profile
builds successfully with this change. The shared permutation helper was
covered by a host regression over two consecutive eight-pixel groups, and the
Olimex framebuffer host test binary passed. The runtime profile was then
flashed successfully over the detected CH340 port. Serial status showed zero
spurious IRQs; late-refill count advanced from 21 to 71 across separate
captures. A known alphabet-and-digit `PRINT` command was sent and echoed by the
REPL. The owner confirmed the alphabet and digits are clean and readable on
VGA. A further 10-second counter capture reached 73 late refills and 2
spurious IRQs; both counts then stayed steady through the capture. The
existing `graphics-partial-draw-test.bas` fixture then loaded from FFS and
completed its 80 rectangles, 80 points, 40 lines, and circle. The owner
confirmed the shapes were correct and stable on VGA. During the drawing run,
late-refill count rose from 230 to 430 and spurious IRQs from 2 to 3; the test
completed without reset. Continue monitoring those counters during heavier
drawing workloads. An 8-second attractor worker run remained active without a
reset, but the owner saw occasional screen flicker. Its late-refill counter
rose from 574 to 645 and spurious IRQs from 3 to 4 during the run. The worker
was killed cleanly. A three-pass 20-line automatic-versus-batched PRINT
benchmark then completed and returned to the REPL; during that run late
refills rose from 962 to 1,043 and spurious IRQs from 5 to 6. VGA review of
the PRINT output is pending. Keep standard FabGL as the recovery/default until
these DMA-path timing events and sustained scanout are resolved.

### Refill-depth comparison

An opt-in 200-row ring keeps the same 50-row refill cadence while increasing
refill lead from 50 to 150 rows. It allocates successfully: the startup report
shows 75,056 bytes of internal DMA use and 134,040 bytes still free. After the
initial 35 late events, the idle count stayed at 35 for a 10-second capture.
The owner confirmed the attractor animation was stable on this profile, unlike
the intermittent flicker seen with the 100-row ring. The 20-line PRINT
benchmark still caused visible vertical flicker/jumps; late refills rose during
the run, while spurious IRQs remained at zero. This indicates that more ring
depth helps the sustained attractor workload, but does not resolve rapid
partial screen updates.

A 400-row one-block profile was attempted but did not start scanout: the 128 KB
ring exceeded the reported 110,580-byte largest contiguous DMA block. The
descriptor chain is also frame-length, so using two frame periods would need
phase-aware descriptors or a different ring design. The test environment was
removed; the device was restored to the working 200-row profile. Further
investigation should reduce repeated full-frame PSRAM copies for small console
updates and batch help/pager output at page boundaries.

### Refill task priority comparison

Compared the ring-200 runtime with refill task priority 7 against its priority-4
control using the same three-pass PRINT benchmark at 240 MHz. Priority 7 had
33 cumulative late refills before commands and 110 after loading the fixture;
the workload ended at 229, a delta of 119. Priority 4 began at 35, reached 113
after load, and ended at 232, also a workload delta of 119. Spurious IRQs were
2 in the priority-7 run and 0 in the priority-4 run. Thus raising task priority
did not reduce measured late refills. The owner reported VGA stable/readable
for both runs; the earlier intermittent PRINT flicker was not reproduced in
this A/B comparison. Keep priority 7 opt-in because its counters did not
improve.

### Partial-copy prototype

Prototyped per-buffer dirty-region catch-up to avoid cloning the full PSRAM
frame before a partial update. The ring-200 PRINT workload's cumulative
late-refill counter rose from 12 to 29 (17 additional events), compared with
119 additional events in the priority-4 full-copy control. The owner reported
artifacts or a reset on VGA, so this implementation was rolled back and the
previous priority-7 firmware was restored. Treat the counter reduction as
insufficient evidence of a safe optimization. After restoration, the owner
reported minor glitching during repeated HELP output. Investigate the console
publication cadence and frame handoff before retrying buffer-delta copying.

The first automated repeated-HELP capture was invalid: pySerial asserted DTR
when opening the port, resetting the ESP32 before the commands. The capture
was repeated with DTR and RTS low before opening. On the restored priority-7
image, `PAGER ON`, `HELP`, and `Q` then completed without a boot sequence; late
refills rose from 80 to 108 during the HELP page and to 115 while quitting, with
zero spurious IRQs. HELP uses REPL output channel #1 here, so the listing is
visible in the graphics pane rather than the serial log. The owner's visual
review of this corrected paged run is pending. A later `PAGER OFF` command
captured 137 late refills and 5 spurious IRQs; this cumulative increase means
the issue is not isolated to the paged HELP action.

## Next work

- Batch help/pager console writes by page and measure late refills, IRQs, and
  visual stability during rapid text updates.
- Inspect repeated full-frame copies in `PsramFramePresenter::presentRegion`
  and measure the clone duration: current 320x200 packed-4 staging copies
  32,000 bytes on each presentation before applying the dirty rectangle. Test
  another partial-update path only after modeling triple-buffer version
  catch-up and its frame-boundary races.
- Compare the presenter's indexed framebuffer bytes with the direct diagnostic
  framebuffer to isolate row composition, palette conversion, and publication
  if any text corruption remains.
- Keep testing RGB222 under the heavier framebuffer workload; its 28 late
  refills remain separate from the clean live-stress result. Keep the watchdog
  on.
- Keep FabGL as the default display until the DMA path passes sustained tests.
- Only then connect the PSRAM framebuffer as BASIC's display backing store.

### Paged HELP frame batching

Added presentation batching around the pager's `resetUpdateState` plus its first
page write. A full flush requested by the reset is deferred until the page has
been composed, so that pair publishes one full frame instead of a full frame
followed by a separate page update. The input prompt still publishes afterward.
The host graphics test passed, and the ring-200 priority-4 Olimex build flashed
successfully. With serial DTR/RTS held low before opening, the quiet baseline
stayed at 35 late refills; enabling the pager added 20, displaying the HELP
page added 4, and dismissing it added 7. Spurious IRQs stayed at zero. The owner
still saw minor horizontally moving lines during the updated page, so batching
reduced work in that sequence but did not eliminate the visible scanout
disturbance. Pager was returned to OFF after the test.

### Pending-frame reuse

When a pending framebuffer already exists, subsequent updates now detach and
reuse that not-yet-visible buffer instead of cloning the active PSRAM image.
The DMA interrupt can only swap a published pending index, so it continues to
read the untouched active frame while composition modifies the detached one.
The ring-200 priority-4 profile built and flashed with clone/reuse counters.
The owner reported a clean idle image on this build. During the PRINT workload,
the counters advanced from 83 full clones / 146 pending reuses / 91 late
refills to 101 clones / 176 reuses / 98 late refills; spurious IRQs stayed at
1. This capture included repeated benchmark markers, so it is not a clean
duration comparison. The owner confirmed PRINT was stable/readable. Paged HELP
still showed moving horizontal lines; its page interval added 20 late-refill
events and only three full clones. The saved GUPDATE hold/flush test also
passed: Screen A stayed visible through the five-second GUPDATE 0 hold, then
Screen B appeared after bare GUPDATE. Scanout counters and clone counts stayed
flat during the hold; the explicit flush added one clone. This confirms the
pending reuse preserves update suppression and presentation semantics, but
HELP still needs more scanout headroom.
