# Keyed declaration seats

> **Status:** Normative
> **Owns:** Declaration ownership and replacement sequencing for Keyed
> **Does not own:** API signatures, allocation implementation, or scheduler policy
> **Code truth:** `common/app/nodes/nestable/Keyed.hpp`,
> `common/app/scene/boundary/detail/BranchSeatDeclaration.hpp`,
> `common/app/scene/KeySnapshot.hpp`,
> `KeyedGenerationDeclaration` in `common/app/scene/boundary/GenerationRoot.hpp`,
> `BoundaryNode::replaceSeatBranch`
> **Verification:** Keyed contract pins in `tests/NodeMatchTests.cpp` and the
> MineSweeper scenario and ownership pins

A Keyed definition borrows its enclosing boundary and a member declarer. The
boundary must outlive the definition. Each committed declaration owns its key
snapshot, definitions, and nested seat plans. A fresh candidate owns those same
facts separately until materialization succeeds. A replacement retires its old
occupant first and waits for its node slots to return. The vacant interval is
observable; a refused candidate never restores the old incarnation. The surviving
request resamples the current key when it can build again.

Every keyed consumer follows the same rule: a committed result owns its key
snapshot (`KeySnapshot`). A consumer needs a new declaration if and only if
there is no committed result or its snapshot no longer matches the live key.
The driver (Boundary for seats, App admission for a document window set) owns the
candidate until it is complete. A failed candidate never touches the committed
result.

The seat window uses a separate NodeComposition and a runtime generation state
owner. States created inside a Keyed/LazyScope runtime generation use the tagged
heap state gate; a Section stays their logical owner; Boundary-lifetime states
and Sections outside such generations keep the StateArena. It never opens the
boundary's declaring window: boundary composition and bindings persist. Nodes created beneath the candidate still
open their ordinary binding windows at attach. The declarer supplies a root
with `c.declare`; the declaration encloses that definition in a Fragment so a
nested seat can switch its root without changing the outer seat's root identity.

Nested plans belong to their declaration scope. A scoped plan key refers to
that owner, so two declarations can reuse anonymous slots without aliasing.
Runtime rows remain in the existing boundary ledger. Candidate runtime rows
are staged until the outgoing subtree and its nested parked residents have
crossed the detach/retire line. Only then are the new declaration and runtime
rows published. No subtree reconciliation or parking applies to Keyed's own
outgoing branch. Before replacing a committed declaration, the boundary drains
all runtime and parked references to its scope, including nested declaration
scopes. The scope-destruction wall checks that no ledger reference survives;
it remains active in lifecycle-audit builds even with NDEBUG. Plan keys always
carry their real scope; a scope-less key cannot be default-constructed.

Definitions may copy the uncommitted declarer instruction. A committed
Declaration is not cloneable: its plan identities belong to its own scope.
Lifecycle-audit builds reject copying a definition after declaration commit.

Key changes enter the normal boundary UPDATE path. Destructive replacement
first detaches and retires the outgoing branch. Reclamation runs at the owning
clock boundary; the next eligible admission can build the replacement from those
same slots and report structural work. Several key writes before that admission
coalesce. Address inequality does not distinguish fresh generations.

Observation registration describes the committed tree. An ObservedStatePassScope
begins before traversal and completes on every exit, including a non-nestable
boundary; DETACH is a no-op. Both root and nested boundary paths finish their
compose result before the scope completes. Begin registers the current seat
sources so writes during composition keep their CHILD|LAYOUT classification.
Replacement withdraws outgoing declaration sources before destroying their
scope, then registers committed seat sources and the boundary's own sources.
Seat evaluation precedes the descendant composition walk, so surviving child
nodes subsequently register their own shared-source uses in the same pass.
Newly committed declaration sources register at publication. Completion only
finishes the existing observation pass. A removed declaration's source loses
its observation; a source still used by another node or seat remains registered.
Failed candidates never publish observations. No additional flags, counters,
or per-source reference counts describe the pass.

## Document-driven window set

The App's window seat is a keyed consumer under the same snapshot rule above;
its driver is App admission. A config-owned `DocumentRoster<T, N>` owns the list
and its tracker. The application removes a document with `DocumentRoster::close`.
The factory receives the document's `ItemId` so the window's content can close its
own document with `close`.
Within one walk the seat adds windows for new documents, then closes the windows
of documents that left the roster, through the same `requestWindowClose` a native
close takes. This order keeps a close-then-open replacement from emptying the
group between the two. The seat always advances its snapshot to the revision
read at the start of the walk. It copies desired identities first, so edits
during a factory or mount leave structural work for a following admission.
The walk copies identities at its start but reads each entry's facts live at
apply time and copies them into its Window (#1245). A moved roster revision
is pending window admission work, including an edit after a tail's last admission.

Cost: one ListRevision compare per admission (two per Win32 tail, every Toolbox
pass, every macOS flush tick); when the revision moved, increase walks desired x
group rows and decrease walks group rows x desired once per stale row plus one
final full scan, all over this App's group and its own seat's desired copy.
Facts application adds one roster lookup and value copy per desired row with a
Window, then at most two State sets (equal values are no-ops). A content-only
revision after the tail's last admission costs one extra Win32 loop iteration,
not a spin; admission commits the starting revision even without creates (#1245).

A document whose window cannot be created or adopted at runtime is dropped from
the roster, never retried; the application observes removal as a list fact
(A′, ruling of 2026-10-10, Window rally page 2). At launch a refused adoption
takes the bootstrap door (#1186). A terminal native removal of a keyed window removes its
document. Runtime refusal uses the existing close batching: group removal and
scene detach occur in the refusal tail, while the Window is reclaimed at the
next tail. Closing remains a decided fact. Window rally 3b d0 (owner ruling, 2026-10-13)
adds a separate native close request: App writes the roster's `closeAsked`
box, and the document node either removes its own row or writes a decline to
`closeAnswer`. Each request carries identity and sequence; repeated requests
for the same document are distinct. Null routes native asks in d0; d1 routes the production rails
(Win32 `WM_CLOSE`, macOS `windowShouldClose:`, Toolbox after `TrackGoAway`)
through the same `App::closeAsked` door, and the three menu Quit commands call
`App::requestQuit`; terminal removal and factory refusal never ask. SimpleText declines edited or
for the same document are distinct. Only Null routes native asks in d0;
terminal removal and factory refusal never ask. SimpleText declines edited or
busy documents in d0; a clean document's menu Close uses the same node policy.

`requestQuit` asks the first roster document, then advances at the admission
tail when that row disappears. A matching decline cancels the attempt. Empty
rosters quit even when static windows remain. Because the roster only appends
and removes, the asked first row's disappearance is answered by count and first
identity, without a row walk. The two boxes and their writer walls live in
[DocumentRoster.hpp](../common/app/core/DocumentRoster.hpp); the serial attempt
lives in [App.cpp](../common/app/core/App.cpp).

**Window-less launch.** Every static `WindowDef` can be refused, a roster whose
attach was refused can seed nothing, or every launch document create can be
refused. After static hooks and launch admission, `App::run` checks the group
once, whether or not a document seat exists, and calls `bootstrapWindowless()`
only when it is empty (#1215, extending the refused-adoption door of #1186).
The default quits. Toolbox treats this as out of memory and uses the existing
#1107 Stop alert before quitting without returning. macOS does not enter its
run loop; living without windows is deferred until a reopen path exists.
A multi-window static app with one refused `WindowDef` and others created stays
silent today and after; that is not #1215's claim. Runtime emptiness is answered
by `requestWindowClose`, where the last-window quit check remains (A′).

## Review risk profile

Four flags require explicit review under the
[complexity gate](ReviewPolicy.md#complexity-gate): new definition ownership
and cleanup; multiple new borrowed context fields; State/Boundary interaction;
and dirty/attach/detach behavior. This note records the combined design rather
than treating those changes as independent conveniences. Review lenses are
allocation refusal, scope lifetime, observation cancellation, and clock order.
The contract pins cover same-key writes, failure/retry, enclosing Section
ownership, nested seats and parked reentry, a direct nested-seat root, candidate
observation refusal, and removed/shared observations. MineSweeper pins preserve
the click and drain sequence and all board audits.

Validity invariant: a published runtime seat row borrows a live declaration plan
and a runtime parent/state owner that outlive that row. Replacement, outer-arm
retirement, detach, and boundary teardown remove runtime rows before destroying
their definition scope or reclaiming their runtime owner. A failed candidate
publishes neither rows nor observations. Memory reclamation cannot invoke the
declarer or write application State.

The declarer and candidate materialization visit only the seat's new subtree;
retirement visits its old subtree and existing nested parked residents. Existing
boundary routing still visits its seat rows once per UPDATE. Observation pass
completion walks the observed ledger owned by that boundary once per pass,
matching the ownership of the existing begin-pass walk. There is no per-node or
per-key scan of another boundary's ledgers. Successful replacements restore all
committed seat sources in their own boundary, each using the existing linear
observed-entry lookup: K replacements across S seat sources and E observed
entries can cost O(K * S * E). Scope retirement likewise filters the owning
boundary's runtime and parked ledgers per outgoing declaration scope.

## Candidate 68K size evidence

The delegator measured candidate 95e41429 against
`tools/ci/retro68_68k_size_baseline.json` at main 70e85250:
LokaMine68K +6272 bytes (about +5900 from E2), LokaSmirkBench68K +4224
(E2 share about +1280), LokaHello68K +3584 (E2 share about +1150), and the
other four applications +1536..+1920. The MineSweeper E2 attribution was
48% shared seat-scope/context plumbing present in every app, 31%
KeyedDefinition<int>/Declaration/MemberDeclarer, and 20% newly instantiated
NodeState<int> family, minus 664 bytes for the retired boundary declaration adapter.
E3 removal of the local-recompose family (about 6.8 KB of symbols measured in
LokaMine68K) is expected to return most of it. This is supplied candidate
evidence, not a fresh measurement of the corrections or a passing size gate.

## LazyScope

LazyScope is a keyed declaration seat whose arm root is a LazyScopeNode.
Its props are copied values; its key is borrowed live State. The runtime root
is constructed through the ordinary node factory before its member declarer
runs. The root owns generation-scoped state through its concrete inner owner,
without introducing a nested Boundary. Its states use the tagged heap gate,
never the enclosing Boundary's bump-only StateArena: destroying an arena state
does not reclaim its block, so repeated replacements would otherwise accumulate
storage for the Boundary's lifetime. Heap state storage is reclaimed with the
generation through the existing retirement clock.

Constructor state declarations queue registrations; the candidate window
connects them to the root's concrete inner owner before bindings and declareScope.
A refusal of those root-owned registrations records LAZY_SCOPE_STATES_REFUSED
and rejects the candidate,
preserving the committed arm and key snapshot. A descendant Section reports its
allocation refusal to the enclosing Boundary without changing the generation
root's status. No attach-time declaration commits
structure. The completed declaration supplies its prepared root at materialization.
Nested seat plans live in that declaration, and runtime rows live in the enclosing
Boundary. Discovery, source registration, replacement, and scope retirement use
Keyed's existing recursion. Inner tracker commits invalidate the enclosing Boundary.
Children and registrations are released before the inner owner is destroyed.
Each UPDATE visits the scope's seats; tracker begin/end visits its own states.
Each binding window allocates one callback entry per watch, in addition to binding
storage. These costs do not make a scope update constant-time.

## Typed reservation and strict node route

Keyed requires a `reservation::SeatNodes<List>` argument at both the helper and
its direct definition constructor. `Nodes<T, N, Tail>` describes completed,
accessible, unambiguous Node-derived runtime types and positive counts; `End`
terminates the list. An explicit empty payload is legal. The internal recipe
adds the generation root and declaration Fragment. Applications declare authored
payloads, including component ATTACH descendants. Equal size/alignment pairs
merge with checked addition; the envelope accounts for layouts, not type names.

At cold installation the enclosing Boundary owns an immutable normalized table
and its checked byte count, and boots its embedded NodePartition through the
existing tagged allocation gate. Failed metadata or backing acquisition publishes
nothing and follows nullable mount refusal. Successful installation never boots
again for later keys. Uncommitted definition clones install separate reservations;
no clone borrows another instruction's mutable installation state.

Every materialization reached in a Keyed build uses a stack-local
`SeatNodeStorageView`: generation root, Fragment scaffold, ordinary definition
recursion, contextless recursion, local rebuilding and component ATTACH children.
The view borrows a noncopyable ticket. The partition owns one bounded remaining
quota per layout class, reset only at admission. Consumption precedes placement
construction. Unknown or exhausted classes assert educationally in Debug and
return the existing materialization refusal in Release, without heap fallback or
another reservation. Returning an unconstructed slot does not refund quota.

Cold declaration and materialization precede the surrounding normal ATTACH walk.
The class quota remains with the Boundary-owned partition until that walk reaches
its nodes; a new stack-local ticket resumes the same quota, without resetting it.
This preserves sibling order, including an earlier sibling that creates a Held
payload. Warm replacement holds its ticket through candidate ATTACH before
publication. The existing compose-attach lifecycle records that the subtree has
already attached, so the ordinary traversal does not attach or update it twice
in that admission. No borrowed stack view is stored on a runtime node.

Node storage provenance carries a typed arena-or-partition landlord in one union.
`arenaOwner()` remains a NodeArena pointer and is null for partition nodes.
Partition nodes are never classified as heap by child cleanup or arena snapshots.
The deallocation-function guard is a no-op for partition storage, as for arenas;
normal destruction and slot return still belong exclusively to the partition's
reclaim protocol. The live child tree is reclaimed before orphan partition roots,
so nested partition providers cannot be destroyed ahead of enclosing borrowers.
Failed declaration roots queue on the Boundary clock. Unpublished component
children keep their registered resident-owner edge until whole-candidate reclaim.

## Waiting admission and completion

Each reservation contains one enabled `SeatBuildRequest`. It stores coalescing
demand, not a captured key or candidate. A committed source marks that demand.
Destructive retirement removes the active child and retains its insertion position
in the surviving reservation. It cancels descendant requests and observations
before disposing their declaration scopes and queues the old root on the existing
clock. Cancellation does not discard the outgoing node-return obligation.

Admission visits valid requests serially in the existing scope/seat order. The
bank checks current free class counts and the outgoing obligation. Unavailable
capacity pending known returns waits without partial entitlement; an unsupported
envelope asserts and refuses. `AdmittedSeatStorage` authorizes the subsequent
build. Node return includes registered provider dependents, parked descendants,
unpublished residents and complete owned nested-landlord destruction. The bounded
reclaim paths report completion for every destroyed node; bounded generation
snapshots use a stack-local callback. No completion callback escapes its landlord.

The drain requests structural refresh when surviving demand remains; it does not
invoke the factory. A following admission can therefore build without another
State event. A failed candidate keeps demand pending while its occupied slots
return. Parked arms remain occupied residents until actually retired.

A Keyed re-declaration refused for memory after the mount (or a For batch
inside one) leaves its seat vacant, because `RETIRE_BEFORE_BUILD` has already
retired the outgoing generation, and retries at the next admission once memory
returns. A LazyScope refusal keeps its installed generation linked
(`PRESERVE_INSTALLED`, see "LazyView reserved generations") and settles until
the key changes. No rail reports either case; only a launch mount refused for memory
is reported (the Toolbox Stop alert, #1186); a runtime document whose window
is refused is dropped from its roster, see [Document-driven window set](#document-driven-window-set). Ruling of 2026-10-10, Window
rally page 1 topic 2: a per-seat "refused and still vacant" fact is deferred
until an application needs to show it. The Keyed retry cadence is tracked in
#1194.

Costs remain explicit: notification marks one request; admission scans its bank's
class free lists up to the requested counts; allocation searches that bank's
layout classes. The traversal reads storage provenance in O(1) and walks the
candidate's children. Vacant-child edits walk that parent's children and the
owning Boundary's reservation rows. Complete node return visits that Boundary's
request rows. Legacy snapshot completion matches outstanding requests against
its own snapshot identities; bounded completion uses its existing planned rows.

## Migration and certification limits

This route certifies Keyed node storage, not non-node State, Flow, Held or String
storage and not whole-cycle zero acquisition. Those returns remain named later
slices. Show and Conditional/Match retained-arm routing remain separate follow-ups, with no
permanent app-facing bypass. Later transitions through those doors still use their
existing routes; descendants materialized as part of a strict Keyed build consume
that build's declared envelope.

Sections are logical owners within the declared Keyed payload; they are not by
that fact independent allocation landlords. A nested Boundary node belongs to
its outer payload, while the nested Boundary's residents and banks belong to it.
Nested Keyed seats install their own reservations. Ancestor-backed provisioning
for repeatedly remounted nested landlords remains excluded from the warmed-cycle
claim. The outer occupant's completion still waits for owned nested teardown.

Verification lives in StrictNodeRouteTests, the Keyed/request/reclaim contract
pins, and the unchanged MineSweeper scenario goldens. The legacy bump-arena
retirement fixtures explicitly use their isolated arena route; they do not
establish production Keyed behavior.

## From AGENTS.md

For LazyView (#990), a viewport or list revision notification enters one
selection writer whose interval calculation is O(1). Props binding windows use the same writer. The completed
`(first, count, structureRevision)` key drives the existing LazyScope route:
an unchanged key makes no replacement; a changed key declares only `count`
items, including overlap. There are no per-item visibility states or Show seats.
All item-local state and focus are lost on a successful key change. Native focus
acceptance on target systems remains a runtime-verification follow-up.

The stable view owns its viewport copy and key State. A generation borrows the
current view inputs upward and owns its completed window and a bounded index
of its own fixed item nodes. Building that index is O(count) per attach. Content
refresh intersects the published change with the admitted window and directly
applies props at `global - first`, O(changed ∩ window); a batch visits the window.
Refresh is skipped while a declaration is pending, including after refusal.
If demand returns to the installed key, that cancellation refreshes the retained
window in O(count), recovering content edits skipped while demand was pending.

`LazyLayout` is the props-owned policy value, answering extent, selection and
placement. `FixedGrid` supplies both axes and a default margin of one main-axis
row. The box clamps selection through the policy's current list bound. Canvas's
completed placement input separates full-list extent from the resident interval;
LazyView places every resident at its global content coordinates. ScrollView
alone translates those coordinates. Standalone Canvas keeps its historical
conservative clipping and viewport subtraction.
Horizontal viewport movement selects the window but leaves residents in absolute
content coordinates; horizontal scrolling is pending a ScrollView X offset.

Candidate declaration refusal preserves the old LazyScope generation. At an
already moved ScrollView viewport origin, that old window may be stale or vacant; no
old-offset/pixel atomicity is promised. The full-list extent still follows current
list size. A pending declaration requests layout through the existing Scene
after-run door, so retry needs no fresh scroll and does not spin the current
scheduler drain. The reserved seat's request phase determines whether the after-run poll asks
for another run. A terminal warm declaration refusal settles until a key change.
Cold mount keeps the shared Keyed replay policy: drain-only does not retry;
an external update may retry by design.

### LazyView reserved generations (#704)

`LazyColumn`, `LazyRow`, direct `LazyView`, and `LazyViewProps` require a typed
per-item recipe and positive maximum M. The surviving LazyView Boundary owns
one seat reservation with immutable one-build quota E: M item recipes plus one
generation root, Fragment and WindowCanvas. A temporary doubled table boots the
backing with 2E slots; the stored table and every build ticket still grant E.
Recipe identity and M participate in generic definition compatibility, including
clones and custom LazyView-derived nodes. Changing either requires a new Boundary.

The warmed zero-node-upstream claim covers **only the outer generation allocation
domain**. Nested Boundary residents and nested seats are excluded from both this
claim and preserve-installed failure handling; the nested Boundary node itself
uses an outer slot. A LazyView remounted inside an outer generation obtains a new
bank. States, definition clones, ItemIndex, strings and native allocations are
also outside the node-storage certificate.

The closed replacement policy keeps Keyed's `RETIRE_BEFORE_BUILD` behavior and
single E backing. LazyView uses `PRESERVE_INSTALLED`: WAITING demand, no outstanding
root return, and free E capacity admit one strict candidate while the installed
generation stays linked. Candidate B ATTACHes before A synchronously DETACHes at
commit. Warm-up must cover the observed-state A+B peak. Incomplete candidates
use the same discard doors; the installed generation and bindings survive.

A shared retirement helper records only the old or discarded generation root,
never descendants, as the request's single return obligation. Recording does not
mark demand. Detach remains synchronous; slots return only after child-first
reclaim. The existing drain tail wakes waiting demand without a fresh State event.
Unknown/exhausted classes have no heap fallback, and terminal warm refusals do
not poll themselves. Window selection always publishes the window and structure
revision as facts, including counts above M. Declaration refuses an over-M key
before constructing a generation; the same wall covers cold and direct entry.

Costs: recipe emission walks its own at-most-32 leaves once at installation;
normalization is quadratic in those leaves. Admission examines its own class
free lists up to E slots. Reclaim walks E residents against 2E backing rows,
O(M²) for a fixed item recipe; no foreign Boundary rows are searched. Two envelopes
remain resident for the Boundary lifetime. These are structural costs, not native
runtime timing claims. Pins live in LazyViewTests, SeatBuildRequestTests,
SeatReservationTests and the C++98 compile pairs.

Selection is bounded in total list size N for fixed window size K. Declaration,
indexing, projection, and retirement visit O(K) owned residents; existing generic
seat-ledger/observation matching can add K-dependent costs described above. This
is not a measured native timing claim. PR 1's N-dependent counters remain
historical baseline evidence; PR 2 renews Null counts and allocation ceilings.
The #639 LazyFlex page-flip timings apply only to that former all-N Show policy.
Toolbox timing and golden acceptance belong to #990 PR 3.

`LOKA_LAZYFLEX_MAX_ITEMS` remains a reserved-list-capacity admission contract,
not a window or timing bound. `LazyFlex`, its Props/Node/status family and its
header are removed; `LazyColumn` and `LazyRow` are fixed-grid helpers for
`LazyView`. See [LazyView.hpp](../common/app/nodes/nestable/LazyView.hpp),
[LazyLayout.hpp](../common/app/layout/LazyLayout.hpp), and the LazyView/LazyLayout
contract tests for exact surfaces and evidence.
