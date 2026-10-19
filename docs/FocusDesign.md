# Focus design

> **Status:** Normative
>
> **Owns:** Focus fact ownership, participant lifetimes, requests, publication and completion
>
> **Does not own:** Exact API signatures or native focus order
>
> **Code truth:** [SceneFocus.hpp](../common/app/scene/SceneFocus.hpp),
> [FocusPublisher.cpp](../common/app/FocusPublisher.cpp),
> [Window.cpp](../common/app/core/Window.cpp), [App.cpp](../common/app/core/App.cpp)
>
> **Verification:** [SceneFocusTests.cpp](../tests/SceneFocusTests.cpp),
> [FocusPublisherTests.cpp](../tests/FocusPublisherTests.cpp),
> [AllocPinTests.cpp](../tests/AllocPinTests.cpp),
> [ToolboxFocusHostTests.cpp](../tests/toolbox/host/ToolboxFocusHostTests.cpp),
> [Win32FocusTests.cpp](../tests/Win32FocusTests.cpp),
> [MacFocusTests.mm](../tests/MacFocusTests.mm), and the focus compile pins in
> [tests/compile/pins](../tests/compile/pins/)

## From AGENTS.md

Keyboard focus is reported into the fact box of an app-owned
`loka::app::Focus<K>`, one per screen by convention; its request box carries
the app's `post`; each Scene owns its single publication. Rails call
`App::reconcileFocus` at their outer completion and read native focus at an
admitted completion; native notifications never write the fact, and the
Scene's publication is not duplicated in a rail-owned record.

## The fact

The app declares one `loka::app::Focus<K>` for its screen. A screen that
mixes key types (an enum form beside an id-keyed list) needs one fact per key
type; the Scene still publishes one input at a time, and a move between facts
passes through none (see [Write rules](#write-rules)). The fact always starts
at `none()`; an initial declaration key initializes the request, not the fact.

`Focused<K>` has an explicit `none()`; `is(key)` tests a held key, and `key()`
requires a held value. Keys are app data: an enum, integer id, or `ItemId`,
independent of a control's address. LazyView can recreate a control while its
model identity survives; a pending request can target a recreated input with the same key.

[Focused.hpp](../common/app/Focused.hpp) owns the key wall. `FocusKeyTraits<K>`
has no permissive primary: a mapping must be lossless and fit one machine word.
Application enums opt in; supported integer types and `ItemId` have mappings.
Strings and unregistered keys are refused at compile time by the Props door.

## The Props door

`.focusedAs(fact, key)` on [EditText](../common/app/nodes/controls/EditText.hpp)
and [TextEditor](../common/app/nodes/controls/TextEditor.hpp) stores one
non-template [FocusBinding](../common/app/FocusBinding.hpp): two borrowed write
seats extracted through the Props door, a per-key-type function table, and a key
word. Both seats preserve the declaring owner's tracker and transaction.
Identity includes the fact state, request state, and key, participates in Props ordering, and is
not a dirty source. The app's declaring owner gives the fact its lifetime;
the binding does not extend it.

## Ownership: the shape

```text
[Window] --owns--> [SceneManager] --current--> [Scene] --owns--> [SceneFocus]
    |                                                            |
    |                                      +--membership---------+
    |                                      |                     |
    |                                      v                     |
    |                              [rows inside leaf nodes]      |
    |                                      ^                     |
    |                                      +--publication 1:1----+
    |
    +--owns--> [controller: native read + Null/Toolbox source slot]
                                      |
                                      +--source 1:1--> [row]
```

The kernel stores links and the phase; it knows nothing of bindings or facts.
Comparison, fact writes and audits live in app code, in
[detail::FocusPublisher](../common/app/FocusPublisher.hpp), the single app
`detail` class befriended by `SceneFocus`. Reconciliation reaches it only
through Window; there is no public publisher entry. Participant lifecycle
and binding hooks also enter this private policy to complete their writes.
The controller owns the native read and, on Null/Toolbox, a read-source slot.

## Three links and their lifetimes

Membership joins at `BoundaryNode::composeTree` ATTACH after
`attachStateOwner` succeeds, through `Node::asFocusParticipant`. Only leaves
are queried for joining: no Boundary, composable or nestable node is admitted,
including a Boundary used as a Scene root. Joining is idempotent and silently
refuses RETIRED nodes. It also refuses another live membership (a debug
assert; a no-op in release builds). A refused owner-attach subtree never
joins. See [Boundary.hpp](../common/app/scene/boundary/Boundary.hpp),
[Boundary.cpp](../common/app/scene/boundary/Boundary.cpp), and
[Node.hpp](../common/app/scene/Node.hpp).

Membership survives parking. RETIRED, the row destructor, and the unconditional
`disconnectAll` at the end of `Scene::teardownComposition` unlink it. The
rootless early return also disconnects. `SceneFocus` survives
`REQUEST_DETACH` / `REQUEST_REARM`.

Publication and read source are separate noncopyable, two-ended, callback-free
links; a row can carry both. Cutting is idempotent, and either endpoint's
destructor cuts both ends. Leaving ATTACHED cuts the source even for an
unpublished row; a published row also cuts publication before writing none.
An exchange rewires both publication endpoints before its first fact write.
The controller's source endpoint cuts on destruction.

Row destruction, source destruction and teardown disconnect are no-write
safety nets. Disconnect reads neither bindings nor surviving app facts;
reclamation invokes no living detach hook.
Debug builds assert only that no publication survives `SceneFocus` destruction.

## Reconcile: read, do not listen

Native notifications are not fact-write points. Each admitted outer completion
calls `App::reconcileFocus`, which re-enumerates the live App-owned windows and
calls private `Window::reconcileFocus` once per visited window identity,
skipping close-pending windows. Re-enumeration tolerates observers removing
windows. The ordinary walk uses inline visited storage without allocation;
overflow allocates (see [Cost lines](#cost-lines)).

Four gates precede the native read: App admission flush, SceneManager apply,
Scene run, and the current Scene's Publication phase. The current Scene must
also be attached, have a root, and have a live rail/controller. These checks
remain active in release builds.

[readNativeFocus](../common/app/scene/projection/PlatformController.hpp) has
three answers: cannot answer keeps the current publication and fact unchanged;
answered none clears publication; answered context supplies a candidate. A
candidate needs an app participant type, membership in the current Scene,
logical ATTACHED status and a valid binding. A context that fails any check —
including a text input without `.focusedAs`, which is still a member and can
be a read source, or a control that is not a participant — is treated as
answered none. Inactive native windows decline, so their last value is held.

Reporting occurs at the next admitted completion after the gates clear. A
modal entered from a fact observer suspends that window's reporting and,
through [Busy exclusion](#busy-exclusion), its admission and close; native
focus still moves.

| Rail | Typed participant mark | Completion point | Declared behavior change |
|---|---|---|---|
| Toolbox | Existing edit-control rows, or a row-linked fallback source; native focus takes precedence | Tail of foreground `ToolboxApp::present`, after admission and render; background flushes windows, then returns before dialogs and focus (#503) | Fallback identity is the row, not text state plus rect; key delivery uses the current source context. This prevents stale retired targets and migration between fields sharing text. Native focus, successful native promotion, clipped-away fallback hits and blank clicks clear the fallback. |
| Win32 | `Win32FocusParticipant` uses a `SetPropW` property on EditText/TextEditor HWNDs, removed on detach; read requires a descendant of the active root | After final admission in each message-loop iteration, including `IsDialogMessageW`, before waiting or continuing | Active/click-active `WM_ACTIVATE` with the minimized bit clear restores the current Scene's published control through `SetFocus`, before default processing, only while its typed mark still matches. Deactivation does not restore. |
| macOS | Class-checked first-responder hops through Loka's field/view and delegate owner | End of `MacApp::flushInvalidationsTick`, after admission and pending relayouts | Posted focus calls `makeFirstResponder:` after admission; existing capture/restore remains before ordinary completion. Non-key windows decline. |
| Null | Test-selected `FocusParticipant` connected to the controller's source slot | Startup and scenario-pump completion call App; direct Window completion is available through test access | Simulated focus follows the same publication and source-lifetime rules; no native behavior. |

No rail interprets untyped per-window user data as a participant. Table
sources: [ToolboxFocus.cpp](../apple/toolbox/src/ToolboxFocus.cpp),
[ToolboxPresent.cpp](../apple/toolbox/src/ToolboxPresent.cpp),
[ToolboxEditTextBinding.cpp](../apple/toolbox/src/ToolboxEditTextBinding.cpp),
[ToolboxTextEditorBinding.cpp](../apple/toolbox/src/ToolboxTextEditorBinding.cpp)
(`handleEditClick`, where native focus cuts the fallback),
[Win32FocusParticipant.hpp](../win32/src/context/Win32FocusParticipant.hpp),
[Win32EditTextContext.cpp](../win32/src/context/Win32EditTextContext.cpp) and
[Win32TextEditorContext.cpp](../win32/src/context/Win32TextEditorContext.cpp)
(mark attach and detach),
[Win32ScenePlatformController.cpp](../win32/src/Win32ScenePlatformController.cpp),
[Win32App.cpp](../win32/src/Win32App.cpp),
[Win32Window.cpp](../win32/src/Win32Window.cpp),
[MacScenePlatformController.mm](../apple/macos/src/MacScenePlatformController.mm),
[MacEditTextContext.mm](../apple/macos/src/context/MacEditTextContext.mm),
[MacTextEditorContext.mm](../apple/macos/src/context/MacTextEditorContext.mm),
[MacApp.mm](../apple/macos/src/MacApp.mm),
[NullScenePlatformController.hpp](../tests/platform/null/NullScenePlatformController.hpp),
[NullApp.hpp](../tests/platform/null/NullApp.hpp),
[AppTestAccess.hpp](../common/testing/app/AppTestAccess.hpp),
[WindowTestAccess.hpp](../common/testing/app/WindowTestAccess.hpp) (direct
Window completion), and the Null completion pins in `FocusPublisherTests.cpp`.

## Write rules

Write only on difference: even an equal State set would dirty its tracker.
Move the publication endpoint first. Within one Scene, moving between inputs
of the same fact writes the new key once, without intermediate none. Between
different facts, clear the old fact first, then re-check the live publication
edge before dereferencing the target, check ATTACHED, and re-read its binding
after observers before publishing the new key. Membership was checked at
admission; retirement and disconnect cut the edge, so the edge re-check also
protects against their membership loss. An invalid re-read binding writes
nothing.

Observers may briefly see no holder, never two. Reconcile does not loop to
stability: an observer's native focus change is read at the next completion.
Across a Scene swap, the old Scene's fact passes through none when its published
row leaves ATTACHED during teardown; the new Scene has its own bundle and
publishes at its first eligible completion. The stranded-row exception is in [Known
limits](#known-limits).

## Participant lifecycle

Only the published participant writes the fact. Attaching, replacing the
binding of, or detaching an unpublished participant never touches it.
Leaving ATTACHED while published, whether terminal or retained/hidden, cuts
source and publication before clearing the old binding.

Replacing the published binding uses the same exchange: key-only change writes
once; a different fact clears the old fact, then publishes the new key subject
to the observer re-checks above. The applier keeps an old-binding snapshot
until exchange completes. Reclamation writes nothing. The two controls share
[FocusParticipant](../common/app/FocusParticipant.hpp) and publication policy.

## Busy exclusion

`Scene::isBusy` means a run is in progress, that Scene is publishing focus,
or its controller has an open [controller borrow interval](RequestDeliveryDesign.md#platform-operation-interval-968).
Window declines focus completion during that interval. A borrow interval opened
inside an existing focus publication restores its own outer phase independently.
App admission, retired-Scene reclaim and close draining skip the busy window's
row entirely. This prevents swap during publication followed by nested reclaim
from freeing the Scene beneath the publication frame, and prevents close
from deleting the window during publication. Pending work remains for a later
flush; no separate busy flag is stored.

## LazyView and other seats

An item keeps its fact and key for its structural lifetime. Content updates
apply item Props without re-declaring its children, so they do not reapply a
nested `.focusedAs`; change the key through structural replacement. Ordinary
retirement on a window-key change (including overlapping items) reports none (except for #912; see [Known
limits](#known-limits)). App data retains the key, not the focus value, and
returning does not restore focus. Parked Match/Show branches retain membership
but lose publication and source.

## App errors, audited not enforced (debug builds)

Two ATTACHED inputs with the same fact and key in one Scene, or one fact
simultaneously published by two live Scenes, are app errors. Debug audits run
for uniqueness at publication of a new target and replacement of the published binding.
The separate placement audit checks inspected bound participants at completion.
They do not enforce uniqueness in release builds.

Registration can overlap legally: local-rebuild REPLACE attaches the new child
before retiring the old, and a prepared Scene composes before installation.
Therefore registration is not an audit point. A conflict introduced later
remains unreported until the next publication or published-binding replacement;
the publication audit compares simultaneous publications. The placement audit
additionally rejects a borrowing field whose bundle belongs to another Scene.

## Known limits

- [#912](https://github.com/SimplySmirkySoftware/Loka/issues/912): a local-rebuild refusal
  can strand children. A stranded published row can keep its fact HELD until
  teardown disconnects it without writing; disconnect does not repair the fact.
- [#913](https://github.com/SimplySmirkySoftware/Loka/issues/913): a prepared replacement
  Scene can project early. Publication rejects the wrong Scene's rows by
  construction, but whole-sequence safety is not claimed until that fix lands.
- A refused `attachStateOwner` subtree never joins and cannot publish: this
  failure stays on the safe side of the focus contract.
- macOS capture/restore carry-key cleanup remains deferred under item 8 of
  [#911](https://github.com/SimplySmirkySoftware/Loka/issues/911). Its existing temporary
  native restoration state is not another Scene publication record.

## Cost lines

The amended [#911 ruling](https://github.com/SimplySmirkySoftware/Loka/issues/911) supplies
these doors; the App enumeration and checked app-row conversion reflect the
landed implementation in [#915](https://github.com/SimplySmirkySoftware/Loka/pull/915).
Costs below exclude observer work; debug audit costs are listed separately.

| Door | Caller / frequency | Work and owner of rows visited |
|---|---|---|
| Join | Kernel, each eligible leaf ATTACH | One capability query; O(1) idempotent membership insert; no fact write |
| Leave / retire | Every node lifecycle-fact change, all node kinds | One capability query per node (`Node::asFocusParticipant`); for participant rows, O(1) endpoint cuts, at most one none write if published; O(1) membership unlink on RETIRED |
| Teardown disconnect | End of each composition teardown | O(M) over this Scene's surviving members, normally empty; no writes |
| Reconcile | Each eligible live window, once per completion | One admission read; O(M) scan of this Scene's members, including parked/unbound rows, even with no request. A take adds one O(M) identity relookup, at most one native write and one native readback. Publication rewires in O(1) and makes zero to two direct fact writes. |
| App enumeration | Each outer completion | Repeated scans of App-owned group entries; for W windows, O(W squared) group visits and O(W cubed) worst-case visited-identity comparisons, plus close-pending lookups. No allocation within the inline visited capacity of `App::reconcileFocus`; overflow uses O(W) storage and allocates. Non-window group entries also participate in scans. |
| Busy check | App admission/reclaim and close draining, per window | One focus-phase and one borrow-phase read beside the existing run check |
| Binding replacement | Participant applier | O(1) exchange and zero to two direct writes when published |
| Source replacement | Rail, on native/test source change | O(1) endpoint rewire; no fact writes |
| Debug audit | New publication or published-binding replacement | O(M) current Scene members plus the registry of live `SceneFocus` owners |
| Storage | Scene / participant / controller | One `SceneFocus` per Scene; membership hook plus publication and source endpoints per row; source endpoint on Null/Toolbox controller. No Node field; `Node::asFocusParticipant` is a virtual capability, and `FocusRow` has a checked app-extension type key. |

The allocation pin covers steady-state App enumeration within inline capacity;
it does not certify arbitrary window counts or allocations made by observers.
Exact storage capacity and representations remain in the code.

## Rejected shapes

- Writing the fact from native notifications: no rail delivers a complete set ([#911](https://github.com/SimplySmirkySoftware/Loka/issues/911)).
- One fact per input (`.focused(aFocused)`): "one of them" is not a type, and it dies with a LazyView item ([#911](https://github.com/SimplySmirkySoftware/Loka/issues/911)).
- `FocusScope` / `FocusField` seat nodes: a Keyed arm misses the enclosing scope, a wrapper is not layout-transparent, and finding the scope is a multi-hop traversal ([#911](https://github.com/SimplySmirkySoftware/Loka/issues/911)).
- A per-node "Scene resident" bit: it carries no Scene identity, so a prepared replacement's rows could not be rejected ([#911](https://github.com/SimplySmirkySoftware/Loka/issues/911)).
- A common virtual focus holder implemented once per rail: it repeats the common mechanism across four implementations ([#911](https://github.com/SimplySmirkySoftware/Loka/issues/911)).

## Not covered here

Automatic reveal/scrolling, replies, and cross-Scene carry-over remain outside
this contract. Focus order and selection are specified only for the rail paths
described below, including Toolbox Tab traversal.


## App-posted focus (#960)

`Focus<K>` owns declaration handles for two owner-backed states:
`Reported<Focused<K> >` is written by the reporting seam and
`Request<detail::FocusTarget<K> >` is posted by the app and consumed by completion.
The bundle is non-copyable. Both handles must be valid for `post` and
`.focusedAs`; a partial allocation cannot expose a partial binding. No field
stores a separate validity or pending bit. `FocusTarget` adapts `Focused` to the
request protocol and opts into latest-wins coalescing, not a command queue.

Declare with `state(focus)` for no request, or `state(focus, KEY)` for initial
focus. The corresponding `declareStates` entry consumes two physical rows.
Both forms initialize the reported fact to none. Initial focus is a request
initializer: it waits for the first eligible completion. Declaration does not
materialize storage early; a constructor `post` is refused while its handles
are invalid. Post from handlers after connection. This is the owner's
implementation clarification to the frozen [#960 ruling](https://github.com/SimplySmirkySoftware/Loka/issues/960).

The declaring owner must be an ancestor in the same Scene as the borrowing
fields. A component's ordinary states remain backed by its Boundary; retiring
the component does not independently reclaim those states. Generation states
follow their generation owner. No request crosses a Scene replacement.
Debug completion audits both seat trackers against the current Scene's owners
before reading a row's request. Root-owned facts cost O(1) per audited row;
nested owners may require a tree walk, O(M*N) worst case for M members and N
Scene nodes. This diagnostic has no release-build walk or registry. In release,
forbidden ownership remains an application contract violation; the audit is
not a general stale-pointer revocation mechanism.

After Window's existing structural gates, the existing focus phase covers the
entire operation (the historical `isPublishing` name is retained):

1. Read native focus for admission; a declined/inactive read leaves requests alone.
2. If native focus moved between two fields of the same fact, clear that fact's
   request (D1). A move to another fact does not clear it.
3. Walk this Scene's membership for the first attached, projected row whose
   request matches its key. Copy the binding to the stack, then consume once.
4. Re-look-up by the copied binding identity, independent of the now-empty or
   newly posted slot. A retired/rebound target refuses the consumed attempt.
5. Apply native focus, re-read, and publish the observed result through the
   existing reconciliation. The native return value is not publication authority.

D1 continues into step 3 without an extra read. Clearing can notify, so the
walk also revalidates the saved admission identity before using that
observation if no request is taken. There is no pointer dereference across the
clear. A request taken in step 3 is bounded to one per completion; a repost
during consumption/application/publication waits for a later completion.
The phase blocks nested completion, admission/swap and close, but does not
prevent a callback from running its own Scene synchronously.

`applyNativeFocus` defaults to refusal. Null checks an attached participant
belonging to the controller's Scene, connects its source link, and compares
the subsequent read with the copied context identity.
All rails must copy handles/identity before a callback-capable native call and
never dereference the context afterward.
Null's confirming read adds a read inside its write door to common admission
and readback. No normal-path allocation or retained candidate is added.

An absent LazyView row or parked Show field is deferred until it appears and a
later completion runs. An inactive window does not take or activate itself.
An attempted native refusal consumes the request with no retry (D2). A clipped Toolbox field has no TE or usable fallback hit, so its request is
refused and consumed. Win32 and macOS accept a clipped control even when the user
cannot see it.
There is no reply, autonomous wake-up, timeout or fairness guarantee between
facts. Membership order is reverse attach order; one fact per screen is the
convention. On Win32 a post made inside completion may wait for the next
message. A never-appearing key can remain pending indefinitely; reusing a key
can cause a newly created field to take that pending request.

`post` costs O(1) plus owner tracker/observer work. Completion costs above are
framework work excluding callbacks and native calls. Host measurements belong
in the implementation evidence; no 68030 timing is inferred from host results.

### Toolbox write door (PR b)

Toolbox uses `IsAttachedCurrentContext` from `Node.hpp` for both focus
application and Tab candidates: the owner must still project through that
context and be ATTACHED. Retired context rows are erased synchronously; this
predicate is queried only for rows the rail still holds, never deleted contexts.
Its O(1) check runs once per focus application and once per Tab candidate,
unchanged from the previous inline checks.

Toolbox resolves the context in its own native edit ledger before changing
focus. The window must be frontmost, the context current and attached, and the
projected row used, nonempty and backed by a TE. EditText and TextEditor share
the click path's activation step: deactivate the old TE, change the ledger's
focused position, cut the fallback source, and activate the target TE. The
step sets the target window's port around TextEdit activation/deactivation and
restores the caller's port. A post never calls click, caret, text or scroll
operations; completion re-reads actual focus before publishing.

An EditText without a TE can accept a post through a current, nonempty hit in
that controller's fallback ledger (D3). It clears native TE focus and connects
the existing fallback source; absent a usable hit it refuses without disturbing
the old focus. Native activation cuts that source, so later TE retirement does
not revive a former fallback destination. Existing fallback editing limits
remain unchanged. Plain TextEdit activation has no Loka callbacks; the write
door copies its native handles/source before the calls and never reads the
context afterward.

The write door runs at most once per taken request: O(E) over its controller's
native edit rows, then O(H) over its fallback hits when no native row exists.
The shared activation step runs up to two overlap repairs, each O(U) over this
controller's unit ledger. Each overlapping unit appends a window damage rect
(`push_back`, which may allocate); activation adds no retained state.
Host pins cover activation, refusal, key delivery, selection/scroll preservation
and port restoration; Classic runtime verification belongs to the MAME rig.

### Toolbox press door (#1493)

`ToolboxInputDoor::mouseDown` selects one pressable row in reverse render-reach
order from the controller-owned `ToolboxPressLedger`. Later rows are on top;
a live button above a still-visible parked row receives the press. Ledger rows
contain only kind and index into the existing native, hit, or unit registry.
Render appends into STAGING and swaps it into LIVE only on completion. Both
refusal checks precede ephemeral hit/unit clearing, so a refused render keeps
LIVE and its indexed rows. Individual erases forget the row and shift later
indices of the same kind in both lists; bulk teardown clears both lists.

Selection resolves the owner and native part once into an event-local
`ResolvedPress`. The owner question uses `IsAttachedCurrentContext`; viewport
bars use their resolved ScrollView's lifecycle fact. A parked row stops the
press without action, tracking, focus change, or fall-through. An opaque
ZStack unit's empty area and disabled/inactive pressable controls also stop it
and preserve focus. Disabled native buttons previously fell through and could
clear focus; shielding them is an intentional change. Ownerless rows and rows
without an action (buttons without emitters, cells without onClick, and popups
without items) are transparent. Native visibility uses `contrlVis`, never
`usedThisFrame`.

Native dispatch leaves focus alone; EDIT_CONTROL activates its native edit;
EDIT_HIT connects fallback focus after native deactivation. Popup, cell, drawn
button, and empty-space dispatch first clear native and fallback focus.
Native dispatch returns false, edit dispatch returns true, and hit dispatch
returns its context handler's result; the result is not a universal handled bit.

Cost: one push per reached row (normally tens) plus one swap per render.
Render-reach helpers resolve IDs/contexts through the controller's existing
registries; those bounded searches do not run on press. Each press walks LIVE
backward to the first pressable row, resolves viewport bars through the existing
barId H x V search once per eligible candidate, asks one owner question, and
dispatches once. Selection allocates nothing and walks no foreign rows. Erase
performs one forget over LIVE/STAGING (tens), shifting that kind's later indices.
Existing dispatch may enqueue damage or perform an allocating app action.

TextEditor withdraws its edit row and hides/revokes its bar when the parked
fact is delivered; subsequent presses there see empty space or an underlying
row. Before delivery those rows refuse the press. A retired unit keeps shielding
until the next render, matching its remaining pixels; its stack identity is
never dereferenced for input. Native painting outside the render walk
(HideControl, HiliteControl, TrackControl, and kind-ordered control redraw) can
differ from ledger order. There is no window-wide pending-redraw gate.
Hover (`isPointInEdit`) and wheel routing are separate follow-ups; Win32 and
macOS already use native targeting and need no equivalent change here.

Keys follow the same rule. A focused edit row keeps native focus until its
sweep (TextEditor: until the parked fact is delivered and the row withdraws),
so `handleKeyDown` and `handleScrollKey` first ask the same predicate of the
focused row's owner; a parked owner consumes the key and nothing changes. The
cost is one predicate per entry reached, at most two per key event. A fallback
field needs no check: leaving ATTACHED cuts its focus source, so its keys
decline and go on as if nothing were focused. Idle still runs on the parked
focused row until it goes: an EditText caret keeps blinking on its
still-visible pixels, and a TextEditor's idle tick runs until delivery.

Post-paint admission can apply props/text before the next draw, as can
TextEditor idle destRect/scroll changes; an area erased by a closing modal waits
for an update event. Render early returns can retain rows without pixels, a
pre-existing limit that the window-wide gate never covered.

### Win32 write door (PR c)

The root HWND must exist and be this thread's active window. The context must
have an owner, be that owner's current context, and be ATTACHED. The shared
`Win32FocusParticipant::target` resolver maps EditText and TextEditor contexts
to HWNDs for both this door and `WM_ACTIVATE` reactivation restore. Restore
keeps its existing mark check. The write door additionally requires a child
of its own root and a matching context mark; any admission failure returns
false without changing focus or activating a window. A disabled HWND needs no
check of its own: Windows does not move focus to it, so the readback refuses.

The door copies the HWND before `SetFocus`. IME commit
can re-enter through `WM_KILLFOCUS`, `EN_CHANGE`, a text State write and a
synchronous Scene run, retiring the target context. After the native call,
it never dereferences that context: it compares `GetFocus()` with the saved
HWND. Which context, if any, that HWND still represents is the common
readback's question, not the door's. `SetFocus`'s return
is the previous handle, not a success result. Common completion re-reads
native focus before publishing; a retired, unmarked HWND can publish none.

A post changes only native keyboard focus: it does not issue text, selection,
scroll or activation operations. There is no clip check or scroll-to-reveal:
a clipped or scrolled-out control can receive focus although the user cannot
see it (D2), unlike Toolbox's refusal when no usable TE/hit exists. A refusal
after take consumes the attempt. A post made inside completion waits for the
next message when the loop reaches `WaitMessage`; it does not wake the loop.

Initial focus is declared with `this->state(focus_, KEY)`. Its request is taken
at the first completion after activation. The first `WM_ACTIVATE` has no
published focus, so no restore competes with that request.

The door runs at most once per taken request and the resolver once per write
or activation restore. They walk no framework rows and allocate nothing;
`IsChild` checks native ancestry under this controller's root. Win32 pins cover
posts to both field types, initial focus, inactive retention, mark/disabled/
foreign-controller refusal, preservation and kill-focus retirement. Their
runtime verdict belongs to the Windows VM; Linux suites do not compile them.

### macOS write door (PR d)

The root view must belong to a key window; the door never makes it key. The
context must be its attached owner's current context. EditText resolves through
`nativeField()`; TextEditor resolves through the narrow const `nativeFocusView()`
accessor to its document text view, never its scroll container. The native view
must belong to this window and its class-checked `fromNativeFocus` round trip
must return the supplied context. All admission checks remain active in release.

The door copies the window and view before `makeFirstResponder:`. Resigning can
commit marked text through `controlTextDidChange`; TextEditor selection delegates
can enter `handleSelectionDidChange` and `moveCaret`. These callbacks can run the
Scene and retire the context, so the door never touches the context after the
call. It returns AppKit's BOOL success; common completion re-reads the actual
responder and alone decides publication. No new retain or restoration state is
introduced. Cost: at most one call per taken request per completion, constant
resolution work, no framework rows walked; native and observer work is excluded.

A post requests only first responder, without text, selection, scroll or key
status operations. A target that already has focus is left alone: making an
`NSTextField` first responder again selects its whole text (observed on Tahoe).
A clipped control can become first responder while invisible
(D2), unlike Toolbox's missing-TE refusal; there is no automatic reveal. Native
refusal after take consumes the request without retry.

Initial focus uses `state(focus, KEY)`: the first eligible completion after the
window becomes key takes it, moving focus from whatever responder AppKit chose
first (the content view in the Tahoe pin).
Legacy `captureFocusedEditField` / `restoreFocusedEditField` remain unchanged.
The rail builds no key-view chain: each window sets
`autorecalculatesKeyViewLoop`, so Tab and Shift-Tab follow AppKit's geometric
loop and reach the fact through the same completion read (#1461). The content
view accepts first responder, so the window's keys reach it while no field is
focused, but it is no key view: the loop wraps from the last participant
straight to the first. Ordinary pending relayout capture/restore precedes focus
completion; the one-shot post is not a lock against later restoration. The
macOS pins cover initial ordering, native snapshots, admission, and an
end-editing text-bridge observer that retires the target. These pins require
Tahoe runtime verification; Linux suites do not compile this rail.

### Toolbox Tab traversal (#1461)

Tab visits EditText rows in geometric order `(rect.top, rect.left, ledger index)`;
the index breaks exact ties. Shift-Tab reverses that order and both directions
wrap, including a single field wrapping to itself. Landing selects all text
using its explicit TE length. With no native focus, including fallback focus,
Tab starts at the first field and Shift-Tab at the last. Shift-Tab's no-focus
start is not pinned on other rails and is not a cross-rail parity claim.

Candidates are current, ATTACHED EditText owners with used, nonempty native TE
rows intersecting the window port. Fallback hits and TextEditor rows never join
this traversal; when a TextEditor has focus, Tab remains text input. macOS's
key-view loop includes TextEditor. With no candidate the existing key path runs.
With eligible fields, Tab no longer reaches the app's `onKeyPress`, even when no
field has focus. Traversal is defined over `editControls_`; future focusable kinds
must join that ledger or extend traversal.

The body writes neither focus box. Completion (`ToolboxApp::present` through
`reconcileFocus`, `FocusPublisher::complete`, and `readNativeFocus`) publishes the
landing field. A same-turn post is consumed when a previously published field
of that fact has moved natively; with nothing published yet, the post wins.

Cost: ToolboxApp key-down (`keyDown` and `autoKey`) calls once per Tab. Search is
one O(E) walk over this controller's own `editControls_`, with no allocation.
Moving runs up to three `invalidateUnitOverlap` calls, each O(U) over this
controller's unit ledger; each overlapping unit appends one window damage rect
(`push_back`, which may allocate), repainted at completion. No foreign rows.

Known limits: immediately after a modal closes, one Tab may use the previous
layout, though parked rows are excluded. A field fully covered by a ZStack unit
remains a target and its selection highlight can stay painted over the unit,
the same unsupported case as clicks. Tab does not resynchronize the keyboard
script to the landing font. If the app rewrites the field's State before the
next render, `syncEditTextFromState` moves the caret to the end and loses
select-all. Typing over the selection retains the existing installation and
round-trip checks and their refusals. Host evidence covers the traversal;
`helloworld bmi-tab` is staged for real ADB/MAME verification, not an approved
runtime verdict.
