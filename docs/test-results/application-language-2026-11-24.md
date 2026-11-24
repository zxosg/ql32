# Application language extensions — implementation and validation

Date: 2026-11-24  
Source baseline: `6d52ea2ec2690b5da5d9ac963b67654ad5997836` plus the working-tree changes described here.  
Specification: [QL32-LANG-002 v0.3](../specification/QL32-LANG-002-application-features.md).

## Implemented

- Named pre-test `REPeat name WHILE condition` and post-test `END REPeat name UNTIL condition`, including named NEXT/EXIT targets and closing-condition source locations.
- One-statement TRAP and RAISE, with eight bounded protection records, persistent protection across scheduler waits, call/FOR/SELECT/dynamic-evaluation unwind, and allocation-free error capture. Runtime EVAL parse errors are catchable; malformed initial program source cannot install a trap. Cancellation and internal failures bypass protection.
- Numeric INKEY and printable INKEY$, with poll/finite/indefinite waits, stable normalized keys, fixed-capacity input routing, serial ANSI/UTF-8 decoding, lone-Escape timeout and preserved monitor protocol framing. Linux terminal mode is restored at input ownership/session teardown.
- Session directory channels: bounded native cursors, wildcard filtering, independent folder inclusion, incremental sorted collection and heapsort, staged reads, stable last-entry metadata after EOF, metadata validity bits, and exact size conversion rules for SINGLE/DOUBLE.
- Common include constants, language/API/VM documentation, and regression tests.

## Memory and scheduling

A sorted listing allocates one fixed 14 KiB payload block by default. Metadata records grow from the front, packed filenames from the back; there is no per-entry string allocation or extra sorting index. The cap is 256 entries, 16 KiB of payload/session, 32 KiB/registry and two directory handles/session. Entry or payload exhaustion raises code 18; allocator failure raises code 8. An application can explicitly retry with KDirSortNone.

Streaming enumeration allocates no directory-sized listing. It retains a bounded native cursor, path/pattern state, one lookahead filename and the last committed metadata. Sorted collection releases its native cursor and lookahead/pattern buffers before sorting. Job batches yield through the existing scheduler; native primitive I/O latency remains an adapter measurement requirement.

**Payload limits are not total heap limits.** Cursor implementation memory, bounded control/path buffers, BASIC variables/strings, compiler state and retained constants are additional costs. The common include's new constants also add compiler/runtime metadata because EVAL retains declared constants. The compiler now transfers its constant table into bytecode metadata instead of copying it, removing the second full constant-table allocation during preparation. Actual heap peaks and largest-free-block behavior on each board remain unmeasured.

SPIFFS virtual folders use repeated bounded scans, avoiding a folder-sized deduplication set. Scan time can grow substantially with many folders. Empty virtual folders have no independent representation.

## Host validation

Commands:

```sh
cmake -S . -B /tmp/ql32-app-build -DQL32_BUILD_TESTS=ON
cmake --build /tmp/ql32-app-build -j3
ctest --test-dir /tmp/ql32-app-build --output-on-failure -j4
```

Final host result: **68/68 tests passed; 0 failures**.

The application regression executable covers:

- Zero-pass pre-tests, post-tests, NEXT/SELECT behavior, recursive FOR ownership, trapped callee errors, failed stores, EVAL unwind and runtime syntax errors, rejected block traps, and UNTIL error locations.
- INKEY waits inside an expression without replaying a side-effecting function; missing capability, invalid timeout, cancellation, UTF-8, Down/Escape decoding, bounded queue overflow, focus isolation and monitor protocol discrimination.
- Directory folder/filter/name ordering, EOF and metadata lifetime, empty streams, quota failure without leaked handles, explicit streaming fallback, yielded REPL OPEN ownership and an inner trap preserving its caller's PRINT channel.
- A generated 1,000-entry stream without snapshot allocation, and an allocator-disabled directory failure that recovers with error 8 and publishes no handle.
- Typed device argument errors remaining catchable, and resumed INPUT failure cancelling only its abandoned UI request while preserving an unrelated notification.

Existing persistent-lifetime channel/hardware tests now use REPL submissions rather than whole-program execute calls, matching the pre-existing program-completion cleanup contract. The FabGL test fake now models the Bitmap/RGBA API already used by the presenter. Rendering code was not changed.

## Embedded compile checks

These are builds only. No firmware, filesystem image or board settings were flashed.

| Profile | Build | Static RAM bytes | Flash bytes |
| --- | --- | ---: | ---: |
| esp32_2432s028_spiffs | Passed | 51,608 | 1,600,365 |
| lilygo_vga32 | Passed | 54,104 | 1,667,781 |
| ql32_tenstar_esp32s3 | Passed | 50,608 | 1,560,801 |

Static linked RAM does not include runtime heap use. No baseline-to-change size delta was measured. The Olimex-specific entry-point branches and macOS builds were not separately built in this run.

## Hardware acceptance still required

- Measure per-session heap, peak directory/control/cursor allocation, fragmentation and largest-free-block changes, including compilation with the common include.
- Exercise serial and physical keyboard focus, dialogs/INPUT, lone Escape, protocol framing, cancellation and session switching on the actual boards.
- Exercise SD removal/I/O errors and SPIFFS virtual folders, concurrent filesystem changes, large/long-name directories, unknown metadata and repeated open/read/close cycles.
- Measure scheduler/display/audio progress and worst primitive I/O latency during scanning and sorting.

These checks qualify board runtime behavior; successful compilation alone does not.
