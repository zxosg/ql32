# Olimex high-resolution DMA telemetry — 2026-11-17

## Runtime profiles

Added opt-in PlatformIO profiles for the 640×400 and 640×480 DMA runtime. They
enable detailed refill timing. Counters accumulate in the scanout object and
the diagnostic runtime profiles expose a `DMADIAG` command to print one
snapshot after a workload. The regular DMA runtime and FabGL fallback do not
enable this command. Both profiles built with 56,844 bytes static RAM and
1,803,309 bytes flash.

The 640×400 profile booted at 640×400 with Indexed4, a 320-byte source stride,
three PSRAM frames (384,000 bytes), and a 100-row internal line ring. During
the captured run it reported about 70 frames per second, a maximum refill
batch of 2,142 µs, and maximum row time of 82–89 µs. Late refills, unexpected
interrupts, and active-slot overwrites remained zero.

The 640×480 profile's captured counters advanced by 480 rows per frame at
about 60 frames per second, consistent with the requested full height. Its
120-row ring reported a maximum batch of 2,261 µs and maximum row time of
87 µs; late refills, unexpected interrupts, and active-slot overwrites stayed
at zero. The boot geometry line was missed in this capture, so this run is
counter-backed but lacks the complete startup record.

Both profile builds used 56,852 bytes static RAM and 1,803,377 bytes flash.
The known-good `olimex_esp32_sbc_fabgl_graphics_profile` fallback also rebuilt
successfully (54,820 bytes RAM; 1,804,913 bytes flash).

The final profile was flashed and `DMADIAG` was sent after boot. It returned
one counter snapshot: 1,493 frames, 716,640 rows, zero late refills, zero
unexpected interrupts, and zero active-slot overwrites; maximum refill batch
was 2,287 µs and maximum row time was 140 µs. This confirms the command returns
accumulated counters without periodic UART traffic. The owner confirmed that
the 640×480 graphics and printed text rendered correctly in the runtime smoke
test. Raw capture:

- [640×480 `DMADIAG` runtime capture](olimex-dma-runtime-640x480-dmadiag-2026-11-17.log)

These were idle scanout runs with no competing PSRAM stress writer. They
demonstrate stable refill accounting under the runtime profile, not full
stress qualification. Raw captures:

- [640×400 runtime profile](olimex-dma-runtime-640x400-profile-2026-11-17.log)
- [640×480 runtime profile](olimex-dma-runtime-640x480-profile-2026-11-17.log)

## 640×400 PSRAM-stress diagnostic

The existing moving-gradient stress profile built and flashed successfully at
both 640×400 and 640×480. The owner confirmed stable moving gradients at each
size. The serial captures returned no bytes, including no boot banner or
counters, so stress visuals passed but telemetry is not qualified. Captures:

- [640×400 stress profile](olimex-dma-stress-640x400-profile-2026-11-17.log)
- [640×480 stress profile](olimex-dma-stress-640x480-profile-2026-11-17.log)

The 640×400 and 640×480 stress profiles each built at 33,920 bytes RAM and
762,573 bytes flash.

The board is currently running the 640×480 QL/32 runtime profile with deferred
diagnostics. Run `DMADIAG` after a test to print the accumulated counters. The
owner confirmed the display was allocated at 640×480 and all printed content
rendered correctly.
