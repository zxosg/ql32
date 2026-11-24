# Olimex 640x480 encoder cost isolation (2026-11-20)

Env: `olimex_esp32_sbc_fabgl_psram_dma_display_640x480_present_profile`
(standard placement, ring 120, refill group 60, refill task priority 4 on
core 1). New diagnostic serial command `ENCBENCH` (`QL32_OLIMEX_DMA_REFILL_PROFILE`).

## Microbenchmark (240 MHz, 640 px row, 1024 rows, interrupts masked)

| Piece | Before us/row | After us/row |
|---|---|---|
| micros() | 0.73 | 0.77 |
| PSRAM->DRAM copy (320 B row) | 11.6 | 12.1 |
| encode from DRAM | 20.3 | 14.4 |
| encode from PSRAM | 30.1 | 26.0 |
| copy + encode | 31.1 | 27.1 |
| copy, sequential rows | n/a | 11.9 |

Sequential and strided copy cost the same, so the PSRAM cost is not a
row-order or prefetch artefact: about 280 cycles per 32 B cache-line fill.

## Cause of the encoder cost

Disassembly of the old `encodePacked4Row` showed byte stores (`s8i`), a `memw`
per iteration (`-mfix-esp32-psram-cache-issue`), `width_` reloaded every
iteration and stack spills: about 60 cycles per 8-pixel group.

Fix: `encodePacked4AlignedRow` (portable, `dma_pixel_order.hpp`) uses aligned
32-bit loads and stores with locals for the bound and LUT. The remaining
tail is handled by the old loop. The encode is still above the 5-8 us
estimate (about 14 us), so the expectation was too optimistic. Part of the
rest is the `memw` the compiler inserts.

## Live results (standard 640x480, board stable)

- Refill average over an attractor run: 27.9 us/row (was 33.9 idle).
- Late refills: 0 (idle/draw 0 of 6,678 EOF; attractor 20 s, 0 of 21,037 EOF).
- PRESDIAG, attractor, one 30 s window: 22 full presents, avg 495 ms,
  provider 6.0 s, write 3.2 s. Before: 3 full presents in about 40 s at
  4.2 s each. That is about 8x faster, about 0.7 fps (was about 0.07 fps).
  Row cost fell from about 7.5 ms to about 0.88 ms per row. The VM is
  evidently less starved, but this is one run and was not A/B'd in the same
  session; the attractor workload could also differ in how much it draws.
- 640x400 and FabGL 400x300 attractor fps: not measured.

## Observation

After a deliberate RTS reset the serial log showed "Brownout detector was
triggered" and the owner reported flicker. After a USB replug the picture
was stable and readable. The owner later corrected that the faster-encoder
picture itself was stable. Treated as a USB power event, not a regression.

## Status

- Built: yes (present_profile with ENCBENCH and the new encoder).
- Host-tested: `testPacked4AlignedRowEncoderMatchesReference`; ctest has only
  the 3 known failures (scheduler, channel, hardware).
- Hardware-verified: ENCBENCH, DMADIAG late 0, PRESDIAG, stable picture
  (owner-confirmed). Board left on standard present_profile.

## Next step proposal (not started)

Option 2: present coalescing / frame-boundary rate limit, since presents are
still about 0.5 s each. Also the 12 us/row PSRAM read is now the largest
single refill piece; reducing it needs DMA-assisted prefetch (deferred).
