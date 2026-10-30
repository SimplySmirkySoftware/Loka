# Sudden termination

The Toolbox rail's `loka::toolbox::SuddenTerminate()` is a free function with no
instance or owned state. It calls a fixed participant list in order: currently
only `UnicodeTextSuddenTerminate()`. There is no registry, callback, allocation,
or native call in the dispatcher.

`QuitForOutOfMemory()` is its single production caller, after the alert branch
and immediately before `ExitToShell()`. The promise is a call site immediately
before each exit reached by that function, not an invocation count. A nested
fatal during the alert can reach the door first; re-entry belongs to the
participant contract. The alert may fail and no reserve or drawing environment
is guaranteed at the door.

## Reach paths

All six production paths converge on that caller:

| Path | Origin | State at entry |
|---|---|---|
| P1 | `ToolboxOperatorNew.cpp`: plain `new` / `new[]` allocation refusal | Any stack, including static initialization before `run()` |
| P2 | `ToolboxOperatorNew.cpp`: `std::__throw_bad_alloc` / `__throw_bad_array_new_length` | Direct fatal; array length failure need not mean exhausted memory |
| P3 | `App.cpp` initial window mount or initial document admission refusal → `ToolboxApp::bootstrapWindowRefused` | Startup with a refused scene |
| P4 | `App.cpp` windowless startup → `ToolboxApp::bootstrapWindowless` | Startup without a window |
| P5 | `ToolboxApp::windowAdopted` reserve-spent check | After open/mount, including later document admission |
| P6 | `ToolboxApp::run` loop-tail reserve-spent check | After present, still inside the turn's Operation scope |

The grow-zone callback only releases the reserve; a later P5/P6 check or fatal
allocation exits. This does not exclude P1 inside another native callback.

Cost: `QuitForOutOfMemory` has one call site per reached exit, no normal update
call, and O(P) fixed participant calls (P = 1). No owned or foreign rows are
walked. The participant has one call per dispatch, constant empty work and zero
native calls.

## Participant contract

Every participant must establish every item below or remain a no-op. Adding
initialization, environment repair, event pumping, TSM service, focus, drawing,
or normal teardown is a contract violation, not a way to qualify.

- (a) Non-returning exit path only: `ExitToShell` follows. Do not establish
  consistency for resumed execution, an empty ledger, or paired Session work.
- (b) Accept any initialization stage: static initialization, before `run()`,
  before/during/after Session initialization, after termination, or during
  window teardown. Call native code only if the participant can determine
  whether it started without reading Loka state; otherwise do nothing.
- (c) Never cause a fatal allocation or call `QuitForOutOfMemory`. A nested
  fatal in the alert can enter again; the same rules apply each time. Do not
  create an exactly-once flag. If repeated native calls cannot be shown safe,
  remain a no-op.
- (d) Allocate nothing, including inside native calls: no `LokaAllocRaw`, plain
  `new`, Handle, Ptr, or UPP. A native API needs evidence of no internal
  allocation beyond its interface header: Apple documentation or measurements
  with positive controls. Ignoring failure does not neutralize hangs,
  re-entry, or memory corruption.
- (e) Cause no callbacks, event pump, or TSM service. Do not call `WaitNextEvent`,
  `TXNTSMCheck`, or `ModalDialog`, or read/write Loka State, Scene, Node, ledger,
  or controller state.
- (f) Depend on no drawing environment: port, clip, and update brackets may be
  indeterminate. Do not touch QuickDraw.
- (g) Do not substitute for normal drain, Scene teardown, destructors, or
  quiescent shutdown. OOM does not satisfy that separate contract's premises.
- (h) Ignore failure: return no value and discard OSStatus results.
- (i) Declare in a `/** */` API comment what closes, what remains, and how
  (b)–(j) are met, including the reason when the participant is a no-op.
- (j) Return in finite time: no waiting, polling, or retries. Do not depend on
  suspended native processing resuming on the caller's stack or on another
  participant progressing. This must hold inside native calls too; otherwise
  remain a no-op.

## MLTE in 0.0.8

`UnicodeTextSuddenTerminate()` closes nothing. It leaves live TXNObjects and the
TSM connection to process exit. The interface header does not establish the
termination operation's no-allocation, re-entry, finite-return, live-object, or
pre-initialization behavior; contract (b), (c), (d), (j) cannot be shown.
The unconditional empty body is identical in the TE and Unicode cores and
satisfies all items by doing nothing. This is not MLTE cleanup. It calls neither
the termination operation nor object deletion; deletion safety here is unknown,
not evidence that deletion necessarily allocates or draws.

Promotion requires all three conditions in a separate change:

1. The separate lifecycle probe's OOM observation shows residual system resources
   or damage from exiting without termination.
2. An item-to-source table establishes every contract item (a)–(j), beyond the
   interface header, using Apple documentation or measurements naming the OS,
   MLTE version, measured scope, and positive controls.
3. The owner explicitly approves the decision.

The probe compares OOM with an explicit terminate-then-OOM experiment while two
objects are live. It observes return to Finder, relaunch/init/install/draw, TSM
and other-app input, system heap/free-block changes, and second-launch status.
It cannot establish safety on arbitrary fatal stacks, during pre-init,
re-entry, callbacks, reserve exhaustion, or suspended native processing. Two
successful runs are not proof of the participant contract. Missing any promotion
condition leaves the participant a no-op.

## Deliberately separate exits

- `RequireUnicodeSystem()` refusal occurs before the participant starts and
  does not call the door.
- A future Session initialization failure is also excluded: no successful
  Session exists and partial-initialization cleanup remains unknown. This does
  not assert that nothing was created.
- Contract-violation `std::abort()` paths do not invoke native cleanup on a
  broken contract, even where the runtime eventually reaches `ExitToShell`.
- Normal shutdown uses its own teardown; standalone test-app exits are test
  endpoints. Neither calls this door. External Force Quit and power loss are
  outside the caller's control.

These exclusions do not remove P1's pre-init reachability. Other rails gain no
door until they have both a caller and a participant; `PlatformContext` gains
no common door.

## Evidence boundary

The OOM host fixture replaces only the participant and records `U` immediately
before each reached exit, including the returning fake exit during re-entry.
Its three allocation-attempt counters cover only `NewPtr`, `NewHandle`, and
`NewModalFilterUPP`, including refusals, with individual positive controls.
They do not detect arbitrary host `new`, `malloc`, or the event string's
allocation: this target excludes `ToolboxOperatorNew.cpp`. Actual allocation
absence in the dispatcher and production no-op is established by reviewing the
one-call dispatcher and empty participant and their call dependencies.
