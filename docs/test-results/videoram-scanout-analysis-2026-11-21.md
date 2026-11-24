# Videoram scanout analysis and hardware check — 2026-11-21

## Scope

Compared the shared-videoram proposal with the current Olimex PSRAM-DMA
implementation; see [the design analysis](../plans/videoram-scanout-design.md).
Then ran the requested staged packed-4 checks on the Olimex board with the
owner watching. No runtime behavior, pinning, refill priority, or timing was
changed. All builds used an external temporary build directory; only the
application partition was flashed.

The fetched `origin/main` matched the worktree at the start. The named
`olimex-vga-mode-reliability-2026-11-21.md` was absent; the existing
`olimex-final-mode-matrix-stress-2026-11-21.md`, graphics plan, and dated
experiment reports were used as the current evidence.

## Build and hardware results

| Environment | Build | Serial and visual result |
| --- | --- | --- |
| `olimex_esp32_sbc_fabgl_psram_dma_test_400x300_static` | Success; 33,904 B RAM, 764,053 B flash | Startup: 400x300, 60.32 Hz, indexed-4, 100x400-byte ring, 55,920 B internal DMA; EOF and live refill disabled. Owner confirmed the static picture and geometry stable. The 30-second snapshot had zero events/counters, so this checks the static modeline/picture only, not active refill. |
| `olimex_esp32_sbc_fabgl_psram_dma_test_400x300_packed4` | Success | Startup: 400x300, 60.32 Hz, indexed-4, 100-row/50-row refill group on core 0, 55,920 B internal DMA, 186,964 B DMA heap remaining. After about 35 seconds: 1,811 frames, 10,868 EOFs, 543,400 rows/refills, 0 late refills, 0 spurious IRQs, 59 palette applications, and 139,526,144 PSRAM stress bytes. Owner reported stable picture and smooth palette cycling. This base environment is not the separate 150-row/core-1 variant. |
| `olimex_esp32_sbc_fabgl_psram_dma_test_640x400_packed4` | Success; 33,904 B RAM, 766,429 B flash | Startup: 640x400, 70.09 Hz, indexed-4, 100-row/50-row group on core 1, 75,948 B internal DMA, 166,936 B DMA heap remaining. After about 35 seconds: 2,105 frames, 16,840 EOFs, 842,000 logical rows, 736,750 refills, 2,355 late refills (about 14.0%), 0 spurious IRQs, 58 palette applications, and 147,193,856 PSRAM stress bytes. Owner saw the wide bars cycle and move consistently, but the narrow left marker flickered at the top and bottom; the middle remained stable. Not a visual/timing pass. |
| `olimex_esp32_sbc_fabgl_psram_dma_test_640x400_gradient_stress_profile` | Success; 33,936 B RAM, 767,281 B flash | Owner reported a stable gradient with sync. The 35-second serial capture stopped after a partial scanout-start line and produced no complete worker summary or refill snapshot; a 10-second follow-up also lacked counters. Treat visual output as owner-observed, but refill/underrun telemetry as unavailable and this profile as unverified for timing. |

## Recovery and status

The qualified `olimex_esp32_sbc_fabgl_psram` image built successfully
(54,836 B RAM, 1,864,285 B flash), was reflashed after the experiments, and
reached `BASIC REPL ready`. A harmless `PRINT "RESTORE OK"` was accepted and
the REPL returned to its prompt. The owner confirmed stable VGA output. The
serial port was closed and released.

- **Built:** the four experimental profiles above and the qualified recovery
  profile.
- **Host-tested:** no code/tests changed; host tests were not run.
- **Hardware-verified:** static 400x300 output; active 400x300 palette/stress
  run; 640x400 base counters and owner-observed localized flicker; owner-
  observed 640x400 gradient; final qualified FabGL restoration and responsive
  REPL.
- **Still unverified:** 640x400 gradient refill counters and timing; sustained
  runs beyond these captures; QL/32 runtime updates or general VM writes into
  the scanout framebuffer; high-transition checker behavior at 400x300/640x400.

The 400x300 short run is promising for its tested diagnostic pattern but is
not production qualification. The 640x400 base run's late-refill and visible
marker artifacts keep it experimental. The gradient's stable appearance does
not override its missing counters. Keep FabGL as the default and do not infer
that a higher interrupt priority, larger ring, or DMA copy would fix these
results without a separate telemetry-backed test.
