# AY-3-8912 and VTX playback on Olimex

**Status:** Software implementation complete; Olimex hardware acceptance pending  
**Target:** `olimex_esp32_sbc_fabgl` first; portability to other boards is a later decision  
**Scope:** AY-3-8912 emulation and VTX playback, with a chip-neutral design for future extensions

## Goal

Add non-blocking AY music playback to QL/32 on the Olimex ESP32-SBC-FabGL Rev B. The first supported file type is VTX. Playback must coexist with the VGA scanout and leave the cooperative BASIC scheduler responsive.

The Olimex profile assigns GPIO 25 as `audio` in [pins.hpp](../../boards/olimex_esp32_sbc_fabgl/pins.hpp); the user has confirmed this connection from the board schematics. The VGA scanout currently uses I2S1, so the proposed output path is the classic ESP32's built-in DAC fed through I2S0 DMA on GPIO25. The ESP32 has an 8-bit DAC on GPIO25/26 and supports continuous DMA conversion; the hardware spike will validate the Arduino-ESP32 2.0.17 driver behavior, output quality, and coexistence with VGA. [Espressif DAC documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/dac.html)

The current implementation covers VTX AY/YM register streams, a portable AY renderer, QL/32 BASIC playback controls, and the Olimex GPIO25 output adapter. VTX input is capped at 64 KiB and decoded register data at 96 KiB. The Olimex PlatformIO profile and host music tests are the software acceptance checks; actual output level, playback quality, and coexistence with active VGA remain hardware checks.

## Scope and exclusions

### In scope

- Platform-neutral AY synthesis and generic music playback control.
- VTX header parsing, LHA5 payload decompression, register-stream playback, and song selection/loop behavior supported by the format.
- Olimex-only audio output adapter using the existing audio pin and I2S0/DAC path.
- BASIC `MUSIC` control to start, pause/resume, stop, and query the current playback.
- Reading music files through QL/32 storage services from `FFS:` or `SDC:`.
- Linux tests for format parsing, register timing, and player lifecycle; target build and hardware audio checks on Olimex.

### Out of scope for this phase

- SQT, STC, PT3, AY-container, or other tracker formats. Each needs its own loader/replayer; AY-container files may require Z80 code emulation rather than a plain register-stream parser. [Project AY format overview](https://worldofspectrum.org/projectay/gdmusic.htm)
- Amiga MOD playback, stereo output, external I2S DACs, and CYD/TENSTAR adapters.
- Direct hardware AY chip support. This phase synthesizes AY output in software and emits PCM.
- Claims of SMSQ/E compatibility. The command syntax and playback behavior will be QL/32-specific unless later backed by references and conformance tests.

## Proposed architecture

Keep the chip emulator, VTX decoder, and playback state in portable code. Do not put ESP-IDF, Arduino, or FabGL types in public interfaces.

1. **Chip renderer:** Define a portable chip-renderer boundary that accepts chip register writes and renders PCM into caller-provided buffers. Implement AY-3-8912 first. Keep chip selection/model details out of the BASIC `MUSIC` command and runtime ownership layer so another sound chip can be added behind the same playback service later. The renderer boundary should not assume that every future chip is mono; the Olimex adapter can downmix to its mono DAC output.
2. **VTX decoder/player:** Parse bounded file data and produce timestamped AY/YM register updates for the AY renderer. VTX stores AY/YM music using LHA5 compression; `libayemu` is a candidate reference/implementation because it provides AY emulation and VTX handling, but its old build system, API fit, code size, and GPL-2.0 licensing need review before vendoring. [libayemu repository](https://github.com/gasman/libayemu)
3. **Runtime music service:** Add a platform-neutral service, injected through `vm::Services`, with session-owned start/pause/resume/stop/status operations. Keep this service generic across file formats and chip renderers. Treat the physical output as a single shared stream; define how a second session is rejected or takes ownership. Stop/release a session's playback when that session ends.
4. **Olimex audio adapter:** Own I2S0/DAC initialization, a fixed-size DMA ring, and producer/render synchronization. Keep file reads, decompression, and memory allocation out of the DMA callback/ISR. Since playback must continue while BASIC waits or yields, audio timing must not depend on `SessionScheduler::tick()` cadence.
5. **Storage:** Open through `StorageService::openRead()` / `StorageReadHandle::readAt()` so `FFS:` and `SDC:` stay behind the existing storage abstraction. Set file-size, decompressed-size, song-count, and malformed-input limits before allocating buffers.
6. **BASIC surface:** Add QL/32-specific `MUSIC PLAY path$`, `MUSIC PAUSE`, `MUSIC RESUME`, `MUSIC STOP`, and a status command or function. Commands return promptly; playback is asynchronous and belongs to the issuing session. `MUSIC` remains format/chip-neutral; the first implementation selects VTX from the file extension or an optional format argument. Decide whether song selection is an optional `PLAY` argument after the VTX parser semantics are confirmed.

## Implementation phases

### 0. Olimex audio feasibility spike — hardware validation pending

- Use the schematic-confirmed GPIO25 audio connection and test its output level, audible quality, and whether additional amplification is required.
- Build a small Olimex-only experiment that streams a generated tone through I2S0's built-in DAC while FabGL VGA is active on I2S1.
- Check startup/shutdown, audible output, clipping/noise, sample-rate stability, CPU load, and interaction with VGA timing.
- Record measured memory, DMA buffer requirements, and the exact Arduino/ESP-IDF API used.

**Exit criteria:** sustained mono output works on Olimex while the normal VGA profile runs, without audible underruns or scanout regression. Record the measured output level and any amplifier requirement.

### 1. Portable AY synthesis and VTX decoding — implemented

- Select an implementation path after evaluating `libayemu` against a small native implementation. Prefer a portable C/C++ subset with no platform dependencies.
- Specify supported AY clock, output sample rate, amplitude table/mixing, tone/noise/envelope behavior, and VTX timing interpretation. Keep the renderer boundary reusable for additional chip implementations.
- Implement strict VTX parsing/decompression with bounds checks and failure reporting.
- Decode on the host first; expose register events or a render-to-PCM interface independent of the Olimex adapter.

**Exit criteria:** deterministic fixtures cover valid metadata, all supported VTX compression/header cases, malformed/truncated data, multiple subsongs, timing, and clean end/loop behavior. Rendered output has no buffer overrun and is comparable to a named reference player for a small test corpus.

### 2. Runtime service and BASIC controls — implemented

- Add the audio service interface and Linux fake/null adapter; add the service to `vm::Services` and Olimex capabilities only when the adapter is installed.
- Implement per-session ownership and cleanup on stop, task completion, cancellation, and error.
- Wire `MUSIC` commands through language parsing/compilation and VM/runtime dispatch, keeping each command non-blocking and independent of the selected chip backend.
- Define errors for unavailable audio, invalid file/format, out-of-range song index, resource exhaustion, and already-owned output.

**Exit criteria:** Linux tests prove command parsing, prompt return, state transitions, session isolation/ownership, and cleanup. Another BASIC task continues to make progress during playback.

### 3. Olimex streaming integration — implemented; target build passes

- Implement the I2S0/DAC adapter with fixed-size PCM buffers and bounded producer work.
- Feed generated PCM from the VTX/AY renderer into DMA without blocking the VM or doing filesystem work in interrupt context.
- Add diagnostics for underruns and peak buffer occupancy in a debug build; keep normal output quiet.
- Build the standard `olimex_esp32_sbc_fabgl` profile first. Use the PSRAM profile for larger VTX streams when the internal heap cannot hold the decoded register data.

**Exit criteria:** PlatformIO build succeeds; hardware playback is stable alongside VGA, keyboard input, graphics, storage reads, and ordinary BASIC worker activity.

### 4. Acceptance and portability checkpoint — pending hardware/corpus validation

- Test a short, legally redistributable or generated VTX fixture plus several independently selected real VTX files.
- Check stop/pause/resume, subsong selection, loop/end behavior, malformed files, SD removal/read errors, simultaneous session requests, and cleanup after task termination.
- Document output quality and hardware prerequisites in the Olimex board README and record measured CPU/heap/DMA use.
- After Olimex acceptance, assess what belongs in common profiles and what remains board-specific. CYD is a concrete follow-up candidate: its onboard speaker is wired to GPIO26, while the current CYD pin map assigns GPIO25 to touch SPI clock. Add that adapter only in the portability follow-up; it is not part of this Olimex implementation phase.

## Key risks and decisions

| Risk or decision | Plan |
|---|---|
| GPIO25 is confirmed by the Olimex schematic, while audible level/quality is not recorded in acceptance notes | Measure in Phase 0 and document any amplifier requirement. |
| I2S0 and built-in DAC behavior varies across ESP-IDF/Arduino versions | Pin the tested API to the project's current Arduino-ESP32 2.0.17 toolchain and keep all driver calls in the Olimex adapter. |
| VTX LHA5 decompression has malformed-input, memory, and task-stack risks | Use explicit input/output caps, validate every length/offset, keep the 8 KiB history dictionary off the BASIC task stack, and use PSRAM for large decoded streams when internal heap is insufficient. |
| AY emulator implementation choice and license | Review source/build/API/license before vendoring; retain an option to implement a smaller clean portable core. |
| Audio competes with VGA, VM, storage, and graphics for CPU time | Use DMA for sample cadence, measure render budget and underruns, and avoid scheduler-tick playback. |
| Multiple BASIC sessions request one physical output | Make ownership and preemption behavior explicit and tested; initial policy should reject a second owner with a clear runtime error. |
| “AY format” can mean multiple unrelated encodings | Document this phase as VTX only; do not imply SQT/STC or `.ay` container support. |
| Later chip support could couple command syntax to one emulator | Keep `MUSIC` control and playback ownership generic; add chip emulators behind the portable renderer boundary. |

## Current implementation notes

- `MUSIC PLAY path$` currently infers VTX and synchronously reads and decodes the bounded file before returning. PCM rendering and I2S0/DAC DMA output run in a dedicated task, independent of BASIC scheduler cadence.
- `MUSIC PAUSE`, `MUSIC RESUME`, `MUSIC STOP`, and `MUSIC STATUS` are implemented. A second session cannot take over an owned stream; session cleanup stops its playback.
- The VTX path supports raw LH5 packets and LHA5 archive-wrapped AY/YM register streams plus loop-frame metadata. Its 8 KiB history dictionary is heap allocated to avoid overflowing the BASIC task stack. Other tracker formats and sound chips remain out of scope.
- Verification completed: synthetic host fixtures exercise VTX/LH5 parsing, AY tone generation, and BASIC music commands. The user-provided `examples/music/Bosconia1.vtx` also parses as 6,656 frames and renders one second of non-silent PCM; `pio run -e olimex_esp32_sbc_fabgl` succeeds. Physical output checks remain pending.

## References

- [Olimex ESP32-SBC-FabGL board notes](../../boards/olimex_esp32_sbc_fabgl/README.md)
- [Olimex pin assignment](../../boards/olimex_esp32_sbc_fabgl/pins.hpp)
- [Platform capability model](../../include/ql32/platform/capabilities.hpp)
- [VM service injection](../../include/ql32/vm/scheduler.hpp)
- [Storage service and bounded read handles](../../include/ql32/runtime/storage.hpp)
- [ESP32 DAC continuous/DMA documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/dac.html)
- [libayemu](https://github.com/gasman/libayemu)
- [Project AY format overview](https://worldofspectrum.org/projectay/gdmusic.htm)
