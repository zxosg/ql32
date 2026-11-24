# Olimex framebuffer render benchmark — 2026-11-14

## Setup

The `olimex_esp32_sbc_fabgl_render_benchmark` profile ran on the Olimex
ESP32-SBC-FabGL at 320×200 and 240 MHz. The benchmark compared FabGL Canvas
operations with direct PSRAM RGB222 and packed indexed-4 operations. Timed
framebuffer writes ran while FabGL VGA scanout was active. The direct PSRAM
framebuffers are separate from FabGL's internal DMA scanout buffers, so this
measures CPU/PSRAM operation timing during live VGA output, not contention from
reading the tested PSRAM framebuffer for scanout. Each original table entry
is a single run, not a median.

The second run followed an optimization to `copyBitmapOpaque` and
`copyBitmapTransparent`: packed-4 copies now operate on bytes for aligned rows,
and the RGB222 transparent path avoids per-pixel format checks. The destination
X positions for the blit comparison are even-aligned.

## Results

| Operation | Iterations | FabGL Canvas | PSRAM RGB222 | Packed indexed-4 |
|:---|---:|---:|---:|---:|
| Full clear | 50 | 7,302 µs | 223,816 µs (`memset`), 240,036 µs (word stores) | 6,085 µs |
| 80×40 fill | 1,000 | 53,660 µs | 16,991 µs | 23,388 µs odd x; 16,054 µs aligned |
| Random pixels | 100,000 | 4,674,865 µs | 251,107 µs | — |
| General lines | 10,000 | 5,240,351 µs | 1,387,186 µs | — |
| Filled radius-12 circles | 500 | 23,537 µs | 38,687 µs | 22,708 µs |
| Full-height scroll up/down | 100 pairs | — | 1,085,781 µs | 339,504 µs |
| Indexed-4 translation | 1,000 full frames | — | — | 2,728,087 µs |
| Opaque 64×64 blit | 1,000 | 848,356 µs | 300,190 µs | 71,295 µs |
| Transparent 64×64 blit | 1,000 | 693,381 µs | 776,577 µs | 1,021,778 µs |
| Palette exchange | 1,000 | — | — | 1,805 µs |

## Three-run repeat

The same firmware and workload were repeated across three board resets. Values
below are median microseconds, with the observed minimum–maximum in brackets.
The three runs were tightly grouped; this confirms measurement repeatability
for the current test setup.

| Operation | FabGL Canvas | PSRAM RGB222 | Packed indexed-4 |
|:---|---:|---:|---:|
| Full clear ×50 | 7,301 [7,301–7,303] | 224,429 [224,429–224,433] | 6,160 [6,158–6,165] |
| 80×40 fill ×1,000 | 53,667 [53,665–53,669] | 17,000 [16,999–17,000] | odd 23,386 [23,385–23,386]; aligned 16,049 |
| Random pixels ×100,000 | 4,673,740 [4,673,739–4,673,757] | 250,863 [250,846–250,883] | — |
| General lines ×10,000 | 5,240,612 [5,240,604–5,240,614] | 1,390,891 [1,390,826–1,390,892] | — |
| Filled radius-12 circles ×500 | 23,588 [23,581–23,588] | 38,753 [38,752–38,759] | 22,801 [22,801–22,802] |
| Full-height scroll ×100 up/down pairs | — | 1,087,594 [1,087,591–1,087,630] | 340,198 [340,194–340,213] |
| Indexed-4 full-frame translation ×1,000 | — | — | 2,741,597 [2,741,595–2,741,714] |
| Opaque 64×64 blit ×1,000 | 848,227 [848,226–848,227] | 301,491 [301,491–301,587] | 69,722 [69,720–69,809] |
| Transparent 64×64 blit ×1,000 | 693,398 [693,394–693,425] | 778,126 [778,111–778,214] | 1,020,931 [1,020,882–1,020,938] |
| Palette exchange ×1,000 | — | — | 1,803 in all three runs |

The repeat confirms the large relative differences are stable in this harness:
aligned indexed-4 fills, circles, scrolls, and opaque blits are faster than
their RGB222 equivalents, while transparent indexed-4 blits remain slower.
The full-clear results also show that format and operation matter: packed-4
clear is fast, but a byte-wise RGB222 PSRAM clear is particularly slow. These
results still do not qualify the custom PSRAM-DMA scanout path or connect these
framebuffers as BASIC's drawing backing store.

## Before and after bitmap optimization

| Operation | Format | Before | After | Change |
|:---|:---|---:|---:|---:|
| Opaque 64×64 blit ×1,000 | RGB222 | 273,589 µs | 300,190 µs | 9.7% slower |
| Opaque 64×64 blit ×1,000 | Indexed-4 | 4,247,881 µs | 71,295 µs | 59.6× faster |
| Transparent 64×64 blit ×1,000 | RGB222 | 3,570,403 µs | 776,577 µs | 4.6× faster |
| Transparent 64×64 blit ×1,000 | Indexed-4 | 3,736,503 µs | 1,021,778 µs | 3.7× faster |

The indexed-4 byte paths remove the former per-pixel helper overhead and make
opaque copies substantially faster. RGB222 opaque blits did not improve in
this single comparison; that path already uses row-wise `memcpy`, so repeat
measurements are needed before drawing a conclusion. Transparent blits are
faster in both formats, though the packed-4 path still trails FabGL Canvas in
this workload. These are single-run timings; small differences should not be
treated as stable regressions or gains.

The optimization passed the host framebuffer tests. The benchmark image was
then replaced with `olimex_esp32_sbc_fabgl_psram_double_buffer_test`; serial
confirmed PSRAM initialized, the FabGL double-buffer viewport started, and the
REPL returned. The board’s VGA had previously been confirmed stable and
readable on this profile.

## Remaining coverage

This benchmark now covers the planned large random-pixel, line, opaque and
transparent blit, and palette workloads. Random-pixel and general-line timings
are currently implemented for RGB222 only. Repeated timing during FabGL VGA
scanout is now recorded, but the custom PSRAM-read scanout profile remains
unqualified and the accelerated framebuffer primitives are not yet the backing
store for BASIC drawing. Integrating them through the graphics service and
measuring end-to-end workloads remains the main performance step.
