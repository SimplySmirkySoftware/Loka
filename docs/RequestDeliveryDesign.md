# Request delivery design

How an application asks a rail to change something it does not own (the caret
of a native text control today; selection, focus and scroll position later),
and how the rail answers. The app-facing description is in the
[Programming Guide](ProgrammingGuide.en.md#reported-facts-and-requests); this
page holds the contract the rails implement. Source rulings: #873 (frozen
2026-09-22), the two-take bound accepted on #874, and the entry-completion
ruling from the #879 review (2026-09-23).

## Boxes

| Box | Type | Writer | Reader |
|---|---|---|---|
| fact (`cursor`) | `Reported<LineCursor>` | the node's commit seam only (`TextEditorDocument::moveCaret` / `applyReplace`) | app, through `state()` |
| request (`moveCaretTo`) | app-owned `Request<LineCursor>` (a private `NodeState<LineCursor>`) | app (`set`), rail (last-wins take: snapshot, then `None`) | rail |
| queue endpoint ([#892](https://github.com/SimplySmirkySoftware/Loka/issues/892)) | app-owned `RequestQueue<T, N>`: slot + reply + fixed ring | slot: app `post`, binding `consume`/`cancelFrom`; reply: rail; ring: endpoint `post`/advance/clear | slot: rail; reply: app; ring: endpoint only (rails never see it) |
| reply (optional, `RequestWithReply<LineCursor>::reply()`) | `Reported<Reply<LineCursor>>` inside the request handle | rail, once per take, forced publish | app |

`Reported<T>` has no `set` and no conversion to a mutable handle; Props extract
its write seat through `NodePropsBase::reportSeat`, and the request and reply
seats through `NodePropsBase::requestSeat` / `replySeat`; the Props door has one
overload per handle type and refuses a bare `NodeState`. Facts are named as
nouns (`cursor`, `offset`), requests as verbs (`moveCaretTo`, `scrollTo`).

## Settle (#882)

The EventCall contract is frozen in [#882](https://github.com/SimplySmirkySoftware/Loka/issues/882).
Its implementation series is [#883 (a, common/Null)](https://github.com/SimplySmirkySoftware/Loka/pull/883),
[#884 (b, Toolbox)](https://github.com/SimplySmirkySoftware/Loka/pull/884),
[#885 (c, Win32)](https://github.com/SimplySmirkySoftware/Loka/pull/885), and
[#886 (d, macOS)](https://github.com/SimplySmirkySoftware/Loka/pull/886).
The authoritative common sequence is in
[`RequestSettlement.hpp`](../common/app/scene/state/RequestSettlement.hpp);
[`Request.hpp`](../common/app/scene/state/Request.hpp) defines the request/reply boxes.

A rail context settles once per admitted live operation: attach, props apply,
one input action, restore/retry completion, or one deferred callback. Settle is
an operation-completion door, the platform counterpart of a tracker commit.
Nested observations (delegates, selection notifications, `EN_CHANGE`) belong
to their owner's operation; a deferred callback starts a new operation.
Cancelled operations never call a retired context. Shared helpers such as
`project`, `replaceProjection`, and `restoreCommittedProjection` return their
follow-up decision to the owner; they never settle or take requests themselves.

One settle is one entry operation with one `SettleOwner<Fact>` and seats in a
fixed order. The owner supplies `finishSettle` once and, under `TEST_BUILD`,
`fact`; each `SeatOperation<Request, Fact>` supplies the seven take doors below.
`RailOperation<T>` combines the caret seat and settle owner, inheriting
`SeatOperation<T, T>` and `SettleOwner<T>`.
`RequestSettlement<Fact>` builds `SeatRunner<Request, Fact>` values and their
pointer array on its own stack and walks them; each runner holds its binding
by value and, under `TEST_BUILD`, its trace row; `FollowUps` stays local to the
driver. The existing
single-seat entry, a single heterogeneous-seat overload, and a two-seat
overload share that walk; TextEditor uses the two-seat overload for caret then
command (#902).
Sources: [#898, ruling items 1–3](https://github.com/SimplySmirkySoftware/Loka/issues/898),
[#900](https://github.com/SimplySmirkySoftware/Loka/pull/900), and
[#904](https://github.com/SimplySmirkySoftware/Loka/pull/904).
The non-template `SettleOwnerBase` opens a `BorrowScope` on the rail's
controller before native calls or callback-capable work. It borrows the node
and stores a comparison-only context identity. All settle overloads require
that owner; `hasSameContext()` checks identity at each seat entry, after
publications, and after `finishSettle`. This is not an ATTACHED predicate: a
parked context can still match. A mismatch stops later seats, the epilogue and
trace. Lifecycle eligibility remains the seat's separate check.

### Platform operation interval (#968)

Window owns the controller; Scene and contexts borrow it. The controller owns
one small noncopyable `BorrowPhase`; only `BorrowScope` (formerly `OperationScope`)
opens it and restores the previous value. The policy openers are
`SettleOwnerBase`, the projection-visit owner, and the rail input door described
below; the scope constructor remains public for fixtures. There is no counter, allocation, flush on scope
exit, or coupling to the focus publication phase. A non-template completion
owner carries this interval independently of the fact type. A controller-less
fixture door is available only in `loka::app::testing` under `TEST_BUILD`.

While an interval is open, its controller remains alive, a mounted Scene keeps
that controller, and the borrowed node belongs to that projection. Public
`Scene::mount` returns false without changing anything on a remount of the
same mounted Scene or a mount on an open controller. A prepared old/new Scene
pair sharing a controller is legal. Refusal also asserts in lifecycle-audit
builds. Owner/test-only unmount, direct projection/teardown and native drain
must occur outside the interval; audit assertions diagnose misuse. Controller
destruction asserts that its phase is closed. These trusted doors do not gain
an arbitrary-callback lifetime guarantee.

State writes, observers, Flow work, request consumption and replies still run
immediately. The controller's Scene cannot start a draining run, and Window
cannot synchronize, drain native retirements or complete focus. App admission
skips that window's whole busy row: native visibility, Scene replacement,
dialog-result delivery and close reclamation wait too. Other controllers are
unaffected. Pending work remains on its existing clock until a later flush.
A projection-visit scope follows the same rule. Inside a Scene run it defers
Window synchronization and native-retirement draining. Outside one (for example
the layout of a mount) it also refuses the immediate Scene run that a viewport
publication asks for. Both wait for the next ordinary flush; scope exit does not
flush.
Focus publication alone still permits a Scene run. Layout reached from native
callbacks also requires a borrow interval: it publishes node facts (for example,
`RectSurfaceNode`'s laid-out extent), even when it does not run the Scene.
This includes Win32 size/DPI layout, macOS resize and deferred relayout, and
Toolbox update/draw/render layout and extent publication. A pending-layout map
walk invokes each controller's step through its own door, not the whole walk
under one controller's scope. This corrects #968's native-only relayout
rationale; production rail routing follows in the #977 rail PRs.

This changes **input completion** to the rule props-apply completion already
follows: a reply observer that hides the editor does not retire it inside the
borrow interval. Later takes in that same interval continue; focus facts and #932
participation remain until the post-scope run. A hide/show pair that cancels
inside the interval need not retire anything. Outside an interval, ordinary
immediate writes keep their existing behavior.

Win32 performs one additional window flush after focus completion, without a
new continue. The bound is two scheduled tail flushes, not a bound on reentrant
calls or message draining. The second flush advances admission clocks and can
retry a failure once more in the same loop. Existing admission continues and
idle transitions remain; work produced by the second flush can still wait for
the next message. macOS and Toolbox cadence is unchanged.

#### Input-operation door (#977)

The frozen [#977 ruling](https://github.com/SimplySmirkySoftware/Loka/issues/977)
(2026-09-28, N1–N9) defines the intended all-rail policy below. **Implemented
coverage as of 2026-09-28 includes Null, Win32, macOS and Toolbox**: Null ScrollBar complete
gestures and split input entries, plus the TextEditor input adapter, use
`NullInputDoor`. Win32 command inputs (Button, EditText, PopupMenu, TextEditor),
Cell clicks, ScrollView vertical scrolling (including the whole wheel loop), TextEditor
subclass input/retry, and root size/DPI layout use `Win32InputDoor`. These doors
use the internal `scene::detail::InputInvocation` mechanism. The Win32 pins
(`tests/Win32InputDoorTests.cpp`) are runtime-verified on the Win32 VM rig as
recorded in the #977 Win32 PR. macOS Button, EditText, PopupMenu, Cell
(including its native continuation), ScrollView bounds notifications, TextEditor
delegates/deferred selectors, window resize/move and each pending relayout step
use `MacInputDoor`; its pins (`tests/MacInputDoorTests.mm`) are runtime-verified
on the macOS rig as recorded in the #977 macOS PR. Toolbox controller mouse/key
operations (including tracking and commit), render/layout, dirty-render
fallbacks and idle TextEditor retries use `ToolboxInputDoor`. Its Linux host
pins (`tests/toolbox/host/ToolboxInputDoorHostTests.cpp`) exercise production
operation bodies with substituted OS calls and geometry traversal; they do
not establish native Classic timing or layout accuracy. Toolbox evidence and
native verification limits are recorded in the #977 Toolbox PR.

#580 PR 2 extends the Win32 ScrollView input door to `WM_HSCROLL` and horizontal
wheel messages (`0x020E`, including the whole wheel loop). Its added horizontal
pins in `tests/Win32InputDoorTests.cpp` and `tests/Win32ScrollViewTests.cpp` await
Win32 VM runtime verification; the historical #977 evidence above does not
cover these additions.

Each rail has one synchronous scope-owning invocation implementation, exposed
through named typed entries. It opens `BorrowScope` (formerly `OperationScope`) on the
borrowed controller unconditionally and covers the complete borrow interval and its
continuation: a wheel loop, a Popup write followed by its change emit,
Toolbox tracking plus commit, a TextEditor saved-procedure call, or a macOS
Cell emit followed by `super mouseDown:`. Existing `SettleOwner` scopes nest
unchanged. There is no new phase, counter, lease, registry, reverse pointer or
settle family.

Native adapters recover the receiver through their existing live back-pointer
and call the door. Null or retired receivers take the adapter's exact existing
no-op or native return without reaching an input body. An adapter must not
access its receiver after an invocation that may retire it. Context input
bodies are private and the rail door is their supported caller. A compile pin
with a same-includes accepting twin proves that an external caller cannot call
a private body; it does not prove correct controller pairing, continuation
coverage, or that trusted context/door implementation never calls a body
unscoped. Those are shape-review and runtime-pin obligations. An optional
lifecycle-audit phase assertion is supplementary; the scope itself is always
on. Generic `WriteSeat::set` gains no input-phase assertion.

The door returns **without pumping**. Scope exit only restores the previous
phase; held Scene/owner work advances at Win32's `flushIterationTail`, macOS's
`flushInvalidationsTick`, or Toolbox present. An outermost scope exit does not
prove that all borrowers have returned: the caller can still be an immediate
projection binding, a native continuation, or a pending-layout map walk.
For a routed entry, existing request settlement stays inside the invoked body
and completes before invocation returns. Other entry settlements keep their
existing `SettleOwner` scopes and delivery sites. Door return is not a new
request-delivery site; the delivery-site inventory below is unchanged.

Owner operations retain their regimes: App admission/close/dialog-result
delivery, menu and idle pumps, dialog transport, window creation before a
controller exists and teardown after it is gone, and pure native stretches
such as paint-only drawing or immediate native fallback. A controller-borrowing
suboperation reached from any of these goes through that controller's door
(for example a menu command writing a context's node or idle relayout).
The criterion is the borrowed controller and its continuation, not whether a
State write occurs somewhere down the stack. #968 locality, gates,
modal-holds-until-return and owner preconditions remain in force.

Two consequences require pins on each rail that has the corresponding path:

- During a layout with two RectSurfaces, the first extent observer hiding the
  second no longer cancels the second's pending extent row in that same layout:
  detach waits for the pump, so the second fact may publish before detachment.
  Pin first-hides-second and hide/show cancellation within one borrow interval.
- During Win32 move/size tracking, structural work requiring held Scene/owner
  pumps may wait until tracking returns and the queued-message drain reaches
  `flushIterationTail`. State observers and direct native bindings remain
  immediate; native layout can proceed while an extent-driven Show waits for
  a Scene run. This accepted drag tradeoff and its native repaint/resize timing
  probe are tracked in [#984](https://github.com/SimplySmirkySoftware/Loka/issues/984),
  not runtime-verified here. Live structural updates, if required, need a
  separate Win32 owner-level native-loop completion design. Other rails need
  no symmetric workaround: macOS registers its timer in default and common
  modes, and Toolbox calls `SizeWindow` after `GrowWindow` returns; their
  native timing still requires verification.

The seven seat doors occur in this order; failed stages skip dependent work,
not the completion path of a still-live take:

1. `admit` re-evaluates phase, status, and ownership for each take; eligibility
   is not a cached input. Binding extraction on deferral follows the rail
   gate contract described under [Command seat](#command-seat-902).
   Empty/deferred admission skips the take, but still reaches the epilogue.
   On admission, `consume()` snapshots the request, then clears a last-wins slot
   to `None` or advances a queued slot directly to its next value (or `None`
   when empty); then check liveness ([#893](https://github.com/SimplySmirkySoftware/Loka/pull/893)).
2. `resolve` checks binding currency. Failure is a refusal, not an early return
   that strands the phase.
3. `validate` asks the seam whether the requested value is valid.
4. `apply` performs the native write, clamp, or repair, returning a
   `RequestApplication<Fact, RequestTraits<Request>::Reason>` and follow-up
   decision; check liveness afterward.
5. `report` asks the seam to commit the applied value, then checks liveness.
   Build the reply from that seam result, not native apply success alone.
6. `current` checks the captured binding before publishing the optional reply.
   Publish once per take with forced update, then check liveness. A discarded
   binding receives no reply; an overwritten request was never taken and gets none.
7. `finishTake` receives the reply and original application result, performs
   post-report repair and conditional phase restoration, and returns a follow-up.
   Check liveness; the next admission reads the resulting phase afresh.

The owner's `finishSettle` runs once after both ordinary take opportunities
for every seat, folding `FollowUps` from `apply` and `finishTake` into the
completion decision.

Each request type whose reply or settlement is instantiated names its reason
through `RequestTraits<Request>::Reason`, `accepted()` and `unavailable()`.
The fact type needs no trait. Editor request specializations live in
`TextEditorRequests.hpp`; both keep `EditorResult` and its existing values.
Plain requests (including Focus) require only `coalescable`, not a reason.

The `formReply` overloads deduce their argument types: the same-type overload
keeps `Granted`/`Clamped`/`Refused` selection; different request and fact types
yield `Refused(pending, result)` on failure or `Granted(pending)` on success.
Replies remain request-typed; #902 adds request/fact-typed trace rows, as
described below.
Sources: [#898, ruling item 5](https://github.com/SimplySmirkySoftware/Loka/issues/898),
[#900](https://github.com/SimplySmirkySoftware/Loka/pull/900).

`Granted` means the seam accepted that take, not that subscribers left the fact
unchanged afterward. `Clamped` records requested and applied values; `Refused`
records the reason and leaves the fact unchanged. Replies are not dirty sources.
If native apply succeeded but report refused, `finishTake` restores the native
selection/projection from the committed fact before reopening native admission.
Refusal before a successful native apply does not itself require a native write.

`FollowUps` combines `FOLLOW_NONE`, `RESTORE_QUEUED`, `SCHEDULE_RESTORE`,
`SCHEDULE_HIGHLIGHTS`, `OWNER_FOLLOWS`, `SCROLL_CLEANUP`, and `REPAINT` decisions.
Phase transitions stay synchronous in the rail; timer/selector arming belongs
in the epilogue, while cancellation/coalescing stays in the rail. Win32's
settle-scoped RETRY + UNAVAILABLE conversion belongs in `finishSettle`.
Repaint is requested only after a native write or repair; an empty request
slot alone does not justify it.

`finishSettle` returns `FOLLOW_UP_ARMED`, `FOLLOW_UP_FAILED`, or `FOLLOW_UP_NONE`.
A failed arm adds at most one refusal-only take per seat from that seat's last
admission's still current, nonempty binding: `consume()` (last-wins clear or queue advance),
check liveness/current binding, and publish
`Refused(requested, RequestTraits<Request>::unavailable())`. It does not resolve,
apply, report, or write the fact. The driver returns the arm result after this tail,
so native admission stays closed until refusal publication finishes; retirement
returns `FOLLOW_UP_NONE`. A binding discarded during consumption gets no reply
([#893](https://github.com/SimplySmirkySoftware/Loka/pull/893)).

Under `TEST_BUILD`, `testing::SettleTrace` stores a fixed-capacity history of
value rows: stimulus, admissions, take results, seam results, and fact delta.
Rows are per seat: a later seat contributes only if it took a request; its
untaken fact delta folds into the preceding candidate. Seat 0 is always a
candidate, emitted only for a take or fact change. The failed-arm
refusal shares that seat's row (at most two ordinary takes plus one refusal-only
take). Seat 0's `before` is the caller's pre-entry snapshot, never a fresh sample
at settle entry; later seats sample their entry fact after the preceding seat's
publications. `after` finalization and row append happen after the epilogue and
the failed-arm tail: each contributing candidate's `after` is the fact captured
at the next contributing candidate's entry, and the last candidate's `after`
is the owner's final fact.
Untaken later seats fold backward rather than creating rows (#902 item 8).
Sources: [#898, ruling item 4](https://github.com/SimplySmirkySoftware/Loka/issues/898),
[#900](https://github.com/SimplySmirkySoftware/Loka/pull/900), and
[#903](https://github.com/SimplySmirkySoftware/Loka/pull/903).
Golden records are grouped per scenario step under each rail's PNG approval;
timer/retry counts are not golden expectations.

The bound is per seat: **two ordinary takes (each may refuse) plus at most one
failed-arm refusal-only take**. With one seat this is exactly the #892 bound,
unchanged. TextEditor's two seats have an aggregate bound of four ordinary
takes plus two failed-arm refusal-only takes (#902 item 6).
Sources: [#898, ruling item 7](https://github.com/SimplySmirkySoftware/Loka/issues/898),
[#900](https://github.com/SimplySmirkySoftware/Loka/pull/900).

The sourceless `RequestBinding<T>` seat-only constructor, including its default
reply argument, instantiates `RequestDeclarationWall<T>` and refuses
non-coalescable or unspecialized types. Queued bindings pass the queue by
reference alongside the request and reply seats borrowed from that same queue.
Sources: [#898, PR w](https://github.com/SimplySmirkySoftware/Loka/issues/898),
[#899](https://github.com/SimplySmirkySoftware/Loka/pull/899).

## Endpoint and queue (#892)

The endpoint ruling is [#892](https://github.com/SimplySmirkySoftware/Loka/issues/892),
implemented by [#893](https://github.com/SimplySmirkySoftware/Loka/pull/893).
`RequestQueue<T, N>` supplies fixed ring storage to the non-virtual
`RequestQueueBase<T>`, which owns the slot and reply handles. It is an app-owned
endpoint with one consumer; the rail receives a `RequestBinding<T>` and never
sees the ring. `pending()` is ring occupancy, excluding the published slot.

`RequestBinding::consume()` snapshots the slot, commits the ring head/count,
then publishes the next queued value directly, forcing publication even when
it equals the taken value. Only an empty ring publishes `None`; a plain
last-wins binding always clears to `None`. It touches no endpoint storage after
publication and returns the local snapshot. A synchronous subscriber's post
therefore follows older queued work. Both settlement sites use this door:
the ordinary take and the failed-arm refusal-only take. Cancellation uses
`PendingRequests::cancel` across only the selected seats, as described
under [Command seat](#command-seat-902). A cancellation subscriber's repost
consequently enters the emptied endpoint. The caller is
`TextEditorNode::discardPendingRequest`, on binding change or detach.
`PendingRequests` holds the cancellation friendship; `SeatRunner` retains
consumption access, and nodes do not access the private binding primitives.
Sources: [#892](https://github.com/SimplySmirkySoftware/Loka/issues/892),
[#893](https://github.com/SimplySmirkySoftware/Loka/pull/893).

`post()` returns `POST_ACCEPTED`, `POST_QUEUE_FULL`, or `POST_INVALID` (`None`);
refused posts change neither slot nor ring. `POST_ACCEPTED` promises one reply
per taken post; caret priority can delay commands indefinitely (#902 item 6).
Discarded slot and ring entries receive no reply; cancellation loses reply
correlation and provides no exact dropped count. The bound is per seat: **two
ordinary takes (each may refuse) plus at most one failed-arm refusal-only take**.
Remaining work gets its next delivery opportunity at the next props apply
within the same flush, subject to admission and caret priority, using slot
publication to mark props dirty, without a queue timer.
Source: [#892](https://github.com/SimplySmirkySoftware/Loka/issues/892),
[#893](https://github.com/SimplySmirkySoftware/Loka/pull/893).

`RequestTraits<T>::coalescable` has no permissive primary; `LineCursor` opts in,
and `EditorCommand` explicitly opts out (#902 item 1).
`RequestDeclarationWall<T>` refuses command-like or unspecialized types on all
three plain declaration routes: `StateBatchBase::CreateImmediateState`,
`ComposableNode::state`, and `NodeStateBatch::state`. This covers both
`Request<T>` and `RequestWithReply<T>`; queue declarations bypass the wall.
The check is a negative-array `sizeof` in C++98 and also a `static_assert` in
C++11+. Cost lines: each plain binding gains one pointer; the settlement driver
pays one source null check per take. A queued take mutates its endpoint's ring
and forces publication when advancing to a queued value; discard walks only that endpoint's
waiting entries on binding change/detach.
Source: [#892](https://github.com/SimplySmirkySoftware/Loka/issues/892),
[#893](https://github.com/SimplySmirkySoftware/Loka/pull/893).

In [PR #894](https://github.com/SimplySmirkySoftware/Loka/pull/894), the Toolbox
`smirkbench/text-editor-plain` cell audit has `settle.<step>.<row>` rows
**recorded per scenario step** by `tests/scenarios/TextEditorSettleAudit.hpp`.
Rows carry the stimulus, take outcomes, seam results, and before/after facts;
only settles with a take or a fact change are recorded. Every input step in
that cell emits a fact-changed row, including steps with no takes. This is the
settle-trace golden record; desktop scenario registration remains a follow-up,
not a claim of Win32/macOS scenario golden coverage.

## Command seat (#902)

The command ruling is [#902](https://github.com/SimplySmirkySoftware/Loka/issues/902).
The implementation series is [#903 (a0, runner and trace)](https://github.com/SimplySmirkySoftware/Loka/pull/903),
[#904 (a, common/Null)](https://github.com/SimplySmirkySoftware/Loka/pull/904),
[#907 (b, Toolbox)](https://github.com/SimplySmirkySoftware/Loka/pull/907),
[#906 (c, Win32)](https://github.com/SimplySmirkySoftware/Loka/pull/906), and
[#908 (d, macOS)](https://github.com/SimplySmirkySoftware/Loka/pull/908).

### Residents and app door

`EditorCommand` has only `PAGE_UP` and `PAGE_DOWN` as executable residents;
`NONE` denotes an empty slot. These verbs accumulate: two posts mean two page
operations, so `RequestTraits<EditorCommand>::coalescable = 0` permits only
`RequestQueue<EditorCommand, N>`, not either plain request handle or a sourceless
binding. Standard Edit menu items first offer the action to the native editor;
when no native target handles it, the copied app binding runs once if present.
A native refusal or no-op counts as handled and does not fall back. App-owned
history, clipboard seams, and selection requests are separate future work.
Focus is separate page 3 work. Scroll-to-caret is idempotent and belongs to a
last-wins scroll request family, not this command queue (#902 item 1).

The Props door is `.command(queue)`, accepting
`RequestQueueBase<EditorCommand> &` into `command_`. It participates in Props
identity after `moveCaretTo_`, and its slot is a props dirty source. The reply
is `Reply<EditorCommand>`: `Granted(pending)` means the verb executed and
`Refused(pending, reason)` means it did not; this seat never produces `Clamped`.
The effect is the existing `cursor` fact, including native clamping; read that
fact to learn where the caret went (#902 items 2–3, PR a).

### Page target and rail pipeline

`TextEditorDocument::pageTarget(command, visibleLines, out)` computes a target
without publishing. It moves in logical rows by `max(1, visibleLines - 1)`,
saturates at either document end, and clamps the current column to the target
row's length (and at least zero), with no goal-column memory. Zero visible
lines or absent document/cursor storage returns `EDITOR_UNAVAILABLE`; a `None`
caret returns `EDITOR_INVALID_CURSOR`, and a stale line identity returns
`EDITOR_STALE_ID`. `NONE` is unavailable, and target-row decoding failures
propagate. Reaching an end can therefore execute successfully without moving
(#902 item 4, PR a).

Each rail's `bool queryVisibleLines(unsigned &out)` answers the count of fully
visible **logical** lines of the editor's unclipped frame, or declines; zero
lines declines. Height divided by line height is only a capacity when wrapping
is possible. Toolbox counts complete logical rows across TextEdit visual rows;
macOS measures whole logical rows including their layout fragments. Win32 uses
its formatting-frame height and font metrics with wrapping disabled. Null uses
a four-line fixture and a test decline mode. Cross-rail caret equality is not
claimed (#902 item 4, PRs a–d).

The rail owns `CommandOperation : SeatOperation<EditorCommand, LineCursor>`
and borrows its caret pipeline. Caret admission is split into binding extraction
and `enterTake`: check phase eligibility, endpoint non-emptiness and priority,
then open the rail's exclusion. Null and Toolbox open INPUT; Win32 opens COMMIT,
retaining its RETRY follow-up; macOS captures `completion_` and opens INPUT.
On phase deferral Null/Win32 supply the binding, while Toolbox/macOS do not.
macOS extracts its binding before the later caret-priority check, so that
priority deferral can still supply it. Resource and tracker refusals stay in
`resolve`, using the command endpoint's own identity and tracker checks
(#902 item 5, PRs a–d).

Reconcile before geometry on every rail: Toolbox uses `prepareApply`, Win32
and macOS use `ensureProjection`, so subscriber edits cannot leave geometry
and the logical target on different snapshots. Null reads the current model
and fixed fixture geometry without a native projection to reconcile. Then
query geometry, compute `pageTarget`, reuse native selection application,
report the native-clamped cursor through `moveCaret`, and pass a synthesized
`Reply<LineCursor>` to the caret `finishTake`. Selection and viewport work
remain under the rail's exclusion (#902 items 4–6, PRs a–d):

- Toolbox uses `TESetSelect` and `TEScroll` to reveal the target. A Granted page
  supersedes the saved viewport for `SCROLL_CLEANUP`, so cleanup from a refused
  key in the same settle cannot undo it; a refused report retains the old view.
- Win32 uses `EM_SETSEL` and an `EM_LINESCROLL` delta from the target row and
  first/last visible rows. Only on Granted does command `finishTake` refresh
  the selection/scroll cache before shared repair can call `restoreSelection`.
- macOS uses `setSelectedRange` and `scrollRangeToVisible` inside APPLYING.

### Priority and cancellation

One settle visits caret then command, with two ordinary take opportunities and
at most one failed-arm refusal-only take **per seat**. A command admission
requires the caret slot to be drained: invalid or `None` counts as drained.
Commands wait for the caret slot to drain; a continuous caret feed delays them.
If the slot stays nonempty at every command admission, that delay is indefinite;
`POST_ACCEPTED` promises one reply per taken post, not eventual admission
(#902 item 6, PR a and PRs b–d).

A document-list replacement or detach discards both seats; changing only one
request binding discards that seat. `discardPendingRequest` passes only those
seats to `PendingRequests::cancel`, command before caret. The helper first
copies all bindings into stack locals, clears all selected rings without
notification, and snapshots all slots (None for invalid bindings); then it
cancels in argument order. Binding replacement during a subscriber callback
cannot retarget the copied bindings. `cancelFrom(snapshot)` leaves a
changed slot untouched, preserving work posted by an earlier
cancellation subscriber. Otherwise it advances to a new ring head with forced
publication, or publishes `None` when empty. Cancellation therefore publishes
`None` or the next queued value. Same-seat reposts after `None` enter the emptied
endpoint (slot, then ring). A cross-seat post to a plain slot **equal to the
snapshot** is indistinguishable from discarded work and is cleared: this is a
known, pinned edge. Reposts are endpoint-relative; a repost into a replaced
endpoint is not delivered. Discarded work gets no reply or exact dropped count
(#902 item 7, `Request.hpp` / `TextEditor.hpp`, PR a).

### Per-seat trace

Under `TEST_BUILD`, `SettleTraceRow<Request, Fact = Request>` and
`SettleTrace<Request, Fact = Request>` type takes by request and before/after
by fact; `SettleTrace<LineCursor>` retains its name. The walk stamps `row.seq`
at append through `SettleTraceClock<Fact>`; `SettleTraceCapture<Fact>` resets
both histories and that clock together and reports overflow across them.
`TextEditorSettleAudit.hpp` merges caret and command histories by `seq`, using
the merged position in `settle.<step>.<row>`; command takes add, for example,
`seat=command requested=PAGE_DOWN` (#902 items 8–9, PRs a0 and a).

An untaken later seat contributes no row and folds its fact delta backward;
seat 0 is always the candidate, emitted only when there was a take or fact
change. Thus an epilogue or failed-arm tail that changes the fact cannot add
an empty command row to a command-free settle. A taken no-op command keeps its
row. Single-seat behavior and command-free golden records remain unchanged
(#902 item 8, PRs a0 and a).

## From AGENTS.md

- **Delivery sites.** A delivery site is the completion of an entry operation:
  a props apply, an attach, an input action's outer completion, or a retry or
  deferred completion that is itself the entry. A shared helper that several
  entries call (`project`, `replaceProjection`, `restoreCommittedProjection`)
  never takes a request; it returns an outcome to its caller.
- **Bound.** Per seat, two ordinary takes (each may refuse) plus at most one
  failed-arm refusal-only take, never another application. With one seat this
  is exactly the #892 bound, unchanged; TextEditor now has caret and command
  seats (#902). Pending work stays in the slot
  or endpoint ring; the consumption publication (last-wins `None` or direct
  queue advance) marks props dirty, so the next props apply offers delivery
  subject to admission and caret priority.
  Sources: [#892](https://github.com/SimplySmirkySoftware/Loka/issues/892),
  [#898, ruling item 7](https://github.com/SimplySmirkySoftware/Loka/issues/898),
  [#900](https://github.com/SimplySmirkySoftware/Loka/pull/900).
- **Refusal.** A request that cannot be applied (stale line identity, document
  unavailable, missing native resource) is taken and dropped; the fact stays
  unchanged. A last-wins slot reads `None` unless a subscriber reposted; a
  queued slot advances to the next value, or `None` when the ring is empty.
  Source: [#892](https://github.com/SimplySmirkySoftware/Loka/issues/892).
- **Binding change.** When the node's document binding changes (a different
  list or request seat in new Props, the end of the node's attachment), a list
  change or detach discards both seats; a single request-binding change
  discards only that seat before the new binding is installed. There is no
  replay of discarded work on attach; cancellation subscribers can post new
  endpoint-relative work through the two-pass protocol (#902 item 7).

## Where each rail consumes

Null: attach, props sync, input completion. Toolbox: `onPropsApplied`,
`finishInput`, native creation in `render`, `retryProjection`; layout settles
only when the TextEdit record exists, and before it render's attach settle
delivers (#1141). The Toolbox editor bar's `scrollInput` uses the existing
`finishInput` completion; `idleTick` and `syncPresentation` never take requests
([Toolbox editor bar](ToolboxTextEditorScrollBar.md)). Win32:
`syncFromNode`, outside-input `handleCommand`, the outer `WindowProc` after a
rejected input is restored, the RETRY timer handler. macOS: ordinary sync
completion, selection completion, `VIEW_CHANGE` completion, the deferred
storage-edit completion (`applyHighlights`), `UNAVAILABLE` refusal. These are
the entry completions governed by the settle contract above; shared projection
helpers are not additional delivery sites.

Null ScrollView consumes once after the reached `layoutScrollView` helper returns
at the ScrollView branch of `layoutNode`, including refused visits. Its request
box marks `NODE_DIRTY_LAYOUT`; each settle takes at most two requests, with no
failed-arm take because `finishSettle` returns `FOLLOW_UP_NONE`. Consumption
and origin publication may coalesce into the next placement pass. Children a
refused ancestor never reaches keep their pending requests.

Toolbox ScrollView consumes at the ScrollView layout arm of `LayoutNode` in
`ToolboxNodeDispatch.cpp`, after `layoutScrollView`, including refused visits.
Its request observes `NODE_DIRTY_LAYOUT`; one settle takes at most two requests,
with `FOLLOW_UP_NONE` and no failed-arm take. The visit borrow opens before the
helper and restores after settlement. It also covers render outside a Scene
run, under `ToolboxInputDoor::renderDirty`'s outer borrow, which remains open
when the visit closes.

Win32 ScrollView consumes at the ScrollView arm of `computeLayoutResult` in
`Win32ScenePlatformController.cpp`, after `layoutScrollViewNode`, including
refused visits. Its request observes `NODE_DIRTY_LAYOUT`; one settle takes at
most two requests, with `FOLLOW_UP_NONE` and no failed-arm take. The visit
borrow opens before layout and restores after settlement. For `WM_SIZE`/DPI
layouts it nests under the input door's outer borrow, which remains open when
the visit closes. Only macOS delivery remains for PR 4 of #1591.

## Focus requests (#960, common + Null)

Focus has its own completion and bound, described in
[Focus design](FocusDesign.md#app-posted-focus-960-common--null).
`Focus<K>` bundles a reported fact and a coalescable request; it does not
use TextEditor's settlement seats or reply channel. `state(focus, KEY)` supplies
an initial request, while `post(KEY)` overwrites a materialized bundle's slot.
The Window focus completion takes at most one request across its Scene,
consumes through a stack copy of the binding, re-resolves that identity, then
applies and reads back native focus. Missing/inactive targets defer without
consumption. A native refusal consumes without retry. A field's detach does
not cancel its ancestor's pending focus request. No slot observer supplies an
autonomous wake-up, and a post during the take waits for a later completion.

`DialogResultTransport` is the single Window-owned ledger behind the existing
`DialogResultDelivery` admission door. Its polymorphic entries carry file
(`OpenFileDialogProps` / `FileChooserResult`) or confirm (`ConfirmDialogProps` /
`ConfirmResult`) payloads; registration and return ports remain typed per
payload. Both share one finite delivery batch and the same synchronous
revocation and silent reclamation paths. Toolbox uses the same ledger for file and confirm results; its native modal
presentation is scheduled separately as described below.
revocation and silent reclamation paths. Confirm has no native presenter in
this leg; Toolbox retains the copied file-channel delivery described below.

### Toolbox file dialog presentation

`ToolboxApp::present` consumes pending file and confirm dialogs in its foreground
arm, after all window invalidation flushes and before focus reconciliation. The
App must be running, the window must have a Toolbox controller and no pending
close, and that controller's borrow phase must be closed. The foreground pass
walks the Window group in order and presents at most **one dialog for the whole
App**, taking the first eligible Window FIFO head (owner ruling, 2026-10-13,
Window rally 3b d2-3). The next pass selects again from the surviving enrollments.
Background and no-group presents leave rows pending. A head whose node is no
longer attached waits for lifecycle apply to unlink it.

Contexts own their FIFO enrollments and typed ledger registrations. Attach and
refresh reserve a registration before enrollment; props changes revoke the old
registration. A pending retarget enrolls the latest props; a consumed presentation
waits for reattach. Detach and destruction synchronously unlink and revoke.
Taking a row returns its presenter owner, which captures options and a typed
return port before running native code. No context is accessed after the modal:
the App cursor is reconciled and the stack port is sealed. The ordinary completion
admission at the tail of the same tick delivers through the common Window ledger.
There is no direct result delivery and no admission inside the FIFO walk. Close
and quit callbacks run before the next pass can present another dialog.

FIFO take, ledger reserve and seal are O(1); native presentation is at most once
per App present pass. The presentation selector walks its own Window group once
per foreground tick, with no extra snapshot allocation. Ordinary App admission
still discovers Window work and delivers O(k) entries for each admitted Window;
the tail adds no per-dialog group walk. Confirm uses hand-built Dialog Manager
items (Save, Cancel, Don't Save), bounded native-script message text, and seals
allocation refusal as `Error(memFullErr)` without retry.
