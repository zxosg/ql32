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

## Open
- Residual blink before the message: writes below the 4 KB threshold and YMODEM open/rename flash ops are outside the hold (~12 late groups per upload).
- Owner request after the visual test: keep the real picture on screen and show the message as an IPRINT-style corner notification in red/black/yellow. Not implemented; needs a full-frame internal-RAM snapshot replayed by the ISR (see handoff proposal).
