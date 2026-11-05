# Phase 3 implementation log

## 2026-11-05 — Baseline / before A0

- **Step:** baseline before Track A0.
- **Starting point:** tag `pre-compact-vm` points to `b2e0b3b`. Work branch is
  `codex/compact-vm`, created from that tag. The updated plan commit was carried
  onto the branch as `d67a39a`.
- **User work:** the original `codex/newvm` checkout had uncommitted user edits.
  They remain in place and untouched. Implementation is isolated in the
  `/private/tmp/ql32-compact-vm` worktree.
- **Plan path deviation:** the implementation instructions referenced
  `docs/plans/PHASE3-compact-vm.md`; the actual committed plan is
  `docs/plans/plans_PHASE3-compact-vm.md`. That actual plan is the spec used.
- **Host baseline:** macOS, AppleClang 21. Debug configure/build succeeded;
  CTest passed 60/60. ASan+UBSan configure/build succeeded; CTest passed 60/60.
  Both builds emitted one existing warning in `src/platform/esp32/drive_router.cpp`
  about a captured structured binding being a C++20 extension under C++17.
- **ESP32 baseline:** `pio run` succeeded for `lilygo_vga32`,
  `olimex_esp32_sbc_fabgl`, and `esp32_2432s028`. Flash/static RAM figures are in
  `phase3-metrics.md`.
- **Hardware:** `pio device list` found `/dev/cu.usbserial-1110` (VID:PID
  `1A86:7523`), but did not identify its chip or board. No hardware test or flash
  was attempted; hardware gates are pending identification.
- **Decisions:** keep all existing user modifications out of this worktree;
  use the current tracked plan despite the path mismatch; establish this clean
  baseline before adding the curated corpus and sanitizer preset.
- **Open risks:** no board-specific live heap measurements yet; connected USB
  serial device is unidentified.

## 2026-11-05 — A0 complete

- **Step:** Track A0, deterministic safety net.
- **Commits:** pending commit of this step.
- **Changes:** added the `ql32_golden_corpus` CTest target over 11 stable CLI
  fixtures; added a CMake sanitizer option and `sanitized` preset; added a
  separate benchmark runner and BASIC workloads for loops, recursion, array
  fill, string concatenation, and Mandelbrot iteration.
- **Tests:** Debug CTest passed 61/61, including the curated corpus. The
  ASan+UBSan preset build and CTest passed 61/61. Corpus output and errors
  matched the checked-in CLI expectations.
- **Benchmarks:** three-run median baseline is recorded in `phase3-metrics.md`.
  Benchmarks are informational and not part of CTest.
- **ESP32:** no embedded source changed in A0. Baseline builds for all three
  supported environments passed; their size figures are recorded in
  `phase3-metrics.md`.
- **Hardware:** pending board identification; no flash attempted.
- **Deviation:** the existing rendered Mandelbrot example takes the graphics
  path and is not a stable VM-only timing workload. The benchmark uses the same
  numeric iteration algorithm without drawing, to keep VM timings independent
  of the presenter.
- **Open risks:** device runtime heap measurements remain pending.
