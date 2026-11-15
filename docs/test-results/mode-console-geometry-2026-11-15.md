# VGA console geometry after MODE — 2026-11-15

## Finding and fix

The retained window manager resized console window `#0` when BASIC `MODE`
changed the display geometry, but `VgaConsole` kept the dimensions cached at
startup. Its bounds setter then clipped the resized console against the old
320×200 dimensions. Mode 0 kept the startup geometry and therefore appeared
to work; taller and wider modes could clip or hide the console.

The console-bounds callback now reads the current display dimensions from the
graphics service and updates `VgaConsole` before applying the resized bounds.

## Verification

- `platformio run -e olimex_esp32_sbc_fabgl_psram` passed.
- Firmware was flashed successfully to the Olimex board on `/dev/ttyUSB0`.
- Serial REPL accepted `MODE 1,0` through `MODE 4,0`, returned to the prompt
  after each command, and accepted `MODE 0,0` to restore the original mode.
- VGA visibility and console placement require a visual check; serial output
  alone cannot verify scanout composition.
