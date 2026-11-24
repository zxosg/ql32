# Debugger serial input check — 2026-11-15

## Change

The FabGL/Olimex serial receiver now recognizes a raw debugger frame beginning
with `@` at an empty REPL line before passing bytes through the graphical
terminal adapter or keyboard escape decoder. The rest of each frame continues
through the existing protocol-line parser.

This addresses debugger request bytes such as `@QL32DBG/...tasks.list` being
interpreted as REPL text while graphical mode is active.

## Verification

- `platformio run -e olimex_esp32_sbc_fabgl_psram` completed successfully.
- Firmware was flashed to the Olimex board on `/dev/ttyUSB0`.
- A direct `tasks.list` request with id `555` returned a valid
  `@QL32DBG/1\t555\tOK\t...` JSON response rather than echoing the request as
  BASIC input.
- An upload followed by a direct request with id `556` also returned a valid
  JSON response. That combined capture contained extra prompt/input bytes, so
  it does not establish that all concurrent upload and polling traffic is free
  of interleaving. Repeat this check through the desktop debugger after the
  GUI dependencies are installed.

## Break hotkey responsiveness

FabGL already queues PS/2 key events. The main loop polls one event before each
scheduler tick, while a tick may execute up to 128 VM instructions. A BLIT is
one synchronous VM instruction and reads/draws the image row by row. Thus a
queued Break is handled only after the current tick returns, and cannot
interrupt a single in-progress BLIT today.

To make Break responsive without lowering throughput for all BASIC code, a
future change should add cooperative break polling at bounded points inside
long graphics operations (at least between BLIT rows), with the platform
adapter preserving non-Break key events it encounters. The current release
does not include that change.
