# QIMG v2 implementation checks — 2026-11-15

## Implemented

- Firmware `LOADIMAGE` now requires the fixed 64-byte QIMG v2 header, embedded
  16-entry RGB palette, and packed indexed-4 payload. QIMG v1 is rejected.
- `BLIT` maps source RGB entries to the nearest active display-palette color;
  transparent indexes still refer to the source image palette.
- The Python debugger has an Image converter tab for loading supported Pillow
  image formats, previewing and resizing a 16-color conversion, exporting
  `.qimg`, and uploading it to a device path with YMODEM.
- The converter composites source transparency against black. BASIC `BLIT`
  can separately skip a selected palette index.

## Verification

- QIMG Python serializer tests: 3 passed; Pillow conversion test skipped because
  Pillow is not installed in the current Python environment.
- Python source syntax validation passed for the debugger, converter, and tests.
- `tests/graphics_tests.cpp` compiled with the portable runtime and Linux
  adapters using `g++`; the graphics test executable passed, including v2
  palette mapping and v1 rejection.
- `git diff --check` passed.
- Full CMake/CTest was unavailable because `cmake` is not installed.
- Olimex PSRAM firmware built successfully with PlatformIO. Size report:
  54,764 bytes RAM (of 327,680), 1,788,925 bytes flash (of 3,538,944).
- Flashed the Olimex WROVER over `/dev/ttyUSB0`; the image and BASIC test were
  uploaded to `FFS:` over YMODEM.
- Device `LOAD`/`RUN` printed `QIMG LOAD/BLIT/UNLOAD PASS`.
- Captured a 320×200 VGA frame over YMODEM. The graphics area contained 51,024
  red background pixels; the 17×17 fixture rendered 49 blue, 49 green, 49
  yellow, and 29 white pixels. The 64 transparent image pixels showed the red
  background through the image border. No reset or transfer error occurred.
- An initial UUID-derived `FFS:` destination failed before YMODEM because the
  basename exceeded the firmware's SPIFFS limit: `CONFIG_SPIFFS_OBJ_NAME_LEN`
  is 32, allowing 31 bytes per path component. A short filename transferred
  successfully. The debugger now proposes an ASCII-safe filename within that
  limit and rejects overlong manually entered `FFS:` components before send.
- The Python GUI itself was not launched because PySide6 and Pillow are not
  installed in this environment; the Pillow-backed unit test remains skipped.
