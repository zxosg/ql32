# QL/32 application language extensions

**Document ID:** QL32-LANG-002  
**Version:** 0.3  
**Status:** Implemented; 68 host tests and three firmware builds pass. Board runtime acceptance remains pending.  
**Scope:** Conditional named `REPeat`, timed key input, directory channels, and runtime error trapping.

Implementation validation is recorded in [the application language report](../test-results/application-language-2026-11-24.md).

This supplements [QL32-LANG-001](QL32-LANG-001.md). The syntax agreed for loops and directory `OPEN` is retained. The additions are QL/32-specific; inspiration does not establish SMSQ/E, OPL, or Lua compatibility.

## 1. Review conclusions and implementation baseline

Review baseline: local checkout `6d52ea2ec2690b5da5d9ac963b67654ad5997836`. This review examines that source, not an assertion that the checkout matches current GitHub main.

| Finding in version 0.1 | Revised decision |
| --- | --- |
| Runtime sorting was described as memory-efficient without any bound. | Explicit snapshot byte and entry budgets, including allocation peaks; failure rather than truncation or implicit fallback. |
| `StorageService::list` returns a full vector. | Add an independent storage cursor contract; using `list()` underneath streaming enumeration is prohibited. |
| `EOF` could invalidate the final entry's metadata before it was queried. | Separate next-entry state from current-entry metadata; EOF queries never invalidate current metadata. |
| A snapshot was implicitly atomic with filesystem changes. | A frozen result after collection, with no atomic directory-view guarantee during collection. |
| Keyboard focus and special-key encoding were unspecified. | Use session console input `#0`, one input owner, bounded queues, stable disjoint special codes. |
| Suspending inside a function was treated as an ordinary call. | Resume through VM continuation state; evaluate operands once and deliver one result. |
| `TRAP` had no unwinding or out-of-memory recovery contract. | Bounded protected-call records; restore execution checkpoints before resuming. |
| Every failure was described as a session error. | Preserve existing untrapped behavior; cancellation, compiler errors, and internal faults bypass traps. |

Relevant source contracts:

- `src/language/interpreter.cpp`: `blockRepeat()` already lowers repeats into jumps and resolves named loop control. Add conditional lowering here.
- `src/language/bytecode.hpp`: reuse jumps and add compact protection/continuation metadata only where needed. Do not enlarge every instruction with feature-specific payloads.
- `src/vm/execution_state.hpp`: operand stack, call frames, FOR states, SELECT state and pending waits already live in the execution state.
- `src/vm/interpreter.cpp`: outer run/resume exception handlers currently destroy execution state. Traps must intercept recoverable errors before those handlers.
- `include/ql32/input/service.hpp`: `KeyEvent` and nonblocking `KeyboardService::poll()` already exist. Reuse them through a session input router.
- `include/ql32/runtime/storage.hpp`: existing `FileEntry` has name, size and directory status, but no modification time or attribute validity. Extend through a new cursor/metadata contract without breaking the existing listing API.
- `include/ql32/runtime/channel.hpp`: session-owned channel namespace and typed device arguments already exist. Directory handles join that namespace; they are not OS file descriptors or one file handle per entry.
- `include/ql32/runtime/error.hpp`: reuse stable error numbers; use typed errors in new paths rather than matching message text.

## 2. Embedded architecture and resource rules

The frontend parses and compiles. The VM manages continuations and error protection. Portable runtime services implement input routing, directory filtering/sorting and quotas. Platform adapters expose primitive nonblocking input and incremental storage operations; no FabGL, LVGL, filesystem-native structs or native attribute bits enter portable interfaces.

The snapshot-payload quota bounds the extra directory-sized allocation. It is not a total-heap or total-I/O budget: adapter-native cursor memory, control records, bounded filename/path buffers and allocator bookkeeping must be added to profile RAM accounting. This implementation clarification replaces the earlier claim that the payload quota covered every allocation.

Resources belong to the session and participate in the existing execution/channel teardown paths, including normal program completion, failed execution and cancellation. `STOP` retains state under the existing continuation rules. Ordinary trap recovery does not close all session channels.

Resource limits are configuration, not new BASIC syntax. Proposed initial embedded defaults below are engineering starting points, **not measured board capacity or final acceptance results**. Profiles may lower or raise them after measurement; host builds must also exercise constrained settings.

| Resource | Initial embedded budget | Accounting rule |
| --- | ---: | --- |
| Directory channels | 2 per session | Also obey existing shared channel/session limits. |
| Sorted snapshot payload | 14 KiB per handle; 16 KiB per session; 32 KiB per channel registry | One fixed block contains packed names and metadata; no growth/copy/sort scratch allocation. Bounded cursor/control costs are additional and must be measured. |
| Entries per sorted directory | 256 | Byte budget still applies first; long names reduce capacity. |
| Input queue | 16 normalized events per active input endpoint | Fixed-capacity queue; no directory-dependent or key-repeat-dependent growth. |
| Active trap records | 8 per execution | Allocate once when needed; no heap allocation on each protected statement. |
| Captured trap context | 96 UTF-8 bytes plus terminator per interpreter | Fixed capture storage; truncate detail on a UTF-8 boundary. String materialization is lazy. |

Streaming directory memory must be independent of entry count: one adapter cursor, one current record and at most one lookahead record, plus bounded path/pattern buffers and one reusable I/O scratch area. Publish each profile's actual cursor size, maximum path/name byte lengths and allocation peak before board acceptance. Overlong input or names fail explicitly; never silently truncate a usable filename.

Long scans, filtering and sorting must be incremental jobs with a bounded amount of work per scheduler turn. Check cancellation between batches, release filesystem locks before yielding, and share scratch space only while an operation is running. An adapter may perform a synchronous primitive disk read, but the plan must measure its worst observed latency; bytecode slicing alone cannot bound time spent inside storage calls. No full-directory lock or long recursive sort runs inside a single VM turn.

## 3. Conditional named `REPeat`

### 3.1 Syntax and meaning

```basic
REPeat name WHILE condition
    statements
END REPeat name

REPeat name
    statements
END REPeat name UNTIL condition
```

- `WHILE` evaluates before every pass. False exits; zero passes are possible.
- `UNTIL` evaluates after every pass. True exits; at least one pass occurs.
- Both use existing numeric truth and type rules. Names match case-insensitively.
- A loop cannot have both clauses. Conditional forms are block-only; existing unconditional block and inline forms remain unchanged.
- `EXIT name` goes directly after the target loop, without testing again.
- `NEXT name` goes to the target loop's condition: the leading `WHILE` test or trailing `UNTIL` test. For unconditional repeats it retains its current restart behavior.
- Outer-loop targets must discard inner FOR/SELECT bookkeeping as required by existing named control rules. Apply existing source-jump scope restrictions: a `GOTO` must not enter a protected or structured region illegally.

### 3.2 Implementation cost

Lower conditions to existing conditional/unconditional branches. Keep loop identity and pending branch patches in compiler state. No heap object, iterator or runtime condition closure is added per repeat. Preserve source positions on the test for stepping and error reports. Check keyword abbreviation conflicts when adding `WHILE` and `UNTIL`.

```basic
running = 1
REPeat controls WHILE running
    key% = INKEY(250)
    IF key% = KKeyEscape THEN running = 0
END REPeat controls

REPeat prompt
    INPUT "Continue? "; answer$
END REPeat prompt UNTIL UPPER$(answer$) = "Y"
```

The prompt exits on Y. This corrects the inverted example in version 0.1.

## 4. Timed keyboard input

### 4.1 Public functions

```basic
key% = INKEY([timeoutMs])
key$ = INKEY$([timeoutMs])
```

Omitted parentheses follow existing zero-argument function parsing conventions. Timeout omitted or zero polls; a positive integer waits up to that many milliseconds; `-1` waits indefinitely. Accept finite integral values `-1..2147483647`. Wrong types raise code 4; fractional/out-of-range values raise code 5. Use a monotonic deadline stored once when the wait starts.

The source is session keyboard console `#0`, resolved using the existing session binding rules. It is independent of the output default selected by `CHANNEL`. Version 1 does not add arbitrary file/serial channel input to these functions. A rebound endpoint lacking key-input capability raises code 19; an absent input service raises code 13. A background session with a valid console but no focus waits for routed input or its timeout, rather than consuming another session's keys.

`INKEY` returns `0` on timeout, Unicode scalar values for printable text, and the following QL/32 key constants. These values fit Int32 and remain exact in SINGLE arithmetic.

| Constant | Value |
| --- | ---: |
| `KKeySpace` | 32 |
| `KKeyEnter`, `KKeyEscape`, `KKeyBackspace`, `KKeyTab` | 1114112, 1114113, 1114114, 1114115 |
| `KKeyInsert`, `KKeyDelete`, `KKeyHome`, `KKeyEnd` | 1114116, 1114117, 1114118, 1114119 |
| `KKeyPageUp`, `KKeyPageDown` | 1114120, 1114121 |
| `KKeyLeft`, `KKeyRight`, `KKeyUp`, `KKeyDown` | 1114122, 1114123, 1114124, 1114125 |
| `KKeyF1` through `KKeyF12` | 1114126 through 1114137 |

Special codes begin at `0x110000`, outside Unicode. NUL, releases, modifier-only events and unsupported events are not delivered as keys. Modifiers may affect produced text but are not returned separately. Break/cancel remains a supervisor action and cannot be swallowed by `INKEY` or `TRAP`.

`INKEY$` consumes the same event and returns its printable UTF-8 scalar, or `""` for timeout or a nonprinting key. It does not keep waiting after consuming a special key. This agreed convenience form deliberately loses that distinction; use numeric `INKEY` for application controls. No echo and no Enter requirement. Neither form flushes pending input.

### 4.2 Routing and continuations

One router polls physical input and routes it to the active endpoint. The REPL line editor, dialogs, BASIC `INPUT` and `INKEY` must not poll competing copies of the same keyboard. Modal input temporarily owns focus; worker input never steals supervisor keys. Clear queued events and partial decoder state when ownership transfers between unrelated sessions or between supervisor and application; do not transfer old key presses to a new owner. A full queue drops the newest normal event and increments a diagnostic counter; cancellation uses a separate path. Auto-repeat rates remain adapter-defined.

Serial input must use normalized events too. Add missing Down/Escape decoding and a bounded escape-sequence timeout (proposed 30 ms) so a lone Escape works without returning an arrow's escape prefix as a separate key. Decode UTF-8 incrementally with bounded state. The Linux console adapter must deliver unbuffered key events and restore terminal mode when focus is released or execution ends.

Timed input needs a VM continuation, not an operating-system sleep or a platform polling loop. Save evaluated arguments, deadline, result mode and continuation address; yield only this session. On wake, push exactly one result and continue the suspended expression. Do not re-evaluate preceding side effects or restart the timeout on every scheduler pass. Keep an enclosing trap active across waits. Reuse existing scheduler waiting machinery with key-ready/deadline checks; do not allocate a timer/thread per call. Polling returns immediately; ordinary bytecode scheduling still guarantees fairness.

Timeout expiry makes the task eligible to run; delivery is subject to scheduler and device latency, not a hard real-time deadline. If a key is queued when the resumed operation checks readiness, it wins over expiry. Suspension by the supervisor retains the wait and its original deadline.

## 5. Directory enumeration channel

### 5.1 Opening, options and filtering

```basic
d% = OPEN(KDeviceDirectory, "SDC:/music", "*", KDirIncludeFolders + KDirSortNameAscending)
```

Keep the agreed four-argument typed `OPEN`. Reuse session handle allocation, ownership checks, binding resolution and `GETCHANNEL` reporting, adding a Directory endpoint type without renumbering existing device/type values. Each directory uses one channel and one cursor or snapshot. Do not open every listed file.

All options are finite non-negative integers, fitting Int32. Use independent single bits, not an encoded sort enum whose sums might accidentally select a different valid mode:

| Constant | Proposed value |
| --- | ---: |
| `KDirSortNone`, `KDirExcludeFolders` | 0 |
| `KDirIncludeFolders` | 1 |
| `KDirSortNameAscending`, `KDirSortNameDescending` | 2, 4 |
| `KDirSortDateAscending`, `KDirSortDateDescending` | 8, 16 |
| `KDirSortSizeAscending`, `KDirSortSizeDescending` | 32, 64 |

Allow at most one sort bit and reject unknown bits (code 2). Addition is valid when each chosen nonzero constant occurs once; runtime cannot recover duplicate intent from an already evaluated numeric expression. Validate the resulting mask only.

Omitting the inclusion bit excludes directories. With inclusion enabled, return all immediate child directories regardless of the file wildcard. Never return `.` or `..`. The pattern matches file leaf names: `*` means zero or more Unicode scalars and `?` one scalar. Use existing QL/32 ASCII case folding with exact non-ASCII comparison; no locale tables or Unicode normalization dependency. Reject path separators in the pattern; no recursion or special `**` traversal. Use a nonrecursive matcher with bounded state and include matching work in scheduling batches.

Paths and returned names retain actual case. Hidden entries are not implicitly excluded. Follow existing drive/root confinement rules; do not escape a device through links. Link handling must reuse the adapter's confinement policy and be covered by adapter tests. SPIFFS synthesizes a virtual hierarchy from slash-separated file names with repeated incremental scans and no directory-sized deduplication set. Virtual folders have unknown metadata and exist only while a descendant file exists. There are no independently persistent empty SPIFFS folders.

A valid directory with no matching entries opens successfully. Missing directories/devices and non-directory paths produce the existing appropriate typed error. Directory channels reject byte transfers, seek, write and file-only operations with code 19. Close and metadata operations follow normal binding resolution and ownership checks.

### 5.2 Reading and metadata lifetime

```basic
REPeat entries WHILE EOF(d%) = 0
    name$ = READ$(d%)
    isFolder% = DIRINFO(d%, KDirIsDirectory)
    size = DIRINFO(d%, KDirSize)
    flags% = DIRINFO(d%, KDirFlags)
    modified$ = DIRINFO$(d%, KDirDateTime)
    PRINT name$
END REPeat entries
CLOSE d%
```

`READ$` returns exactly one leaf name, with no path prefix or slash. `EOF` reports whether a next matching entry exists. Streaming `EOF` may fill one lookahead record; repeated calls are idempotent, must not consume entries, and may suspend while scanning for a match. I/O failure is an error, never EOF.

Metadata belongs to the last **successfully returned** record. `EOF`, including true after the last record, does not invalidate it. It is replaced only by the next successful read or invalidated by close. Before the first read, metadata access raises code 2 with contextual detail. Reading past EOF raises code 12 with end-of-directory detail; do not invent a new stable code in this addition. Wrong/closed handles raise code 14; unknown or wrong-result-type selectors raise code 2.

The service must stage a result before advancing the externally visible cursor: if constructing the BASIC string fails, retry returns the same pending record and prior current metadata remains valid. A failed read must not silently skip a filename.

| Selector | Result |
| --- | --- |
| `KDirIsDirectory` | Numeric 1 for directory, 0 for file. |
| `KDirSize` | Numeric byte count; 0 for directories or unknown size. |
| `KDirFlags` | Portable attribute mask, unsupported/unknown bits clear. |
| `KDirKnownFields` | Validity mask described below. |
| `KDirDateTime` through `DIRINFO$` | Modification text `YYYY-MM-DDTHH:MM:SS`, or empty if unknown. |

Define `KDirKnownSize`, `KDirKnownDateTime`, and `KDirKnownFlags` as bits 1, 2, 4 in `KDirKnownFields`. Attribute flags are a separate domain: `KDirFlagHidden`, `KDirFlagReadOnly`, `KDirFlagSystem`, `KDirFlagArchive` use bits 1, 2, 4, 8. Only set KnownFlags when all four portable attributes can be determined under the adapter's documented mapping. Missing metadata must not be indistinguishable from a known zero without a validity query. Type is required; an entry whose type cannot be determined causes a read/open error rather than being mislabelled a file.

Store sizes as unsigned integers internally, never floats. Numeric exposure is limited to the consecutive exact-integer range of the selected math precision: `0..2^24` in SINGLE and `0..2^53` in DOUBLE; larger sizes raise code 6 when requested. A `%` destination adds its existing Int32 conversion constraint. Sorting uses exact internal sizes regardless of BASIC precision.

Time text is the adapter's documented filesystem wall-clock convention, with no invented UTC suffix. FAT timestamps may have no timezone and coarse resolution. Do not convert unknown time to the Unix epoch. Within a device, date sorting uses that same convention; no cross-device chronology is promised.

### 5.3 Streaming and sorted collection

- `KDirSortNone` streams with bounded memory in unspecified native order. Concurrent filesystem mutation can affect results; no stable view or repeat order is promised.
- Sorted modes collect the matching entries incrementally at `OPEN`, then freeze the collected names and metadata and sort in portable runtime code. They are stable after successful publication. Collection is not atomic against concurrent directory changes; additions/removals during collection may affect the result. Never claim an atomic snapshot without filesystem support.
- Folders sort first. Within each group, use the requested key/direction, then ascending ASCII-folded name, then ascending exact UTF-8 bytes. Unknown primary metadata sorts last within its group in either direction. Sorting does not stat/open files repeatedly inside the comparator.
- Store names and metadata from opposite ends of one fixed block, using compact offsets. Avoid one `std::string` allocation per filename and full-vector copies. Incremental heapsort swaps metadata records in place; recursion, an index array and entry-count-sized merge buffers are unnecessary.
- Apply both entry and payload-byte quotas throughout collection. There are no payload growth/copy peaks because the block is allocated once; account bounded control and adapter costs separately. Example only: 256 records at a candidate 24 bytes, 256 four-byte indexes, and mean 24-byte terminated names already require 13 KiB before cursor/chunk overhead. Thus 16 KiB is not a promise to hold 256 arbitrary filenames. Measure the actual record layout rather than adopting that estimate as an ABI.
- Quota exhaustion raises code 18; allocator failure raises code 8. Unwind the job, release its cursor/chunks, and publish no channel. Never return a truncated listing, silently switch to streaming, or spill to flash. The caller may explicitly retry using `KDirSortNone`.
- Sorted directory reads remain valid if a listed file is later deleted; a subsequent file open may fail normally. To refresh, close and reopen. Reopening a stream starts a new live traversal, not a rewind of the old view.

`OPEN`, streaming `READ$` and streaming `EOF` therefore need resumable service jobs, not just new synchronous calls. Add a storage cursor with distinct entry/end/error outcomes and portable metadata validity. Adapters lacking bounded iteration report unsupported; do not implement the embedded streaming path with `StorageService::list()` as a fallback. Sorted collection must also read that cursor, avoiding a hidden intermediate full listing.

## 6. Runtime error trapping

### 6.1 Statement boundary and error state

```basic
TRAP executable_statement
RAISE code
```

`TRAP` protects one simple executable statement, including an assignment or a call to a user procedure/function. It covers argument evaluation and synchronous or resumed failures anywhere in that call. It does not prefix declarations, structured blocks, block delimiters, control transfers (`GOTO`, `EXIT`, `NEXT`, `RETURN`, `STOP`, `RUN`, `NEW`, task control) or another `TRAP` prefix. Calls may execute inner trapped statements: protection nests dynamically even though `TRAP TRAP ...` is invalid.

A colon ends the protected statement: `TRAP a = 1 / 0 : PRINT ERR` protects only the assignment. Existing inline IF branches may contain trapped simple statements; `TRAP IF ...` itself is rejected. Apply scope validation to prevent jumps into or out of an active protected statement; procedure return removes records owned by the returning frame.

At entry clear `ERR` and captured context. At normal completion set them to 0/empty, including after a called procedure handled its own error. On failure the innermost active trap receives the stable code and bounded context, unwinds to its checkpoint, and resumes after its statement. Ordinary successful statements do not clear this trapped error; a subsequent trap or existing explicit error-context operation may replace it. Copy errors to locals before another trapped call if they must be retained.

`ERR$(code)` continues to format the existing fixed message; `ERRX$` materializes the captured detail on demand. Those string-returning functions can themselves run out of memory. Code storage and recovery must work even if no diagnostic string can be allocated.

Assignments prepare their result and perform existing type/range conversion before committing the destination. If evaluation/conversion fails, that destination remains unchanged, including array elements. Side effects of called functions, earlier statements and device operations are not rolled back.

`RAISE` accepts defined positive QL/32 error codes except cancellation code 17. Zero, undefined values and nonintegral codes are invalid arguments; wrong types are type errors. It raises through the same trap path. Custom error namespaces/messages are deferred. Named error constants for used stable codes belong in the common include.

### 6.2 VM recovery and bounded cost

Compile protected statements to begin/end markers with a validated continuation address. A trap record contains only checkpoint data: operand stack depth, call depth, owning frame, scoped FOR/SELECT state boundary, dynamic-evaluation boundary, and continuation address. Reserve at most eight records lazily before entering the first trap; failure to allocate the protection storage cannot be caught by a trap that was never installed. Exhausting nesting while an outer trap is active raises code 18 to that outer trap.

On recovery, destroy temporaries and callee locals, discard FOR/SELECT and dynamic-evaluation state created below the checkpoint, cancel only the pending operation being abandoned, and restore the caller's stack/PC. Reset abandoned statement scratch state, including a partially prepared PRINT operation, without undoing bytes already output. Preserve global variables and session resources except incomplete unpublished results. Do not copy all globals, arrays, bytecode or channel maps to implement a trap. Existing map-backed FOR state needs explicit scope/frame ownership for this unwind; a stack-depth snapshot alone is insufficient.

Protection survives scheduler yields, timed input, UI waits, directory jobs, and `STOP`/continue inside a called procedure. A C++ try/catch whose scope ends at a scheduler yield cannot implement this contract alone. Catch typed execution/service errors inside the VM before current run/resume teardown handlers; retain those handlers for untrapped/fatal failures.

Allocation failures are recoverable only at defined operation boundaries with valid VM state. Make stack pushes and destination updates exception-safe, release incomplete operation allocations, and record code 8 without allocating detail. Existing emergency-reserve release is a last-resort fatal path, not a per-trap allocation budget. Unknown C++ exceptions, violated VM invariants, initial program compiler/bytecode verification errors, supervisor cancellation and shutdown bypass traps. Parsing an expression supplied to runtime `EVAL` occurs inside the protected operation, so its syntax error is catchable; this does not permit malformed program source to install a trap. A program must never prevent `KILL` through trapping.

Channels successfully opened earlier in a trapped procedure remain session-owned after a later failure; traps provide no automatic lexical resource cleanup. The caller must close retained handles or rely on session teardown. Document this in examples; future resource scopes are separate language work.

A trap around an asynchronous launch catches launch failure only. A later playback/child-task failure belongs to that service/task and is obtained through its status or wait interface; it must not jump into an expired trap.

### 6.3 Example: explicit sorted-to-streaming fallback

```basic
INCLUDE "ql32.inc"

d% = 0
TRAP d% = OPEN(KDeviceDirectory, "SDC:/music", "*.mod", KDirIncludeFolders + KDirSortNameAscending)
openError% = ERR
IF openError% = 8 OR openError% = 18 THEN
    TRAP d% = OPEN(KDeviceDirectory, "SDC:/music", "*.mod", KDirIncludeFolders + KDirSortNone)
    openError% = ERR
END IF

IF openError% = 0 THEN
    REPeat entries WHILE EOF(d%) = 0
        name$ = READ$(d%)
        PRINT name$
    END REPeat entries
    CLOSE d%
ELSE
    PRINT "Directory open failed: "; openError%
END IF
```

This demonstrates memory fallback only; a browser also needs read-error recovery and selection state. Keep `DIRINFO` queries immediately after each read, and handle navigation by reopening the selected child path. A small visible-page buffer limits BASIC memory, but does not provide arbitrary backwards access to a live stream. Sorted snapshots can be reopened to rescan; random-access directory indexing is deferred.

## 7. Delivery plan and acceptance gates

Implement in this order so later features use the earlier error and continuation foundations. These are requirements for implementation, not tests already run for this document change.

1. **Conditional repeats.** Compiler lowering, named NEXT/EXIT targets, scope/source maps and abbreviation checks. Verify zero/one/multiple passes, outer-loop targets, condition errors, debugger stepping and unchanged unconditional/inline behavior.
2. **Error protection.** Typed runtime error boundaries, compact protection records, checkpoint unwind, exception-safe stores and bounded diagnostics. Verify colon boundaries, inner/outer traps, locals/arrays/FOR/SELECT cleanup, dynamic EVAL, allocation failures at each boundary, untrapped behavior and cancellation. Exercise existing wait paths before adding new ones.
3. **Timed input.** Shared input ownership and serial normalization first; VM continuation and deadline next. Verify `1 + INKEY(100)` and a side-effecting expression run once across many slices, exact key mapping, empty polls, finite/-1 waits, releases/modifiers, queue overflow, lone Escape/arrow sequences, UTF-8, focus transfer, background sessions, terminal restoration and cancellation. Repeat with an active trap and debugger pause.
4. **Directory channels.** Add bounded storage cursors and metadata; implement streaming first, bounded sorted collection next. Add typed OPEN/channel dispatch and resumable OPEN/READ$/EOF. Verify empty directories, last-record metadata after true EOF, idempotent lookahead, failed-string-allocation retry, no-metadata adapters, SINGLE sizes, exact sort ties, multiple sessions, path confinement, SPIFFS capabilities, scan-time mutation, removal after publication, quota limits and cancellation in every job phase.

Run Linux contract tests under constrained budgets before embedded integration. For each target/profile report added flash size, base per-session RAM, input/trap allocation peaks, per-directory memory, largest-free-block change, scan/sort latency and scheduler progress during large directories. Test one above-budget directory with long filenames: it must fail cleanly in sorted mode and remain enumerable in streaming mode without increasing memory with entry count. Test repeated open/read/close and trap recovery to show no retained resource growth.

Profile RAM budgets remain provisional until these measurements are accepted; do not claim board support merely because host tests pass. Keep the preferred CYD profile `esp32_2432s028_spiffs`; do not change storage profiles or flash a board as part of documenting this proposal.

Implementation must synchronize the public constants in `ql32.inc`, `data/ql32.inc`, board copies where applicable, and the language/API/VM specifications. The implementation updates the common include copies and the language/API/VM documents. Board runtime memory and latency measurements remain pending; a build does not establish hardware acceptance.

## 8. Reference rationale and deferred work

- [SuperBASIC INKEY$](https://superbasic-manual.readthedocs.io/en/latest/I/inkey-dlr.html) is a channel input operation with timed waits. QL/32 retains its session/channel discipline, while the agreed millisecond timeout and numeric special keys are QL/32 choices.
- [FatFs f_readdir](https://elm-chan.org/fsw/ff/doc/readdir.html) provides one record per call and separates status from an empty end marker. This supports the cursor architecture; it does not supply sorted results or atomic snapshots.
- [Lua protected calls and error handling](https://www.lua.org/manual/5.4/manual.html#2.3) demonstrate recovery at an explicit call boundary. QL/32 adopts a bounded VM checkpoint design appropriate to its existing frames; it does not import Lua's value model or runtime.
- OPL's one-operation TRAP and iterator-style directory use motivate the public API already agreed in this discussion. No OPL compatibility claim is made; implementation should add primary-reference conformance cases before making one.

Deferred: global error handlers, TRY/CATCH/FINALLY, resource scopes, arbitrary INKEY channel selectors, full modifier/key-up events, Unicode case folding, recursive wildcards, external sorting/flash spill, directory random access and native list widgets. Each would need separate memory and service contracts.
