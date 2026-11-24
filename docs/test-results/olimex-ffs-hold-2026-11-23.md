# Olimex FFS write scanout hold (2026-11-23)

Fixes the VGA blink during large FFS (SPIFFS) writes described in
`olimex-ffs-vga-blink-analysis-2026-11-23.md`.

## Behaviour
- Platform-neutral `runtime/bulk_write_gate.hpp`: `BulkWriteGate`, counted gate, RAII `BulkWriteTracker` (holds after 4096 cumulative bytes, releases on finish/destructor/close/closeSession).
- Wired to FFS only (`SdChannels`, `SdStorage` write handle/`writeText`/`writeTextStream`) in the Olimex DMA build. SDC never holds. YMODEM receive to FFS goes through `SdChannels`.
- `PsramDmaScanout::holdBegin/holdEnd`: the refill worker fills the line ring with a blue screen showing "Busy, accessing disk" (twice, half-frame ring), the ISR keeps replaying it during the flash stall; on release the worker refills normally. Reset on scanout begin/stop (mode change).
- Hold flags are plain volatiles: an `std::atomic` read in the IRAM ISR rebooted the board silently during flash writes (found and fixed during this work).
- DMADIAG reports `held-groups`; ISR counts groups as held (not late) while hold is active.

## Results
| Check | Result |
|---|---|
| Host CTest | 89/89 (88 + new `ql32_bulk_write_hold_tests`) |
| Builds | retro 1,961,201 B flash / 57,212 B RAM; fabgl_psram, esp32_2432s028_spiffs, ql32_tenstar_esp32s3, lilygo_vga32 all build |
| FFS upload 146 KB (status env) | 20.7 s, held-groups +4650, late-refills +12 (was +644 stale groups before) |
| Owner visual | message steady, no blink during upload; a short blink remains **before** the message appears |
| FLEN readback | not verified (REPL FLEN print did not display); YMODEM CRC/ack completed |
| SDC upload | unchanged (held-groups 0) |

## Final design: snapshot hold with toast (owner-approved, verified)
- Hold starts at the first FFS write (per-op `BulkWriteScope` around write/open/close/rename/remove), which removes the pre-message blink.
- `flashHoldBegin` (main loop task) posts the existing IPRINT info toast (`showInfo`, no new syntax) top-right: yellow text (ink 6), black paper (0), yellow frame (6, new `frame` toast colour, default follows ink), refreshes and flushes the presentation, then `holdBegin(drawNotice=false, blink)` snapshots the real picture including the toast into internal RAM.
- The IRAM ISR `holdRefillRows` replays the snapshot (32-bit loads, O3 unrolled, ~166k cycles avg; heavier versions broke YMODEM receive). Blink: the ISR swaps the toast region between the yellow-text overlay (inset inside the 1 px frame so the frame stays steady) and a yellow->black overlay every 32 frames (~0.5 s), so the blink runs during the flash stall.
- Snapshot only if internal free heap stays >= 48 KB after allocation; otherwise solid-colour hold with the message. Non-main-task callers use the built-in overlay notice. `flashHoldPoll` cancels the toast after release (300 ms linger).
- DMADIAG adds `snapshot-groups`; acceptance: late-minus-held = 0.

## Results (final)
| Check | Result |
|---|---|
| Host CTest | 89/89 (incl. toast frame colour test) |
| Builds | retro 1,963,681 B flash / 57,260 B RAM; fabgl_psram 1,963,309 B; esp32_2432s028_spiffs 1,801,517 B; ql32_tenstar_esp32s3 1,649,581 B; lilygo_vga32 1,767,473 B |
| FFS upload 146 KB x5 (status env) | 33-39 s, late 0 (late-held = 0), snapshot-groups ~8000 per upload |
| Owner visual | "Yes, all good": real picture steady, yellow frame / black / yellow blinking toast, gone after upload |

## Open / risks
- First large FFS upload after a board reset failed once ("data block retry limit exceeded") in 2 of 3 reset rounds of testing; repeats succeed. Cause undiagnosed.
- Rare race if `holdBegin` coincides with the async release instant (unaddressed).
- ISR duty ~166k cycles per EOF leaves limited margin; keep it light.
- Free-internal falling below ~116 KB forces the solid fallback.
