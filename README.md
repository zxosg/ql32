# QL/32

QL/32 is a portable, standalone BASIC computer inspired by SMSQ/E SuperBASIC
and Psion-style application interfaces. It is a native C++ implementation,
not a QL emulator.

The first hardware target is the LILYGO FabGL VGA32 (ESP32, VGA, PS/2), while
the core is designed to support independent TFT firmware builds on newer ESP32
chips. A Linux build is the primary development and test host.

## Repository map

- `docs/specification/` — authoritative requirements and public contracts.
- `src/language/` — lexer, parser, semantic analysis, and bytecode compiler.
- `src/vm/` — bytecode execution and per-session VM state.
- `src/runtime/`, `src/system/`, `src/ui/` — portable services.
- `src/platform/` — Linux, FabGL VGA, and ESP LCD/TFT adapters.
- `boards/` — board-specific build profiles and pin/resource definitions.
- `tests/` — host-side language, VM, and integration tests.

## Foundation milestone (M0)

M0 establishes the build, architectural contracts, board profiles, and host
test layout. The next implementation slice is lexer/parser support for simple
expressions, followed by bytecode execution of variables and `PRINT`.

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Embedded board environments are placeholders until the M0 hardware
qualification pins compatible framework and library versions.

