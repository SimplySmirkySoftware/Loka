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

`image-ground-transparent` reads local (110, 20) inside the ImageDamage
surface's original sprite, below the ImageView placeholder label and inside
its frame. It expects black after the full paint. The base and the temporary
ImageView-self-erase mutant are expected to fail this arm. This is a rail
regression pin, not a guarantee that nodes overlaying RectSurface compose
portably. The original replay arm now searches the label area after the sprite
moves away; searching it before the move would select exposed sprite ink.
`image-overlap-shows-surface` reads surface-local (140, 20) after the move: the
moved sprite under the transparent placeholder. A ZStack window holding an
ImageView replays exact updates in composition order, so this pixel stays
black; the kind-ordered fast path erased it to the window ground. The replay's
render runs under a clip, so the overlap arm no longer requires
`totalRenderCalls` to stay unchanged; `image_whole_window=0` still pins that no
whole-window repaint happens.

`image-vacated-setup` shows an ImageView alone in its own window (no ZStack,
no other drawers), so its dirty repaint takes the controller's image path. The
arm paints black inside the ImageView, where its next draw puts nothing, as a
stand-in for what a larger earlier image left behind, and requests the
ImageView's rectangle. `image-vacated-restores-window` requires that the
sample at image-local (30, 40) is white again, with dirty flush/replay
counters advancing and no full repaint. Removing the image path's erase leaves
the sample black. The three new vacated phases also shift the six later
`column_phase` audit values from 58–63 to 61–66.

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

Matrix comparisons retain one packed native capture (at most 675 bytes), then
read the current port directly. The scroll arm retains one 5,040-byte packed
capture. Both use nullable `NewPtrClear` storage disposed within the arm.
Refusal records `card-matrix-capture-allocation FAIL` or
`card-scroll-capture-allocation FAIL`; neither arm dereferences refused storage.
No new full-frame static buffer remains, and the SIZE partition is unchanged.

Timing lines are observations in `CARDTIME.TXT`, separate from the deterministic
`LOG.TXT` audit. The two timing-log-open arms must also pass. Extract that
file from the run's `LokaDev.hd` with hfsutils after the runner exits. Add `card-scroll`
ticks to `card-new-arms` ticks and require at most 3600 ticks (60 seconds).
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
coverage, call order, props, and allocation-refusal restoration. The host
fake does not rasterize QuickDraw; pixel and scroll acceptance remains the
maciix run.
