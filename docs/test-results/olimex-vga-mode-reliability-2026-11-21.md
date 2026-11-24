# Olimex VGA MODE reliability, 2026-11-21

## Scope and firmware

Investigated the owner report: "vga modes are not working reliably. changing
modes are rejecting or not changing graphic mode."

Fetched `origin/main` and the clean starting worktree both resolved to
`289f0c8`. Music was playing on the installed board, but music does not
identify the VGA environment. Before flashing, debugger task geometry was
320x200, `MODE(0),MODE(1)` returned `0,0`, PRESDIAG/COALDIAG were undefined,
and MODE 1 successfully changed to 320x240. This proves a switchable FabGL
path, consistent with the plain PSRAM profile; it does not prove the exact
installed revision. `SYSINFO$(1)` is `unknown` and `SYSINFO$(3)` is a static
display string, so neither uniquely identifies the running binary.

Built and flashed `olimex_esp32_sbc_fabgl_psram`, first unmodified main and
then the candidate fixes. Flashing used esptool `write_flash 0x10000` with
only the application binary. FFS and SDC were mounted and their contents
were not replaced. Serial: `/dev/ttyUSB0`, 115200 baud.

Final candidate firmware binary SHA-256:
`b1e1775f186cbc2e5c6c365eead574560a011f498cd263f2570278a251ec2519`.
No core pinning, DMA ring depth, VGA modeline or scanout timing was changed.

## Distinct root causes

| Category | Finding |
| --- | --- |
| By design | Fixed experimental DMA 640x480 firmware accepts only MODE 4,0; fixed 640x400 accepts only MODE 3,0. MODE 0-2 rejection there is intentional. This was not the installed path reproduced here. |
| Memory capacity / misleading expectation | FabGL exposes presets 0-4, not merely 0-2. All scanout pixels still use internal DMA RAM. Several MB of free PSRAM do not make MODE 2-4 or double buffering fit. Earlier plain-PSRAM MODE 2 allocation refusals reproduce on main. |
| Genuine shrink defect | With automatic console updates/coalescing enabled, MODE 1 -> MODE 0 repeatedly failed with `composed region exceeds VGA canvas geometry`. The old display's deferred console damage was merged with a new console-bounds invalidation after shrinking, without clipping the merged rectangle. Disabling updates avoided the failure. |
| Genuine double-buffer defect | MODE 0,1 on a fresh plain-PSRAM boot corrupted the heap and rebooted. FabGL's partial double allocation can allocate an odd total number of rows, divide the viewport height by two, then write the extra row pointer past the visible pointer array. The corruption is detected while freeing that viewport during rollback. |

Unmodified-main representative responses:

```text
MODE 1,0
Display mode set to 320x240 (320x240); window contents preserved where possible.
MODE 2,0
BASIC session 1 VM error: VGA framebuffer allocation incomplete for 400x300: allocated 400x226, required 400x300
MODE 3,0
BASIC session 1 VM error: VGA framebuffer allocation incomplete for 640x400: allocated 640x145, required 640x400
MODE 4,0
BASIC session 1 VM error: VGA framebuffer allocation incomplete for 640x480: allocated 640x142, required 640x480
MODE 0,0
BASIC session 1 VM error: composed region exceeds VGA canvas geometry
```

Fresh-boot double-buffer reproduction:

```text
MODE 0,1
CORRUPT HEAP: Bad tail at 0x4009c638. Expected 0xbaad5678 got 0x3ffd6060
assert failed: multi_heap_free multi_heap_poisoning.c:259 (head != NULL)
Rebooting...
```

The decoded candidate stack is `VGABaseController::freeViewPort` ->
`freeBuffers` -> `end` -> `setResolution` -> `DisplayModes::apply`
during rollback. FabGL v1.0.9 allocation code was inspected read-only;
dependency files were not patched.

## Changes and rejected approaches

Coalescing now intersects merged pending damage with the current display.
A host regression that invokes console invalidation during a shrink failed
before this change and passes afterward.

All FabGL builds preflight mode and buffering-option changes, accounting for
both current and requested buffers. Total free DMA bytes alone are not
sufficient: a first attempt still reproduced double-buffer corruption.
A largest-single-block check prevented the crash but wrongly refused working
MODE 1 because FabGL can use several pools. That approach was discarded.

The final adapter releases the old viewport for growth and probes **complete
row-pool allocation**, including scanout bookkeeping and FabGL's per-block
minimum-free-block constraint, before calling FabGL with the requested size.
Temporary allocations are released on both successful and failed probes.
Rejected probes restore and validate the old viewport. Rollback failure is
reported explicitly instead of silently swallowed.

Errors identify modes supported by this build, qualified by internal DMA
capacity, and explain that PSRAM cannot supply scanout. Invalid named modes
and option bits on fixed DMA builds now also identify their single supported
mode. MODE 4 double buffering remains explicitly unavailable.

Final-firmware representative safe refusal:

```text
MODE 0,1
BASIC session 1 VM error: not enough contiguous DMA memory for VGA mode 320x200: largest block 106484, framebuffer 128000, scanout overhead 23280, reserve 40960; PSRAM cannot supply scanout; supported modes (internal DMA memory permitting): 0 (320x200), 1 (320x240), 2 (400x300), 3 (640x400), 4 (640x480)
```

## Built / host-tested / hardware-verified

- **Built:** final qualified PSRAM FabGL firmware.
- **Host-tested:** Linux preset build and full CTest, **84/84 passed**.
  Regression coverage includes deferred damage during console resize,
  buffer-count/row-pool thresholds, fragmented double-buffer refusal before
  FabGL allocation, multi-pool growth, repeated requests, shrink, probe cleanup,
  explicit failed rollback, and fixed-build support diagnostics.
- **Hardware-verified by owner:** labeled 320x200 SINGLE and 320x240 SINGLE
  border/text fixtures both stable and readable; MODE 1 visibly taller.
- **Serial-verified:** **41 MODE attempts**, each followed by MODE queries,
  supervisor geometry from the debugger, free heap and largest free block.
  No unexpected reboot, heap corruption, worker error or successful response
  with mismatched geometry occurred on the final firmware.

| Phase | Requests | Successes | Explicit refusals | FREE(0) range | FREE(1) |
| --- | --- | --- | --- | --- | --- |
| Idle: three 0/1/1/0 cycles plus refusal matrix | 22 | 12 | 10 | 4,208,587-4,223,475 | 4,063,220 |
| Loaded: two 1/0/0/1 cycles plus refusal matrix | 18 | 4 | 14 | 3,994,595-3,995,307 | 3,932,148 |
| Recovery MODE 0 after stopping workers/audio | 1 | 1 | 0 | 4,210,083 | 4,063,220 |

The refusal matrix covers `(mode,option)` pairs `(0,1)`, `(1,1)`, `(2,0)`,
`(2,1)`, `(3,0)`, `(3,1)`, `(4,0)`, `(4,1)`, `(5,0)`, `(0,2)`.
All refusals leave the queried mode/options and supervisor geometry unchanged.
Normal MODE 0/1 cycling succeeds with no workers. Under simultaneous
Bosconia1 VTX playback, scrolling PRINT, string/array churn, arithmetic loop
and an animated attractor, **all four MODE 1 requests are explicitly refused
for internal DMA capacity**. MODE 0 remains active and tasks keep progressing.
This is a memory limitation, not a claim that every advertised preset now fits.
All four workers were RUNNABLE with empty errors at loaded checkpoints.

FREE(0)/FREE(1) above are aggregate heap metrics including PSRAM, not DMA
capacity. DMA refusal diagnostics separately report free/reclaimable bytes,
largest released block and requested framebuffer/scanout overhead. The four
cancelled task records remain listed after KILL; the final aggregate heap is
therefore not asserted to equal the pre-worker baseline.

The serial harness initially expected killed workers to have status `KILLED`,
but the protocol actually returns `CANCELLED`. That final harness assertion
was corrected; all 41 already-recorded hardware requests, checkpoint queries
and terminal task states were revalidated from the raw capture. This was
not a firmware or worker failure.

Owner confirmed the final MODE 0 recovery picture is stable/readable and the
prompt usable. Mid-stress picture stability was not separately owner-confirmed.
Music is stopped; all four test workers are CANCELLED with zero-size viewports.
The serial connection was closed and `fuser /dev/ttyUSB0` reported no owner.
The board is left on the final qualified PSRAM FabGL application, MODE 0,0.

Evidence: [unmodified-main serial log](olimex-vga-mode-reliability-2026-11-21-baseline.log),
[final serial matrix](olimex-vga-mode-reliability-2026-11-21-serial.log),
[structured checkpoints](olimex-vga-mode-reliability-2026-11-21.json).
