# QL/32 contribution guide

QL/32 is a portable, native BASIC computer. Keep the language frontend, VM,
runtime services, and UI independent of a particular board or graphics library.

## Architecture rules

- `src/language` parses and compiles source; it does not access hardware.
- `src/vm` executes bytecode and owns per-session state; it does not call a display library directly.
- `src/runtime`, `src/system`, and `src/ui` expose platform-neutral services.
- `src/platform/*` contains adapters only. FabGL and ESP-IDF/LVGL types must not appear in portable public interfaces.
- Each BASIC session owns its resources. A blocking-looking UI command must suspend only its session, never the scheduler.

## Development

- Make Linux builds and tests pass before embedded integration.
- Add tests with each language or VM behavior change.
- Keep compatibility claims explicit: SMSQ/E-compatible behavior requires a reference and a conformance test; otherwise label it QL/32-specific.

