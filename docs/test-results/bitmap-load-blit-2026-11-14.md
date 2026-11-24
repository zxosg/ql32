# Indexed bitmap loading and blitting — 2026-11-14

## Change

Added the QL/32-specific QIMG v1 uncompressed indexed-4 format and the BASIC
`LOADIMAGE(path$)`, `BLIT [#channel,] handle,x,y[,transparentIndex]`, and
`UNLOADIMAGE handle` operations. File readers and handles belong to the
interpreter session. Blits validate the complete source against the selected
window palette, skip an optional transparent palette index, batch presentation,
and preserve the caller's ink setting.

## Verification

- The Linux graphics test executable built with `g++` and passed. Coverage
  checks high-nibble-first unpacking, odd-width rows, transparent pixels,
  presentation batching, preservation of ink, invalid handles, and truncated
  payload rejection.
- PlatformIO built the normal Olimex WROVER/PSRAM image and the experimental
  `olimex_esp32_sbc_fabgl_psram_double_buffer_test` profile with QIMG included.
  The normal image was flashed to `/dev/ttyUSB0`; its build reported 54,764
  bytes of static RAM and 1,788,649 bytes of flash. The experimental profile
  reported 54,764 bytes of static RAM and 1,788,825 bytes of flash.
- Physical QIMG check passed on the Olimex board. The 169-byte fixture and BASIC
  runner were uploaded to `FFS:` by YMODEM; the runner printed
  `QIMG LOAD/BLIT/UNLOAD PASS` and returned without a BASIC error. A SCREEN
  capture showed the expected four indexed colors, white cross, and transparent
  border. Captures were saved under `/tmp/ql32-olimex-qimg-test.bmp` and
  `/tmp/ql32-olimex-qimg-crop.png` for this session.

## Defect found during implementation

Adding a `size_t` row offset to a signed coordinate converted the sum to an
unsigned value. The signed bounds check then rejected valid pixels. Both the
portable default row renderer and retained-canvas row renderer now convert the
offset to `int64_t` before addition; the odd-width bitmap test covers this
path.
