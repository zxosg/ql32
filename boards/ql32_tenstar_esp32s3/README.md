# TENSTAR TS-ESP32-S3

This opt-in profile targets the Tenstar TS-ESP32-S3 (ESP32-S3FH4R2), with
4 MiB flash, QSPI PSRAM, a 135×240 ST7789 panel, and a USB serial BASIC REPL.
The default display orientation is landscape at 240×135. The QMI8658C, BMP280,
and RGB LED are not integrated into QL/32 services yet.

The board adapter uses the following reported wiring:

| Signal | GPIO |
| --- | ---: |
| TFT MOSI | 35 |
| TFT MISO | 37 |
| TFT SCK | 36 |
| TFT CS | 7 |
| TFT DC | 39 |
| TFT reset | 40 |
| Backlight | 45 |
| I²C SDA | 42 |
| I²C SCL | 41 |

Build and flash from the repository root:

```sh
pio run -e ql32_tenstar_esp32s3
pio run -e ql32_tenstar_esp32s3 -t upload --upload-port /dev/ttyACM0
PLATFORMIO_DATA_DIR=data/boards/ql32_tenstar_esp32s3 pio run -e ql32_tenstar_esp32s3 -t uploadfs --upload-port /dev/ttyACM0
pio device monitor -p /dev/ttyACM0 -b 115200
```

The firmware profile reserves 512 KiB of the 4 MiB flash for SPIFFS at `FFS:/`.
The Tenstar-only filesystem folder contains a compact `startup.bas`,
`graphics-test.bas`, `sincos64-grid.bas`, `fsconsole.bas`,
`memory-test.bas`, three persistent worker programs, and a concurrent
Mandelbrot workload; `PLATFORMIO_DATA_DIR` selects it for the `uploadfs`
operation without changing the other boards' filesystem images.
`sincos64-grid.bas` draws a 64×64 eight-color field using four sine/cosine
modulations and task-origin phase. On the 240×135 panel, use six 80×67 task
viewports at X=0/80/160 and Y=0/68. Enter `WINDOW #0,239,0,1,135` once in the
supervisor REPL before launching them to narrow the text-console overlay to the
right edge, leaving the full display height available for both rows. The
Tenstar window manager allows 16 live windows with its 32,400-byte canvas
budget, enough for this six-task grid at four-bit depth.
The memory test reports `FREE(0)` (free heap bytes), `FREE(1)` (largest free
block), and `FREE(2)` (free blocks) to the USB serial channel before opening,
while holding, and after closing a small graphics window. The BASIC display
resolution and depth report as 240×135×4-bit. Confirm the actual USB port with
`pio device list`.

Board run on 2026-11-06 (ESP32-S3 rev 0.2, 4 MiB embedded flash, 2 MiB
embedded PSRAM):

| Check | Result |
| --- | ---: |
| Firmware static RAM | 50,136 / 327,680 bytes |
| Firmware image | 1,441,217 / 3,538,944 bytes (40.7%) |
| Stable free heap before window | 2,387,631 bytes |
| Free heap with 80×60 window | 2,384,735 bytes (2,896-byte drop) |
| Free heap after `CLOSEWINDOW` | 2,387,247 bytes (2,512 bytes recovered; 384-byte residual) |
| Largest free block | 2,064,372 bytes at all three checkpoints |
| Free blocks | 22 → 21 → 21 |

Multitasking stress run on 2026-11-06 used `DEBUGBAR ON`, `SLICE 64`, three
continuous workers, and `stress-mandelbrot.bas`. All four tasks remained
`RUNNABLE` during the first 15-second overlap. The three workers produced
heartbeats and reached roughly 120–180 cycles. After the workers were killed,
Mandelbrot remained `RUNNABLE` through a further 28 seconds and was then
cancelled. A few screen pixels blinked during concurrent drawing; the user
confirmed blinking stopped after the workers were closed, with no persistent
screen corruption.
The largest free block stayed at 2,064,372 bytes. Total free heap changed from
2,309,855 bytes under load to 2,375,191 bytes after all task windows were
closed; the final task list contained only the supervisor.

Repeat from the USB REPL with:

```basic
probe=OPEN(2)
DEBUGBAR ON
SLICE 64
TASK "FFS:/stress-worker-1.bas"
TASK "FFS:/stress-worker-2.bas"
TASK "FFS:/stress-worker-3.bas"
TASK "FFS:/stress-mandelbrot.bas"
TASKS #probe
```

Use the IDs shown by `TASKS #probe` with `KILL id` to close tasks. Mandelbrot
did not finish in the measured run; its run time should be revisited separately.

The startup, graphics smoke test, and memory test all ran through the serial
REPL without VM errors. Hardware still needs a visual color-order check; the
ST7789 initializes and accepts drawing commands, and USB CDC plus PSRAM were
confirmed on this board.
