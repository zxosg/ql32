# The Grand Tour, Phase 0 spike (2026-11-28)

CYD (`esp32_2432s028_spiffs`, /dev/cu.usbserial-1120) and Tenstar (`ql32_tenstar_esp32s3`, usbmodem11401), current main.

| Question | Result |
| --- | --- |
| Can `PRIORITY`/`SUSPEND`/`RESUME` be called from BASIC? | **No.** Host reports "undefined procedure or function"; they are shell-only (src/language/interpreter.cpp:243). Scene 4 uses differently paced tasks instead. |
| What does a worker's `GWIDTH()/GHEIGHT()` report before its first `WINDOW`? | CYD `160x240` at origin `160,0` (a half-width slot, not the screen); Tenstar `240x95` at `0,0`. **Not** the screen size, so workers cannot derive tile size from it. |
| How do workers learn the screen size? | `SYSINFO$(3)` = `ili9341-tft 320x240 rgb565` (CYD), `st7789-tft 240x135 rgb565` (Tenstar). The layout library parses `WxH` from it; the director measures the usable height from its own default window. |
| `GPALETTESIZE()` | 16 on both boards. |
| `INKEY(timeout)` over serial | Works in a foreground program on both boards (letters; Esc = 1114113; ANSI arrow sequences arrive as `91`,`65`, not decoded). Timeout returns 0. |
| Capability for audio/SD | `CAPABILITY("audio")` returns `-1` (unknown) on both boards; `SYSINFO$(6)` (features) lists no audio on either; `SYSINFO$(5)` storage is `ffs` only (no SD listed, card absent?). The tour must probe by trying (`TRAP MUSIC PLAY`) instead. |
| Task canvas after `WINDOW 0,0,160,100` | CYD free 157.6K (idle 163K); Tenstar internal heap unchanged. |
| Music + tiles (earlier measurement, same firmware) | CYD: KNUCKLE/Lyra + mandelbrot or sincos: 92-95K free, 86K largest, late=0, failedalloc=0, musicmaxgap 11.7 ms. |

Go decision: proceed. The estimates in the plan (about 60-80K free with 4 tiles and music on the CYD) are plausible; they are re-measured in Phases 2 and 4.
