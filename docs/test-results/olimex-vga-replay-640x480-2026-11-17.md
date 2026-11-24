# Olimex VGA replay — 640×480 — 2026-11-17

Replayed the four VGA checks with the owner present on the standard
`olimex_esp32_sbc_fabgl_psram_dma_display_640x480_profile` firmware. The
attractor and draw-only benchmark files remained on FFS.

| Check | Result |
| --- | --- |
| 1. Flash and boot standard 640×480 profile | Stable and readable. |
| 2. Run `FFS:/attractor.bas` as task 2 | Animation and console stayed stable/readable, but animation was extremely slow, nearly still. |
| 3. Run `FFS:/attrdraw.bas` (100-frame draw-only benchmark) | A second console appeared at the top while the REPL console stayed at the bottom. The image flashed slightly. No completion marker was observed before stopping the test. |
| 4. Reflash standard profile after stopping the draw test | Stable and readable. |

The draw-only program opens channel 2 for its benchmark messages, which
accounts for the extra console window. The flashing occurred while the
benchmark was running. The task did not finish during the replay, so no
duration or post-run counters are reported for it. Reflashing the standard
profile ended the task and restored the normal screen.

Serial task-start capture: [attractor replay](olimex-dma-attractor-replay-640x480-2026-11-17.log).

The board is left on the standard 120-row 640×480 profile. The owner confirmed
the final screen is stable and readable.
