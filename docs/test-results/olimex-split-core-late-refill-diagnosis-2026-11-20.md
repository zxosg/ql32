# Olimex split-core late refills: idle, CPU-only, attractor, FFS save (2026-11-20)

Step 4. Goal: why the split build is ~16% late at idle. Owner present; app-partition flashes only; standard `present_profile` reflashed at the end (picture flickered while a CPU-only benchmark task was still running; stable after a board reset).

## Code review (no flash)

- EOF interrupt: `ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM`, pinned to core 1 in the split build (`QL32_OLIMEX_DMA_INTERRUPT_CORE1_TEST`); the handler is `IRAM_ATTR`. A flash-cache stall does not block the ISR.
- Refill worker `VgaRefill`: priority 4, core 1 (`QL32_OLIMEX_DMA_REFILL_CORE1_TEST`), body and PSRAM source reads not IRAM-resident. Ring is 120 rows in 60-row groups, so a batch must finish within about two groups (about 3.8 ms) or the next EOF finds the row unready.
- Standard profile runs loop, ISR and refill all on core 1; the split moves the Arduino loop (BASIC) to core 0.
- Lateness is counted in the ISR (`lineReadySequence_` mismatch), so it counts only rows not refilled in time, not skips.

## Measurements (split+budget build, budget 0 unless noted)

| Window | Late / EOF | Refill avg per row | Per 60-row batch | Max batch |
| --- | --- | --- | --- | --- |
| Idle, fresh boot, 28 s and 33 s | 15.7% / 15.4% | not yet measured | - | 9.5 ms |
| Idle 20 s (new counter) | 15.7% | 38.1 us | 2.28 ms | 9.5 ms |
| CPU-only (abcpu) 20 s | 66.5% | 98.4 us | 5.9 ms | 10.7 ms |
| Attractor 30 s | 46% | n/a | n/a | 28 ms |
| FFS `SAVE` window (x5, 6.6 s each) | 21-23% | n/a | n/a | 11.4 ms |
| Standard placement, idle 20 s | **0%** | 33.9 us | 2.03 ms | 2.3 ms |

- Scanout needs one row every 31.8 us. Standard idle refill cost is 33.9 us per row, average batch 2.03 ms against a ~3.8 ms deadline.
- On the split build idle cost rises to 38.1 us per row (+12%) with outlier batches of 9.5 ms; those outliers are the ~16% late refills. CPU-only more than doubles per-row cost (98 us), so about two thirds of EOFs are late.
- FFS saves add about 6 points over the 16% idle baseline within the same window (the first run with the attractor still running was uninformative). No sign of a flash-cache stall long enough to dominate; `SAVE` printed no confirmation, so file creation was not independently verified.
- Standard CPU-only could not be measured: the REPL did not answer `DMADIAG` while the task ran, and the board had to be reset.

## Conclusion

- Hypothesis supported: the refill is marginal on its own (about 34 us per row vs a 31.8 us row period; only the 2-group lead saves the standard build) and is slowed by activity on the other core, even an idle REPL loop (+12%, occasional ~10 ms batches), and heavily by CPU-bound BASIC (x2.6). That points to shared SPI/PSRAM/flash bus or cache contention across the cores, not CPU starvation of the refill task (core 1 is otherwise empty on the split).
- This is why standard is clean (nothing competing on core 0) and why splitting BASIC off hurts the picture although it speeds up presents about 30x.
- Presenter write budget (previous report) is irrelevant because the contention source is general core-0 traffic, not present writes alone.

## Status

- Built: split budget env, standard present_profile (both with `refill-total-us`).
- Host-tested: unchanged since the previous report (ctest 80/83, 3 known failures: scheduler, channel, hardware); only diagnostic counters changed in this step.
- Hardware-verified: numbers above; standard picture stable after reset.

## Proposed step 5

1. Make the refill cheaper per row so there is margin: copy PSRAM rows with 32-bit aligned loads, unrolled, or stage 60-row batches via one `memcpy`; target well under 25 us per row. Telemetry: refill-total-us / refills on idle and CPU-only. Owner test: standard and split pictures.
2. Place the hot refill path and its LUT in IRAM/DRAM so it does not compete for the flash/PSRAM cache with core 0.
3. Only then retest the split with 160-row ring versus standard (needs its own hypothesis).
