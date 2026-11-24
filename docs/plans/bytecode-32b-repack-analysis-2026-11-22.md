# Analysis: can `Instruction` reach 32 B? (2026-11-22)

Analysis only; no source change. Baseline: `fb57269` on origin/main, `Instruction`
= 40 B on x86-64, about 32 B on ESP32 (32-bit pointers; layout estimate, no 32-bit
toolchain on the host to confirm). History: 256 -> 104 -> 48 -> 40 B host.

## 1. Current byte budget

| Field | Host (LP64) | ESP32 (ILP32) |
| --- | ---: | ---: |
| `name` (`const std::string*`) | 8 | 4 |
| `extras` pointer | 8 | 4 |
| 12 flag bits (bit-fields) | 2 | 2 |
| `line`, `column`, `basicLine` (u16 each) | 6 | 6 |
| `target`, `exitTarget`, operand word (u32 each) | 12 | 12 |
| `rank`, `op`, `separator` (u8 each) | 3 | 3 |
| sum | 39 | 31 |
| padded record | **40** | **32** |

Cost per program (measured): `foot_big.bas` = 7,231 instructions. At 32 B that is
231 KB of the 271,652 B measured on the Olimex; the rest is deque chunks,
extras, text pool and VM state. 1 B/instruction = 7.2 KB on this program.

## 2. Replace `column` by an 8-bit statement/token number?

What `column` is today: the exact 1-based character column of the token start in
its physical source line (lexer `column_`, incremented per character). It is
not a token index.

Consumers: about 10 VM error sites (`line N, column M: ...`), the compiler
`error(Instruction&)` helper, debugger current location, breakpoint
`DebugLocation`, call frames (`callSourceColumn`), the trace output, the ESP32
`remote_debug_protocol.cpp` JSON field `"column"`, QL32-LANG-001 section 2
("diagnostics report ... the source column"), and 10 test expectations
containing `column N`.

Precision lost: a statement/token number is not a column. Diagnostics and the
remote-debug JSON would change from a character column to an ordinal
(e.g. "token 7"). That is a visible behavior and spec change.
Overflow: 8 bits holds 255. Source lines in the corpus reach 94 characters, but
a long DATA line or a `:` chain can exceed 255 tokens or columns; the options are
saturating (wrong position, silent) or throwing at compile time (rejects
programs that compile today). Neither is behavior-identical.

Verdict: do not replace column by a token number. If a byte must be saved, an
exact column saturated at 255 changes only lines longer than 255 characters,
but still needs owner approval, a spec sentence (QL/32-specific), and a test.

## 3. Other repack candidates

| Candidate | Saves | Behavior change | Notes |
| --- | ---: | --- | --- |
| Pack `separator` (5 values, 3 bits) into the flag half-word | 1 B | none | 12 + 3 = 15 bits |
| `rank` limit to 4 bits and into the flag word | 1 B | limit change (rank > 15 rejected) | ranks are small in practice |
| `basicLine` to 15 bits (language caps at 32767) | 0 alone | none | enables sharing a word with 1 bit |
| `exitTarget` to extras (ForInit/ForNext only) | 4 B | none | ForNext gains an extras allocation (few per program); Push literal moves to target+operand |
| `op` stays u8 (already) | 0 | none | 65 opcodes |
| `name` pointer -> u32 pool id | 4 B host, 0 ESP32 | none | pointers are already 4 B on ESP32; every `instruction.name` site needs pool context: large rewrite for a host-only gain |
| `extras` pointer -> u32 id | 4 B host, 0 ESP32 | none | same |
| `line` below 16 bits | - | spec | physical lines up to 65535 are accepted today; not recommended |
| Line/column side table | 0 | none | dense per-pc entries cost the same 6 B (see results doc) |

## 4. Concrete layouts

**Host 32 B is not reachable without id indices.** Even with every change above,
two 8 B pointers + 17 B of small fields + 8 B of words is 33 -> 40 B. Reaching
32 B needs `name` and `extras` as u32 ids (-8 B), which is the large rewrite
with no ESP32 benefit. Not recommended; the host is not the memory target.

**ESP32 28 B (recommended), all Xtensa-aligned (u32 on 4 B boundaries):**

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | `name` pointer |
| 4 | 4 | `extras` pointer |
| 8 | 2 | flags (12) + separator (3) |
| 10 | 2 | `line` |
| 12 | 2 | `basicLine` |
| 14 | 1 | `op` |
| 15 | 1 | `rank` |
| 16 | 4 | `target` |
| 20 | 4 | operand word (loopId / argCount+optionCount / lineTarget / Push literal high) |
| 24 | 4 | `column` (u16, 2 spare bytes) -- or u8 only with the saturation change |

Exact `column` stays u16 here, so the record is 28 B with **no behavior change**:
that needs `exitTarget` moved to extras, `separator` packed, and `Push` literal
stored in target+operand. 24 B would additionally need column at 8 bits plus
rank at 3-4 bits and a 15-bit `basicLine`, 65 bits of small fields in 64 bits: not
achievable without dropping a field. **24 B is not reachable** without removing
`line` or `column`.

## 5. Expected saving

| Step | ESP32 record | Saving on `foot_big` (7,231 instr.) | vs 271,652 B |
| --- | ---: | ---: | ---: |
| now | 32 B | - | - |
| 28 B layout (behavior-identical) | 28 B | about 29 KB | -10.6% |
| 24 B (needs column/rank limit changes) | 24 B | about 58 KB | -21%, not reachable as above |

CYD uses the same record size, so the saving is 4 B per instruction there too: a
1,000-instruction program saves about 4 KB of its heap. The full 90% target
(256 -> 16 B) stays out of reach; 28 B is 82% below the original ESP32 160 B record.

## 6. Approval, risks, steps, gates

Owner approval needed only if column is narrowed or the rank limit changes
(QL/32-specific; diagnostics text and `"column"` in the debug JSON are visible).
The 28 B layout needs no approval: behavior stays identical.

Risks: ForNext extras allocation timing (needs `forStates` unchanged), `Push`
literal storage now overlapping operand (assert only Push touches it), ESP32
alignment of the u32 words (keep them at 4 B offsets; verify with a
`static_assert(offsetof(...) % 4 == 0)`), and the 32-bit `sizeof` cannot be
compiler-verified on this host (only the PlatformIO build and the hardware
heap numbers confirm it).

Steps: (1) pack separator into flags; (2) move `exitTarget` into extras and
store the Push literal in target+operand; (3) reorder to the table above and add
`static_assert`s for size and word offsets; (4) update the size budget test (host stays 40 B; assert the ESP32 offsets) and the results doc.
Gates (same as earlier stages): CTest, golden corpus, debugger/scheduler/error
line+column tests, `benchmarks/*.bas` bounded regression, four PlatformIO builds,
Olimex heap + golden + MODE check labelled "verified on hardware (Olimex)".

Recommendation: do step 1-3 (28 B, about -29 KB on the 1,200-statement program, no
behavior change). Do not rename column to a token number; do not chase 32 B on
the host or 24 B on ESP32.
