# PaintDamage standalone

Status: the pre-B2 baseline is runtime-verified at 135 PASS / 0 FAIL on the `toolbox-maciix` rig (MAME maciix, 8 MB,
the rig descriptor's boot template), with a byte-identical `LOG.TXT` across
runs and across the Universal Interfaces and Multiversal builds (#1126).
B2 (#1199) adds four arms: 139 PASS / 0 FAIL on the same rig, with a
byte-identical `LOG.TXT` from the Universal Interfaces and Multiversal builds.
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

Six new arms are predicted, pending MAME verification on both header builds.
`ink-white-empty`, `ink-black-visible`, and `ink-unset-visible` paint the same
`MMMM` Text into a cleared scratch seat and count black pixels. At 1 bit white
leaves none; black and the window pair leave ink. This exercises real ForeColor.
The additivity arms compare uniform `MMMM` against `M` and `MMM` runs split only by
Red, logging both measured widths (predicted Chicago 12: 36), then compare line
widths and ending offsets when wrapping at 18 pixels. The existing 139-arm
runtime evidence remains valid for the old baseline; the predicted new log has
145 PASS arms and requires renewal by the delegator.
