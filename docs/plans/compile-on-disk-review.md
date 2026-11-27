# Review: compiling long sources and INCLUDE, and an on-disk compiled artifact

Status: review and design only; nothing here is implemented. Date: 2026-11-27, `main`
at 36159bc plus #49/#50. Companion to `docs/plans/compact-bytecode-plan.md`.

Question: can QL/32 compile from disk, free the source, and keep only bytecode in RAM?
Short answer: yes in two independent steps. Step A (cheap, no new format) removes the
duplicate source copies that exist today. Step B (an on-disk artifact `.q32b`, cached by
source hash) removes the compile peak and, combined with the compact bytecode, most of
the retained size. Keeping the editable program in RAM (`LIST`/`EDIT`) is a separate,
larger change that this review does not recommend yet.

## 1. Current flow (code audit)

| Step | Where | What happens to the source |
| --- | --- | --- |
| `LOAD "p"` | `src/language/interpreter.cpp` (LOAD branch, ~4339) | `storage_->readText(path)` reads the **whole file into one `std::string`**; lines are canonicalised into a candidate `ProgramStore` (one text buffer + 12 B index per line), then `contents` is freed and `program_.swap(candidate)`. Peak = file + candidate = **about 2x file size**, with a contiguous block of file size. Unnumbered files get automatic numbers 10, 20, ... (max 3276 lines). |
| Edit (`10 PRINT`, `EDIT`, `DELETE`) | `validateProgramLine` | Per-line lexical/statement check only; no whole-program compile. Source lives only in `program_` (RAM). |
| `SAVE` | LOAD/SAVE branch | Streams `program_` to the file (`writeTextStreamAtomic`). **RUN does not save first**; the file on disk can be stale or absent. |
| `RUN` | `Interpreter` RUN branch (~4584) | `joinProgramLines(program_)` builds a **second full copy** of the source (line numbers re-added, `reserve(listingBytes)`). In the interactive scheduler this string is moved into the task (`takePendingRunSource`) and **stays alive for the whole run** (`Task::releaseSource()` runs only when the program ends). The VM does not read it after compile (to be confirmed in Phase B0). |
| Compile | `detail::compileSource` (`src/language/interpreter.cpp` ~3869) | Pass 1: `Lexer::countTokens` streams tokens through a counter (finds `DEFine FuNction` names and the count of bytecode-producing tokens). Admission check: `operations * 60 B` plus a 2 KiB block against `MemoryService`. Pass 2: `Compiler(source, declared)` streams the lexer line by line (`tokens_(source)`, a window; tokens are `string_view`s into the source, no per-token heap text). Resolution passes (source jumps, DATA/RESTORE, calls) run over the compiled instructions, then compile-only metadata is dropped (P12). |
| `INCLUDE` | `mayContainInclude` (`src/vm/interpreter.cpp` ~56, 619); `expandIncludesRecursive` | A textual pre-pass: if the word `INCLUDE` appears anywhere, the whole program is **expanded into a new string** (`expanded`), each included file read with `readText`, up to 16 nesting levels, a cycle check, `INCLUDE` allowed only in the header, expanded size capped at **64 KiB** ("expanded source exceeds 64 KiB"). Positions in diagnostics are lines of the expanded text. The expanded string lives only during compile (local to `executeSlice`'s `if`). |
| After compile | `verifyBytecode`, VM | Code lives in `std::deque<Instruction>` plus text pool, extras, functions, arrays, constants. |

### 1.1 Memory per stage (source of S bytes, N instructions, ESP32)

| Stage | Source copies alive | Notes |
| --- | --- | --- |
| Idle with program | `program_`: ~S + 12 B/line | the only intended copy |
| LOAD (peak) | file string S + candidate ~S | needs a contiguous S block |
| RUN, before compile | `program_` + joined copy (+ expanded copy with INCLUDE, up to 64 KiB, + each include file transiently) | up to 3S |
| Compile (peak) | same as above + growing code (~58 B/instruction retained, more transiently) | admission refuses when `60 B x operations` exceeds free heap |
| Running | `program_` + task copy (2S) + code | the task copy is dead weight |

dirplayer: S = 7.9 KB, 1162 instructions; the dead copy costs 8 KB, small next to the
~67 KB code. At 3000 lines (S about 100 KB at ~35 B/line) the copies dominate.

### 1.2 Limits and failure modes for 3000+ line programs and nested includes (CYD)

- **LOAD** already fails near S = 60-70 KB (about 1800-2000 lines): needs file + candidate
  (~2S) and the file string is one contiguous block (largest block at idle ~110 KB).
- **RUN** needs the joined copy as one contiguous S block next to `program_`; at S = 100 KB
  this alone exceeds the free heap.
- **Admission** (`60 B x operations`) puts the practical program size at roughly
  1500-1700 instructions at idle (142 KB free), independent of the source size; 3000
  lines is typically 3000-4500 instructions = 180-270 KB, refused with
  "...; shorten the program" (a graceful failure, not an ALLOCFAIL).
- **INCLUDE**: expanded source capped at 64 KiB regardless of heap; nesting depth 16 with
  a recursion that holds each file's `contents` and the partial `expanded` string at
  once (depth x file size on the stack of strings, plus the result). A diamond (the same
  include twice) is allowed but duplicates the text; modules cannot be shared.
- Error positions for included code refer to expanded line numbers, so a message
  does not name the include file.

## 2. On-disk compiled artifact (`.q32b`)

### 2.1 Idea

On `RUN`, compute a key from the program text (and the included files), look for a
matching artifact in a hidden cache directory (for example `FFS:/.q32/<hash>.q32b`, or
`SDC:` when present). **Hit:** read the artifact into the code store, skip the lexer and
compiler entirely. **Miss:** compile as today (streaming), then write the artifact
atomically for the next run. Only the code store (plus tables) is resident while the
program runs; no source copy is needed by the run.

Compiling *from disk* (a lexer reading lines straight from the file through
`StorageService::openRead`, `readAt` with a 512 B window) is an optional extension for a
`RUN "path"` style entry (a language change, see 5); the cache design does not need it.

### 2.2 File format (sketch)

- Header: magic `Q32B`, format version, **build id** (compiler/opcode-set version
  and the built-in constant table, e.g. the KKey constants of #48), source key (64-bit
  FNV-1a over the canonical source text plus its length), CRC32 of the body, section
  directory.
- Include manifest: for each included file, normalised path, size, content hash
  (hashed while streaming, 512 B buffer, no heap growth).
- Sections (the compact-bytecode layout): code segments, jump index table, position
  table (delta line/column), text pool, side tables (names, `forItems`, parameters,
  locals, select items), function metadata, arrays, constants.
- Loading: allocate the 1 KB segments one by one and fill them with `readAt`; no block
  larger than a segment is needed. Fully resident in v1. **Lazy segment paging** (fetch
  segments on demand from flash) is possible with the segmented store but costs
  SPIFFS read latency in the interpreter loop (about milliseconds per fault on a 240 MHz
  core) and a cache policy; defer until Phase 3 of the compact plan is measured.
- Integrity: CRC check and the existing `verifyBytecode` run on every load; a failed
  check discards the file and recompiles.

### 2.3 Key and invalidation

An artifact is valid only if all of these match, else it is rebuilt:
1. Source key of the program text (after `canonicalizeKeywords`, with line numbers).
2. Include manifest: every included file's path, size and content hash (re-hashed on
   each RUN, a streaming read; an included file edited or deleted invalidates).
3. Build id (firmware compiler version, opcode set, built-in constants).
4. Compile-time options that change code (none today; keep a field).

`NEW`, `LOAD`, `EDIT`, `DELETE` need no explicit invalidation because the key changes
with the text. Old artifacts are evicted by count (for example the 8 newest) and by free
space; hidden from `DIR`. Writes happen only on a miss, so flash wear is bounded by
distinct program texts.

### 2.4 Behaviour that must be preserved

- **Errors and positions:** the position table carries `line`, `column`, `basicLine` per
  statement, so VM and debugger diagnostics are identical without the source. Positions
  for included code remain expanded-source lines for compatibility; an optional later
  improvement is `file:line` through the include manifest.
- **EVAL (immediate mode):** `compileExpression` needs the function table, text pool,
  arrays and constants of the running program; the artifact loads them into the same
  structures. EVAL-appended code is a RAM overlay after the artifact's last segment,
  erased on completion, never written back.
- **TRAP / rollback:** instruction indices are unchanged, so rollback is unaffected.
- **Debugger/breakpoints:** use `statementStart`/`basicLine` from the position table.
- **INCLUDE changes:** handled by the manifest (2.3). The 64 KiB expanded limit applies
  to compilation (misses) only; hits do not expand sources. A miss on a very large include
  tree can later compile include by include (an `INCLUDE` directive is already
  header-only, so a per-file streaming compile is feasible).
- **`LIST`/`EDIT` need the source:** `program_` stays resident in RAM in this plan. The
  artifact does not make the *editable* program disappear; it removes the *run-time
  copies* and the compile peak.

## 3. Staged proposal and expected heap effect

### Step A: remove the duplicate source copies (no format change)

1. Compile straight from `program_` through a streaming line source (the lexer takes a
   "next line" callback instead of one contiguous `string_view`; `ProgramStore` already
   iterates lines). Removes `joinProgramLines` (S bytes, contiguous) from the compile
   peak and from the run, and the task copy.
2. Stream `LOAD` through `openRead` with a line buffer (no `readText`, no 2S peak).
3. Stream `INCLUDE` expansion into the lexer (a line source that walks the include tree)
   and raise the 64 KiB cap to the program-size admission instead.

Saving: dirplayer ~8 KB during RUN and 8 KB at compile peak; for a 100 KB program
about 100-200 KB of peak and 100 KB while running. Risk low-medium (lexer source
abstraction; every test already compiles from text). No on-device format risk.

### Step B: `.q32b` cache hit path (needs the compact bytecode, Phases 1-3 of the plan)

- Hit: no lexer, tokens, window, resolution passes; peak = resident code + 1 KB read
  buffer. The admission estimate switches from `60 B x operations` to the artifact's
  recorded resident size. With ~18-22 B per instruction retained, a 1500-instruction
  program needs about 30 KB, and 3000+ instruction programs fit (about 60-70 KB).
- Miss: still pays the compile peak once (first run after an edit), then the cache.
- Retained effect for dirplayer: the compact plan's 35-45 KB saving applies to the
  resident code; this step adds the elimination of the compile peak, expected 20-40 KB
  less transient heap (to be measured in Phase 0; the host harness shows the instruction
  store and pool are the bulk, not the token window).

### Step C (optional, later): run from disk and keep no editable source in RAM

`RUN "path"`/`CHAIN`, or a paged `ProgramStore` whose text lives in a file with a RAM
line index and a small window. Language/shell-visible, high effort; revisit after B.

## 4. Risks

| Risk | Mitigation |
| --- | --- |
| Stale/corrupt artifact | key + manifest + CRC + `verifyBytecode`; any mismatch recompiles; never trust the file without these checks |
| Flash wear and space | write only on miss, atomic temp+rename, keep the newest N artifacts, cap total bytes, skip caching when free space is low |
| Cache hit differs from compile (semantic drift) | differential test: run each example and test program via fresh compile and via artifact load, compare output and error positions; run in CI on the host |
| Include edited between hashing and load | hash during the same read that fills the manifest; a mismatch on the next RUN rebuilds |
| Cooperative tasks | artifact is immutable and shared read-only; EVAL overlay per session |
| Build-id mistakes (opcode/constant change not bumping the id) | derive the id from a table-driven hash (opcode list, constant table) checked by a unit test |
| SD removal/no storage | cache is optional; on any I/O error compile in RAM as today |
| 16-bit limits | artifact encodes the same limits and raises the same `program exceeds a bytecode field limit` error at compile time; format version bump if widened |
| Lazy paging latency | not in v1; measure before adopting |

## 5. Language-visible changes (only if the optional items are taken)

None for A and B. C adds a way to run a program without loading it for editing
(`RUN "path"`); INCLUDE diagnostics naming files would change message text and need
a spec note (QL/32-specific).

## 6. Recommendation and phase order

1. **A1 (first, small):** drop `joinProgramLines` and the task source copy by compiling
   from `ProgramStore` lines (streaming line source). Gate: host + sanitized ctest, 3
   pio builds, golden 16/16, CYD dirplayer RUN start FREE >= +8 KB versus the current
   96.7K, no change in output or error positions.
2. **A2:** stream `LOAD` (no `readText`); gate: LOAD of a 100 KB file succeeds on the
   CYD and leaves idle free memory unchanged.
3. **A3:** stream `INCLUDE`; raise the 64 KiB cap to the admission check; add nested
   include and diamond tests.
4. **Compact bytecode Phases 0-3** (separate plan; the artifact format reuses its
   segment, table and position layouts, so build the file format on top of Phase 1's
   encoder).
5. **B1:** `.q32b` write on miss, read on hit, key, manifest, CRC, build id,
   differential test; gate on hit-versus-compile equality and the heap numbers.
6. **B2:** raise the admission for artifact hits and measure the largest program that runs
   on the CYD (target: 3000 instructions).
7. Re-evaluate lazy segments and Step C only if B leaves a measured gap.

Step A is worth doing now: it is independent of the bytecode work and gives a concrete
heap and program-size gain at low risk. Step B should follow the compact encoding rather
than precede it, because a cache of the current 32 B/40 B records would be about as
large as the resident code and would only save compile time.
