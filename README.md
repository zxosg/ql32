# QL/32

QL/32 is a portable, standalone BASIC computer inspired by SMSQ/E SuperBASIC
and Psion-style application interfaces. It is a native C++ implementation,
not a QL emulator.

The first hardware target is the LILYGO FabGL VGA32 (ESP32, VGA, PS/2), while
the core is designed to support independent TFT firmware builds on newer ESP32
chips. A Linux build is the primary development and test host.

## Repository map

- `PROJECT-HANDOFF.md` — durable milestone context and next steps for new chats.
- `docs/specification/` — requirements and evolving public contracts.
- `docs/reference/` — language manual links and provenance.
- `src/language/` — lexer, parser, semantic analysis, and bytecode compiler.
- `src/vm/` — bytecode execution and per-session VM state.
- `src/runtime/`, `src/system/`, `src/ui/` — portable services.
- `src/platform/` — Linux, FabGL VGA, and ESP LCD/TFT adapters.
- `boards/` — board-specific build profiles and pin/resource definitions.
- `tests/` — host-side language, VM, and integration tests.

## M1 status

The Linux CLI includes expressions, structured blocks, procedures, functions,
locals, arrays, `SELect ON`, in-memory numbered-line commands (`LIST`, `RUN`,
and `NEW`), and a persistent terminal REPL. Its 30-case CTest suite passes,
and M1 acceptance is complete on Linux. See [PROJECT-HANDOFF.md](PROJECT-HANDOFF.md)
for the M2 starting point. Embedded firmware and the GUI are later integration
work.

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Embedded board environments are placeholders until the M0 hardware
qualification pins compatible framework and library versions.

