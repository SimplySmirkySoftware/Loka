# Review history

> **Status:** Historical
>
> **Owns:** Worked review incidents and relocated mechanism explanations
>
> **Does not own:** Repository rules, current API contracts, or renewed runtime evidence

Source: AGENTS.md at `3d1d54815de884fb29df99c9aa213346b7fe0318`,
relocated under the offline reviewer addendum for #729. Wording below is
preserved from that source; mixed rule/incident sentences remain in AGENTS.md
and are repeated here where splitting them would change rule wording.

## Shape Review Gates — worked incidents

### 529 — Gate 1 check 6: tracker depth

Derivable means do not add it: a tracker's re-entrancy depth was already its phase (#529).

### 527 — Gate 1 check 6: BranchArm and sibling-tag collision set

Two primitives carrying one meaning — a `bool` beside an index, a hand-rolled optional — become one small value type with an explicit none (`BranchArm`, #527). A mid-procedure result kept for a later reader travels as a return value, not a member (the sibling-tag collision set in #527).

### 525 — Gate 1 check 6: State and echo flag

An owned `State` with a single write door, tracker registration, and explicit readers (`Window::nativeFrame()`, #525) is a box, not a flag, and is exempt. A bit whose meaning depends on event order — an echo-suppression flag armed before a native call and cleared by whichever observer fires first — is the worst case: the first draft of #525 failed three different ways under observer count, notification timing, and re-entry.

### DSL unification topic 5

Ruling of 2026-10-17 (DSL unification page 3, REFUTE held with corrections). Making `DocumentWindows` a For-shaped node seat, or sharing a declaration part between the Boundary seats and the window seat, was refused on Gate 1 check 1 read by deletion: neither removes code, because the window seat builds no candidate and has no declaration part in common with the Boundary seats, and its commit means "walked this revision" while a Boundary commit means "published this candidate or vacated the seat". What the consumers share is the `KeySnapshot` type and the needs rule (k1, #1190), recorded in `docs/KeyedSeatDesign.md`; the one-window rail's conflict with increase-before-decrease is tracked in #1399.

### 610 — Gate 2 check 1: foreign ledger walk

A door that walks rows owned by someone else — every ledger in a controller to find one context's entries, every hit to answer whether a State still has a user — is returned for reshaping, not noted as a medium smell: the rows belong with their owner, and a walk over foreign rows is the same misplaced-invariant shape as a foreign reader of a flag (#610's `refreshContextProps` walked seven ledgers on every retained props apply and was accepted from a smell list that had named it).

## Win32 wide API

### 160 and 15 — ANSI payload loss

The `A` variants encode in the process ANSI code page, so a full-width payload is destroyed at the boundary and the loss stays invisible until a much later open or draw fails. This has been the mechanism of two separate bugs: the ANSI `EDIT` control (#160) and the ANSI open dialog (#15).

### Window class mechanism

`RegisterClassA`/`CreateWindowExA` produce an ANSI window, so a later `WM_SETTEXT`/`GetWindowText` on it silently goes through the code page even though the call site looks wide. `testWin32CustomWindowClassesUseWideApiFamily` pins this contract for the repository's custom Cell, ImageView, and RectSurface classes.

### Compile-time wide API contract

An explicit `...A` is the easy case. Repository Win32 targets receive `UNICODE` and `_UNICODE` through `LokaWin32WideApiContract`, and `PlatformContext.hpp` refuses a Win32 compile without both, so unsuffixed aliases resolve to their `W` variants and an alias handed a `char*` stops compiling.

### Existing ANSI classes

An explicitly ANSI-registered class converts the payload through the code page however wide the new call site looks, and the `A` calls responsible may be untouched by that diff, so scanning added lines alone reports clean.

## Classic redraw heuristics (pre paint-ledger)

The #518 ruling of 2026-09-13 (context-owned damage, counted fallback) supersedes
the dirty-rect guidance below; it is preserved verbatim as history. The 68k
hot-path, triage, quantized-gating and previous-state-caching rules stay
normative in `AGENTS.md`.

- Redraw/performance triage: first identify whether cost comes from scene/update routing, boundary-local apply, or platform-specific fallback invalidation. Prefer measuring real redraw triggers before attempting dirty-rect shrinking.

- Classic/68k redraw policy: when broad repaint remains, prioritize suppressing redundant follow-up redraw triggers before fine-grained dirty-rect tuning.

- Classic/68k optimization order: first remove redundant state updates and compose passes (`forceUpdate`, unused state writes, extra Boundaries), then reduce redraw area, and only then add platform-specific dirty-region tricks.

- For moving-rect redraw on Classic, `erase old minus new` is a safer first optimization than `paint new minus old`; only add more aggressive paint diffing after measurement proves it helps.

## Framework gaps — worked example

Relocated from AGENTS.md at `975c2eea` by the AGENTS.md refresh, which keeps
issue numbers out of rule text.

### 873 — delivery sites listed per rail

The #873 delivery-site enumeration (three rails hand-listing their completion tails, then a rail-wide ruling from a bot review) is the worked example of a framework gap worked around per rail instead of named and designed once.

## Debugging — 398

### Silent instrument

On #398, five debug runs "proved" a crash bypassed abort, exit, and every exception vector; the breakpoints were sitting at addresses the runtime segment was never loaded at, and one byte-compare exposed it. The next run captured the real backtrace.

### 1107 — out-of-memory alert

Relocated from AGENTS.md at `975c2eea` by the AGENTS.md refresh. Since #1107 (PR #1186, `3887805b`, 2026-10-10), the Classic plain-new/`std::vector` failure path (`QuitForOutOfMemory` in `apple/toolbox/src/ToolboxOutOfMemory.cpp`) shows the out-of-memory Stop alert before `ExitToShell()` whenever it can, so an allocation failure is no longer a silent return to the Finder.

### SIZE partition

Suspect allocation failure first, and use the SIZE partition as the discriminator before blaming a machine class: shrinking it reproduces the failure deterministically on any 68k machine, growing it makes the failure vanish (#398: 512K red on the Quadra, 768K green; 448K red even on the IIx, one swap earlier). Small allocations are served from a process-lifetime pool under the same global `operator new`; pooling preserves that refusal/abort policy rather than adding one, so still suspect allocation first — the pool's refill-failure path is chunk, then single request, then a nothrow null or plain-new abort.

## Test builds — relocated dates

Relocated from AGENTS.md at `f287b75b` by the AGENTS.md refresh, which keeps
dates and issue numbers out of rule text.

### 117 and 118 — common sources under TEST_BUILD

Since #117 (macOS, `c29ba017`) and #118 (Win32, `bd247f1c`), both 2026-07-18, `LokaTestsMacOS` and `LokaTestsWin32` compile the common sources under `TEST_BUILD` into the test executable instead of linking the production cores, so `TEST_BUILD`-only hooks in `tests/TestingHooks.cpp` link on every platform while `LokaWin32Core` and `LokaMacOSCore` carry none. Linux reaches the same arrangement through the configure-time `TEST_BUILD` configuration.

### 172 — compile pins outside Linux

`tests/compile` is configured only by a configure-time `TEST_BUILD`. On 2026-10-15 that meant the Linux `testing*` presets: the macOS and Win32 CI presets define `TEST_BUILD` per target, not at configure, so they do not build the pins. Building the accepting twins on the macOS/Win32 toolchains was not wired when #172 closed on 2026-10-11.
