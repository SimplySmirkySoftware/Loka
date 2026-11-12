# Web Rail

> **Status:** Normative; frozen Web rally pages 1, 2, 3a, 3b, 4 and 4b, with the delegator's applied-root-visibility clarification.
>
> **Owns:** DOM resource ownership, browser entries, completion, shutdown, text wire and typography environment.
>
> **Does not own:** Font loading/raster guarantees, browser scheduling guarantees, or common lifecycle semantics.
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

Page 4 replaces page 2's fixture-specific `start(config)` spelling with `start()`; bootstrap lifetime is unchanged.

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

PR 1 initially projected one-line Text through INaturalSizeSource with fixed metrics (eight CSS pixels per scalar, sixteen-pixel Text height, twenty-four-pixel Button height and sixteen-pixel horizontal inset). The Text projection section below replaces those metrics and adds wrapping. The preview style descriptor carries all six TextStyle axes, preserving absence and signed point values; wire v2 and font resolution are specified below. Style constants come from `tools/ci/gen_style_vocab.py`.

Buttons are native `<button type="button">` elements. Mouse, Space and Enter use the same click export. Browser focus is read at completion; no keyboard or focus-event export is added. Layout containers other than ScrollView have no DOM elements.

EditText, TextEditor, PopupMenu, ImageView, RectSurface, ScrollBar and CardBand are explicit refused handlers. A reached ScrollView visit settles once after projection, including refusal; a deferred visit leaves its request pending. Menus use the default no-op, dialogs the existing Window refusal seam. Title is unsupported: document.title is not a per-Window projection. IME, general pointer input and hot reload remain outside this increment. Spacing-only skins are specified below.

## Host contract

Load `<EXPORT_NAME>.js` as a classic script, then await its module factory with
`{locateFile}` to map the wasm asset name to the host's URL. This mapping grants
neither fetch permission nor a MIME type. The module's `main()` creates one
heap-resident WebSession for the module lifetime; it is never deleted. Only the
shutdown task destroys its App chain, independently of EXIT_RUNTIME.

Call `module.web.start()` once: 0 means STARTED, 1 WINDOWLESS, 2 FAILED. A second
start returns FAILED without changing the running session. The app supplies
[`CreateAppConfig`](../web/entry/WebAppEntry.hpp), called at most once after
admission succeeds. The session owns the returned config and deletes it at
shutdown; null fails startup, and a missing definition fails linking.

Before discarding or replacing a page, call `web.dispose()` and await
`web.completed` with a host-owned timeout/error channel. Dispose forces shutdown;
`web.quit()` instead asks the roster, which may decline. Completion resolves only
if the document remains alive and teardown finishes. If resources remain,
`web_completed` throws and the Promise stays pending, not rejected. Load failure,
abort and callback exceptions are separate failures, not completion signals.

The seven production exports are `start`, `quit`, `dispose`, `click`, `resize`, `wake` and `scroll`.
`click`, `resize`, `wake` and `scroll` are adapter-only exports, not host entry points. The
adapter's listeners and ResizeObserver deliver input; timers use the wasm
function table callback. The host must not run a wake loop.

The current host profile appends roots to `document.body` and constrains requested
size by the measured content box (including the adapter's `maxWidth:100%`).
HelloWorld requests 420×330. Container selection and panel filling belong to
page 5. HelloWorld has one scroller directly under its root; projected leaves belong
to its content element. The smoke test clicks the visible Probe Button at 420×330.

For CSP, put the same nonce on both the external classic script and inline
bootstrap. Allow `'wasm-unsafe-eval'` in `script-src` and the wasm fetch origin in
`connect-src`; `default-src 'none'` alone blocks fetching. The driver tests both
with and without wasm compilation permission. Extension URI handling is page 5.

[`loka_add_web_app`](../cmake/LokaWebApp.cmake) owns the pinned link settings:
classic modularized output, web environment, dynamic execution disabled, runtime
exit disabled, automatic main invocation, asynchronous wasm compilation, 8 MB
stack, growing memory, and the `locateFile` incoming module API. It also owns the
entry TU, C++98 warning floor and JS link dependencies. The `web` configure/build
preset is the Release production profile; `testing-web` remains the Debug fixture
profile. Both HelloWeb and counter use this same function.

## Reproduction

Build `LokaWebTests` with the existing testing, testing-release and testing-asan presets and run `ctest --test-dir <build> -R '^webRail$' --output-on-failure`. The fake boundary implements the same imports as the real adapter. `LOKA_WEB_CAPTURE_GOLDEN=1` prints the initial instruction sequence; ordinary runs compare the committed golden.

The driver requires Node 22.x from 22.18, or Node 24 or newer (not Node 23). With emsdk 7.0.0 sourced, use `emcmake cmake --preset testing-web`, then build `build/wasm/testing`. The node fixture alone links NODERAWFS. The browser fixture links production Web/common libraries without TEST_BUILD and keeps EXIT_RUNTIME disabled. Run `node tests/web/driver/run.mjs build/wasm/testing/web`; absence of Chromium is an explicit skip, and `CHROMIUM=<executable>` names a browser outside PATH. No npm installation is needed.

Production build: `emcmake cmake --preset web && cmake --build --preset web
--target LokaHelloWeb LokaWebCounter`. Run
`CHROMIUM=/path/to/chrome node tests/web/driver/run.mjs build/wasm/testing/web`.
The same driver serves HelloWeb from `build/wasm/web/example/HelloWorld` and checks
its content box, ScrollView content tree and Probe Button, completion and zero resources, plus staged
fixture configuration, second-start refusal and both CSP branches.
`tests/web/mutations/entry-{1,2}.py <worktree>` exercise factory dispatch and staging;
entry placement and fixture leakage are separate expected compile refusals.

Each `tests/web/mutations/NN.sh <worktree>` builds and verifies a baseline, patches one behavior, requires the mutant to compile, runs the fixture, restores exact source bytes, rebuilds, and reports dirty counts. A compilation failure is never a killed mutation. Generated build/log artifacts stay under build/.

## Text vocabulary and environment

Wire v2 is nine signed 32-bit words: version, count, size kind, signed size
payload, weight, italic, family, underline, color. The stack-owned
`WebTextDescriptor` borrows the existing TextStyle queries; the boundary borrows
its words only for the call. NONE requires payload zero, POINTS accepts the full
signed short range, and ROLE accepts only generated roles. Unset toggles and
weight remain -1. The decoder validates count, unsigned aligned heap bounds,
headers and discriminated values before projection; protocol refusal remains
active in release. The C++ caller consumes the boolean result before asserting.

The generator owns every output path and the freshness checker imports that
inventory. The generated schema fingerprint is FNV-1a 32-bit over UTF-8 canonical
JSON (sorted object keys, compact separators, array order preserved), including
ordered fields, headers, signed ranges and enum name-to-number maps. It detects
accidental skew, not adversarial collisions. `web_text_admit` compares unsigned
fingerprint and count inside BOOTSTRAP, before PlatformContext/Config/App
allocation. Failure returns the existing FAILED and schedules the existing
shutdown path. JS emits one `[web-text] refused: wire|canvas|lang` diagnostic.

Admission builds a private detached canvas, context, fixed session language and
Env Map, publishing only after all probes pass. The same ordered property table
powers canvas and element application and capability probes: accessor presence
before assignment, a non-default round trip, restoration, CSS declaration-name
support, and finite font bounding ascent/descent. Language is an attribute on
the element. The language is captured once from document lang, navigator
language, or `en`, rejecting empty and `inherit` candidates. Completion drops
the service after native teardown, including subsequent bootstrap failure.

Font keys resolve to 48 combinations: six generated sizes, bold, italic and
monospace. NONE is 12; SmallLabel is 10; numeric points snap nearest, ties low.
Unset resolves to normal, non-italic and system. Color and underline do not
participate. Envs are immutable, allocated lazily once per key and owned by the
session Map. Their font shorthand has no line-height. Text projection
writes line-height after every font write. Metric-affecting styling enters
through shared Env resolution.

The Env explicitly resets kerning, spacing, caps, stretch, rendering, direction,
bidi, language, synthesis, size adjustment, font features/variations, optical
sizing, text transformation, writing mode/orientation, indent, CJK spacing,
text size adjustment (including the WebKit spelling), palette and white space.
Font is written first so shorthand resets cannot undo later longhands. This
isolation covers inherited typography, not unrelated element properties.

The support target is current evergreen browsers that pass admission; CI
verification covers Chromium only, not Firefox or Safari. Older engines missing
required controls refuse startup. Advance parity assumes the same Env, available
fonts, and no ancestor zoom/transform. Direct host CSS selecting the element,
fonts loaded after admission, changing document language during the session,
fallback ink overflow and DPR raster differences are outside this contract.
The wire/environment slice originally retained PR 1 fixed geometry; the Text
projection section below supersedes that geometry. The one admission metric
probe establishes service capability, not layout.

Reproduce the JS pins with `node tests/web/js/decode.test.mjs` and
`node tests/web/js/boundary.test.mjs`, and
`python3 tests/web/js/schema.test.py`; run the six `text-*.py` mutations under
`tests/web/mutations/` with the worktree path. Browser cells use
`node tests/web/driver/run.mjs build/wasm/testing/web`; no Chromium prints an
explicit skip. The fake boundary's header/fingerprint checks are limited host
evidence, not evidence of browser decoding or typography.

## Text projection

Each Text or Button context owns one immutable `WebTextSnapshot`: prepared UTF-8,
code-point byte ranges, line byte ranges and widths, joined display bytes, font
metrics, extent, and `WebTextConstraint {fontKey, BlockStyle, availableWidth}`.
Only prepared-byte offsets enter measurement or the breaker. The context compares
the constraint first, then scans normalized current text once. Equal values reuse
the snapshot, including equal values arriving through a different State handle.
No revision, last-seen flag, or additional subscription is retained.

| Input | Prepared text |
| --- | --- |
| CRLF or lone CR | LF |
| Tab or form feed | Space |
| NUL | U+FFFD |
| Invalid UTF-8 | U+FFFD using WHATWG decoder error consumption |
| LF in a Button label | Space |

The normalizer wraps the shared strict scalar decoder. A stray continuation byte
produces its own replacement; an incomplete valid prefix consumes only the bytes
accepted before the error. Build and equality use that same normalizer. Borrowed
UTF-8 needs no allocation for equality; a non-borrowable String uses the deliberate
Web-local twin of ToolboxUtf8Bytes. Expansion and table/join allocation sizes are
checked before allocating.

Build owns all temporary storage and commits only a complete projection. Metrics
must succeed before the breaker runs. Every wrap mode, including NONE, uses
TextLineBreaker. Each range is measured standalone, never by subtracting prefix
widths. A per-build open-addressed memo shares probes across the count/fill passes;
it grows with checked allocation and is discarded after the build. WORD lookahead
can still process quadratic bytes. Browser cost cells report calls, bytes and time
for 200 characters of prose and `x ` plus a 198-character word; there is no budget.

The generated C ABI status distinguishes OK, REFUSED, and PROTOCOL. Width is
`ceil(measureText().width)`; finite/nonnegative and INT_MAX checks precede narrowing.
Metrics use A = ceil(raw ascent), D = ceil(raw descent), H = A + D, checked within
1..SHRT_MAX before writing. Outputs are unchanged on failure. JS consumes exact
byte lengths through TextDecoder and retains no heap view. Context loss and
allocation failure return REFUSED. A null descriptor decode returns PROTOCOL.

REFUSED retains the last successful snapshot and uses the existing requeue and
after-flush relayout seam. Persistent refusal is persistent work: one build
attempt per run, without a retry budget. PROTOCOL asserts in debug and ends the
build without requeue or a relayout request in release. Later legitimate input
may try again. A first failure displays empty text at height zero. Neither failure
clears an existing snapshot. Retire drops the slot; park retains it.

Each render with a snapshot unconditionally makes five writes: projected, style,
line-height, block properties, then joined text. Font, line step and block come from the successful snapshot;
current color and underline overlay that descriptor without rebuilding geometry.
The order restores line-height after the Env's font shorthand. An absent snapshot
writes projected=false, then empty text without line-height; projection visibility
hides chrome and prevents native hit testing without changing lifecycle display. ELLIPSIS suppresses wrapping and projects
only the first explicit line. NONE and CLIP retain all explicit lines. Overflow is
hidden; CSS performs no wrapping.

The allocated line step is H CSS pixels. **Conditionally**, if CSS uses the same
raw ascent/descent as canvas, half-leading is `(H - asc - desc) / 2` in [0, 1),
and the baseline is `asc + half-leading`, less than 0.5 px from A. Different
fallback metrics invalidate this conditional equation. Ink position and fitting
inside the line box are not promised. CHAR wraps at code points, not graphemes.

Natural-size queries use temporary normalized text and the same service, without
publishing a snapshot, consuming a token, writing State, or requesting relayout.
Text width declines empty or multiline input; height declines multiline input or
wrap other than NONE. Answers must fit 1..SHRT_MAX; decline leaves the output
untouched. The PR 1 fixed metric table is no longer used.

Button chrome is Web-specific generated geometry: insetX 8, insetY 4, border 1.
It is not common style vocabulary or a claim about Win32 theme geometry. Creation
sets appearance none, border-box, padding 4px 8px, border 1px solid, margin zero,
minimum width/height zero, preformatted whitespace, and hidden overflow. Outline
is untouched. Every render applies the default Env and measured line-height.
Natural width is label advance + 18 and height is H + 10, including empty labels;
the upper bound is checked before addition. Browser cells exercise native Tab,
Space, Enter and focus-visible outline, content/padding/border geometry and narrow
offers. A missing Chromium is a reported skip, not runtime verification.

## ScrollView projection

SCROLL is resource kind 3. One token owns an absolute scroller and its relative
content element; child tokens own their own elements. The shared
`WebResourceContext` retires either leaf or scroll resources. The route record
holds one base receiver; checked conversions live in that record. Release is
order independent among descendants: removing a scroller detaches its subtree,
and later child releases remove their own elements and rows. The root releases
last. `web_completed` counts rows, so SCROLL counts as one.

Children use content coordinates through `ProjectionParentScopeStack`.
`deriveScrolled` receives positive seat coordinates `(x, y - top)`, as on macOS;
the browser applies scrolling. Content uses `overflow:clip` so children from a
failed traversal cannot create a second scroll offset. Scroller, content and
measurement probe use physical LTR, horizontal writing coordinates.

The decide-first vocabulary distinguishes `knownOverflowWidth`, `viewportWidth`,
`documentWidth = max(viewportWidth, knownOverflowWidth)` and
`maximumOffsetX = max(0, knownOverflowWidth - viewportWidth)`. The width question
runs before traversal. The vertical gutter is always reserved; horizontal chrome
is reserved only when the range and seat height are positive. Children receive
viewport width, and the channel accumulates their height plus content insets.
A poisoned channel stops further child dispatch. Nested viewport admission is
refused without poisoning its enclosing channel. Text measurement refusal also
refuses the reached visit; its pending request is taken and dropped, including
on first measurement.

One synchronous import, `web_commit_scroll`, checks resource kind and layout box
before writing rect, horizontal overflow, document size and origin in that order.
APPLIED returns both rounded native offsets; C++ clamps read-back to its completed
range before publishing. BOXLESS writes nothing, publishes nothing and schedules
no retry. PROTOCOL asserts in debug and preserves the fact in release; writes
may already have happened, so this is not a native rollback guarantee. Origin
assignment is skipped when the current rounded offset already equals the target.
`WebScrollMetrics` preserves the last successfully committed rect, document,
viewport and native axes for request replay; failed layout never replaces it.

A detached node is not visited. A parked root produces a deferred visit: no
traversal, writes, publication or settlement. Successful root reveal requests
layout through the Window-owned `requestSceneRelayout()` door, which calls its
Scene's `requestLayoutAfterRun()`. Reached refusals instead settle and consume
requests through `SettleScrollViewVisit`, with the shared bound of two takes.
No Show/Match attach hook is added.

`WebScrollbarGeometry` is a session value, read through the controller's existing
session reference. A hidden fixed probe is measured during bootstrap admission
before constructing the App, and inside accepted resize dispatch before native
size handling. An unmeasurable bootstrap fails through pre-commit rollback;
unmeasurable resize preserves the old value. Changed geometry visits the route
store's live root rows and requests each eligible Window's own Scene layout.
Style-only scrollbar changes without resize remain stale until module replacement.

The passive listener immediately samples `scrollLeft` and `scrollTop`, rounds
them, and calls the seventh export `web_scroll`. Its typed input invocation
checks the SCROLL receiver, attachment, completed metrics and owning root
admission, then publishes origin once without writing native offsets. There is
no cached event origin or mute flag. Requests replay completed geometry through
the same commit import and publish only on APPLIED.

Web scrollbars are not shortened by content insets. Grid uses the common grid
computation through the Web adapter described below. These are Web policies,
not claims of native parity.

### Stack layout

The Web registry owns a Stack handler that borrows its creating controller.
It calls the same `LayoutStack` computation as the common handler; native common
handlers retain their copied metrics. No typography state is added to the
Session or controller, and startup and text admission are unchanged. Generic
registration still receives null Grid metrics; the Web Grid adapter is
registered separately.

Each reached column or row without vertical alignment passes default metrics
without fetching an estimate. Each reached aligned row calls
`WebTextLayout::rowEstimate` once, resolving `WebFontKey(TextStyle())` lazily.
The existing text service owns the cache. Successful estimates populate only:

| Row metric | Value |
| --- | --- |
| textHeight, fallbackHeight | Default font line height H |
| buttonHeight | H plus twice the Button vertical inset and border |
| editTextHeight, popupMenuHeight, imageFallbackHeight | Zero |

Both H and H plus chrome must fit 1..32767 before either output is assigned.
`rowEstimate` neither normalizes nor allocates. Natural queries retain their
normalization, allocation, keyed measurements, decline behavior and call order;
Text height is not limited by Button chrome. Leaf chrome arithmetic is unchanged.

An estimate failure **does not refuse the Stack visit**. The handler dispatches
children through the common computation with all-zero metrics. REFUSED (including
out-of-range heights) requests relayout once per visit through the existing
controller door; it does not restore Stack layout inputs. PROTOCOL asserts in
debug, and release dispatches with zero metrics without a handler-originated
retry. Leaf measurement can independently refuse and request a retry.

Zero metrics are a lower estimate, not a measured value or a switch to top
alignment. Natural Text answers and explicit heights still contribute. With all
answers declined and no positive explicit height, each offer is zero and the
returned bottom is the greatest actual child bottom. With a Text answer of 13
and a Button of actual height 23, CENTER can place the Button at y+6 and return
y+29 instead of y+23. The common maximum also preserves the estimated reservation.

If leaves and coordinates succeed, a ScrollView commits the degraded extent.
Recovery recomputes extent but does not restore an origin clamped by that commit
or replay a request already settled against it. Estimate degradation is distinct
from leaf refusal: a failed first leaf measurement may leave an empty display;
a reusable snapshot can continue to display. A leaf refusal in a ScrollView can
stop later dispatch and prevent commit. There is no such channel on the
projection root.

Persistent REFUSED retries have no budget, like the existing Text leaf path.
After-run requests avoid re-entering the current Scene drain, but finite Web tail
batches can arm another IMMEDIATE task indefinitely. There is no promise of one
attempt per event-loop turn or of stopping between flushes. Backoff for both
paths remains separate work.

SHARED rows divide remaining width without natural width consultations. NATURAL
and Toolbar's NATURAL_LABELS_SHARE use the traversal as their natural source;
unknown widths remain flexible. Refused kinds still occupy attached seats and
gaps without DOM. EditText/PopupMenu estimate zero; Cell/CardBand use H;
ImageView/RectSurface use explicit heights or H. An aligned all-refused row
reserves its largest estimate, falling back to H if that maximum is nonpositive;
an unaligned all-refused row returns its starting y.

Wrapping Text is measured at its allocated width and retains its actual height.
Vertical alignment still uses an estimate, shared with Win32/macOS, and is not
repeated after measuring wrapped lines. HelloWorld at root widths of at least
400 therefore has two panels; below 400 they stack. The narrower SHARED seats
can clip the growing Add message. This accepted result does not change examples.

Columns now apply remaining-height offers, natural height and horizontal
alignment. Zero remaining height does not become a content-sized ScrollView
viewport. Common range/spans forwarding remains caller-owned and is not passed
to descendants. Axis State changes affect layout, while Lazy window placement
remains Canvas-based. Toolbar and Ribbon retain their existing common policies.

Row coordinate checks precede narrowing on both axes. A failing check returns
the maximum bottom so far and stops remaining children in that row. Within a
ScrollView, the channel also stops later dispatch and metrics/fact commit; at the
projection root, subsequent siblings are still visited. Earlier DOM writes are
not rolled back. A leaf setting the channel during dispatch does not stop the
common loop itself; the Web dispatch entry refuses its later visits.

Cost: one estimate boundary crossing per reached aligned-row visit, no extra tree
walk. Common aligned height consultation occurs at most twice per child; natural
width consultation can recurse through wrappers and rescan when shrinking.
Natural Web text consultations normalize and measure each time. Retry revisits
the existing Scene root. No metrics, retry flag or last-height member is retained.

Build-verified: Linux x86_64 C++98 native testing, Release and ASan builds,
Emscripten 7.0.0 testing-web, and production LokaHelloWeb, with the host fake and
wasm Node cells. ASan ran with leak detection disabled, so it gives no leak verdict.

Browser-verified (Chromium 157 on Linux, through the driver): the Stack cells for
alignment, estimate degradation and recovery, scroll extent and origin after a
degraded commit, wrapping at two widths, axis changes, column offers, scroll
reveal, and a same-root HelloWorld 420 → 399 → 420 sequence with the growing
message's clip record. A missing Chromium is a reported skip, not verification.
Other browsers and the VS Code preview are not verified for these cells.

Scroll reproduction adds `LOKA_WEB_CAPTURE_SCROLL=1` for `scroll.golden`,
`node tests/web/js/boundary.test.mjs` for deterministic adapter read-back and
listener checks, and `tests/web/mutations/scroll-{1,2,3,4,5,6,7}.py <worktree>`.
The counter golden includes the intended additional root-reveal projection pass.

### Grid layout

The controller's registry owns one `WebGridLayoutHandler`, a policy adapter for
`computeGridLayoutResultY`. It retains no controller, metrics, typography or
flags. Each reached visit supplies zero gaps and the existing traversal helpers.
The common computation's optional synchronous coordinate callback borrows the
same context as child dispatch; omitted or null preserves existing native
policy. The common Grid handler and native direct calls omit it.

The Web wall admits the **offered cell**, not the eventual descendant drawing.
Before narrowing it checks x, right, y and bottom in int. It stops that child and
all remaining Grid children at the first refusal, returning the greatest bottom
already dispatched (the incoming y if none). Previous writes and old DOM are not
rolled back. At root the Grid stops its own remaining children and later siblings
can still be visited. Inside a ScrollView the refusal also stops subsequent scope
dispatch and its commit, preserving the previous extent and origin.

Arithmetic safety here is limited to Web's short layout state with zero gaps.
Cell extents are nonnegative; negative offers become zero, and nonpositive row
or column counts become one. Arbitrary int gaps or int coordinates in the generic
computation can overflow before the callback; the wall makes no guarantee for
those inputs. The callback costs at most four O(1) calls per examined child.
The handler walks only its Grid's children up to the lesser of capacity and
child count, with no extra traversal or retained visit state.

Grid divides its parent's offer; it has no height prop or natural-height repair.
Root, a column's remaining height, a ScrollView and a fixed Box can supply positive
height. A zero-height offer places every row at the same y, sharing the current
native rule. Buttons retain their measured height plus chrome and do not fill
the offered cell. Grid creates no DOM panel or fixed height. If every visited child is refused without tripping the coordinate wall, the
returned bottom is the last visited row's top (incoming y for an empty Grid).
Sparse and mixed Grids use the greatest returned child bottom.

Children beyond rows times columns are not dispatched. Initially unprojected
children create no DOM on that visit. Shrinking capacity while retaining attached
Buttons can leave excess children's previous DOM, rects, visibility and input
routes active. This accepted limitation does not promise hiding or input removal;
that would need a separate projection-eligibility design. Restoring capacity
lays out the same retained children again. MineSweeper uses the Cell projection
and the `LokaMineWeb` entry described below.

Grid build-verified: Linux x86_64 C++98 testing and ASan builds, Emscripten
7.0.0 testing-web, and production LokaHelloWeb. Host and wasm Node cells passed
for offers, refusal and retained-history behavior, and the full native suite
passed. The selected ASan grid/layout/web tests ran with leak detection
disabled, so they give no leak verdict.

Grid browser-verified (Chromium on Linux, through the driver; separate from the
Stack evidence above): `Grid2x2` (four Button rects in a 100x80 Box at x 0/50,
y 0/40, width 50, measured height below the cell height) and
`GridCapacityHistory` (shrinking to 1x1 keeps all four DOM identities, widens
the first Button to the whole offer and preserves the excess Buttons' previous
rects and visibility, with the fourth Button's click route still working;
restoring as 4x1 lays the same Buttons out again). Without an installed Chromium the driver prints a skip.

### VS Code shell

The shell procedures live in `tools/vscode-loka/src/previewHtml.ts` (documents),
`src/preview.ts` (panel lifetime), `src/build.ts` (task lifetime), and
`src/catalog.ts` (synchronous catalog decoding and current-example resolution).
`src/wire.ts` supplies the native VS Code ports. Externally destroyed documents
(panel close or window reload) terminate abruptly: there is no host disposal
call and no completion acknowledgement.

Each owner uses an event queue: nested calls only enqueue, so the current event
may still install a document, post disposal, or launch a task before a queued
close/dispose takes effect. The task owner retains early end facts and reconciles
them by execution identity at acquisition, preferring process results; rejection
or disposal drops them. Unexpected exceptions stop that owner's queue permanently;
recovery requires reopening the panel or reloading the window. Panel access uses
an `install` / `postDispose` capability seam: the adapter revokes capability
synchronously on native disposal before posting CLOSE. Native disposal and fault
retirement share a capability-taking cleanup path.

Only `workspaceFolders[0]` is used. Open the repository in Remote WSL with
Node/npm and the Emscripten tools available to tasks. With extension dependencies
installed, select **Loka: Preview Extension** and press F5. The unchanged
prelaunch task runs a clean `npm run build`; a failed build stops launch (do not
choose Debug Anyway). The launch passes `${workspaceFolder}` as a separate
argument to open the repository in the Extension Development Host. Opening a
source file does not open or select a preview.

The removed `loka.preview.module` and `loka.preview.buildCommand` settings are
no longer read. Remove those overrides and set `loka.preview.buildDirectory`
and `loka.preview.configureCommand` together. The former is absolute or relative
to the first workspace folder; the latter is responsible for configuring that
directory. The extension neither infers nor rewrites presets. For the repository
presets, the pairs are:

| Preset | buildDirectory | configureCommand |
| --- | --- | --- |
| web | `build/wasm/web` | `emcmake cmake --preset web` |
| testing-web | `build/wasm/testing` | `emcmake cmake --preset testing-web` |

Use **Loka: Configure**, **Loka: Select Example**, **Loka: Build**, then
**Loka: Open Preview**. Configure and Build share one task owner and refuse a
second operation during acquisition or execution. Configure needs trust and a
folder, but no catalog; success reports `Configured` without reloading. Build
reads the catalog and selection at admission and starts a `cmake` process with
`--build`, the absolute build directory, `--target`, and the selected target as
separate arguments, with the workspace folder as cwd. Configure uses a shell
command with the same cwd. Changes to the Configure shell's environment do not
persist into a later Build task. Build keeps its immutable operation through
completion, reports the captured target on success, and calls parameterless
reload on the current panel, if any. It never opens a panel.

Select reads the catalog after trust admission and presents its labels in
producer order. It saves only the chosen identity in `workspaceState`, under a
key containing the opening folder's URI. Cancellation does nothing. Successful
persistence calls the activation's current-slot reload, never a captured panel.
Picker, persistence, reload and error-notification failures are contained by the
asynchronous handler. After activation disposal, late preference writes are
allowed; success touches no panel, status item or output channel, and errors use
only the window notification service. Overlapping Select commands leave the
value actually committed to storage, regardless of invocation, picker-close or
promise-settlement order. No selection phase, generation counter or descriptor
cache is retained.

Build and every preview replacement read trust afresh. Untrusted workspaces run
no task and receive a script-free MESSAGE page; Select does not read the catalog.
Catalog reads and validation are synchronous at Select, Build and replacement
preparation, with no watcher or editor/configuration listener. Missing, malformed,
unsupported-version, empty and unreadable catalogs diagnose **Configure**.
A nonempty saved identity absent from a valid catalog diagnoses **Select Example**;
it never falls back silently. Missing or empty selection uses `apps[0]`, without
sorting or promising HelloWorld as the default. Explicit identity survives
reordering. Validation precedes task creation and resource URI derivation, using
the producer's lexical path rules. The build directory is the lexical containment root for catalog paths;
the selected JavaScript file’s directory is the webview resource root;
JavaScript, companion Wasm and factory name come from the validated row. Missing
artifacts diagnose **Build**. Open reveals an existing panel; Reload replaces it.

The following limitations are intentional and pinned by the shell tests:

- Build completion and displayed artifacts are not paired. Selecting B during a
  build of A can load B immediately and A's completion can reload B again. B may
  be missing, stale or fail startup. Preview status can overwrite the captured
  target's success report in the shared status item.
- Replacement resolves selection, settings and a newly validated catalog when
  preparing installation, including after disposal acknowledgement or the two
  second timeout. Intermediate selections may never be displayed. Reloads
  during DISPOSING retain the original deadline and produce no extra disposal
  post or replacement.
- Manual settings changes and catalog regeneration leave the current document
  untouched until the next operation. A picker may save an identity removed
  after it opened; subsequent resolution diagnoses it. Without explicit
  selection, catalog reordering or membership changes may change the default.
- A running task retains its initial target and directory. Success reloads the
  panel that exists then, resolving the selection and directory at replacement
  time, including after closing and reopening the panel or changing directories.
- Reload during Build uses whatever artifacts exist at replacement and browser
  fetch time. JavaScript and Wasm are not an atomic snapshot: missing, partial
  or differently aged files can fail to load. The displayed document is not
  guaranteed to be the completed task's result.
- These stale/availability outcomes do not bypass trust, schema or lexical path
  containment. Only the first workspace folder participates; no folder picker
  is provided.

Verified on Linux x86_64 with Node 24.19.0 and Emscripten 7.0.0: `npm run check` and `npm run build` exited 0,
`node test/artifact.cjs` passed, `node --test` over the extension tests passed,
and the mutation script with type checking enabled killed every mutant.
The extension artifact is build-verified. Native `ctest --preset testing`
exited 0; with Node available, `scriptVscodeLoka`, `scriptVscodeLokaLauncher`
and `scriptScenarioTools` pass, while without emsdk,
`scriptWebAppCatalogIntegration` prints its skip. The browser driver passed,
including `preview shell mine`. `cmake --build <build/wasm/web> --target
LokaMineWeb` compiled and linked with an environment stripped of every emsdk
variable (`PATH=/usr/bin:/bin`).

Nothing in this change is runtime-verified inside VS Code yet: F5 with the
folder argument, Remote WSL authority, ProcessExecution inside the task service,
and the trust refusal still require the owner's host hand run. The earlier
HelloWorld hand run recorded for PR 5b predates these commands.

### Cell projection and MineSweeper preview

Cell uses the existing BUTTON resource and its click listener, through a typed
`Handler<CellNode, WebElementContext, BUTTON>`. Resource kind describes DOM storage;
logical node type decides enabled policy, sizing, and text preparation. Text,
Button and Cell share one context implementation and one click procedure. The
procedure rechecks admission after the test hook and borrows the current logical
node's emitter. An enabled Button or a Cell can be active; a Cell with no emitter
has no action. Text and unknown nodes cannot be active. No receiver caches an
emitter, enabled flag, or Cell policy.

File-local leaf queries retain nothing. Label preparation normalizes Button and
Cell to a single line; Button alone receives measured chrome height and remains
eligible for natural-size queries. Cell refuses both natural queries without
changing the output. Its CENTER block is part of the constraint before snapshot
construction; Button retains its default LEFT. Row estimation still treats Cell
as an unknown child contributing the fallback line height, then gives it the
final row-height offer. Button's checked chrome arithmetic remains unchanged.

Cell retains the offered width and takes the offered height when positive,
otherwise `FallbackControlMetrics::kCellHeight`. This choice is independent of
snapshot availability and goes through the existing leaf height, bottom and
projection checks before narrowing and writing the rectangle. Grid checks the
*offered* rectangle, which does not cover a later fallback enlargement. At root,
a Cell at y=32760 with fallback height 20 writes that rectangle and returns the
clamped bottom 32767; root does not reject its bottom 32780. Inside ScrollView,
the content-bottom check refuses before the rect write and prevents extent and
origin commit. This is an existing root limitation, not a new Grid safety wall.

Measurement REFUSED preserves the last successful snapshot, restores the layout
inputs, and schedules the existing after-flush retry. An initial failure stays hidden; a retained snapshot
stays visible. Refusal inside ScrollView conservatively prevents its commit even
though Cell's height does not require measurement. PROTOCOL retains its existing
debug assertion and release behavior without retry. No new retry clock exists.

The accepted preview appearance is Web Button chrome: padding, border,
`appearance:none`, no wrapping, and hidden overflow. At width 27, the content
width is 9 CSS pixels; a glyph may clip. Narrowing does not truncate snapshot
bytes. Width remains part of snapshot identity. No styling aims for native Cell
pixel or metric parity. Tab focus and Space/Enter clicks come from `<button>`;
action timing is DOM click, rather than the mouse-down timing of some native
rails. NATURAL Columns offer zero height; ordinary Columns give the remaining
height, which a Cell fills. Later siblings can receive zero and use fallback.
Positive ScrollViews offer viewport height. A cramped Grid can have overlapping
logical fallback rectangles; CSS padding and border can also enlarge the rendered
border box beyond a small positive style size. These preview outcomes are
accepted independently of hit testing at normal measured cell centres.

`LokaMineWeb` uses the shared, include-guarded C++98
`MineSweeperProductionAppConfig` constructor, which samples the time only when
constructed. Win32, Classic and macOS entries use the same adapter; the macOS
outer autorelease-pool shutdown handling is unchanged. Fixed-seed test configs
remain separate. The requested 220 by 240 content area flows through Column and
Keyed to an eight-by-eight Grid. Window title remains unsupported.

Successful New Game replaces 64 Cell resources and retains the New Game Button.
Old routes and listeners revoke synchronously, and the existing controller drain
releases DOM resources after the borrow. Tokens are unsigned 32-bit identities,
never reused; each successful New Game consumes 64 more, and failed creation can
also consume tokens. Exhaustion permanently refuses new resources for that module.

Cost: each materialized Cell uses one existing DOM resource, one click listener,
and one slot holding at most one successful snapshot. Each reached leaf visit
selects its file-local typed handler once; no per-controller registration is added.
Leaf policy queries are O(1) and walk no foreign rows. Snapshot matching scans the
normalized text; rebuilding retains the existing allocation and measurement cost.
Click admission is checked at ingress and dispatch, with at most one emission.
Retirement walks only the retiring owner's subtree through the existing path.

Cell build-verified: Linux x86_64 C++98 testing, ASan, and NDEBUG release;
Emscripten testing plus production `LokaHelloWeb` and `LokaMineWeb` targets.
Host and wasm `WebCellTests` pass policy, refusal and lifecycle checks, and the
full native suite passes. ASan ran with leak detection disabled, so it gives no
leak verdict. Of the native MineSweeper targets that share the adapter header,
`LokaMine68K` is build-verified (`retro68-68k-release`); `LokaMineWin32`,
`LokaMineMacOS` and `LokaMinePPC` are built by CI only and were not built
locally. No native runtime verification is claimed.

Cell browser-verified (Chromium on Linux, through the driver): 64 Cells in
eight distinct rows and columns with CENTER beside the Button's LEFT; a CDP
mouse press and release at each measured centre reaches that Cell only, each
from a fresh generation; Tab focus, Space and Enter; New Game replaces all 64
(old DOM clicks and old tokens do nothing, each old listener is removed before
its element is released, once) while the New Game Button is retained; cramped
roots overlap as described above; a fixed-seed board shows a blank, a number
and a mine; and the production `LokaMineWeb` module at 220x240 shows 65
Buttons, runs New Game and disposes. Without an installed Chromium the driver
prints a skip.

## Generated Web example catalog

The root Emscripten configure finalizes `loka-web-apps.json` in each binary
directory after visiting the examples. `loka_add_web_app` registers only callers
beneath the repository root's `example/` directory, including nested projects;
the build graph must visit a directory for it to register. Membership is
lexical (the calling source directory's path), not resolved filesystem
containment: a caller reached through a symlink beneath `example/` can register
even when the symlink points outside that directory. Browser fixtures such
as `LokaWebCounter` are excluded. The VS Code preview extension reads this catalog at Select, Build and document
replacement preparation; it does not maintain a second example list.

Schema v1 is `{"version":1,"apps":[{"id","name","target","exportName","sourceDir","js"}]}`.
`id` equals the unique `target` (`[A-Za-z0-9_]+`); `name` is the example directory
label, not identity. `exportName` is a JavaScript identifier
(`[A-Za-z_][A-Za-z0-9_]*`). `sourceDir` is repository-relative beneath `example/`;
`js` is a nonempty binary-directory-relative `.js` path. Neither path may contain
a parent traversal component. Replace `.js` with `.wasm` to locate its companion.

V1 covers single-configuration Web builds and the helper's configure-known
output specification. The finalizer refuses registrations and output-property
overrides that violate that specification, before writing any catalog. Each
successful configure replaces the complete file, including an empty `apps`
array; stale artifacts cannot restore removed registrations. Fields preserve
spaces, non-ASCII text and semicolons. The serializer escapes backslash, quote,
backspace, tab, newline, form feed and carriage return, and refuses other C0
controls representable in CMake strings (NUL is not representable).


## Spacing-only skins

The factory option `lokaSkin` selects only the Web rail's immutable `RailSpacing`.
The ordered `webSkins` table in `tools/style-vocab.json` generates the C++ rows
and the JavaScript name resolver:

| Name | Vertical gap | Horizontal gap | Standard window margin value |
| --- | ---: | ---: | ---: |
| `web` (default) | 12 | 4 | 9 |
| `win32` | 6 | 6 | 10 |
| `macos` | 6 | 6 | 16 |
| `classic` | 8 | 8 | 12 |

For example, await the module factory with `{lokaSkin: 'macos', locateFile}`,
then call `module.web.start()`. BOOTSTRAP first admits scrollbar geometry and
text/schema compatibility. Only after both succeed does `web_skin()` read that
module's current `Module['lokaSkin']`, exactly once, before PlatformContext,
Config or App construction. Names are case-sensitive. Missing, unknown and
non-string names resolve to `web`, including inherited-property names such as
`constructor` and `__proto__`. A signed import index below zero or at least
COUNT is instead a protocol failure: it constructs none of those owners,
returns the existing FAILED result and schedules the ordinary shutdown task.
Completion releases the already-admitted text service.

Each WebSession starts with a non-null pointer to the static `web` row and
replaces it at most once after the bounds check. Controllers borrow the session's
const reference; the builtin Box handler copies that value at construction.
Later windows use the same selection. Changing the option after startup has no
effect, and concurrently live modules select independently. There is no live
skin switch, selection cache in JavaScript, allocation, observer or new clock.
Cost: one startup import and at most four generated name comparisons; each
layout consumer reads in O(1), with no additional tree walk.

The existing admission fingerprint now also covers
`webSkinSelection = {default: 0, names: [...]}`: array order defines name-to-index
identity and length defines COUNT. A renamed or reordered table changes the
fingerprint and refuses mismatched JavaScript/Wasm before root creation. Text
wire version 2 and its nine words are unchanged. Spacing numbers are consumed
only by C++ and remain outside the fingerprint; changing only those numbers
leaves it unchanged. This detects accidental skew, not adversarial collisions.

A skin leaves Button and Cell sizing rules, padding and borders unchanged
(their allocated dimensions can still change with the space the new gaps
leave), and does not change fonts, colours, scrollbar geometry, DPI policy,
or explicit gap overrides. It does not
apply a window content margin at the root. HelloWorld's inner ScrollView Box
uses the selected standard margin and its default gaps change, while the
measured root width breakpoint remains 400 (399 is one column; 400 and 420 are
two). MineSweeper's New Game-to-board gap changes, moving the board and changing
its remaining height. The root size and the board's Cell chrome are unchanged.

Skin build-verified: Linux x86_64 C++98 testing, ASan, and NDEBUG release;
Emscripten 7.0.0 Debug fixture and Release `LokaHelloWeb` / `LokaMineWeb`.
The host and Node/Wasm fake suites pass, including the unchanged instruction
goldens, and the full native suite passes. The ASan web/layout tests ran with
leak detection disabled, so they give no leak verdict. The spacing mutations
compile and reach their discriminating assertions.

`lokaSkin` is a plain property of the module argument, read through
`Module['lokaSkin']` when `web_skin()` runs. It is not listed in
`INCOMING_MODULE_JS_API`: Emscripten accepts only its own API names there and
warns about any other entry.

Skin browser-verified (Chromium on Linux, through the driver), for each of
`web`, `win32`, `macos` and `classic`: the fixture's gaps, an explicit gap
override that no skin changes, and a selection that stays frozen after start;
HelloWorld at measured root content-box widths 399 / 400 / 420 with its inner
padding and an unmoved two-column threshold; MineSweeper's Button-to-board gap
with clicks and New Game still working. Two modules with different skins stay
independent while both are alive. Without an installed Chromium the driver
prints a skip.
