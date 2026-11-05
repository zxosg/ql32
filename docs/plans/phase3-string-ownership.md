# Phase 3 string ownership and lifetime rules

Status: reviewed before any B2 representation changes. The current runtime
uses owning `std::string` values. No raw string pointers or views are retained
by VM state.

## Owners and lifetime

| Location | Owner | Lifetime |
|---|---|---|
| Session globals | Global slot value vector | Until the value is overwritten or the interpreter is cleared/destroyed |
| Function locals and parameters | Active `RuntimeFrame::locals` vector | Until the frame returns or execution unwinds |
| Operand stack | The individual `StackEntry` | Until popped or the execution state is destroyed |
| String arrays | `RuntimeArray` string vector | Until execution state teardown; array element assignment replaces the owned string |
| Source literals and constants | Compiled instruction/value and constant tables | Until the current compiled program is discarded; dynamically compiled EVAL instructions live until the dynamic frame returns |
| Built-in arguments and results | The argument/result vector or local `Value` | Until moved into a destination, returned to the operand stack, or the call ends |
| Debugger snapshot and EVAL values | The returned snapshot/value object | Owned by the debugger caller after return |
| Platform/runtime services | The receiving service when it copies or moves an argument into retained state | As specified by that service; the VM does not lend a temporary buffer beyond the call |

## Copy, move, replacement, and unwind

- Copying a `Value`, stack string, array element, or literal creates an
  independent `std::string` owner. Moving transfers the string buffer and
  leaves the source valid for destruction or reassignment.
- Assignment overwrites through normal `std::string` assignment. The old
  allocation is released by the string implementation when no longer needed.
- Function arguments are held in the call's argument vector. Scalar/string
  parameters receive an owning value in the frame. Returning a string moves
  the result to the caller stack where practical; any remaining copies are
  independent owners.
- Returning from a function destroys its local vector. Returning from dynamic
  EVAL also removes the appended dynamic instructions and their literal values.
- A BASIC error or `bad_alloc` destroys the active execution state, including
  its operand stack, call frames, and arrays. Session global values remain
  valid. `Interpreter::clear()` destroys global values and symbol metadata.
- BASIC currently rejects DIM of an already dimensioned array, so there is no
  user-visible array replacement path. A new array is fully built before it is
  inserted into the per-execution array table.
- No code stores a `std::string_view`, pointer, or reference to a string beyond
  the operation that obtained it. Any such view is valid only while its source
  owner is unchanged and alive.

## Service boundaries

Calls pass strings through the declared service API. A synchronous service may
read a `const std::string&` only during the call; a service that retains text
must copy or move it into service-owned state. Queued UI requests, channel
parameters, file paths, network requests, graphics text, and debugger output
must not retain references to VM stack or frame strings. The existing service
implementations and fakes follow value ownership; B2 tests must cover any
service boundary touched by a handle representation.

## Requirements for a future handle pool

If B2 adopts `StrId`, each owner listed above becomes one explicit handle
reference. Copies retain the ID before the source can be released; moves
transfer it and invalidate or clear the source. Assignment retains the new ID
before releasing the old one. Frame exit, stack pop, array teardown, program
discard, debugger snapshot creation, and error unwind release their owned IDs.
Constants and session globals must not point into a pool with a shorter
lifetime. Service calls materialize an owning `std::string` at the boundary
unless the service itself accepts and retains a stable pool handle.

The current RAII implementation is the reference behavior. A pool is adopted
only if measured workloads show a useful memory reduction after handle tables,
pool entries, free-list metadata, and retained string bytes are counted.
