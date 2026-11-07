# Web Rail

> **Status:** Normative; frozen Web rally pages 1 and 2, with the delegator's applied-root-visibility clarification.
>
> **Owns:** DOM resource ownership, browser entries, completion and shutdown.
>
> **Does not own:** Text geometry beyond the PR 1 fixed metrics, native typography, browser scheduling guarantees, or common lifecycle semantics.
>
> **Code truth:** [`web/src`](../web/src), [`web/js`](../web/js).
>
> **Verification:** [`tests/web`](../tests/web), including the host fake boundary, instruction golden, mutations and Chromium CDP driver. A browser build is not browser runtime verification.

## Owners and identities

A module instance owns one `WebSession`. It remains alive for that module's lifetime, including after disposal. The session constructs PlatformContext, Config and App in that order. Bootstrap is Web-local; common `RunApp` remains the blocking-rail bootstrap. The session owns its token allocator, live route index, runtime Regime, execution/lifecycle phases, and idle scheduler. No mutable current-module variable redirects old callbacks.

A WebWindow owns a WebRoot and its scene controller. Each Text/Button context owns one WebElement token. The session's route store reserves a stable resource record before native creation. Its intrusive binary digit index needs at most 33 record probes; extraction needs at most two such paths and allocates nothing. There is no scene/window scan for input. A click additionally checks the owning root's admission and visibility through its token. Whole-input cost includes the existing App-owned pending-close query: O(pending closures), twice per click and once per resize. The rail delegates that query rather than walking App rows or caching a second close flag. The route-probe fixture bounds routing only.

Tokens are unsigned 32-bit, zero means none, and issued identities are never reused. Exhaustion is an always-on refusal and remains exhausted until a new module. This is a preview limitation: 100 new leaves at 60 generations per second exhaust the space in approximately eight days. JS normalizes tokens with `>>> 0`. DOM resources are exclusively owned by the adapter: keeping or reinserting a released element does not restore its route.

## Resource transitions

Creation reserves record/index storage, creates a hidden DOM element, and commits ownership before context adoption. Native construction failure rolls back the same record and temporary element. A missing context can be attempted on a later legitimate ensure visit; materialization failure itself schedules no retry. Post-commit adoption refusal delivers terminal lifecycle and transfers the record to retirement instead of construction rollback. Adoption precedes initial lifecycle visibility, which precedes `afterAttach`.

Park is reversible. Show/Match normally retain context, token, element and listener; activity checks refuse input while detached. Keyed replacement and destroy-on-detach instead retire the old generation.

Terminal revoke extracts the route and clears receiver pointers synchronously, hides the element, removes listeners/observers, and transfers the record into the controller's single intrusive retirement list. Logical nodes own the live-state subscriptions; these contexts add none. The context token becomes none before context deletion. A retired record holds no live C++ receiver pointer.

Release happens at `Window::flushSceneInvalidation`'s controller drain, or controller destruction. It removes the element and Map entry exactly once, with no application callbacks or State writes. Release reports consumed, absent, or zero-token/no-ownership. Only test history distinguishes a double release from another absent token; production keeps no release history.

Root creation precedes mount. The adapter establishes actual content-box dimensions while the root is noninteractive, before hiding it with display:none; requested dimensions do not override the measured result. Resize reports the same content-box source. Root park is driven by App visibility admission and retains the native root. WebRoot owns `AppliedVisibility`; its apply door updates it only after successful native application. Failed application preserves the old fact. WebWindow compares intent with that fact, the counterpart of Win32 HWND presence and Null controller presence. This poll makes no JS call.

Window teardown unmounts the Scene and drains children before extracting the root route, disconnecting its observer, and releasing it. A close-queued root is refused before borrowing its controller.

## Entries and completion

Execution is IDLE, BOOTSTRAP, DISPATCHING or COMPLETING. Lifecycle is STARTING, RUNNING, QUITTING or DISPOSED. These answer different questions. `Operation::hasActive()` cannot identify export execution: close clears it before reclaim finishes.

Ingress decides before lookup, borrow or Operation construction. IDLE/RUNNING accepts an outer entry; DISPATCHING joins a nested Operation and dispatches without a tail, sample or schedule; BOOTSTRAP, COMPLETING and closed lifecycle refuse in production too. Native input goes through `InputInvocation`, restoring the previous controller borrow without pumping.

`WebEntry::run` is a stack procedure with one lexical Operation around dispatch and completion. Its Web-local tail deliberately mirrors `Win32App::flushIterationTail` and `AppTestAccess::completeTurn`:

1. Settle.
2. Admit/apply windows.
3. Reconcile completion facts.
4. Serve URLs with entry kind captured before the turn.
5. Admit/apply again, unconditionally.
6. Close the turn.
7. Reclaim windows.

Each continuation has a fresh lexical turn. K = 4 counts additional tails after the initial tail. Win32 can continue indefinitely because it drains messages on the next iteration; a browser callback cannot. Remaining work therefore schedules an immediate task. Browser input **may** interleave, but task-source ordering is not promised. Both input-before-continuation and continuation-before-input are host fixtures.

## Timing and shutdown

WebIdle alone owns policy identity, last sample, interval deadline, accumulated frame elapsed, and independent frame/task slots. Every outer entry advances the sample timestamp, even under NONE. INTERVAL uses App's accumulation and advances its deadline after dispatch. EVERY_TICK accumulates intervening event elapsed and dispatches only at frame provenance. Policy changes reset timing and reconcile both slots. Valid callbacks consume their handles before entering; replaced, cancelled and already-consumed callbacks do nothing.

Scheduling runs once after an outer batch. EVERY_TICK independently arms rAF. Pending admission selects immediate; otherwise INTERVAL selects a timeout; otherwise the task slot is empty. QUITTING cancels normal registrations and selects shutdown. Immediate currently uses `setTimeout(0)`. Its nested-timer clamp can add delay; MessageChannel is a permitted future adapter implementation of the same task door, not a stronger input-order guarantee.

Bootstrap runs App without a Regime. A windowless bootstrap may move STARTING directly to QUITTING; it creates no Regime and schedules shutdown directly. Ordinary runtime declares the Regime after bootstrap and starts with an empty completion entry.

User-facing quit is an ingress-checked request that the roster may decline, leaving the module running; App calls the terminal rail quit door only after acceptance. Dispose is a forced request that bypasses the roster. Only an internal shutdown task, at IDLE, releases the Regime and destroys App, Config and PlatformContext in reverse order. Adapter services remain callable through all teardown and final resource drain. The JS resource ledger must then be empty before `completed` resolves. Repeated disposal is inert. This task boundary keeps bootstrap, input borrows, Operations and producers off the destruction stack.

## PR 1 projection and limits

Text is one line; geometry uses the existing INaturalSizeSource seam with fixed metrics (eight CSS pixels per scalar, sixteen-pixel Text height, twenty-four-pixel Button height and sixteen-pixel horizontal inset). It does not claim wrapping or accurate browser font measurement. The preview style descriptor carries all six TextStyle axes, preserving absence and signed point values; actual size resolution remains fixed until page 3 replaces measurement and rendering together. Style constants come from `tools/ci/gen_style_vocab.py`.

Buttons are native `<button type="button">` elements. Mouse, Space and Enter use the same click export. Browser focus is read at completion; no keyboard or focus-event export is added. Layout containers have no DOM elements.

EditText, TextEditor, PopupMenu, ImageView, RectSurface, ScrollBar, Cell and CardBand are explicit refused handlers. A ScrollView visit settles refusal without publishing a viewport or projecting children. Menus use the default no-op, dialogs the existing Window refusal seam. Title is unsupported: document.title is not a per-Window projection. Examples, IME, pointer input, scrolling, skins and hot reload are outside this PR.

## Reproduction

Build `LokaWebTests` with the existing testing and testing-asan presets and run `ctest --test-dir <build> -R '^webRail$' --output-on-failure`. The fake boundary implements the same imports as the real adapter. `LOKA_WEB_CAPTURE_GOLDEN=1` prints the initial instruction sequence; ordinary runs compare the committed golden.

After sourcing emsdk, use `emcmake cmake --preset testing-web`, then build `build/wasm/testing`. The node fixture alone links NODERAWFS. The browser fixture links production Web/common libraries without TEST_BUILD and keeps EXIT_RUNTIME disabled. Run `node tests/web/driver/run.mjs build/wasm/testing/web`; absence of Chromium is an explicit skip. No npm installation is needed.

Each `tests/web/mutations/NN.sh <worktree>` builds and verifies a baseline, patches one behavior, requires the mutant to compile, runs the fixture, restores exact source bytes, rebuilds, and reports dirty counts. A compilation failure is never a killed mutation. Generated build/log artifacts stay under build/.
