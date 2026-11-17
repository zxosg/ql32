# 640×480 split-core performance probe — 2026-11-17

The owner reported that QL/32 REPL and BASIC execution at full-screen 640×480
felt about one tenth as fast as 320×200. The 640×480 runtime had its Arduino
`setup()`/`loop()` task and DMA row-refill worker on core 1. At 60 Hz, scanout
reads 640×480 pixels per frame, about 4.8× the pixel rate of 320×200 at 70 Hz.
This made CPU contention a plausible cause, but it did not explain BASIC-only
work by itself.

## First test profile

The opt-in `olimex_esp32_sbc_fabgl_psram_dma_display_640x480_split_cores_test`
profile sets `ARDUINO_RUNNING_CORE=0`, while retaining the DMA refill worker on
core 1. The first version left the I2S EOF interrupt on core 0 with the
interpreter. It separated the refill task from the interpreter, but not all
VGA-side work.

## Results

At 240 MHz/SINGLE, the CPU-only 100-frame attractor benchmark completed in
14.562 s on the split-core profile. A prior 320×200 reference took 11.217 s,
about 1.30× faster; these are not a controlled A/B because they used different
runtime profiles. The result does not support a tenfold slowdown for CPU-only
BASIC on the split-core build.

The full 100-frame attractor completed in 59.902 s on 640×480. A prior 320×200
run took 15.934 s, about 3.76× faster. Since this drawing workload clears and
updates the display each frame, this is close to the 4.8× pixel-rate increase
and points to display work as a large part of the user's perceived slowdown.
The historical comparison is indicative, not a matched profile benchmark.

The owner reported that the first split-core VGA lost signal. The standard
640×480 profile was rebuilt and reflashed; a subsequent `DMADIAG` reported
1,984 frames, zero late refills, zero unexpected interrupts, zero active-slot
overwrites, and a 2,286 µs maximum refill batch. Raw benchmark and restore
captures are retained alongside this report.

## Second test profile: interrupt and refill together

The test profile was then corrected to pin the EOF interrupt to core 1 as well
as the refill worker, leaving the interpreter loop on core 0. CPU-only attractor
time was 14.670 s at 240 MHz. The full 100-frame attractor emitted its
completion marker, but no reliable duration was captured. The owner observed
good program speed. The console area flashed, and the graphics image became
unstable during the attractor. The owner described the display as out of sync;
the visual report does not establish whether the monitor actually lost sync
lock or showed rolling/artifacted frames.

`DMADIAG` from this build reported 1,457 late refills out of 9,345 EOF events
(15.6%), a maximum refill batch of 9,877 µs, a maximum row time of 234 µs, and
a maximum backlog of 300 rows. There were no unexpected interrupts or active
slot overwrites. This shows that moving both functions to core 1 did not meet
the scanout deadline under the drawing workload. The late refills are
consistent with image instability during the attractor; CPU responsiveness
alone is not sufficient for acceptance. The separate console flash needs
isolation because the counters do not identify its cause.

The stable 640×480 runtime profile was rebuilt and flashed again after the
owner reported image instability. The owner confirmed VGA returned to a stable,
readable screen. The 160-row candidate was then built and flashed with the
same split-core placement. Before starting the attractor, its `DMADIAG` snapshot
reported 8,781 late refills out of 38,451 EOF events (22.8%), a maximum refill
batch of 15,468 µs, and backlog of 440 rows. This was worse than the 120-row
candidate, so the attractor portion was skipped. The stable 120-row profile
was reflashed; its post-restore snapshot reported zero late refills across
15,904 EOF events, maximum refill batch 2,303 µs, and backlog max 60 rows.
The attractor was then run as task 2 on the restored profile. Its first
accumulated snapshot showed zero late refills across 145,992 EOF events, with a
maximum batch of 2,303 µs and maximum backlog of 60 rows.
The task was still active when first sent `KILL`; the second `KILL 2` returned
“Killed task 2.” Between the two snapshots, another 17,576 frames elapsed
(nearly 4 minutes 53 seconds at 60 Hz) while the attractor remained active;
counters continued to show zero late refills across 286,600 EOF events. A
finite 100-frame draw-only benchmark was then transferred as
`FFS:/attrdraw.bas` and started as task 3. It produced no completion marker;
the serial capture instead showed a fresh boot banner, with no reset reason or
panic captured. Do not use that run as a timing result. After boot, the standard
profile again showed zero late refills. The owner had previously confirmed the
standard profile stable and readable; visual confirmation of the latest run is
pending. See the ring-160, attractor, benchmark, and restore captures below.

## Design conclusion

VGA timing should own the scanout deadline, while BASIC should remain a
producer that publishes work without waiting for each raster line. Simply
pinning the interpreter to the other core is insufficient: the combined
interrupt/refill profile also misses deadlines during drawing. Before another
physical split-core trial, isolate the refill path from interpreter drawing
work, ensure its per-row work is bounded, and use telemetry to prove the
deadline is met during attractor and text workloads. Frame publication should
remain bounded and switch images at frame boundaries. Do not make BASIC wait
at 60 Hz when it has no display work.

The split-core 160-row run had late refills even before launching the
attractor, while the standard 120-row profile had none during a 20-second
attractor run. This points to a core-placement or shared-memory-contention
effect rather than insufficient ring depth. The current counters do not measure
PSRAM bus stalls, so shared PSRAM contention is a hypothesis to test rather
than a confirmed cause.

The four visual checks were replayed with the owner present. The attractor was
stable but nearly still; the 100-frame draw-only test opened a second console
and caused slight flashing, and did not complete before restore. The standard
profile was reflashed and the owner confirmed stable/readable output. See the
[visual replay report](olimex-vga-replay-640x480-2026-11-17.md).

Raw captures:

- [CPU-only attractor](olimex-attractor-cpu-only-640x480-split-cores-2026-11-17.log)
- [Full attractor](olimex-attractor-full-640x480-split-cores-2026-11-17.log)
- [Restored 640×480 counters](olimex-dma-runtime-640x480-restored-2026-11-17.log)
- [Second split-core CPU-only attractor](olimex-attractor-cpu-only-640x480-split-cores2-2026-11-17.log)
- [Second split-core full attractor capture](olimex-attractor-full-640x480-split-cores2-2026-11-17.log)
- [Second split-core refill counters](olimex-dma-runtime-640x480-split-cores2-2026-11-17.log)
- [160-row split-core refill counters](olimex-dma-runtime-640x480-split-cores-ring160-2026-11-17.log)
- [Restored-profile attractor baseline](olimex-dma-attractor-baseline-640x480-2026-11-17.log)
- [Restored-profile draw-only benchmark attempt](olimex-dma-attractor-draw-baseline-640x480-2026-11-17.log)
- [Post-restore 120-row refill counters](olimex-dma-runtime-640x480-restored3-2026-11-17.log)
