# Olimex VGA blink during FFS transfer: root cause analysis (2026-11-23)

Status: **analysis and proposal only; no code changed.** Source b12d4f5, board
Olimex ESP32-SBC (PSRAM), mode 400x300 (default `olimex_esp32_sbc_retro`).

## Observation (owner, visual)

YMODEM upload of a 146,416 B file: SDC (16.9 s) no blink, FFS/SPIFFS (26.6 s)
blinks periodically, roughly once per saved block.

## Measurement (hardware, `olimex_esp32_sbc_retro_status`, `DMADIAG` counters)

Same file, counters read before and after each upload:

| Transfer | Duration | Frames | Rows scanned | Rows refilled | Late-refill groups |
| --- | ---: | ---: | ---: | ---: | ---: |
| idle 10 s | 10 s | n/a | n/a | equal | 0 |
| SDC | 16.7 s | 1,099 | 329,700 | 329,700 | **0** |
| FFS | 21.4 s | 1,383 | 414,825 | 366,525 | **644** |

- FFS: 48,300 rows (644 groups x 75 rows, about 161 frames, about 2.7 s total, about
  12% of the transfer) were scanned out without being refilled. SDC and idle: none.
- `frames` and EOF counters kept advancing during FFS: the IRAM EOF ISR and the DMA
  descriptor loop keep running, so sync timing is not lost. The ring is simply
  re-scanned with stale rows (the 400x300 picture is built from a short ring that is
  refilled just-in-time), which shows as a flash/garbled frames.

## Root cause

On ESP32 every SPI-flash write/erase disables the flash cache on **both** cores and
halts task scheduling (`spi_flash_disable_interrupts_caches_and_other_cpu`). With the
cache off, the VGA refill task cannot run (its code is in flash, `QL32_REFILL_HOT` is
empty unless `QL32_OLIMEX_DMA_REFILL_IRAM_TEST`) and PSRAM (the framebuffer
source) is unreachable. Only IRAM-flagged ISRs still run: the EOF ISR
(`ESP_INTR_FLAG_IRAM`) counts late groups but cannot refill. SPIFFS erases a 4 KB
sector per block (typically tens of ms, up to hundreds) and writes pages in between.
36+ block erases for 146 KB match the periodic blinks. A 200-row ring holds at most
about 13 ms of scanout, so no ring depth or priority change can cover an erase, and
core pinning does not help because both cores are stopped. SDC uses the SPI/SDMMC
peripheral with cache enabled, hence no stall.

This is not caused by YMODEM or serial receive (SDC receives identically).

## Ranked fix proposals (none implemented)

1. **Blank/freeze scanout around FFS bulk writes (recommended).** Add a small
   scanout "hold" API (platform-neutral hook, Olimex adapter implements it): before
   the YMODEM receive (or any FFS write over a size threshold) fill the ring with a
   static solid/blank picture and suspend refill; restore and force a full redraw
   afterwards. Stalls then show a stable black/message screen instead of garbage.
   Risk: screen goes dark for the transfer; must restore on abort/error/timeouts;
   touches single-fb refill and mode-change paths, needs a telemetry-backed test
   (late-refills during FFS acceptable but visual stable) and a visual check.
2. **Buffer writes in PSRAM and flush once at the end** (or on file close):
   concentrates stalls into one block-count-proportional burst instead of periodic
   blinks; does not remove them and needs RAM equal to file size. Low value.
3. **Put the refill path in IRAM and make the framebuffer copy ring-resident**:
   does not help, PSRAM is unreachable while the cache is off. Not recommended.
4. **Skip internal flash for bulk data**: use SDC for large transfers (works now).
   Document as the workaround until option 1 lands.
5. SDK-level options (`CONFIG_SPI_FLASH_AUTO_SUSPEND`) are not available on ESP32.

To confirm option 1 before implementing: hold scanout, run the same FFS upload, check
for no visible garbage (owner) and DMADIAG late-refills ignored while held.

Board left flashed with `olimex_esp32_sbc_retro`, serial port released.
