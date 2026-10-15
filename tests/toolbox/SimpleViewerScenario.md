# SimpleViewer open-image characterization

Status: The template open-image and churn-replace cells have successful tracked System 7
audits. churn-replace was runtime-verified on the maciix/System 7/8 MB rig at
both the 512 KiB and 1 MiB application partitions (ten `image.load ok` results
each, identical to the tracked `churn-replace.audit`).
Owns: Dialog-free image-load measurements and rig-local scenario inputs.
Does not own: Production image-load policy or the fragmented-heap fix.
Code truth: `src/SimpleViewerScenarioDriver.cpp`, `run-scenario.sh`.
Verification: `tests/scripts/ScenarioToolsTest.py`, launch-only runner pins, and the
Retro68 build. Expected audits must come from the rig.

| Cell | Input | Sequence |
| --- | --- | --- |
| `simpleviewer open-sun` | `Sun.pict`, 11,826 bytes | Settle, open, settle, capture |
| `simpleviewer open-bulb` | `Bulb.pict`, 13,810 bytes | Settle, open, settle, capture |
| `simpleviewer churn-replace` | Both pictures above | Sun, eight alternating replacements (Bulb, Sun × 4), final Bulb |
| `simpleviewer open-wide-scroll` | Repository `Wide.pict`, 600×120, one-bit | Settle, open, post-open, Actual Size (Scroll), settle, post-scroll |

The wide cell stages `example/SimpleViewer/assets/Wide.pict`, reproduced by
`generate_wide_pict.py`, directly from the repository. After recording the decided
load outcome, it selects the production Actual Size (Scroll) action and waits
through the same settled-presentation wall before recording `post-scroll` with
`display.mode actual-scroll`. Its tracked audit was measured on the maciix/System 7
rig on 2026-10-15 (`image.load ok`, 600×120, post-scroll at tick 4).
The nominal 480×280 window minus the 200-pixel navigation column leaves 280
pixels for content and 264 for the scroll child, giving maximumX 336 for the
600-pixel image before rail margins and gaps. Toolbox currently applies
12-pixel side margins and an 8-pixel row gap (`ToolboxRender.cpp`,
`ToolboxPlatformLayoutHandlers.cpp`, and `RowWidthConsultation`), so its source
calculation is content width 248, child width 232, and maximumX 368. The owning
rig must confirm the presented geometry. Image height 120 fits the viewport:
the vertical bar is inactive and the horizontal bar is active.

The production MainNode and menu run in a 480×280 content window at (16, 41),
keeping its structure rectangle inside the rig's 640×480 screen. As in
SmirkBench, settled turns exclude Scene invalidation, native retirement and
ToolboxWindow invalidation. Opening occurs on settled turn 2; capture waits on
subsequent settled turns for a decided load outcome (through turn 60 for the
single-open cells). Churn gives each attempt 60 settled turns from issue.
All ten loads occur in one process, and each completed load is audited before
the next is requested. `ImageCell` in the driver owns the bounded count (8);
there is no environment override. The final load is Bulb, replacing Sun.

The test-access layer begins MainNode's existing ImageLoadSession and writes
its chooser result inside a StateTrackerGuard, without showing a dialog.
The driver resolves `File::Application() << File("Sun.pict")` (or `Bulb.pict`)
through Process Manager and `FSMakeFSSpec`, captures the FSSpec with
`ToolboxCaptureChosenFile`, and passes a `FileChooserResult`
carrying both its native locator and display-only filename, just like the dialog. The production
chooser adapters, data-fork read, decode and image commit run unchanged. The
read allocates its buffer through the Blob gate and refuses with
`READ_ALLOCATION_REFUSED` when the allocation is refused; there is no separate
capacity check before it (#1123). Capture first waits until the session has released its Flow, then
checks completed errors before considering the retained image. Success requires
the production chooser message for the requested filename as well as a valid
image. With one outstanding request and alternating filenames, the previous
image/message cannot certify the new load. Unknown outcomes time out.

The record contains `image.load` (`ok` or a numeric production flow error
code), `image.width`, `image.height`, the actual `image.bytes` read via
`GetEOF`, and `heap.probe_covers_image` (`yes`/`no`, `n/a` for the startup
cell): whether the platform's `queryLargestContiguousAllocation()` answer,
sampled immediately before submitting the load, covers the picture. That
answer is the larger of `MaxBlock()` and the room between the application
zone's top and `GetApplLimit()`; it neither compacts nor purges (a `MaxMem`
probe was tried and rejected because its purge bombed the viewer). The load
path does not consult this answer (#1123): it is a measurement of the heap at
submission, not the admission decision, which is the allocation itself. Raw
`FreeMem()` / probe numbers are not audit fields: they move with the
application's code size. The error code is matched from the completed
chooser message using ImageLoadSession's production formatter through a
TEST_BUILD-only friend; unknown messages fail the fixture. A terminal
`succeeded` means the measurement completed, including when the observed
image load failed in the single-open characterization cells. The churn cell
requires `image.load=ok` at every load, including the final replacement; a handled
load failure emits its numeric result and then fails the cell. Terminal success
alone is never the allocator acceptance criterion.

The pristine boot template (`MAME_HDA` in `.env-mame`) must contain data-fork
pictures `:Desktop Folder:Images:Sun.pict` and `:Desktop Folder:Images:Bulb.pict`.
These are rig-local inputs, never committed; the golden bundle keys on the rig.
The runner reads the selected data fork(s) from the template and places them beside
the app on the dev disk under its original name. Its HFS tools use the cell's
isolated `hfs-home`; a missing picture refuses before dev-disk staging.

```sh
HOME="$HFS_HOME" "$HMOUNT" "$MAME_HDA"
HOME="$HFS_HOME" "$HCOPY" -r ":Desktop Folder:Images:$PICT_NAME" "$STAGED"
HOME="$HFS_HOME" "$HUMOUNT"
```

`find_retro68_tool` resolves all three tools. `STAGED` is
`build/mame-scenario/simpleviewer/<cell>/<picture>`; the template is only read.

Build and run from the repository root on the delegator's rig:

```sh
cmake --preset retro68-68k-release
cmake --build --preset retro68-68k-release -j 8
tests/toolbox/run-scenario.sh simpleviewer open-sun --structural-audit
tests/toolbox/run-scenario.sh simpleviewer open-bulb --structural-audit
```

The existing hcopy extraction produces
`build/mame-scenario/simpleviewer/<cell>/LokaTestsToolbox.audit` and the capture
rectangle. Missing tracked expectations remain a refusal; a first measurement
is not a passing comparison. Inspect both actual audits and captures before
copying the measured audits to `tests/scenarios/expected/simpleviewer/` and
committing them, then repeat the structural commands. Heap facts can vary with
rig/partition state; repeat measurements before accepting a baseline.
Do not run `--update-golden` as part of this handoff.

## Rig facts measured on 2026-09-07 (maciix, System 7, 8 MB)

- The Finder needs **three** Tabs to land on the scenario application when
  the PICT is staged beside it (`FINDER_TAB_COUNT=3` in the runner).
- The chosen file goes through the dialog's own door: the driver resolves the
  application-relative FSSpec and hands the session a `FileChooserResult`
  whose item carries the display path. At the time of this measurement, a
  separate chosen-spec table carried identity; #1063 PR 3 replaces that table
  with `ToolboxCaptureChosenFile` and the File-owned locator. A path-less
  application-relative item is treated by the production projection as "no
  file selected" and cancels silently.
- The session advances its Flow over later settled turns; the driver waits
  up to 60 settled turns for a decided outcome (valid image or a completed
  error message) before failing the fixture.
- Generated 1-bit PICT variants bombed after load; generated PixMap variants
  loaded but stalled. The rig's own `Sun.pict` (4-bit PixMap, 11,826 bytes)
  loaded and completed. Whether the generated inputs are invalid or expose a
  viewer defect remains a separate investigation.
- The previous generated-cell audits are stale and removed. The tracked
  `open-sun.audit` and `open-bulb.audit` now both record `image.load ok`.
  These establish the existing single-open baselines; they do not establish
  churn behavior. The churn expectation is deliberately absent until measured.

## Startup cell

`simpleviewer startup` captures the settled viewer without opening anything.
It is the per-example startup golden the atomic bundle requires before any
other SimpleViewer cell can be staged, and it records `heap.probe_covers_image n/a` with `image.load none`.

Raw heap numbers (`FreeMem`, the contiguous-allocation probe) are not audit fields: they
move with the scenario application's code size, so an unrelated change would
break the byte-exact expectation. The audit keeps `heap.probe_covers_image`
(`yes`/`no`), which is the fact #614 turns on; measured numbers live in the
PR bodies and the issue.

## Churn acceptance bake (#642 PR 0)

Run `tests/toolbox/run-scenario.sh simpleviewer churn-replace --structural-audit`
on the maciix/System 7/8 MB rig with both template pictures. Four dev-disk items
use `FINDER_TAB_COUNT=4`; verify that navigation and the final Bulb capture on the
rig (host launch pins verify the selected count, not Finder behavior).

The scenario target copies the shipping SIZE resource: 512 KiB minimum and
1 MiB preferred. Test once with preferred forced to 512 KiB in the generated
`build/retro68/68k/Release/tests/toolbox/SimpleViewerSize.r`, rebuilding the
scenario APPL, then reconfigure to restore the shipping preferred size and
rebuild for the 1 MiB run. Machine RAM is not the application partition.

`tests/scenarios/expected/simpleviewer/churn-replace.audit` is already tracked
from the verified 1 MiB bake and matched the 512 KiB run byte-for-byte, so an
ordinary `run-scenario.sh simpleviewer churn-replace` verifies against it. Re-run
both partitions after any driver, picture, or partition change: inspect ten
successful `image.load` results, the final Bulb (158×189, 13,810 bytes), terminal
success, and the launch log's settled marker, and confirm the audit still matches.
Refresh the tracked audit deliberately (never synthesized from the sequence or the
existing image cells) only when a real behavior change makes both partitions agree
on a new expectation.
`--update-golden` does not bake structural audits; it still requires their
comparison and stages a complete rig-local pixel bundle for separate confirmation.
