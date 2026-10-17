# ScrollView design

> **Status:** Normative
>
> **Owns:** The ScrollView viewport fact, how each rail derives a scroll range,
> when a scroll bar exists, and when native scroll state commits
>
> **Does not own:** Exact API signatures, the lazy-list window selection
> ([KeyedSeatDesign.md](KeyedSeatDesign.md)), or the application scroll-intent
> pipe (`scrollTo_`, #537)
>
> **Code truth:** [ScrollView.hpp](../common/app/nodes/nestable/ScrollView.hpp),
> [LayoutHeuristics.hpp](../common/app/layout/LayoutHeuristics.hpp)
> (`queryContentOverflowWidth`),
> [ToolboxScrollViewLayout.cpp](../apple/toolbox/src/ToolboxScrollViewLayout.cpp),
> [ToolboxScrollViewDecisions.hpp](../apple/toolbox/src/ToolboxScrollViewDecisions.hpp),
> [Win32ScrollViewContext.cpp](../win32/src/context/Win32ScrollViewContext.cpp),
> [MacScrollViewContext.mm](../apple/macos/src/context/MacScrollViewContext.mm)
>
> **Verification:** [ScrollViewScopeTests.cpp](../tests/ScrollViewScopeTests.cpp) (Null),
> [ToolboxScrollViewPins.hpp](../tests/toolbox/host/ToolboxScrollViewPins.hpp),
> [ToolboxScrollViewContractTests.cpp](../tests/ToolboxScrollViewContractTests.cpp),
> [Win32ScrollViewTests.cpp](../tests/Win32ScrollViewTests.cpp),
> [MacScrollViewTests.mm](../tests/MacScrollViewTests.mm), and the MAME cell
> `simpleviewer open-wide-scroll`

## From AGENTS.md

`ScrollViewProps::viewport` is a rail result channel: the app lends its
`NodeState<Frame>`, the rail writes the clamped scroll origin into it through
`NodeState::set`, and the node reads the handle only for props identity. A
fact the rail reports and a request the app posts are two separate boxes with
one kind of writer each.

## The fact

The viewport is one `Frame` in **content coordinates**, not the seat rect.
`x` and `y` are the scroll origin: the content point shown at the seat's
top-left corner. `width` and `height` are reserved for the visible size and are
not reported today; `!hasSize()` means "not reported yet", and every rail
carries the stored size through unchanged. Apps initialise the state with
`Frame(0, 0, 0, 0)`.

Two helpers on `ScrollViewNode` are the only doors (#1357):

- `requestedOrigin()` reads the app's value as a request: an unbound state or
  a negative coordinate reads as 0, a coordinate above `SHRT_MAX` reads as
  `SHRT_MAX`.
- `publishOrigin(x, y)` writes the clamped origin back, keeping the stored
  size, and only when `x` or `y` differs from what is stored. The same-value
  no-op is the echo-loop breaker on every rail: a layout that re-applies the
  published origin publishes nothing.

Option D (one `Frame`, frozen 2026-10-15) was chosen over a second
`NodeState<int>` for X because every other two-axis carrier in Loka is one
`Frame` (`laidOutExtent`, the Canvas and LazyView viewports), a diagonal scroll
is one write, and `State<Frame>` costs no new template family on 68k.

## The horizontal range

A rail never measures content width during layout. The range comes from
`queryContentOverflowWidth(parent, spacing, source, width)`
([LayoutHeuristics.hpp](../common/app/layout/LayoutHeuristics.hpp), the R0
ruling): only content that **overflows the width it is offered** counts: a
declared width (`ImageView`/`RectSurface` with `size`, a fixed-size `Box`), an
INTRINSIC image whose natural width the rail's leaf answers, or a NATURAL row
(whose natural width sums its leaves, text included:
`testContentOverflowNestedMaximumAndOverflow`). Text, buttons and offer-bound
boxes placed directly in the content column are laid out at the offered width
and contribute 0 (`testContentOverflowIgnoresOfferBoundText`).

Cost: the question runs once per ScrollView layout pass and walks the
ScrollView's content subtree, not only its direct children: it descends into
Columns and pass-through nodes (`Fragment`, `Show`), and a NATURAL row's
answer visits that row's subtree through `queryNaturalWidth`. Every row it
walks belongs to the ScrollView's own content; measurement happens only at the
leaves named above.

On Toolbox and Win32, which lay out in integer pixels,
`maximumX = known ? max(0, contentWidth − visibleWidth) : 0`. macOS measures
the same range in projected points: the projected content width minus the
native clip width, converted to logical units once
(`MacScrollViewContext::horizontalRange`), so two logical widths that project
to the same native width leave no range
(`testMacScrollViewCongruentWidthsLeaveNoHorizontalRange`). **An unknown width
must not scroll**: otherwise content moved out of view could not be brought
back. That fallback is one expression per rail, kept in the same spelling.

The vertical range is the content height the layout accumulates, as before.

## Bars

The vertical bar is always present (inactive when there is no range), which
keeps the width offered to the children stable between passes. The horizontal
bar exists only while the horizontal range exists, so every ScrollView whose
content fits keeps the pixels it had before #580.

| Rail | Vertical bar | Horizontal bar | What the bar's presence changes |
|---|---|---|---|
| Toolbox | Always present (inactive when there is no range), 16 px column | Only while `maximumX > 0` and the seat has a height; bottom 16 px row ending 16 px before the right edge; the 16×16 corner is window ground | Content rect, hit rect and the vertical range lose the bottom 16 px ([ToolboxScrollViewDecisions.hpp](../apple/toolbox/src/ToolboxScrollViewDecisions.hpp)) |
| Win32 | Always present (`SIF_DISABLENOSCROLL`), so the client width never moves between passes | `SetScrollInfo(SB_HORZ)` without `SIF_DISABLENOSCROLL`: Windows shows it only while the range exists | The children, the clip and the vertical page get `clientHeightBelowHorizontalBar(seat)`: the bar at the viewport's DPI leaves the seat's projected native height and the remainder converts once |
| macOS | Always present | `setHasHorizontalScroller:` from the same decision | Legacy scrollers take `clientHeightBelowHorizontalScroller(seat)`; overlay scrollers take nothing |

On Toolbox, a ScrollView touching an answered window port shares its bars' borders
with the frame: the right edge adds one pixel to child/content width, and the
bottom adds one pixel to content height only with a horizontal bar; top/left
extensions belong only to the bars (#950 page 4, ruling 5 amended).
A bar touching or intersecting the window's 15 × 15 size box (answered by
`ToolboxWindowSizeBoxRect` in [ToolboxWindowGeometry.hpp](../apple/toolbox/src/ToolboxWindowGeometry.hpp))
ends one pixel past its top/left edge, sharing that line, floored at the bar's start.
The enlarged content rect governs clipping, hit registration, ranges and pages;
without an answered port, frame contacts are absent.

## Decide first, commit once

The shape every rail follows (converged on #1370 after review):

1. Decide the horizontal range and whether the horizontal bar exists **before
   the children are laid out**, from pure questions (the R0 width, the system
   bar thickness). Nothing native changes here.
2. Lay out the children against the height that the bar will leave, and clip
   and hit-test with the same rect.
3. Only when the whole traversal succeeded: apply both bars' native state,
   then `publishOrigin(clampedX, clampedY)` once.
4. A refused traversal (short-range refusal, requeue) leaves the committed
   scroll state untouched: the bars, their native ranges and positions, the
   document (macOS) and the fact. Children the refused pass already placed
   are the rail's own business: Toolbox requeues the whole seat, while Win32
   leaves them at the refused pass's translation (#1372).

Toolbox applies the bars through its control ledger (`ViewportScrollBarBinding`
holds one id per axis, allocated together and released together; the content
rect of the successful pass is stored for render). Win32 commits through one
`setScrollMetrics(bar, …)` body per axis. macOS commits through one
`commitScrollMetrics` body that sets the scroller, the document frame
(`max(content width, clip width)` × content height) and `scrollPoint(x, y)`,
with the clip view's bounds notifications held off during the commit.

## Input doors

Native input moves one axis at a time on Toolbox and Win32, and the other axis
keeps the value the preceding layout clamped:

- Toolbox: the clicked `scrollBarProc` is matched against the binding's two
  ids; the commit publishes `(settled, y)` or `(x, settled)`.
- Win32: `WM_VSCROLL` and `WM_HSCROLL` enter one `handleScroll(bar, …)` through
  `Win32InputDoor::scroll`; the wheel (`WM_MOUSEWHEEL`, `WM_MOUSEHWHEEL` as a
  local constant because the XP headers lack it) steps through the same body,
  positive horizontal delta scrolling right.
- macOS: AppKit scrolls both axes itself; the clip view's bounds notification
  clamps `origin.x` and `origin.y` and publishes one `Frame`.

## Known debts

- #1372: a short-range-refused Win32 traversal leaves the children it already
  placed at the refused pass's translation (both axes; pre-#580 for Y).
- The request box (`scrollTo_`, #537) is not consumed by any rail yet, so an
  app has no sanctioned door to move the viewport; SimpleViewer resets it by
  writing the fact box directly (`viewport_.set(Frame(0, 0, …))`), which makes
  the app a second writer of a rail result channel. That is a recorded debt,
  not a pattern to copy.
- The visible size is not reported; its first reader is the lazy-list window
  fold (LazyColumn reading `viewport_` directly).
