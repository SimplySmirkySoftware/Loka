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
  commands: its default store door does nothing. Win32 store creation explicitly
  refuses until page 1d is frozen and implemented, because committed IME text
  reaches the body later through the message queue (see [Native facts](#native-facts)).
  This is a documented refusal marker, not a compile-time wall. Production rails
  still create no stores, so the shipped `TextEditor` behavior is unchanged.

`TextReader` pulls a revision's logical UTF-8 body in bounded chunks, with CR,
LF, and CRLF each represented by one LF. Malformed content refuses the read;
it is never replaced silently. A reader refuses after a revision change,
withdrawal, or destruction of its document or registration. Do not pump while
a reader is open: finish dialogs before constructing it. Queries scan through
the same chunk door using fixed stack scratch. Do not call `queryLineRange`
per line to walk all lines; use one reader and count LF separators instead.

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
refuse by default; only the Null rail supplies stores in this step. The
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
- **Replacing and releasing a store** (replacement and view detach are later steps)
  - Open builds the new store completely before swapping it in.
  - Detaching a view allocates nothing.
  - Reclaim happens at the owning clock's boundary and fires nothing.
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
| macOS | `NSTextStorage` | exists without a view † | the view's undo manager † |
| Win32 | the EDIT's `HLOCAL`, through `EM_SETHANDLE` | swapping it for an empty handle before `DestroyWindow` | the EDIT; `EM_SETHANDLE` clears it |
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
Null has no native views to transfer. Later rail store-row changes must run the
shared birth conformance helper in `tests/support/TextStoreConformance.hpp`.

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
