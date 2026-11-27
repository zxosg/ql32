# Changelog

## v0.1.0 (unreleased)

First release candidate.

### Added
- Compact byte-oriented program encoding with a decode ring (`QL32_COMPACT_CODE`), enabled on all ESP envs; host dual-run and compact-only test builds.
- Segmented program store and streaming lexer/compiler; program spill to `FFS:` recovery files with sidecar metadata (CYD).
- Relocation slab to coalesce heap holes after compile.
- Memory diagnostics: `HEAP STATS`, `HEAP FREEMAP`, `HEAP LIVEMAP` (CYD diagnostics build).
- `NETTIME$()` (SNTP, UTC) and `DISNET`.
- Digi-drum budget 8192 with a memory-based fallback and `[no drums]` indication.
- BASIC music player example (`examples/music-player`).
- Tests: compact ring-of-8 and truncate/append, EVAL/TRAP rollback conformance case, spill write-failure and missing-file tests.
- `VER` command and version in boot banners (`include/ql32/version.hpp`).
- `programSpillMinBytes`: programs under 4 KB are not spilled on the CYD (spill adds 1.5-2.4 s to RUN on FFS:); see `docs/test-results/CYD-spill-latency-2026-11-24.md`.
- CI workflow: host ctest matrix and PlatformIO builds.

### Fixed
- Orphan `.q32-run-*.meta` sidecars no longer exhaust the recovery-file slots and silently disable the spill.

### Changed
- Debug bar is off by default (enable with `DEBUGBAR ON`).
- Removed the non-building `esp32s3_tft` placeholder env (use `ql32_tenstar_esp32s3`).

### Known limits
- Only the CYD has been hardware-tested; Olimex, LILYGO and Tenstar are experimental.
- Full-screen CYD player: about 140K free / 40K largest at start, 75K / 11K while playing.
- WiFi/SNTP leaves about 16.5K pinned after disconnect.
