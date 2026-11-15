# Cursor-based graphics lines — 2026-11-14

## Change

Added the cursor-relative subset of SuperBASIC `LINE`:

- `LINE x,y` sets the current cursor.
- `LINE TO x,y` draws from the cursor to the supplied endpoint.
- `LINE x,y TO x2,y2` draws between the supplied points.
- Existing `LINE x1,y1,x2,y2` remains available and advances the cursor.
- `GX()` and `GY()` report the current position.

Cursor state belongs to each session and graphics channel. The supported forms
follow the basic behavior in the
[SuperBASIC LINE reference](https://superbasic-manual.readthedocs.io/en/latest/L/line.html);
the complete polyline-list syntax is not implemented.

## Verification

- The Linux graphics test executable built with `g++` and passed. It covers all
  supported forms, endpoint updates, channel-local cursor storage, and `GX()`/
  `GY()` readback.
- PlatformIO built `olimex_esp32_sbc_fabgl_psram_double_buffer_test`:
  54,764 bytes static RAM and 1,771,557 bytes flash in the final combined
  functionality image.
- The firmware was flashed to the Olimex board on `/dev/ttyUSB0`.
- Serial REPL set the cursor to `(2,3)`, drew to `(5,6)`, drew from `(8,9)` to
  `(11,12)`, then used the original four-coordinate form ending at `(3,1)`.
  `GX()`/`GY()` returned each expected coordinate pair.
- VGA appearance was not assessed; this check verified language behavior over
  serial only.
