# Text document design

> **Status:** Draft — common kernel and Null store implemented; native rails pending. The owner ruling of
> 2026-11-03 ([#1733](https://github.com/SimplySmirkySoftware/Loka/issues/1733))
> froze [The document](#the-document),
> [Reads, positions, and requests](#reads-positions-and-requests) and
> [Migration](#migration). [Kernel obligations](#kernel-obligations) lists
> the kernel contract and its implementation, including the common IME commit door
> (page 1c, owner ruling 2026-11-04); native commit implementations remain pending.
> [Why](#why) and [Native facts](#native-facts) are background. The policy in
> [From AGENTS.md](#from-agentsmd) applies to new work now; the shipped
> `TextEditor` keeps its current model until the migration replaces it.
>
> **Owns:** where an editable text's body lives, the app-facing document fact,
> positions, requests, and the migration away from `TextBuffer`
>
> **Does not own:** exact API signatures (the names below are frozen; the
> headers will own the signatures), the kernel seat and its reconcile,
> Edit-menu routing, capacity, or per-rail native mechanics
>
> **Code truth:** [TextDocument.hpp](../common/app/text/TextDocument.hpp),
> [TextDocument.cpp](../common/app/text/TextDocument.cpp), and
> [SceneTextDocuments.hpp](../common/app/scene/SceneTextDocuments.hpp).
> The model it replaces is
> [TextEditor.hpp](../common/app/nodes/controls/TextEditor.hpp),
> [TextBuffer.hpp](../common/app/TextBuffer.hpp),
> [LineCursor.hpp](../common/app/style/LineCursor.hpp),
> [TextEditorDocument.hpp](../common/app/nodes/controls/TextEditorDocument.hpp)
> and [TextEditorDiff.hpp](../common/app/nodes/controls/TextEditorDiff.hpp).
>
> **Verification:** [TextDocumentKernelTests.cpp](../tests/TextDocumentKernelTests.cpp)
> pins common/Null behavior. The native behavior the design relies on is
> recorded under [Native facts](#native-facts).

## From AGENTS.md

Native store rails and editor migration remain target work. New text-editing work
keeps an editable text's body once, in the native editor's own store (the MLTE
object, `NSTextStorage`, the EDIT control's `HLOCAL`, TextEdit's text handle),
and reads the body out only when an operation needs it: save, search, or an
app query. New work adds no Loka-side copy of the body and does not extend
`TextBuffer` or `LineCursor`. The shipped `TextEditor` path, which still
mirrors the body into `TextBuffer`, keeps working and takes fixes until the
migration removes it.

The document's fact has a single writer in the kernel, in the
[Focus](FocusDesign.md#from-agentsmd) shape: it is read at the outer
completion. The command door `App::commitTextInput` enters the same writer
without taking requests; requests stay while the first reading is `MARKED`,
even when their revision differs. Rails answer that writer's questions and never write the fact
from native notifications.

### Names already in the tree

`TextDocument` is the new root-owned declaration. In particular,
these existing names are not it:

- `TextDocumentFile` (`TextDocumentResult`, `ReadTextDocument`) reads and
  writes files through `TextBuffer`. What becomes of it is decided with the
  write requests.
- `TextEditorDocument` belongs to the shipped editor and is deleted by the
  migration.

## Why

Today every rail copies the whole document into the native editor. Every rail
except TextEdit and Null then reads the whole body back after each keystroke
and diffs it into `TextBuffer`'s line list. Measured on the Unicode SimpleText
under Mac OS 9.2.2 (QEMU):

| Case | Heap used |
|---|---|
| Empty document | 9.3 MB |
| A 3 MiB document open | 22 MB |
| A second window | 38 MB |

One keystroke in a 4 MiB document takes about 0.9 s, spent in two whole-body
reads of about 0.43 s each. A standalone MLTE probe measured MLTE's own read
(`TXNGetData`) of a 4 MiB body at 35 ms. The rest is Loka's conversion, diff
and allocation. The native editors already hold the body; the second copy is
where the cost is.

A rail that draws its own text keeps the body in a Loka-owned store, which
then plays the native role. It is still the only copy.

## The document

- `TextDocument` is declared with `document(doc_)` only in the constructor of
  a window's root Boundary. This door is separate from `state()`.
  - A window may declare several documents.
  - Declaring one inside a Keyed or LazyScope generation is refused.
  - A document used inside a Match arm is declared at the root, so the body
    outlives the arm.
  - A varying number of documents in one window (tabs) is a v1 limit.
  - Replacing the Scene ends its documents together with the root Boundary.
- A document has at most one view: a new editor node, a separate type from
  today's `TextEditor`. A second view is a typed refusal.
- The document has one fact box, `State<TextDocumentFact>`. It carries:
  - the state;
  - `textRevision`, monotonic within the document and continuing across
    store rebuilds;
  - an optional invalidation interval at the current revision (changed start
    and inserted end), where the rail can report it; removed length is not exposed;
  - the caret and selection, as `TextPosition`s.

  The same reconcile writes all of them in one transaction.
- The state is one of four:

  | State | Meaning |
  |---|---|
  | `AWAITING_STORE` | Declared; the native store does not exist yet. |
  | `READY` | The store exists and nothing is pending. |
  | `MARKED` | Uncommitted IME text exists. |
  | `UNAVAILABLE` | The store could not be created. |

## Reads, positions, and requests

- **Queries.** Line count, size, line contents and position creation answer
  only in `READY` with no unreconciled native change. Otherwise they refuse,
  answering presence and value from one query. A rail that cannot answer a
  query refuses it.
- **Positions.** `TextPosition` is opaque: only the document makes one.
  - `None()` and copy are public.
  - `!=` compares full identity (revision and content), so `Request<T>` can
    use it.
  - `queryOrder` compares two positions, and `queryUtf16Offset` and
    `queryUtf8Offset` convert a position to an offset. These three refuse
    when the position's `textRevision` differs from the document's.
  - There is no position arithmetic.
- **Caret request.** The caret request is `Request<TextPosition>`.
- **Writes.** A write (open, new, whole replacement) carries its caret as a
  `TextPlacement`: start, end, or a UTF-8 byte offset into the written body.
  The caret after Open therefore arrives with the body it belongs to.
  - A request never carries the body by value. It names the source or holds
    a Blob handle, so copying it and comparing it with `!=` stay O(1).
  - Whole replacement is a `Request`; partial edits are a `RequestQueue`.
  - The exact box shapes are decided with the write requests.
- **Held and dropped requests.**
  - While the document is `AWAITING_STORE` or `MARKED`, the reconcile does
    not take its requests; they stay in their slot until `READY`. This keeps
    the first write posted at attach.
  - When the document is `UNAVAILABLE`, or the revision differs, a request is
    taken and dropped, as the refusal rule in
    [Request delivery](RequestDeliveryDesign.md#from-agentsmd) says.
  - That rule today drops a request whose native resource is missing. For a
    document, `AWAITING_STORE` is the exception: the request waits instead.
    The refusal rule names this exception in the common kernel.
- **Commands.** The command door asks every eligible window to commit uncommitted
  IME text and refresh its facts before these operations (subject to the v1
  limits below):
  - an Edit command;
  - save;
  - close, including the close confirmation;
  - Quit.

  Menu actions and their key equivalents, window close boxes, and macOS quit
  Apple events / termination requests enter the door. A closed window gate
  leaves that window unchanged and answers `TEXT_INPUT_UNAVAILABLE`; other
  windows still refresh. Commands proceed with the available facts, without
  repost or retry. `TEXT_INPUT_SETTLED` means all visited gates opened, including
  an empty walk, not that every document is `READY`.

  V1 limits: Button, Toolbar and Ribbon commands that do not pass through the
  menu do not commit. Classic (Toolbox) does not commit uncommitted text before
  commands: its default store door does nothing. Win32 also leaves composition
  uncommitted: its store reading reports MARKED only for the focused EDIT, and
  treats a failed composition query as marked. Committed characters can still
  be waiting for queued WM_CHAR delivery (see [Native facts](#native-facts)).
  macOS creates parked stores and commits marked text directly on the parked view.
  The shipped `TextEditor` path is unchanged.

`TextReader` pulls a revision's logical UTF-8 body in bounded chunks, with CR,
LF, and CRLF each represented by one LF. Malformed content refuses the read;
it is never replaced silently. A reader refuses after a revision change,
withdrawal, or destruction of its document or registration. Do not pump while
a reader is open: finish dialogs before constructing it. Queries scan through
the same chunk door using fixed stack scratch. Do not call `queryLineRange`
per line to walk all lines; use one reader and count LF separators instead.

`WriteTextDocument(context, file, document)` finishes synchronously: complete
any dialog first and do not pump between the call's start and return. It reads
the entire body twice with fixed stack scratch. Pass 1 validates before any
filesystem work; a revision change before pass 2 refuses without opening the
file. Pass 2 prepares and truncates the destination, then writes chunks with
LF converted to the rail's file separator. Pass-1 malformed, resource, and
not-ready refusals report `NOT_TEXT`, `ALLOCATION`, and `UNATTACHED` respectively.

The `TextBuffer` overload stages a complete copy before touching the file.
The document overload keeps no whole-body copy, but pass 2 can fail after
truncation: MLTE allocates a Handle for each chunk and that allocation can fail.
Any pass-2 read refusal reports `TEXT_DOCUMENT_WRITE_FAILED`; the first failure
is retained through flush and close. This is the v1 limitation (Q2-F = F1);
safe save through a temporary file belongs to a later page. After success,
read `fact()->get().textRevision()` in the same command to record what was saved.

## Migration

- `TextBuffer` does not become a compatibility layer over the document,
  because returning line ids would need a copy of the body.
- The new editor node is a separate type. The old `TextEditor`, `TextBuffer`
  and `LineCursor` stay beside it until every rail has moved; migration PRs
  do not modify them.
- The last migration PR deletes them, together with `TextEditorDiff` and
  `TextEditorDocument`, and re-captures the SimpleText golden.

## Kernel obligations

The common kernel uses [Scene::projectInitial](../common/app/scene/Scene.hpp)
for creation, [SceneTextDocuments](../common/app/scene/SceneTextDocuments.hpp)
for membership, and [TextDocumentPublisher](../common/app/text/TextDocument.cpp)
for the single read/take/apply/read/write sequence. Controller doors in
[IPlatformController](../common/app/scene/projection/PlatformController.hpp)
refuse by default; Null, Win32 and macOS supply stores. The
`commitMarkedText` operation defaults to doing nothing. `TextStoreReading`
reports marked text; the latest reading determines `MARKED`, with positions
assembled just as in `READY`.

The obligations are:

- **Store creation**
  - A candidate Scene that is being prepared never creates a store.
  - A store is created when the current Scene attaches. If creation fails,
    the document becomes `UNAVAILABLE`, without retry until the root is recreated.
- **The fact and its requests**
  - The fact has a single writer in the Focus shape: it reads native changes
    at the outer completion and writes the fact once. The command door enters
    the same assembly through `refresh`, under the same publication and borrow
    interval, but never takes requests.
  - Rails mark changes and answer questions; they never write the fact.
  - Requests are taken only by that writer at completion. A marked first reading
    retains the caret request before the revision-drop rule; newer posts coalesce
    over it. After commit, the next completion drops it if the revision advanced,
    or applies it if the revision still matches. There is no timeout.
  - Queries refuse while a native change is unreconciled.
- **Replacing and releasing a store**
  - Open builds the new store completely before swapping it in.
  - Detaching a view allocates nothing.
  - Reclaim happens at the owning clock's boundary and fires nothing.

  Retired store rows make their window eligible for the next admission that passes
  the close and busy checks. Each participating rail declares one explicit binding
  after its ledger. Store-only work reaches the existing drain without requesting
  platform synchronization. Null, Win32, and macOS each bind their controller-owned ledger.

- **IME commit:** `App::commitTextInput` opens an Operation (joining an outer turn),
  shares completion's live window walk, and enters each window's shared document
  gate. It performs no admission, completion reconcile, or caret delivery.
  Each linked store is asked to commit before the writer reads it. Rails copy
  handles and identities before native calls and revalidate afterwards; native
  callbacks only mark the row's change note. Native implementations follow in
  the store PRs, with the v1 limits above.

### Known visibility limit

Hiding a window on Win32/macOS rebuilds its root when shown again and loses
the body; today's `TextBuffer` has the same limit. The Null visibility pin
makes root recreation and the fresh document's revision 0 explicit. Preserving
the root across hiding is separate work.

## Native facts

Observed OS behavior, recorded here, not contracts. A dagger (†) marks a
documented behavior that no Loka probe has observed yet.

| Rail | Store | Detached from the view by | Undo history lives in |
|---|---|---|---|
| Toolbox Unicode | MLTE `TXNObject`, created with no window | attaching it to an offscreen GWorld | the object |
| Toolbox TextEdit | the `hText` handle † | swapping it for an empty handle before `TEDispose` † | the TE record † |
| macOS | row-owned TextKit 1 network | the text view has no superview or window | the row supplies its own undo manager through the view delegate |
| Win32 | a store-owned EDIT, born with an `HLOCAL` through `EM_SETHANDLE` | parked under a controller-owned message-only window; view attachment is deferred | the EDIT; `EM_SETHANDLE` clears it |
| Null | a host string | not needed | none |

- **Win32** (Windows 11, MS-IME; the comctl32 6 EDIT and the user32 EDIT
  behave alike):
  - `EM_SETHANDLE` and multiline `WM_SETTEXT` raise no `EN_CHANGE` and reset
    `EM_GETMODIFY`. `EM_SETHANDLE` also clears undo.
  - `EN_CHANGE` carries no range, and one replacement or undo raises it
    twice.
  - `EM_GETMODIFY` means "touched since Loka last wrote", not "differs from
    the saved text".
  - Uncommitted IME text never enters the EDIT's body.
  - When `ImmNotifyIME(CPS_COMPLETE)` returns, the body is still unchanged.
    The committed characters arrive afterwards, as posted `WM_CHAR`.
- **macOS** (macOS 26; a TextKit 1 `NSTextView` made the way the Loka rail
  makes it; marked text set by the probe itself, not through a real input
  method):
  - Marked text is inside the `NSTextStorage` string while composing;
    `hasMarkedText` and `markedRange` report it.
  - `unmarkText` commits synchronously: the committed text is in the storage
    when the call returns.
  - `[[view inputContext] discardMarkedText]` changes neither the body nor
    the marked text.
  - The storage delegate reports an edited range and a change in length for
    every change, Loka's own writes included.
  - `textDidChange:` fires for typing and for `unmarkText`. It does not fire
    for the view's `setString:` or `setMarkedText:`, or for writes to the
    storage.
  - Neither a storage write (`replaceCharactersInRange:withString:`) nor the
    view's `setString:` registers an undo action.
  - Not yet observed: the order in which a real input method calls
    `setMarkedText:` and `insertText:`, and Mac OS X 10.4 to 10.6.
- **Toolbox MLTE 1.3** (Mac OS 9.2):
  - `TXNSetData` advances `TXNGetChangeCount` by two and leaves an undoable
    action.
  - The change count drops on undo. `TXNGetActionChangeCount` never drops on
    undo; only `TXNClearActionChangeCount` resets it.
  - No API reports the changed range.
  - With the bottom-line input window, uncommitted text stays out of the
    body, and committed text is in the body when `WaitNextEvent` returns.
  - No commit door is reachable from the application: `FixTSMDocument`
    cannot name MLTE's TSM document.

### Whole-body replacement (page 3)

`document(doc)` declares the replacement request and reply alongside the fact and
caret request. `doc.replace(value)` admits one validated `TextReplacement` at a
time. `POST_QUEUE_FULL` means either a replacement is pending or the Scene's
text-document publication is running; a direct fact or reply observer cannot
reopen the slot. Use a deferred observer or a later command. `POST_INVALID`
means NONE input or a severed declaration. A refused post changes neither box.
Withdrawal, document destruction, and Scene replacement cancel pending work
without a reply. Every taken replacement ends in a forced reply, even when an
identical refusal follows the preceding one. The fact is written first, then
the reply, in the same publication.

Completion takes replacement before caret. Awaited stores retain requests;
refused or unreadable stores answer UNAVAILABLE. Composition answers COMPOSING
and keeps the caret request. Command refresh takes neither request. Successful
replacement advances the revision once, even when the old store also reported
an edit, and invalidates old positions and readers. Failed replacement preserves
the old store while ordinary edits and eligible caret requests still publish.
The QUEUE_FULL and COMPOSING refusals are the kernel's last net: an application's
command entrance is expected to disable Open while pending and commit composition
before opening. Native command API details belong to a later rally.

Initial projection and replacement use the same store-birth door. The rail builds
completely, transfers any views without allocation, places the selection, and
consumes construction marks and baselines before returning BORN. The kernel then
connects the fresh row and retires the old row. Refusal retires any partial row
through its owner and leaves the old row untouched. As clarified after the page 3
freeze, BORN's unchanged, unmarked reading must carry the full native-coordinate
range `[0, new native end]`. The publisher uses that range directly, without a
second traversal; ordinary change-note aggregation ignores unchanged readings.
Null transfers the view claim and its simulated native place during successful
store birth. Production rail store-row changes must run the shared birth
conformance helper in `tests/support/TextStoreConformance.hpp`.

`replaceReply()` exposes a read-only State. Its requested and applied receipts
retain only stage, placement, and logical length: neither body bytes nor source.
With no app-held copies, replacement bytes are released when the publisher's
local value dies. START and END retain their meanings; an explicit UTF-8 offset
beyond the body or within a scalar is clamped to the preceding canonical boundary
and reported as CLAMPED.

The fact's `origin()` is the source File and revision of the last landed
replacement. Refusal, cancellation, edits, and caret movement preserve it.
Equality compares both revision and `File::sharesRepresentation`, since separate
documents can have different sources at the same revision. Only file reading
attaches a source; Empty and memory replacements have no source. Whether a
source-free replacement counts as unsaved is application policy.

An application may keep `saved_ = {File, revision}`, initially `{none, 0}`, with
the same lifetime as its document. After a successful save it records the target
and current fact revision synchronously, before returning from the command.
Destination (also used for the title) is `saved_.file` when
`saved_.revision >= origin.revision()`, otherwise the origin's source. Dirty is
`fact.textRevision() != max(saved_.revision, origin.revision())`. Reply observers
report errors; they do not establish destination. Thus a deferred Open B after
Open A cannot change destination before B lands, and a refused B leaves A.

Revisions advance at most once per completion. Advancing from `ULONG_MAX` is a
contract violation diagnosed by a debug assertion. Revision wrap is outside the
v1 contract; release builds do not add a hot-path saturation check. Even a 32-bit
revision takes over a year to exhaust at 100 advancing completions per second.


### Partial edits (page 3b)

`document(doc)` also declares a private `RequestQueue<TextSplice, 1>`. The only
app admission door is `doc.splice(value)`: NONE and severed declarations are
INVALID; publication, an occupied seat, or any ring entry is QUEUE_FULL. This
always-on gate keeps the ring empty. The publisher's private take door asserts
that invariant and refuses to take a nonempty ring in release builds. A splice's
positions must belong to the same document; positions carry revisions, not
identity. `spliceReply()` is read-only and its receipts retain no Blob.

Completion reads the store, takes replacement, takes splice, considers caret,
then publishes the fact, replacement reply, and splice reply in that order.
Each reply reacquires the document link immediately before writing, so a direct
observer can cancel subsequent replies by withdrawing the declaration. Awaited
stores and command refresh retain splice requests. A landed replacement makes
the splice STALE; a refused replacement may still permit the splice. Refused or
unreadable stores answer UNAVAILABLE, composition answers COMPOSING and retains
caret, and positions are checked against the previous revision plus the first
reading's change. Old positions answer STALE before entering the rail.

The rail's `applyTextSplice` validates ordered, canonical, representable native
endpoints in every build. All fallible conversion, allocation, overflow and
capacity checks precede any body or selection change. Refusal preserves body,
selection, undo and marks. Native veto is UNAVAILABLE. Success collapses selection
to the inserted placement, consumes its own notification marks, and returns an
unchanged, unmarked reading with the inserted native range and selection. Rails
with undo record one editing step; rails without undo are exempt. The default
refuses UNAVAILABLE. Native rail implementations remain separate store-row work.

Successful splice uses the door's reading and range directly, advances revision
once, drops an old caret request and preserves origin. Placement clamping is
reported as CLAMPED. RANGE, NOT_REPRESENTABLE, ALLOCATION, CAPACITY and UNAVAILABLE
refusals preserve ordinary caret and first/second-reading behavior. A rail that
unexpectedly changes the body before ALLOCATION has violated the atomicity
contract: lifecycle audit diagnoses it; release leaves marks intact. Only for
ALLOCATION does the publisher peek for an unreconciled change. If present, it
advances the revision once and drops stale caret; if rereading also fails, caret,
anchor and changed range are absent until a later reading can establish them.

Cancellation through withdrawal, document destruction or Scene replacement is
synchronous and produces no reply. Pending Blob ownership ends at the declaring
Node state's reclaim, not at detach. Taken values last only through publication;
receipts keep no bytes. Direct observers cannot post during publication. Deferred
observers may post for the next completion, with no automatic wake-up. Automatic
Enter/indent behavior needs a separate input-command entrance (Q3b-E); batching
and sealed-Blob overloads are also outside this page.

### Attaching a view (page 4)

`TextDocumentEditor` borrows a root-owned document and an optional ancestor-owned
`NodeState<TextViewAttachment>` result. The store row owns the editable body;
the view context supplies its place. Detach returns the body to the store's
parking place without allocation. Body, selection, undo, and pending marks stay
with the store. Parked stores continue to accept app requests; read-only view
presentation restricts user input, not replacement, splice, or caret requests.
This supersedes the earlier handle-exchange description: detach does not move
marks into a separate view-detach ledger. The native observation table above
remains historical probe evidence pending each rail's qualification.

The kernel admits a view in this order: Scene/controller identity, store
availability, then occupancy. An occupant whose logical node is no longer
attached is detached before the incoming view attaches, regardless of delivery
order. An attached occupant causes a typed refusal. Refused contexts render an
empty, read-only placeholder; refresh and presentation never retry attachment.
A later lifecycle attachment may retry. Native calls run under individual
controller borrows and cannot reenter the kernel; native notifications only
mark. IME commitment is a preceding input operation, not detach work.

A context-owned claim derives ATTACHED from its paired store link. Context end
and store retirement both detach synchronously before severing that link;
deferred native reclamation fires nothing. Replacement prepares completely
before moving the native place and transferring the claim to the fresh store.
The old row then retires without detaching the transferred view. Cosmetic
resource preparation can fall back to defaults, but behavior such as read-only
and wrapping must be honored or attachment must refuse.

The existing document publisher walks its Scene's claims after document facts,
inside the same publication phase and borrow. Only ATTACHED and
DETACHED_RETAINED nodes publish. The node writes the borrowed result without
reading it; equal values suppress observers through `NodeState::set`. Results
are completion snapshots, not a transition log: initial projection does not
publish, and an attach/detach/attach interval may produce no result change.
`commitTextInput` refresh also publishes these snapshots. Structural changes
requested by observers wait until publication ends; document writes posted
from those observers are refused by the existing publication admission gate.

Document identity, result-box identity, and wrap cannot change in an existing editor node.
Compatibility refusal has path-dependent consequences:

| Path | Incompatible identity or wrap |
|---|---|
| Ordinary child retain/reconcile, including an editor nested inside a parked container | Materialize a new child and retire the old node. A parked replacement has no attached view until return. |
| Returning to a parked branch | Discard an incompatible parked candidate and build anew. If construction refuses, the outgoing active branch stays. |
| Applying props while the editor itself is the parked branch root | Refuse; retain the existing node, claim, store, and body with the old wrap. |
| Keyed replacement | Replace the generation regardless of compatibility. |
| Direct Lazy item | Not supported: the editor is not a `ComponentNodeWithProps`. Editors inside Lazy components remain outside this page's scope. |

The branch-root versus nested-child distinction follows the page 4 v1.2 ruling;
Boundary reconciliation is unchanged. Wrap is a birth-time fact on Win32 (probe
P4-2a), so wrap changes are incompatible on every rail and the Win32 view, page 4w,
refuses `wrap=false` (native view implementation in 4w-2b).
Other presentation and focus-binding changes remain compatible and update the
retained context. The kernel and Null pins
are in `tests/TextDocumentViewTests.cpp`. macOS and Toolbox keep the refusing/default
view doors; their native implementations and runtime evidence belong to their rail pages.
The macOS and Win32 store rows are described below.


### macOS store row and parking (4m-1)

The controller's `TextStoreLedger` owns one `MacTextStore` for each live native
document. The row owns the TextKit 1 network. Parking leaves the text view without
a superview or window. Birth streams the source once, places the caret in
UTF-16 units and seeds the storage directly, outside undo. A replacement
builds a fresh row with its own empty undo manager. The kernel retires the
old row after connecting the new row to the document.

Only character edits contribute to the change note. The storage delegate
captures the processed range and signed length delta during the callback.
Reading takes this note and samples the current marked state and both ends
of the selection. A caret request preserves pending edits. A bounded chunk
reads UTF-16 directly from storage and emits complete logical UTF-8 scalars.
The command commit door asks the parked view to unmark and leaves its edit
for the publisher.

Retirement cuts the delegate's borrowed row pointer and clears both native
delegate slots synchronously. At the controller's drain the row clears undo,
removes the layout manager from storage and releases the network. Both
controller exits retire live rows before that drain. View attachment remains
deferred and splice returns the inherited `UNAVAILABLE`. The store row is
runtime-verified on macOS 26 (Tahoe) and build-verified with the 10.9 and 10.6
SDK toolchains. macOS 10.4–10.6 behavior is not runtime-verified.

### Win32 store row and parking (4w-1)

`Win32TextParking` owns one lazily created message-only window and the controller's
`TextStoreLedger`. Each `Win32TextStore` owns a hidden multiline EDIT and a
`TextChangeNote`. The parking procedure receives EN_CHANGE and calls `noteEdit(0)`;
it neither enters the kernel nor forwards to the legacy dispatcher/subclass.
Reading folds EM_GETMODIFY into the note before consuming it and resets the native
modify flag. Peek consumes neither source. EM_GETSEL uses DWORD outputs: the low
endpoint is anchor and the high endpoint is caret, without reconstructing selection
direction. Reading can occur twice in one document completion.

Birth traverses `TextStoreSource` once, converts separators to CRLF, and maps logical
UTF-8 placement to native UTF-16 units with the source's round-down/clamp rule.
A movable, unlocked, terminated HLOCAL transfers to EDIT through EM_SETHANDLE;
the displaced empty buffer is freed once, while EDIT frees the installed buffer
on destruction. NUL refuses as NOT_REPRESENTABLE; integer/capacity overflow refuses
as CAPACITY. Parking/EDIT/DPI refusal is UNSUPPORTED, allocation refusal is ALLOCATION.
A failure after row allocation retires the row through its ledger. Replacement
prepares an independent row and preserves the old body, selection, undo and marks.

`IPlatformController::retireTextStore` is a default no-op, called exactly once per
row retirement after view detach, outside that detach borrow, before either link
is cut. It must allocate nothing, cannot fail or reenter the kernel, and only stops
marks. Win32 clears EDIT's GWLP_USERDATA there. Native destruction happens later:
ordinary controller drains reclaim retired rows; both controller shutdown paths
retire all rows, drain them, then destroy parking. Scene teardown cuts the document
links before root destruction. No second HWND retirement queue owns these EDITs.

Chunk reads lock the EDIT's EM_GETHANDLE storage for the call only. They validate
both endpoints, reject boundaries inside CRLF/surrogate pairs, normalize CR/LF/CRLF
to LF, preserve other Unicode separators, and refuse isolated surrogates without
advancing the cursor. There is no persistent Loka body copy. Caret uses EM_SETSEL;
commit remains a no-op and splice remains UNAVAILABLE. No view or focus participant
is added in 4w-1; positive composition and native view evidence belongs to 4w-2.

Birth checks EDIT/parking DPI awareness equality when the dynamically resolved
Windows 10 1607 APIs exist. Earlier systems omit the query under the frozen
process-awareness limitation. ES_EX_ALLOWEOL_ALL is requested, but older EDITs
(including user32 and Windows 8.1) may ignore it: lone CR/LF still normalize during
reading even when native navigation/display does not treat them as line breaks.
Saving and reopening can therefore change visible line breaks. The native text
limit is explicitly raised with EM_SETLIMITTEXT(0); this is not a capacity guarantee.

These rules follow page 4w v1.0 (§0.4 through §0.1 precedence), Gate 1 v1.1,
page 1d §2.1, page 2 §2.1/§2.2, and page 3 P3-1 (v6 and user32 buffer ownership
and notification probes). `Win32TextStoreTests.cpp` pins the rail contracts;
`TextDocumentKernelTests.cpp` and `TextDocumentViewTests.cpp` pin the retirement
door. Win32 build/runtime verification remains the VM handoff's responsibility;
Linux suite results do not establish native behavior.


### Win32 document view (4w-2b)

`Win32TextDocumentViewContext` borrows its constructor's projection parent HWND;
its claimed `Win32TextStore` owns the EDIT. Attachment is derived from the common
claim link, never from visibility or native parentage. The focus-participant
property recognizes a typed view context; focus resolution additionally requires
the claimed store's EDIT identity. During replacement preparation both HWNDs can
carry the same context property, but only the claimed one is a focus target.

Attachment refuses `wrap=false` and, when the awareness APIs are available, unequal
EDIT/view DPI awareness. It installs the focus property, reparents the hidden
EDIT, and requests layout. The kernel applies presentation once after successful
attachment. Presentation borrows the controller's styled fallback font, applies
read-only and scaled left/right margins, and does not change birth-time wrapping.
EDIT is born with WS_VSCROLL and without ES_AUTOHSCROLL. v1 uses native default
text/background colors; TextStyle colors are not projected. Read-only stops user
input, not document requests. Root and container WM_CTLCOLORSTATIC paths delegate
recognized document EDITs to DefWindowProcW, preserving opaque native ground.

Every layout visit positions the claimed EDIT. Its own WS_VISIBLE bit determines
whether positioning also shows it: SWP_SHOWWINDOW, SWP_NOACTIVATE, SWP_NOZORDER,
and SWP_NOREDRAW only during a native layout pass. The pass's existing final
redraw then paints the final frame. Outside that pass, normal native redraw is
allowed. Attached views answer exact empty framework paint damage because EDIT
paints itself; detached/refused views answer PAINT_REFUSED_NO_CONTEXT.

Detach synchronously removes the property, hides EDIT, returns it to parking,
and clears its borrowed font. The same function rolls back a partially prepared
replacement. The SetParent return is asserted in lifecycle-audit builds. Release
has no recovery branch: the frozen qualification relies on live same-controller
windows and equal birth awareness, rather than introducing a partially detached
state. Earlier Windows versions without awareness queries retain the process-wide
awareness limitation described for birth above. DPI font replacement visits attached
EDITs and selects the node's presentation style; parked EDITs hold no cached font.

Replacement branches on the old row's claim. An audit assertion checks agreement
with native parentage. Preparation qualifies DPI, copies the property, reparents
fresh hidden, copies font/read-only/margins from old, and positions fresh immediately
above old at the same native frame. The controller's native replacement geometry
door uses the preceding sibling (or HWND_TOP), not old as the insert-after target.
Any refusal detaches fresh before partial-birth retirement; no view child or context
property remains while waiting for drain. Commit transfers the claim, shows fresh
without activation only if old's own WS_VISIBLE was set, detaches old, and restores
focus to fresh only if old held focus. The kernel subsequently retires old. No
message pump, body copy, or extra HWND retirement owner is introduced.

Lifecycle delivery parks EDIT before deferred container destruction. Scene teardown
releases contexts before platform/root destruction, including external WM_DESTROY
and retained descendants. EDIT destruction remains on the parking ledger's clock.
The existing dialog Tab path includes attached EDITs and excludes parked ones.
IME reads remain focus-gated and commit remains a no-op (page 1d); v1 does not
promise synchronous delivery of IME's posted characters.

Sources: page 4w v1.0 (precedence §0.4 > §0.3 > §0.2 > §0.1), 4w-2 Gate 1 v1.0
(§0.1 overrides the body), owner ruling W / probe P4-2a, and page 1d v0.4.
`Win32TextDocumentViewTests.cpp` contains the VM pins; the 4w-1 store pins still
apply. These additions are not Win32 build-verified or runtime-verified by Linux
checks. Real-IME and mixed-awareness branches print explicit skip reasons when
the fixture cannot exercise them; the VM handoff must supply those results.
