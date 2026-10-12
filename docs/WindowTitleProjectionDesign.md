# Window Title Projection Design

Status: implemented narrow design note for PR #416.

## Contract

`Window::titleState()` remains the logical, mutable title owned by application
code for non-keyed windows. A keyed document window's `titleState()` is
the admission walk's projection target and is roster-owned: its seat refuses
`titleStatePtr` and `displayTitleStatePtr` (#1245).
`WindowProps::displayTitleState()` optionally supplies a read-only live
projection for native chrome. Without that explicit projection, the display
state is the logical title state, so existing applications and platform
vehicles keep their previous behavior.

Scenario loop owners use the split to compose their operator label without
making the application and the loop write the same mutable state. Both
`ScenarioReel` and autonomous `StandaloneRunControl` reuse
`ScenarioReelPosition` and `ScenarioReelTitle` for this fact:

```text
application -> logical title State ----> Window::titleState()
                    |                         (read/write)
                    v
reel cell + cycle -> composed display State -> native window title
                    (reel owned, read-only to Window)
```

The loop already owns an every-tick idle pump. After driving the current
scenario, that owner compares the logical title with the last title it
composed. A change publishes one new display value in the Window tracker
transaction; an unchanged value does no formatting or State write. A successful
re-arm publishes the new cell and cycle through the same display state.

For non-keyed windows, application code remains free to read and write
`titleState()` and always sees its own logical value. While a loop presentation is installed, the reel
guarantees that native chrome displays that value followed by the current cell
and one-based cycle.

## Lifetime

The loop owner owns the composed display state, and it outlives each Window
created from its config. Native Window backends therefore register visibility,
display-title, and frame observers through the Window-owned native observer
ledger. Each backend detaches the ledger before native teardown; the ledger's
destructor repeats the detach as a non-optional cleanup fallback. A borrowed
state consequently has no observer entry whose callback data names a reclaimed
Window.

## Complexity And Follow-Up Boundary

This change spans logical State, Window projection, and three native backends,
and it adds one borrowed cross-boundary state plus a cleanup owner. Keeping
those risks in one generic display-title door and one private observer ledger
avoids a reel-specific platform hook or three hand-maintained teardown pairs.

The idle comparison is intentionally presentation-local. Keyed document windows
receive roster-owned title and edited facts through admission (#1245); composed
chrome such as Win32's `*` is projected by the rail, not by a derived Window
State. The logical/display split for non-keyed presentation owners and the
observer-ledger teardown contract remain unchanged.
