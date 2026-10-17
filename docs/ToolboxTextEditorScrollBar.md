# Toolbox TextEditor vertical scroll bar

Status: Normative (Toolbox rail, #1414).
Owns: Toolbox editor bar ownership, completion and test routing.
Does not own: SimpleText's window-edge layout, horizontal bars, live tracking,
inactive-window hilite (#502), or inset size-box overlap (#1395).
Code truth: `ToolboxTextEditorGeometry.hpp`, `ToolboxEditorScrollBar.cpp`,
`ToolboxTextEditorContext.cpp`, `ToolboxScrollBarLedger.hpp`.
Verification: `ToolboxEditorScrollBarPins.hpp`; the `smirkbench/text-editor-scroll` scenario cell.

The frozen #1414 v6 ruling keeps native controls in the existing controller
ledger. The context reserves one ID and returns it only on RETIRED. TE loss
and retained detach revoke and hide without returning that ID. Both bulk clear
and individual retirement clear the association before hiding and queueing.
New rows, including app and viewport rows, default to no editor association.

The geometry value supplies frame, text, bar and text hit rectangles. The
frame shares window-port edges; text excludes the entire bar column. An
inactive or hidden bar's column consumes clicks without activating TextEdit.
Native FindControl declines those controls on System 7, so relying on its
inactive-control branch would lose that isolation.

The native destination remains the scroll truth. Range uses visual lines,
including a terminal empty line, in the unclipped text height. The last stop
is bottom-aligned. Queries preserve fractional positions; completion only
normalizes positions outside the range. Geometry, range, destination edges
and deltas are checked before native narrowing. The total-height short bound
is a deliberately conservative supported-range limit.

Completion synchronizes after SCROLL_CLEANUP. Render alone creates rows;
presentation never creates or places one. Shown presentation requires a live
association and `usedThisFrame`; hidden presentation bypasses that placement
gate. Idle compares the focused editor's destination without taking requests;
ordinary EditText keeps TEIdle, and the existing retry pass keeps its settlement.
Selection auto-scroll may leave the bar behind until TEClick returns.

The editor and viewport apply bodies remain separate deliberately. The
viewport apply is a render-owned value projection after layout ensure. Editor
presentation also owns absence, placement refusal, window-port selection,
change detection and damage outside a batch. Combining them would make the
viewport acquire policy it does not need. Creation, tracking, drawing, pooling
and retirement remain shared. The app bar continues to bind State.

## Cost

Geometry and range read one editor's placement and native text metadata.
Each completion, changed idle, and successful render ensure calls presentation
once; render can also synchronize during its preceding layout/attach completion.
Presentation, ensure, revocation and terminal destruction each search the
controller's own B bar rows by ID. Idle walks its E edit rows for retry and
idles at most one focused row. An editor hit uses the hit row's copied
association before any viewport-owner search. Dirty replay retains its existing
controller-owned walks; the extracted replay body changes no routing policy.

No new queue, native clock, source registry or persistent presentation cache
was introduced. The context's only new primitive is its ID: render reservation
writes it, context methods read it, and it cannot be derived from placement or
phase. The association's install/revoke operations own its borrowed pointer.
