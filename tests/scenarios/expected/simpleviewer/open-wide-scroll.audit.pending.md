# Pending measured audit: open-wide-scroll

The delegator must run `tests/toolbox/run-scenario.sh simpleviewer open-wide-scroll --structural-audit`
on the owning MAME rig, inspect the post-open and post-scroll verdicts, and copy
`build/mame-scenario/simpleviewer/open-wide-scroll/LokaTestsToolbox.audit` here as
`open-wide-scroll.audit`. Remove this marker after recording that measured audit.
No expected audit is synthesized. The cell loads the repository's original
600×120 `Wide.pict`, then selects Actual Size (Scroll); the final verdict must
report `display.mode actual-scroll` and `image.load ok`.
