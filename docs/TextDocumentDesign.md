# Text document design

> **Status:** Draft. The app-facing shape (this page) is frozen by owner
> ruling (2026-11-03). The kernel seat that creates the native store and
> writes the fact is designed separately and is not frozen. Nothing here is
> implemented yet; until the new editor lands, `TextBuffer` remains the
> shipped model. The policy in [From AGENTS.md](#from-agentsmd) applies now.
>
> **Owns:** where an editable text's body lives, the app-facing document fact,
> positions, requests, and the migration away from `TextBuffer`
>
> **Does not own:** exact API signatures, the kernel seat and its reconcile,
> Edit-menu routing, capacity, or per-rail native mechanics
>
> **Code truth:** none yet. The retiring model is
> [TextBuffer.hpp](../common/app/TextBuffer.hpp),
> [LineCursor.hpp](../common/app/style/LineCursor.hpp),
> [TextEditorDocument.hpp](../common/app/nodes/controls/TextEditorDocument.hpp)
> and [TextEditorDiff.hpp](../common/app/nodes/controls/TextEditorDiff.hpp).
>
> **Verification:** none yet. The native behavior the design relies on was
> observed on the rigs and is recorded under [Native facts](#native-facts).

## From AGENTS.md

An editable text's body lives once, in the native editor's own store (the MLTE
object, `NSTextStorage`, the EDIT control's `HLOCAL`, TextEdit's text handle).
Loka keeps no second copy and reads the body out only when an operation needs
it: save, search, or an app query. The app reads one fact box, written by a
single kernel reconcile at the outer completion in the
[Focus](FocusDesign.md#from-agentsmd) shape. Rails answer that reconcile and
never write the fact from native notifications. `TextBuffer` and `LineCursor`
are retiring, so new text features are not built on them.

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

One keystroke in a 4 MiB document costs about 0.9 s. Reading the body out of
MLTE takes 35 ms of that, twice; the rest is Loka's conversion, diff and
allocation. The native editors already hold the body; the second copy is
where the cost is.

A rail that draws its own text keeps the body in a Loka-owned store, which
then plays the native role. It is still the only copy.

## The document

- `TextDocument` is declared with `this->document(doc_)` only in the
  constructor of a window's root Boundary. This door is separate from
  `state()`.
  - A window may declare several documents.
  - Declaring one inside a Keyed or LazyScope generation is refused.
  - A document used inside a Match arm is declared at the root, so the body
    outlives the arm.
  - A varying number of documents in one window (tabs) is a v1 limit.
  - Replacing the Scene ends its documents together with the root Boundary.
- The document has one fact box, `State<TextDocumentFact>`. It carries:
  - the state;
  - `textRevision`, monotonic within the document and continuing across
    store rebuilds;
  - the union of changed ranges, where the rail can report one;
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

- **Queries.** Line count, size, line contents, and creating a position
  answer only in `READY` with no unreconciled native change. Otherwise they
  refuse, through the `bool queryX(T &out)` shape. A rail that cannot answer
  a query refuses it.
- **Positions.** `TextPosition` is opaque: only the document makes one.
  - `None()` and copy are public, and `!=` compares full identity.
  - `queryOrder(a, b, out)` compares order.
  - `queryUtf16Offset` and `queryUtf8Offset` convert a position to an offset.
  - All of these refuse when the position's `textRevision` differs from the
    document's.
  - There is no position arithmetic.
- **Caret request.** The caret request is `Request<TextPosition>`.
- **Writes.** A write (open, new, whole replacement) carries its caret as a
  `TextPlacement`: start, end, or a UTF-8 byte offset into the written body.
  The caret after Open therefore arrives with the body it belongs to.
  - A request never carries the body by value. It names the source or holds
    a Blob handle, so copying it and comparing it with `!=` stay O(1).
  - Whole replacement is a `Request`; partial edits are a `RequestQueue`.
- **Held and dropped requests.**
  - Requests that arrive in `AWAITING_STORE` or `MARKED` stay in their slot
    until the document is `READY`, so the first write posted at attach is
    not lost.
  - In `UNAVAILABLE`, or on a revision mismatch, a request is taken and
    dropped (the refusal rule in
    [Request delivery](RequestDeliveryDesign.md#from-agentsmd)).
- **Commands.** The rail commits uncommitted IME text before it runs one of
  these entries, so nothing is saved while `MARKED`:
  - an Edit command;
  - a menu command;
  - an Apple event (save, the close confirmation, Quit).

  The kernel design says how each rail does this; the observed native
  behavior is under [Native facts](#native-facts).

## Migration

- `TextBuffer` does not become a compatibility layer over the document,
  because returning line ids would need a copy of the body.
- The new editor node is a separate type. The old `TextEditor`, `TextBuffer`
  and `LineCursor` stay untouched beside it until every rail has moved.
- The last migration PR deletes them, together with `TextEditorDiff` and
  `TextEditorDocument`, and re-captures the SimpleText golden.

## Native facts

These are observed OS behaviors the design relies on. They are recorded
here, not contracts.

| Rail | Store | Detached from the view by | Undo history lives in |
|---|---|---|---|
| Toolbox Unicode | MLTE `TXNObject`, created with no window | attaching it to an offscreen GWorld | the object |
| Toolbox TextEdit | the `hText` handle | swapping it for an empty handle before `TEDispose` | the TE record |
| macOS | `NSTextStorage` | exists without a view | the view's undo manager |
| Win32 | the EDIT's `HLOCAL`, through `EM_SETHANDLE` | swapping it for an empty handle before `DestroyWindow` | the EDIT; `EM_SETHANDLE` clears it |
| Null | a host string | not needed | none |

- **Win32** (Windows 11, MS-IME; the comctl32 6 EDIT and the user32 EDIT
  behave alike):
  - `EM_SETHANDLE` and multiline `WM_SETTEXT` raise no `EN_CHANGE`. They
    reset `EM_GETMODIFY` and clear undo.
  - `EN_CHANGE` carries no range, and one replacement or undo raises it
    twice.
  - `EM_GETMODIFY` means "touched since Loka last wrote", not "differs from
    the saved text".
  - Uncommitted IME text never enters the EDIT's body.
  - When `ImmNotifyIME(CPS_COMPLETE)` returns, the body is still unchanged.
    The committed characters arrive afterwards, as posted `WM_CHAR`.
- **macOS** (macOS 26, a TextKit 1 `NSTextView` made the way the Loka rail
  makes it; marked text set by the probe itself, not through a real input
  method):
  - Marked text is inside the `NSTextStorage` string while composing;
    `hasMarkedText` and `markedRange` report it.
  - `unmarkText` commits synchronously: the committed text is in the storage
    when the call returns.
  - `[[view inputContext] discardMarkedText]` changes neither the body nor
    the marked text.
  - The storage delegate reports an edited range and a change in length for
    every change, Loka's own writes included. `textDidChange:` fires only for
    changes made through the view.
  - Writing the storage directly (`replaceCharactersInRange:withString:`,
    `setString:`) registers no undo action.
  - Not yet observed: the order in which a real input method calls
    `setMarkedText:` and `insertText:`, and Mac OS X 10.4 to 10.6.
- **Toolbox MLTE 1.3** (Mac OS 9.2):
  - `TXNSetData` advances `TXNGetChangeCount` by two and leaves an undoable
    action.
  - The change count drops on undo, while `TXNGetActionChangeCount` only
    rises.
  - No API reports the changed range.
  - With the bottom-line input window, uncommitted text stays out of the
    body, and committed text is in the body when `WaitNextEvent` returns.
  - No commit door is reachable from the application:
    `FixTSMDocument` cannot name MLTE's TSM document.
