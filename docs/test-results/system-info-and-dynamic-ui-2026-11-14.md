# System profile and dynamic UI checks — 2026-11-14

Repeat physical verification after Olimex power-down: 2026-11-15.

## Implemented

- Added BASIC `SYSINFO$(selector)` with selectors `KSysVersion` through
  `KSysFeatures`, plus `CAPABILITY(query$)` returning yes (`1`), no (`0`), or
  unknown (`-1`). Capability labels are case-insensitive and repeated
  whitespace is normalized.
- Injected compile-time profile descriptions for the host, Olimex/LILYGO FabGL,
  CYD, and Tenstar ESP32-S3 service bundles. The build ID reports `unknown`
  until the build is provided with Git metadata. Capability results describe
  compiled support, not current mount or connection state.
- Added supervisor-only `TILE`, backed by the portable retained-window manager.
- Added `MENABLE menuHandle, commandId, enabled`, which changes a menu item's
  enabled state before its next presentation. Cascading menus remain deferred.

## Verification

- `tests/graphics_tests.cpp`: full graphics/runtime VM suite, including profile
  selectors, case/spacing normalization, unknown capability, invalid selector,
  supervisor window tiling, disabled menu item propagation, and rejection of
  invalid enabled flags and command IDs.
- `tests/include_tests.cpp`: `data/ql32.inc` selector/result constants.
- Normal Olimex WROVER/PSRAM PlatformIO build passed after all changes:
  54,764 bytes internal RAM and 1,788,649 bytes flash; this image was flashed
  to `/dev/ttyUSB0` and its image hash verified.
- The experimental double-buffer test profile also builds:
  54,764 bytes internal RAM and 1,788,825 bytes flash.
- Serial BASIC checks reported profile `olimex_esp32_sbc_fabgl_rev_b`,
  display `fabgl-vga 320x200 indexed3`, input `ps2`, storage `ffs,sd`, and
  features `vga,ps2,sd,network,ffs,psram`. All seven selectors returned their
  expected values (`KSysBuild` is `unknown`).
- `CAPABILITY` returned `1` for PSRAM, FFS, SD, and PS/2; `0` for touch and
  USB HID; and `-1` for an unrecognized name.
- `GWIDTH/GHEIGHT/GDEPTH` returned `320/200/4`; `CPUFREQ()` returned 240 MHz.
  `FREE(0/1/2)` returned 4,221,967 / 3,997,684 / 23.
- Serial accepted `MINIT`, `MCARD`, `MENABLE`, and supervisor `TILE` without
  BASIC errors. A multi-window BASIC fixture opened three retained graphics
  channels, colored them, ran `TILE`, and printed `TILE MULTIWINDOW PASS`.
  Host tests verify the disabled item is propagated to the menu presenter and
  the window geometry is re-tiled. After the board was power-cycled, the
  fixture was uploaded and rerun; the owner confirmed three separate tiled
  areas on VGA. A separate `MENABLE` fixture disabled its second item, and the
  owner confirmed it appeared visibly dimmed while the enabled item remained
  clear. Both fixtures returned to the REPL without errors.
- CYD and Tenstar profile builds were attempted but could not begin compiling:
  PlatformIO requested uncached Adafruit GFX and ESP32-S3 toolchain packages;
  network access is unavailable in this environment.
- QIMG was physically tested: the 169-byte odd-width indexed-4 fixture and
  BASIC program were uploaded to `FFS:` over YMODEM. Running the program printed
  `QIMG LOAD/BLIT/UNLOAD PASS` with no BASIC error. A VGA SCREEN capture was
  inspected and showed the expected four color quadrants, white cross, and
  transparent black border. This QIMG test was repeated after the board was
  powered down: both files were re-uploaded to FFS, the program passed again,
  and a second capture was inspected. The repeat BMP and crop are in
  `/tmp/ql32-olimex-qimg-repeat.bmp` and
  `/tmp/ql32-olimex-qimg-repeat-crop.png`.
