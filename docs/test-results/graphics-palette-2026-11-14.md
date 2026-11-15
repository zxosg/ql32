# Programmable graphics palette — 2026-11-14

## Change

Added QL/32-specific `PALETTE start,rgb[,rgb...]` and `RGB(red,green,blue)`.
Each packed RGB value uses `RRGGBB` order; `RGB` accepts three integer
components in the range 0..255. A command replaces a contiguous range in the display-wide
palette, so all indexed windows using those entries change color while their
stored pixel indices remain intact. `GPALETTE$()` reads the current mapping.
Palette updates follow `GUPDATE` and `GUPDATE SKIP` presentation behavior.
Program-controlled `PALROT`, `PALSHIFT`, and `PALFADE` apply deterministic
single steps; palette animation timing remains under BASIC program control.

The range-redefinition model is informed by SuperBASIC's
[`PALETTE_QL`](https://superbasic-manual.readthedocs.io/en/working/P/palette-ql.html)
command. QL/32 uses its own command name and packed color contract; this is not
a claim of source compatibility.

## Verification

- The Linux graphics test executable built with `g++` and passed, including
  RGB packing, palette readback, 24-bit channel decoding, global visibility
  across sessions, `GUPDATE 0` deferral, and invalid range/color checks.
- PlatformIO built `olimex_esp32_sbc_fabgl_psram_double_buffer_test`:
  54,764 bytes static RAM and 1,771,557 bytes flash in the final combined
  functionality image.
- The firmware was flashed to the Olimex board on `/dev/ttyUSB0`.
- Serial REPL reported `GPALETTESIZE()` as 16. Setting entries 1 and 2 to
  `RGB(18,52,86)` and `RGB(171,205,239)` returned the packed integer
  `1193046`, then `#123456` and `#ABCDEF` through `GPALETTE$()`.
- After `GUPDATE 0`, setting entry 3 to `6636321` returned `#654321` through
  `GPALETTE$()` before bare `GUPDATE`; the explicit update was then accepted.
- `PALETTE 15,16777215,0` correctly reported that the range exceeds the active
  palette, and the existing palette values remained intact.
- On the flashed Olimex profile, read the original colors at indices 1–3,
  disabled updates, then exercised a rotation, shifts in both directions, and
  a half fade. Serial readback matched the host-test values at every step:
  `#708090/#102030/#405060`, then `#AABBCC/#708090/#102030`, then
  `#B8C0C8/#889098/#010203`. The original three colors were restored and
  presented with bare `GUPDATE`.
- VGA color appearance was not checked. This test focused on command semantics;
  low-level VGA presentation artifacts remain parked for later investigation.
