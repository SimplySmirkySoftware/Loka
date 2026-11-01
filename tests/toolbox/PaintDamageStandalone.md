# PaintDamage standalone

Status: the pre-B2 baseline is runtime-verified at 135 PASS / 0 FAIL on the `toolbox-maciix` rig (MAME maciix, 8 MB,
the rig descriptor's boot template), with a byte-identical `LOG.TXT` across
runs and across the Universal Interfaces and Multiversal builds (#1126).
B2 (#1199) adds four arms: 139 PASS / 0 FAIL on the same rig, with a
byte-identical `LOG.TXT` from the Universal Interfaces and Multiversal builds.
A2 (#1196) adds six arms: 145 PASS / 0 FAIL on the same rig, with a
byte-identical `LOG.TXT` from both builds (#1240).
#1283 adds two SmallLabel arms (147 PASS). The ribbon group frame (#1310) adds
six arms: 153 PASS / 0 FAIL on the same rig, with a `LOG.TXT` byte-identical to
the tracked log from the Multiversal build. The Card bands (#1364) add eighteen
arms: 171 PASS / 0 FAIL on the same rig, byte-identical to the tracked log.
The stencil painter (decoration rally 1, P2-2) adds the card matrix, ALPHA,
ground, seam, scroll and timing arms: 408 PASS / 0 FAIL on the same rig, every
`card-v1` line at zero differences, and identical normal and profile logs.
Owns: Toolbox native paint-damage pins that need real QuickDraw and the
Control Manager: exact versus whole-window delivery, presentation history,
control and popup geometry, and busy-cursor borrows.
Does not own: the paint-damage contracts themselves. Their host pins are the
first line (`tests/toolbox/host/*Paint*`, `tests/PaintContractTests.cpp`);
these arms confirm them on the rail.
Code truth: `src/PaintDamageStandaloneMain.cpp`, `run-standalone.sh`.
Expected log: `expected/paintdamage-maciix.audit` (Classic CR line ends, compared
byte for byte).

## When to run it

Run it for any change to Toolbox paint, layout, or control drawing, and update
the expected log in the same change when an arm's outcome or logged value moves
on purpose:

```sh
cmake --build --preset retro68-68k-release --target LokaPaintDamage68K_APPL
LOKA_RUN_WAIT=240 tests/toolbox/run-standalone.sh \
  build/retro68/68k/Release/tests/toolbox/LokaPaintDamage68K.bin \
  --expect tests/toolbox/expected/paintdamage-maciix.audit
```

The app quits itself after its last arm and writes `LOG.TXT` on the dev disk.
A failing arm ends the run, so the arms after it never execute: read the first
`FAIL` line and the diagnostic line before it.

Toolbox CI builds the target (`LokaPaintDamage68K_APPL`), so a source change
that stops it compiling fails there. CI cannot run MAME, so outcomes are only
checked by this run.

## Why the expectations moved (#1126)

The standalone is outside ALL and was not run between #810 (133/0) and
2026-10-09. A fresh build of #810's commit still passes 133/0 on today's rig,
so the drift came from code, not from the rig. Each arm was updated to the
contract of the change that moved it:

| Change | Arms | Now |
|---|---|---|
| `a84cf328` (Toolbox text reuses an unchanged measurement) | `font-busy-layout-24-again`, `-other-family` | the repeat layouts mark their props dirty so they measure again |
| #1015 (Button, EditText, PopupMenu take the offered width) | `popup-exact-setup`, `button-enabled-exact`, `button-label-exact`, `button-label-wider-exact`, `offscreen-button-width-exact`, `column-container-button-no-refusal` | the popup is 164 wide (180 Box less the 16-pixel bar); the title sample sits at the native control's centre; a container-sized Button keeps its rectangle and answers exact |
| #1027 (an unchanged partial repaint keeps its history) | `popup-partial-unchanged-keeps-history`, `button-partial-unchanged-keeps-history` | exact instead of refused |
| #1080 (StateTrackerGuard joins the turn's clock) | `unknown-history`, `popup-detached-refuses`, `offscreen-detached-refuses` | the arm settles the turn before reading a result in the same turn |
| #1162 and #1065 | build | the busy-decode arm sizes its Blob without `LOKA_VERIFY` and decodes through a non-const context |

## Ground pins (#1199 B2)

PR 4a draws native controls in composition order; the transparent-upper-child ZStack overlap arms are retired under the page 2a §16/§18 overlay contract (#1438, #1442), while the non-overlapping image-vacated pins remain.

`image-vacated-setup` shows an ImageView alone in its own window (no ZStack,
no other drawers), so its dirty repaint takes the controller's image path. The
arm paints black inside the ImageView, where its next draw puts nothing, as a
stand-in for what a larger earlier image left behind, and requests the
ImageView's rectangle. `image-vacated-restores-window` requires that the
sample at image-local (30, 40) is white again, with dirty flush/replay
counters advancing and no full repaint. Removing the image path's erase leaves
the sample black. Their original introduction shifted the later `column_phase` values by three;
PR 4a retires six other phases, leaving those values at 55–60.

## Text ink pins (#1196 A2)

Six arms, runtime-verified at 145 PASS / 0 FAIL on both header builds (#1240).
`ink-white-empty`, `ink-black-visible`, and `ink-unset-visible` paint the same
`MMMM` Text into a cleared scratch seat and count black pixels. At 1 bit white
leaves none; black and the window pair leave ink. This exercises real ForeColor.
The additivity arms compare uniform `MMMM` against `M` and `MMM` runs split only by
Red, logging both measured widths (Chicago 12 measured `uniform=36 split=36`),
then compare line widths and ending offsets when wrapping at 18 pixels.

## Ribbon group frame

The final fixture mounts one `RibbonControl` group with a button and caption.
Pixel reads check a solid full-width bottom row, black at the bottom of the
left edge, and two black pixels in an eight-pixel vertical sample of its top
band. Ground samples inside the frame and positive ink samples in the button
and caption check that content survives. The fixture erases a rectangle crossing
the left edge, proves the erase removed a black pixel, then invalidates that
rectangle and checks restoration after the next delivery.

The six `ribbon-*` arms are runtime-verified on the `toolbox-maciix` rig (#1310).

## Card bands

Twelve painter arms use real QuickDraw in the final fixture's window. `CardPixels`
feeds the production framed layout into production `ToolboxCardBandContext`s;
its independent reference draws the actual whole body without band clipping or
local reconstruction. Both the minimum 17×17 frame and a translated 96×72 frame
check whole ink against the content seat, four-band equality, patterned-ground
transparency, a RIGHT-band damaged subrectangle, and enabled → narrow/disabled →
enabled placement. The relayout probe explicitly reconstructs the ground as a
layout-bearing broad replay does; it is a painter/placement probe, not a new
Scene invalidation-routing claim. Existing ribbon arms and earlier log lines
are unchanged.

Six further lines use an attached Card in its own window. The Card sits in a
Row seat whose live Box width claim changes from 96 to 14 and back to 96 (a
width-only Box constrains its content only as a Row seat; as the root it would
pass the full window width through). Four setup lines report, in order, that
the Card was found, that all four bands answer exact damage, that TOP and
BOTTOM damage span 96×72, and that the captured frame holds ink; the first
failure ends the list. The next ordinary Scene/window flush must then erase the
old ring and restore it pixel for pixel, without a manual erase or explicit
invalidation in the probe.

The eighteen `card-*` arms are runtime-verified on the `toolbox-maciix` rig
(#1364). The tracked `tests/toolbox/expected/paintdamage-maciix.audit` was
updated from that run's captured Classic-CR log: 171 PASS, the earlier 153 lines
unchanged and the Card lines added after `ribbon-partial-restores-edge`. The
standalone is not part of the preset's default build, so rebuild it with
`scripts/retro68-cmake.sh --build --preset retro68-68k-release --target
LokaPaintDamage68K_APPL` before running the command above.

## Shared card stencil acceptance (P2-2)

The candidate adds 237 checks (408 total if every arm passes); these are
build-verified only until the maciix run is recorded. The tracked v1 audit is
intentionally unchanged pending that run and owner review of DROP differences.

`card-stencil-matrix-whole` covers DROP and WIDE at 64 paired origin/size
residues each: `(x,y) = (16+i,16+3i mod 8)` and dimensions from the minimum
plus `(j,5j mod 8)`. The diagonal cases also check ALPHA equality, patterned
ground, and partial clips across tile/run and band/band joins. Eight NONE
cases use `card-none-border-only`. The whole-card oracle uses common density
and stencil sampling plus QuickDraw's border; it does not use band recovery
or the painter's transfer partition.

The independent v1 region oracle remains in `CardPixels::reference`. Every
DROP case prints its difference count; a nonzero count emits both captures
as rows of `0`/`1`, preceded by `card-dump` with window coordinates and
size. Preserve these for owner inspection before changing any golden image.
`card-scroll-applied-visible-wide` checks the attached fixture's nonzero
scroll, visible WIDE bands, and a changed row of actual pixels before
`card-scroll-incremental-equals-full`
compares the incremental result with a synchronous production full-window redraw.

Matrix comparisons use one nullable `NewPtrClear` allocation for two packed
images (at most 1,500 bytes); scroll uses 10,080 bytes. `CopyBits` captures the
port into word-aligned images with even row strides. Comparisons mask padding
and OR window-phase ground bytes into the expected image. Both images are
released within the arm. A small independent `GetPixel` probe checks capture
phase and the right edge before repainting. The whole-card oracle transfers
rows instead of issuing one `PaintRect` per ink pixel.
Refusal records `card-matrix-capture-allocation FAIL` or
`card-scroll-capture-allocation FAIL`; neither arm dereferences refused storage.
No new full-frame static buffer remains, and the SIZE partition is unchanged.

Timing lines are observations in `CARDTIME.TXT`, separate from the deterministic
`LOG.TXT` audit. The two timing-log-open arms must also pass. Extract that
file from the run's `LokaDev.hd` with hfsutils after the runner exits. Add `card-scroll`
ticks to `card-new-arms` ticks and require at most 3600 ticks (60 seconds).
`card-matrix-work` reports oracle generation, initial capture, and verification
ticks per case. Verification includes extra rendering, v1 comparison, dumps,
and audit writes; capture excludes the small probe, which is included in total
matrix time. These components are diagnostic, not separate budget allowances.
`card-timing` paths are -2 for small damage, -1 for all four bands, and 0–3
for individual band roles; each runs 16 repetitions. `card-v1-whole` times
the independent whole-body region oracle, not four v1 band paints.

For the timing breakdown, compile the painter and this standalone with
`LOKA_PROFILE_FUNC_TICKS=1`. The existing profiler reports `card.threshold`,
`card.buffer-clear`, `card.copy-bits`, `card.pattern-prepare`,
`card.paint-run`, and inclusive `card.band` calls/ticks. Compare uninstrumented
wall time too: profiler calls perturb the measurement and sub-tick buckets
can report zero. `card-stack before-draw` reports available native stack;
combine it with the candidate compiler's stack-usage report, including
callers and native QuickDraw overhead, rather than inferring headroom from
the application's SIZE resource.

Host transfer pins observe bytes, padding, transparent modes, partition
coverage, props, and allocation-refusal restoration. Since #1447 the painter
draws the border itself, from QuickDraw's radius-6 frame quadrant measured on
maciix, and no longer calls `FrameRoundRect`; the host pins compare it with an
independent copy of that measurement. The host fake does not rasterize
QuickDraw. Pixel and scroll acceptance remains the maciix run, where the
whole-card oracle still calls `FrameRoundRect` and so is what holds the
painter's border equal to QuickDraw's.

## Own control move (#1574)

`button-own-move-full-draw` retains the visible Button in the label window,
changes its layout offset, calls `drawForFlush`, and immediately reads
`WindowPeek::updateRgn` with `EmptyRgn`. Its diagnostic line records
`visible`, `moved`, and `update_empty`; all three must be true. It also requires
`title_draws=1` and `control_draws=1` (#1043 B4-3). A scoped QuickDraw text
bottleneck on the window port counts both the installed and replacement Button
titles (excluding sibling text) during that flush, chains to `StdText`, and
restores the previous GrafPort/CGrafPort
procedures afterwards. The control count is the controller's cumulative native
draw delta, covering ShowControl as well as Draw1Control. Removing the hide
before move/size makes the title count exceed one; retaining Draw1Control after
ShowControl also makes it exceed one. Omitting ShowControl's accounting makes
the control count zero. Without the own-move validation, the Control Manager leaves move/size damage pending and
`update_empty` is false. The host fixture pins admission with synthetic update
rectangles; it cannot establish the real Control Manager's invalidation behavior.

Runtime red/green verification and the tracked-log update are pending the
#1574 delegator's MAME run. Rebuild with the helper's validation removed for
the red run, then restore it for green. Do not generate expected audit lines
from the source: capture the real run. The 38-cell scenario bundle is expected
to remain byte-identical.

## Full-draw scroll observation (#1043 B2)

`viewport-full-draw-observed` accompanies `column-scroll-reveals-current-text`
(the 160-pixel reveal, `column_phase=64 delivery=FULL`). The `present_observe`
line reports the last moving pass's joined full-draw reasons, first vertical delta,
number of moved viewports, overlapping foreground unit seats, OS entry damage
bbox in window-local top/left/bottom/right order, and the first refusal.
`lastMovedPresentObservation` retains that moving pass: viewport republication
can cause the second full layout tracked by #1513, which has no movement.
`lastPresentObservation` still describes the last completed pass. The same line
logs its `last_refusal` separately (for that second pass, `no_viewport_moved`).
Both snapshots clear on explicit diagnostic reset. Empty OS update bboxes are
reported as `0,0,0,0`, independent of the window's port origin.
Presentation remains `erase`; the observation never selects a pixel-copy path.
The admitted scroll-only reason set is any nonempty subset of `layout_dirty` and
`viewport_input`; other reasons refuse as `reasons_beyond_layout`.
The arm expects `content_inputs` with `content_inputs=1` and
`unsupported_kinds=2`: the hidden-check phase wrote the offscreen Text without
any draw in between, so its PROPS mark is still pending when the reveal lays it
out, and that refusal precedes the mounted EditText and PopupMenu (the two
unsupported kinds). The reveal changes one vertical layout input,
boundary apply requests `layout_dirty`, the settled entry has no OS damage,
and the Column has no ZStack units. Global layout adds a rectangle
request without adding another full-draw reason. Skipping `noteViewportMove` at the layout publication point makes the
arm red (zero movement and `no_viewport_moved`). Writing the moving snapshot
unconditionally also makes the retention host pin red after the second pass;
removing empty-bbox normalization makes both damage-entry pins red.

This added arm awaits the delegator's MAME run and captured expected audit;
the preexisting tracked log is intentionally unchanged. Scenario dumps gain
no observation lines because the scenario runner compares whole audit files.

The `present_observe` record includes `content_inputs` (successful projected-leaf
visits with nonzero pre-layout inputs), `unsupported_kinds` (visited nodes whose
paint/clip contract is not admitted), `walk_reasons` (full requests arriving
during layout), and `phase_bound` (successful visited CardBand leaves whose
8x8 shadow stencil is anchored to window coordinates). A phase-bound move
whose vertical delta is not a multiple of 8 reports `dither_phase`, after
`unsupported_kind`, `not_translated`, `no_presented_baseline`, and
`enclosing_clip`, and before damage refusal. The Offscreen column has no
CardBand and its reveal is 160 = 8 × 20, so neither arm changes.
Counts cover visited nodes only: band windows may skip rows,
and only a successfully moved viewport contributes its local counts. They are
diagnostics, not a content-invariance proof; the verdict remains `erase`.

The subsequent `viewport-content-dirty-refused` arm writes the visible Text and reveals by
160 pixels in one transaction/completion. It requires `content_inputs` even
with those unsupported controls present. Removing the successful-leaf
`noteContentInputs()` call turns this arm red (delegator verifies in MAME).
The tracked audit is regenerated only from the delegator's green MAME run.

The selected moving snapshot now reports `eligibility_in=clean|refused` from
its matching entry field, and `collecting_first=<reason> site=<file:line|->`
from the first between-pass refusal. The adjacent `paint_result` line reports
the outgoing eligibility, `first=<reason>`, and `site`. Its derived `rule` is
1 for a recorded refusal, 2 for an unfinished render, 3 for a finished covering
paint, and 4 for a finished noncovering paint. `coverage` is `window` for rule 3,
`not_window` for rule 4, and `-` for rules 1–2 because those snapshots do not
retain entry coverage. Site literals exist only in diagnostic builds; a missing
site prints `-`. These lines describe the same selected snapshot, not a history
of every pass. Every render exit publishes `lastPresentObservation`, including
early returns; host tests read that copy directly. A window-shell failure after
render (for example, allocating the grow-box clip) can lower the live eligibility
after a clean published result, so the next admission can enter refused.
The tracked target audit requires delegator capture; Linux does not establish its
new values.

Placement observation appends `not_translated=<n> baseline=none|stale|valid
enclosing_clip=0|1` to `present_observe`. Full leaf rectangles and anchors are
compared before layout writes; paint clipping does not change that comparison.
The baseline is the first moved viewport's evaluation of its last CLEAN
certificate, including the port intersection (`copyDomain`). REFUSED settlement
clears every certificate. An enclosing ClipBox is observed separately. These
values run in shipping builds; formatting alone is diagnostic.
The updated target measurements and audit remain pending the delegator's run.
