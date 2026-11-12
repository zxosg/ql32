# QL/32 — Platform Portability Requirements

**Document ID:** QL32-PLAT-001  
**Status (2026-11-12):** CYD task cleanup and memory/concurrency behavior have been characterized across the 25-run worker suite, canvas-resize soaks, two-full-screen-canvas tests, and worker saturation/OOM recovery. No monotonic heap loss was observed in repeated cleanup cycles; some ten-worker OOM passes left the largest free block reduced after task reaping. The separate 25-run suite returned total free heap to within 60 bytes of baseline after reboot; the saturation pass itself was not measured across reboot. The measured operating envelope and remaining uncertainty are recorded below and in [the CYD follow-up report](../test-results/CYD-open-points-2026-11-07.md). CYD TFT, serial REPL, BASIC loop, SD text round-trip, compositor movement/clipping, full-screen allocation, rotation, touch corner taps, Wi-Fi station behavior, and TASKVIEW offset bounce are hardware-verified. The 2026-11-08 follow-up also visually checked the four-color pattern at all four rotations; initial `#0` clipping came from its console overlay, so corrected geometry used graphics channel `#1`. Tenstar display colors and repeated rotations are verified after a rotation-order fix; Wi-Fi was skipped. Olimex PSRAM mode switching, double-buffered clearing at supported modes, 640×480 single-buffered operation, and the 240 MHz/DOUBLE attractor benchmark were verified on 2026-11-12. Double buffering at 640×480 is rejected because FabGL corrupts its viewport allocation on a later mode change. LILYGO physical checks remain skipped by user choice. See [the dated acceptance report](../test-results/board-acceptance-performance-2026-11-08.md). Linux remains the first conformance host.

Supported build families are independent firmware targets:

| Profile | Display / input | Status |
| --- | --- | --- |
| `lilygo_vga32` | FabGL VGA, PS/2 | Profile exists; physical acceptance skipped by user on 2026-11-08 |
| `olimex_esp32_sbc_fabgl` | FabGL VGA, PS/2, 512 KiB SPIFFS (`FFS`) and SD (`SDC`) | Built and flashed on Rev B; both drives mount; REPL, dual-drive YMODEM, remote debugger, VGA capture/font, pager, and Mandelbrot checks pass. Optional `olimex_esp32_sbc_fabgl_psram` WROVER profile booted with 4 MiB mapped PSRAM; on 2026-11-12 it passed repeated mode/clear tests for double-buffered 320×200, 400×300, 640×400 and single-buffered 640×480. Double-buffered 640×480 is rejected after a reproducible FabGL heap-corruption failure on later mode changes. See [attractor benchmark results](../test-results/attractor-performance-2026-11-12.md) |
| `esp32_2432s028` | ILI9341 TFT console/graphics, USB serial terminal input, SD storage | Optional SD-only deployment; firmware and host tests pass; TFT, serial REPL, BASIC loop, SD text round-trip, compositor movement/clipping, full-screen resizing, all rotations, and Wi-Fi station behavior verified |
| `esp32_2432s028_spiffs` | CYD with 1 MiB internal SPIFFS (`FFS`) | Preferred CYD profile; built and flashed; storage mounted, `startup.bas` loaded at boot; 25-run cleanup suite, two-full-screen-canvas limit, ten-small-worker saturation, OOM recovery, reboot recovery, and touch corner taps verified; post-saturation largest-block reduction remains a documented limitation |
| `ql32_tenstar_esp32s3` | TENSTAR TS-ESP32-S3, ST7789 240×135, USB serial, QSPI PSRAM, 512 KiB SPIFFS (`FFS`) | Built and flashed; startup and test programs load from SPIFFS; USB REPL reports 240×135×4-bit; 2 MiB PSRAM detected; worker/Mandelbrot stress run cleaned up; display colors and rotations visually checked after rotation-order fix; Wi-Fi skipped; performance matrix open; sensors deferred |
| `esp32s3_tft` | SPI/RGB TFT, USB HID/buttons/touch | Backlog |
| `esp32p4_tft` | RGB/MIPI display, USB HID/touch | Evaluation target |
| `linux` | Host adapter | Development and conformance host |

Platform capabilities provide resolution, pixel format (including three-bit
indexed color for the VGA32's eight-color mode), orientation, scaling,
available input, and enabled services. The shared core must compile without
FabGL, ESP-IDF LCD, LVGL, or board-header dependencies.

`ql32::platform::Capabilities` reports the board profile, optional display
geometry and format, available input kinds (including serial terminal input),
and supported service kinds. A profile must validate before the core uses it.
The descriptor does not assign
pin numbers or assume a specific resolution; those values belong to a
qualified board adapter. Linux remains the development and conformance host,
not a target requirement for firmware.

M5 supports LILYGO FabGL VGA32 V1.4 and Olimex ESP32-SBC-FabGL Rev B. Both
images pin PlatformIO's `espressif32@6.12.0` (Arduino-ESP32 2.0.17) and FabGL
v1.0.9. Their eight-color VGA and PS/2 mappings match; SD-over-HSPI differs:
LILYGO uses MISO GPIO 2, Olimex Rev B uses MISO GPIO 35.
The shared BASIC VM is linked with the portable graphics, storage, and channel
services. A PS/2 prompt accepts immediate statements and numbered program
lines; serial is the BASIC output stream. The firmware runs `startup.bas` from
the card root when present, otherwise a built-in BASIC demo. SD channel handles
are session-owned and paths are confined to the card root. Channel and storage
availability are included in the board capability descriptor. An absent card
reports `Unavailable` for service operations. The graphics adapter currently
permits one active display session. The custom 4 MiB partition reserves a
3.875 MiB single factory app and a 64 KiB coredump, with NVS and PHY init
partitions retained. The current VGA32 build uses 1,298,053 bytes of that app
(31.9%) and 52,424 bytes static RAM; `firmware.bin` is 1,304,624 bytes. Host
builds cover the adapters using fakes; USB serial REPL was verified on the
physical Olimex board using the LILYGO image. That image configured SD MISO as
GPIO 2, while the Olimex Rev B schematic specifies GPIO 35. A separate Olimex
profile now selects MISO 35; the LILYGO profile retains MISO 2. Both use PS/2
port 0 at CLK GPIO 33 and DATA GPIO 32. The Olimex image mounts its 512 KiB
SPIFFS partition as `FFS:/` and the external card as `SDC:/` through ESP-IDF's
SPI2/VFS SD-SPI adapter; the LILYGO adapter retains Arduino SD. On 2026-11-05,
the Olimex firmware was flashed and VGA capture verified the printable ASCII
font. Paged HELP suspended and resumed through its on-screen input panel. The
80x56 Mandelbrot demo rendered on the default graphics surface; its attempted
`WINDOW` resize raised `bad_alloc`, so the example avoids resizing that surface.
Physical PS/2 interaction/shortcuts, clock, and Wi-Fi still need verification.
The async presenter handles alert, confirmation, dialog, menu, and input
requests and is covered by host state-machine tests. The Tenstar ESP32-S3
profile builds and flashes the shared interpreter with an ST7789 display
adapter, scrolling console, 512 KiB SPIFFS, and USB serial REPL. The board
reports 240×135×4-bit, initializes the display path, and detects its 2 MiB
PSRAM. Repeated color-block rotation checks exposed a presenter callback that
ran before window-manager geometry changed; the callback now runs after
rotation is applied. The regression test and visual checks at 90°, 180°, 270°,
and 0° passed. Tenstar USB CDC had a second issue: an unpaced 1,029-byte YMODEM
frame timed out mid-read against the framework's 256-byte default RX queue.
Configuring a 2,048-byte queue fixed the tested upload; the 1,027-byte
benchmark source transferred unpaced and was removed afterward. The
benchmark's first run used `taskId=TASK("FFS:/attractor-benchmark.bas")` and
completed in 26.681 seconds at requested 80 MHz/SINGLE; `LOAD`/`RUN` was the
wrong launch route for this module preamble and produced an assignment-parser
error. Actual frequency readback and repeated samples remain outstanding. A
repeated 80×60 window test reported a 2,896-byte free-heap decrease
while open, with 2,512 bytes recovered on close; largest free block remained
2,064,372 bytes. Three continuous workers and Mandelbrot stayed runnable
together; closing the tasks recovered about 65 KiB, and the user confirmed
concurrent-draw blinking stopped after the workers closed. Mandelbrot remained
runnable after 28 seconds alone and was cancelled. A later bounded run completed
in 33.929 seconds once; subsequent attempts exceeded the observation window, so
there is no reliable performance median. Wi-Fi was skipped by user choice.
Repeated large and multi-file YMODEM transfers remain untested.

The `esp32_2432s028` target applies the same BASIC graphics contract to the
common ESP32-2432S028R yellow-board variant using an ILI9341 TFT. Its USB serial
terminal is the line-oriented BASIC input device; BASIC `PRINT` uses a scrolling
TFT console strip, with optional serial mirroring for diagnostics. Coordinate-
based graphics draw independently on the same panel. The macOS PlatformIO
build on 2026-11-01 uses 1,236,497 bytes of the 3.875 MiB app partition (30.4%)
and 49,872 bytes static RAM; `firmware.bin` is 1,243,072 bytes. The SD profile
requires 4 MiB flash, has no OTA or internal SPIFFS/LittleFS partition, and
stores BASIC files on SD. The separate `esp32_2432s028_spiffs` profile uses a
1 MiB internal SPIFFS partition as `FFS`. The CYD SD profile has a separate SPI
bus configuration for the TFT and SD card. Hardware testing has
confirmed TFT output, serial input, BASIC `FOR`/`PRINT`, and an SD channel
round-trip with successful readback and EOF detection. The board
profile now enables the common XPT2046 resistive touch controller. The adapter
uses nominal raw bounds and flips the Y axis; touch corners were verified at
rotation 0. Alternate CYD revisions may require different wiring or calibration.
Other touch rotations still need physical checking. See the [CYD touch
acceptance record](../test-results/CYD-touch-2026-11-11.md). The CYD
rotation memory check is `examples/cyd-rotation-memory.bas`: the first cycle
reported 100456, 100176, 100172, 100172, and 100128 free bytes after 0°, 90°,
180°, 270°, and return to 0°. The next full cycle reported 100140 bytes at
every sample, indicating stable free heap after warm-up. A board-side
`WindowedService` diagnostic selected each supported canvas depth and sampled
the same `MemoryService` used by BASIC `FREE()` after each rotation:

| Canvas depth | 0° baseline | 90° | 180° | 270° | 0° return |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 bit | 154,332 | 154,332 | 154,332 | 154,332 | 154,332 |
| 4 bit | 125,532 | 125,532 | 125,532 | 125,532 | 125,532 |
| 8 bit | 87,132 | 87,132 | 87,132 | 87,132 | 87,132 |

All values are free heap bytes. The 28,800-byte drop from 1-bit to 4-bit and
38,400-byte drop from 4-bit to 8-bit match the additional storage for 76,800
pixels. The depth was selected through the platform-neutral C++ adapter API;
the BASIC language currently has no pixel-depth setter command. No rotation
produced a further heap change at any tested depth. Shared memory
management uses the bounded compacting `WindowManager` canvas arena, ESP-IDF
`FREE(1..2)` metrics, and graphics cleanup when a task returns to its prompt.
The portable VM also compiles ordinary source without retaining an expanded
copy, reducing compiler preparation peaks. The CYD SPIFFS firmware was flashed
with scheduler source-buffer release and tested with five worker launch/kill
cycles. A no-graphics worker source-buffer probe went from 225,304 free bytes,
110,580 largest block, and 18 free blocks to 224,880, 110,580, and 22 after
one kill. The small total-heap change includes retained task-result metadata.
The graphics-worker run reduced total free heap from 203,556 to 163,816 during
the five-cycle run while the largest block stayed at 110,580; the heap returned
to 225,464 / 110,580 / 15 after reboot. That run also exercised the canvas
arena's retained high-water capacity, so it does not isolate task source
reclamation. The
current CYD firmware's opened-endpoint registry was also exercised: serial text and byte output,
null text/byte/graphics discard, type/count enumeration, and close/reopen all
worked.

### CYD automated heap run (2026-11-05)

The serial-REPL suite in `tools/ql32-memory-test.py` ran five repetitions each
of array, file-channel, graphics, debugger, and combined workers on the CYD
SPIFFS profile. It passed its per-worker recovery and fresh-reboot checks. The
initial idle median was 158,752 free bytes, with a 51,188-byte largest block
and 21 free blocks. Active worker samples reduced total free heap by 7,432 to
32,916 bytes depending on workload; the largest free block remained 51,188
bytes. After all 25 workers, before reboot, free heap was 152,956 bytes (5,796
bytes below the initial median), the largest block was still 51,188 bytes, and
the allocator reported 35 free blocks. After reboot, the median was 158,696
bytes, 56 bytes below the initial total, with the same largest block and 23
free blocks.

This is a pass for per-worker cleanup and reboot recovery, not evidence that
the full worker sequence has zero cumulative cost. The runner currently checks
recovery against the immediately preceding sample and compares the reset
baseline after reboot; it does not fail on cumulative free-heap drift across
separate workload groups. Retained task-result records are a known source of
small persistent allocations, but this run did not isolate their contribution.
The machine-readable run report is written to `memory-test-report.json` by the
runner.

### CYD memory and concurrency operating envelope (2026-11-07)

Follow-up saturation tests found no monotonic free-heap decline across the
repeated cleanup cycles. The ordinary five-by-five worker suite recovered to
within 1,872 bytes before reboot after explicitly reading cancelled-task
results and reaping them with a second `KILL`; reboot returned to within 60
bytes of the initial total-free baseline. Five 64×64-worker saturation cycles
left total-free readings 2,432–2,952 bytes below baseline, but the largest
block returned to 110,580 bytes after every recovery-window cycle. The
free-block count rose from 14 to 23–27, consistent with allocator
fragmentation/high-water variation rather than a steadily growing loss.

The practical graphics and worker limits are workload-specific:

- At 320×240 and 4 bits per pixel, one canvas requires 38,400 bytes. The
  configured 76,800-byte canvas arena admits two such canvases; a third is
  rejected by the canvas budget with over 109 KiB of general heap still free.
- Ten animated 64×64 workers ran with 13.4–13.7 KiB total free heap and a
  10.7–11.3 KiB largest block. The next worker failed during execution. The
  task was reported as out of memory while the supervisor remained responsive
  and `SAVE` succeeded. Ten is a demonstrated saturation ceiling, not a
  recommended target with additional workloads.
- A 320×240, eight-iteration Mandelbrot render did not finish within five
  minutes while array and channel workers were active. Treat that combination
  as outside the interactive operating envelope; use one compute-heavy
  full-screen render at a time and reduce other load when response time matters.

After a ten-worker OOM pass and task reaping, `FREE()` sometimes remained well
below its initial total and largest-block values; the largest block varied
from 31.7 KiB in one pass to 15.9 KiB in a later pass. The separate 25-run
cleanup suite returned to within 60 bytes of its initial total-free baseline
after reboot. OOM handling and supervisor recovery are verified, but the
remaining post-saturation high-water/fragmentation contribution is not
isolated and is not evidence of monotonic leakage. Use both `FREE(0)` and
`FREE(1)` to assess headroom before launching another graphics workload. These
measurements close the CYD memory/concurrency characterization item; see the
[full test log](../test-results/CYD-open-points-2026-11-07.md) for checkpoints.

The current implementation includes module-level `OPTION KOptMathSingle` and
`OPTION KOptMathDouble` (DOUBLE is the default), plus
`OPTION KEspCpuFreq80`, `KEspCpuFreq160`, and `KEspCpuFreq240` for ESP32
targets. The CPU request is device-wide and takes effect when a module starts;
Linux and macOS accept but ignore it. An earlier CYD firmware run using SINGLE
and 240 MHz animated without a reported VM error but was still slow by user
observation. The run predates the latest upstream pull and did not measure
frame duration or clock readback; rebuild and repeat before considering the
current tree hardware-verified.

