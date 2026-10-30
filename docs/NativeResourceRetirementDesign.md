# Native Resource Retirement

> **Status:** Normative, #1065 frozen ruling v1 with REFUTE 2 amendments and
> owner decisions A/B/C (2026-10-07).
>
> **Owns:** Shared native-resource obligations, consuming publication, safe
> completion, and quiescent shutdown. Exact signatures remain in headers.
>
> **Delivery:** PR 1 provides common machinery and host pins. PR 2 wires Win32
> decode and capture. PR 3 wires macOS decode and capture. PR 4 removes the
> unused Toolbox owned-PICT path. Toolbox has no ticketed producer today.

## Owner and clock

```text
RunApp construction: PlatformContext -> ConfigT -> App
       destruction: App -> ConfigT -> PlatformContext

PlatformContext (noncopyable)
  owns NativeResourceRetirement (value, noncopyable)
    owns held_ <-> Ticket { owner, prev, next, handle, dispose }
    owns queued_ -> Ticket
    owns inFlight_ -> Ticket

rail producer --borrows context--> Reservation --reserves--> Ticket
ImageRecord --releaseUserData--> Ticket --owner--> retirement service
App::reclaimWindows --config_->getPlatformContext()--> retirement service
```

A ticket belongs to exactly one context. Contexts are independent; there is no
process-global queue. Shared Images can outlive windows and Boundaries but must
not outlive their PlatformContext. Controller retirement queues and Boundary
Held storage have different owners and are not replaced by this service.
Native side effects belong behind the platform clock, after logical release;
Image's common releaser still runs synchronously exactly once on last-copy loss
or allocation refusal. The releaser transfers the obligation before ImageRecord
storage disappears. Image and ImageRecord need no special retirement fields.

Tickets have private fields. Only the rail-internal
`app/internal/NativeResourceReservation.hpp` exposes reservation; app-facing
headers do not include it. Rail translation units bind static disposal
functions. This is type-erased, rail-bound disposal, not compile-time typed
disposal and not an application callback registry. Inspection belongs solely
in the existing testing access layer.

## Toolbox: memory-only payloads and borrowed pictures

Owner decision A (2026-10-07), implemented by PR 4, removes the owned-PICT
path, which had no production caller. Toolbox has no ticketed native-resource
producer today. PICT_BYTES payloads release inline through the existing Image
releaser: this returns memory only and preserves Classic's immediate capacity
recovery before replacement allocation (pinned by the SimpleViewer capacity
retry in tests/FlowDslTests.cpp).

MakeImageFromPicHandle is borrow-only. The caller keeps the PicHandle alive
past every Image copy and disposes of it itself. Loka never calls KillPicture
on wrapper refusal, Image record/control-block refusal, or last-copy release.
The wrapper's kind determines payload ownership: PICT is borrowed and
PICT_BYTES owns its memory payload; no separate ownership flag is needed.

A future owning Toolbox producer must reserve through
loka::app::internal::Reservation before any native acquisition, then publish
through the common consuming ticket path. It must not restore inline native
disposal or add a Toolbox-specific queue or drain.

## Ticket state machine

```text
reserve (LokaNew failure -> refuse creation)
  |
  v
reserved in held_ --publish non-null handle--> live in held_
  |                                            |
  | cancel / early return / null handle        | last Image copy or
  v                                            | FromNative refusal
cancelled (LokaDelete)                          v
                                             queued_
                                               | detach finite snapshot
                                               v
                                             inFlight_
                                               | dispose, then unlink/delete
                                               v
                                             disposed
```

`held_` is doubly linked so cancellation and transfer cost O(1). `queued_` and
`inFlight_` are singly linked snapshots. Transfer allocates nothing. A ticket's
handle is null while reserved and becomes non-null at consuming publication;
this permits auditing armed reservations without a separate flag or counter.
The handle is recorded by release as well. There is no separate state enum:
list membership and the Reservation's one nullable ticket pointer carry phase.

Reservation is a noncopyable stack RAII helper. Its constructor reserves a
ticket bound to the disposer. A failed reservation refuses creation **before
any native acquisition**, including native temporaries. Its destructor unlinks
and deletes an unconsumed ticket. Before entering Image::FromNative,
`publishImage` consumes the ticket. Both success and Image record/control-block
allocation refusal consume it: refusal synchronously calls ReleaseThroughTicket
and queues the ticket. Disarming only on success would double-cancel it.
A null handle is not published and leaves the ticket armed for cancellation.
Publication on an invalid reservation refuses without consuming the handle.

Pre-publication native failure is construction rollback: the producer releases
its partial native acquisitions inline, and Reservation cancels its ticket.
Reservation does not own those partial acquisitions. Acquisition ordering is
reserve, acquire, finish native construction, publish. Win32 decode reserves
after blob/range validation and before COM/WIC in `Win32PlatformContext::createImageFromBlob`;
`CaptureWindowClientBitmap` reserves after geometry validation and before
GetWindowDC. The Window passes its PlatformContext borrow into
Win32ScenePlatformController at construction; Button and Text contexts capture
through that controller. A standalone controller without the borrow refuses
capture. The borrow is fixed for the controller lifetime; the ancestor context
must outlive the controller, its node contexts, and all resulting Images.

On macOS `MacPlatformContext::createImageFromBlob` reserves after blob/range
validation and before both NSData and NSImage allocation. Text and Button
`captureBitmap` remain const and forward through their existing controller link
to `MacScenePlatformController::captureViewBitmap`. One `PlatformContext *const`
borrow is supplied from `Window::context()` when MacWindow constructs the
controller. The controller alone reads that borrow and forwards to the shared
rail-internal `loka::macos::CaptureViewBitmap` in MacBitmapCapture.mm. An absent
context refuses capture; no inline fallback.
The helper validates view geometry, reserves before
`bitmapImageRepForCachingDisplayInRect:`, retains the returned autoreleased
bitmap, caches pixels, then consumes the retained obligation through
`publishImage`. A nil native result cancels the reservation. FromNative record
or control-block refusal queues the retained bitmap; it must not be released a
second time by the producer. Controllers and their node contexts die before the
ancestor context; captured Images may survive the window, but not that context.
Caller replacement remains temporary-build/commit where required;
publication is a consuming operation, not a preserve-old replacement policy.

## Eligible completion and re-entrancy

The only ordinary drain site is the tail of `App::reclaimWindows`, after
`pendingReclaim_.clear()` and `flushingWindowWork_ = false`, inside its entry
guard. App borrows the service through its configuration's platform context;
missing configuration or context is a no-op. This also covers no-window tails.
No rail calls drain directly.

Drain refuses while an Operation is active, including a joined nested close,
or while `inFlight_` is nonempty. It detaches queued work into `inFlight_`, then
for each current ticket calls dispose, advances to current->next, and deletes
the ticket with LokaDelete. The current ticket stays linked throughout dispose,
including the last ticket. This derives the re-entrancy wall from real phase
rather than a boolean. Work queued by disposal waits for the next snapshot.
After a completed drain, inFlight is empty; queued is empty only when disposal
enqueued nothing new.

The guarantee is the **next actual eligible outer completion**, not every turn
or any fixed elapsed-time limit. Existing rail completion reachability is to be
pinned in the corresponding integration PRs: Win32 idle and non-idle tails,
macOS timer ticks. Toolbox has no native retirement producer to drain. WM_QUIT can
bypass a normal tail; context final drain covers shutdown obligations.

Code running a nested modal outside an Operation must not carry a raw native
handle borrowed from an Image across that modal call. Operation exclusion is
not a universal proof of modal safety. Synchronous modal work inside an
Operation defers retirement; a deferred presenter outside it may admit eligible
nested completions. The macOS Open and Save panels (#1136) share
`presentIfNeeded` and
`presentDialog`: the deferred presenter schedules a timer and enters the panel
outside the apply Operation in the ordinary path. Nested ticks can then close
an outer Operation and drain. If presenter allocation fails, `presentIfNeeded`
calls `presentDialog` synchronously inside apply: nested ticks join the active
Operation and cannot drain. A deferred timer dispatched by an already nested
loop can also join an active Operation; eligibility depends on actual entry
state, not presenter identity. This applies to NSOpenPanel `runModal`, NSSavePanel
`runModal`, and its legacy `runModalForDirectory:file:` fallback alike. Each
branch copies logical inputs and carries only the revocable ReturnPort across
the modal call, not an Image-derived raw handle. Image loss during synchronous
modal dwell remains queued until an actual eligible completion.

Disposers are rail static functions only. They must not write State, create an
Image, reserve a ticket, or destroy the context. The Win32 bitmap disposers (`ReleaseWin32Bitmap` and `ReleaseCapturedBitmap`)
call DeleteObject only and drop no Loka value; it must not create a new queued cascade after the last pre-sleep
tail. The macOS `ReleaseNSImage` and `ReleaseCapturedBitmap` disposers send `release`
only. They must not explicitly run application work. A dealloc cascade may drop
an existing Loka Image; its ticket queues for the next completion, outside the
current finite snapshot. `MacApp::flushInvalidationsTick` closes its Operation
before calling `App::reclaimWindows`, under a local NSAutoreleasePool so dealloc
chains may autorelease even on direct tick entry. Neither the tick nor its timer
callback previously had a lexical pool; normal AppKit dispatch supplies an outer
pool but direct callers need not. The repeating timer is the normal route to
later completions
for cascaded tickets; allocation refusal or undelivered modal modes do not
establish any finite wall-clock guarantee. The example main's outer application
pool survives RunApp
and thus the context final drain. There is no macOS-specific drain call.

## Shutdown and invalid lifetime containment

[Sudden termination](SuddenTerminationDesign.md) does not invoke this document's retirement and does not relax the quiescent-shutdown premise.

Exit order is App, ConfigT, PlatformContext. Config-owned Images dropped after
App destruction still queue into the surviving context. PlatformContext's value
member performs final drain. Entry must be quiescent: no active Operation, no
armed Reservation, no producer, and no in-flight disposal. Lifecycle-audit builds
assert the representable preconditions (Operation, in-flight list, and no held
null handles); no separate producer activity counter is needed in this
single-threaded regime.

At entry count H held tickets once. Drain at most H + 1 snapshots: one initial
queued snapshot, and each further snapshot requires a distinct held ticket to
be released. Quiescence prohibits new reservations or publication. This bounds
snapshot passes, not native disposer duration. The final audit requires **all
three lists zero**. Native resources/runtime services used by static disposers
must still be available during base-member destruction.

For invalid late Image lifetime only, release builds revoke the owner of every
leftover held ticket by setting owner to zero. Its eventual Image releaser
deletes only the ticket and never calls the native disposer or dead owner.
This is **release-build containment of invalid lifetime**, not a valid shutdown
or a promised short-lived leak. The allocation backend must still be valid for
late ticket deletion. Other shutdown precondition violations are not supported
alternate lifetime regimes. Static native Images with retirement tickets are
forbidden; explicit owners must release them before their context dies. Passive
Toolbox PICT_BYTES and borrowed PICT paths remain outside native retirement.

## Admission, cost, and future kinds

No speculative pending-count cap is introduced. Ticket allocation refusal is
not a native-byte budget. Acceptance is measured per workload by PRs 2 and 3:
record baseline and retirement peak bytes/handles, image sizes, burst length,
modal path/dwell, refusal behavior, and recovery after completion. Include
multiple replacements within one outer turn and a LazyView generation swap,
plus ordinary Scrapbook navigation and SimpleViewer replacement. Win32 records
GDI handles. A provisional extra-one-image criterion is workload-specific,
never a universal bound; excess requires an evidence-based decision before
rail acceptance. Never drain from Flow to recover memory.

### Win32 measured acceptance

Run the MSVC `LokaTestsWin32` target, including the registered
`testWin32NativeRetirement*` pins, before the workload trials. Compare the same
Win32 rig, configuration, image inputs and scripted actions at the PR 1 baseline
and the PR 2 candidate. Record both revisions, OS/architecture, build flags and
rig descriptor. Every Win32 row below is measured.

For each trial sample process GDI objects with
`GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS)` and private bytes with the
rig's process memory sampler. Record baseline, peak and post-completion counts
and bytes, decoded dimensions/bytes per image, burst length (replacements before
one eligible completion), modal path/dwell, refusal behavior, and recovery.
Include a sample immediately before completion so brief retention peaks are not
missed. Compare peak private bytes between baseline and retirement runs; peak
working set alone is not private-byte evidence. Preserve logs with the candidate.

| Workload to run on Win32 | Images / burst / modal dwell | Baseline and retirement GDI counts / peak private bytes / recovery | Refusal and acceptance decision |
|---|---|---|---|
| Scrapbook continuous page navigation | Scrapbook pages 1-4, 122,880 private bytes per decoded bitmap; 40 flips, one per outer Operation; no modal | Baseline: GDI 45 on image pages (44 on the text page); the replaced bitmap is deleted inside the replacing turn. Retirement: 46 GDI, 1 queued, +122,880 bytes at the end of the replacing turn; 45 and 0 queued at the next turn in 40/40 flips of each run | No refusal. One extra decoded image for less than one turn: accepted |
| SimpleViewer image replacement | Two chooser picks: a 1024x768 32bpp image (3,145,728 pixel bytes), then an 800x600 one (1,920,000); SimpleViewer applies each posted result in a later turn | Both revisions: at the first sample after the replacing turn, GDI 121-123, the 800x600 image shown, the old bitmap gone (retirement: 1 held, 0 queued), private bytes 16.7-17.6 MB in both. The in-turn peak is not sampled: the posted result is applied during the turn's tail | No refusal. No retention survives the replacing turn: accepted |
| Multiple replacements inside one outer Operation | The same pages; three clicks in one idle turn, 10 bursts | Baseline: GDI 45, no extra bitmap. Retirement: +3 GDI, 3 queued, +368,640 bytes at the end of the turn; all recovered at the next turn in 10/10 bursts of each run | No refusal. Exceeds the provisional criterion: retention equals the images released within one outer Operation, recovered at its completion. Accepted by the owner on 2026-10-09 with no cap. A user can reach it, because one Win32 turn dispatches every queued message (`Win32App::run`); an app that must bound it defers the next load to a later turn, so only the last request decodes |
| LazyView generation replacement containing images | A probe LazyColumn whose 9 resident items each decode their own 256x256 32bpp image (262,144 pixel bytes) into item state; 10 generation swaps (first item moved to the end), one per turn | Both revisions: after the edit and a settle inside the turn, +9 GDI and about +2.67 MB with the old generation still alive (retirement: 18 held, 0 queued); at the next turn 47 GDI and about 5.0 MB private bytes after the first swap's one-time heap growth (retirement: 9 held, 0 queued), in 10/10 swaps of each run | No refusal. Peaks equal: the old generation is released at the tail's reclaim and drained by the same tail: accepted |
| Open-file dialog held inside the outer Operation, then dismiss | SimpleViewer's chooser; one 1024x768 32bpp image (3,145,728 bytes) released 1 s into a 2 s dwell inside the modal loop, with `Operation::hasActive()` true; dismissed with Cancel | Baseline: GDI -1 and about -3.1 MB private bytes at the release, inside the active Operation. Retirement: 1 queued, GDI and private bytes unchanged for the rest of the dwell; 0 queued after dismiss, 108 GDI in both (the chooser's own residue) | No refusal. No deletion while the outer Operation is active, recovery at the next completion; one extra image for the dwell: accepted. Final-exit recovery is pinned by `testWin32NativeRetirementFinalDrain`, not by this trial |

Evidence (2026-10-09): rig `loka-win32-rig`
(Hyper-V guest, Windows 11 Pro 10.0.26200, x64), MSVC 19.44.35228,
`win32-standalone-debug` (Debug). Baseline = `49e2b3c9` (PR 1) with the probe
sources applied; candidate = `main` with the probes. For the first three rows
`main`'s Win32 and common sources were identical to `bce6582e` (PR 2); the
SimpleViewer and LazyView rows were measured on a later `main` whose only
common change since is #1171 (`CommandSet` and its binding entry), which no
probe uses; that second run set also reproduced the first three rows' GDI and
ledger deltas. Three interleaved runs per revision in each set; GDI and ledger
counts were identical across runs. Private bytes include heap
growth: the baseline's per-turn deltas were 0 or +/-163,840 bytes, while the
retirement deltas above repeated in every run. The baseline's in-turn and
in-modal deletions are the probes' positive control. The `post` samples are
taken after the action and its scene flush (the LazyView probe also settles the
turn first, because the list publication reaches the view at settle), before
the turn's tail; copies the tail releases are drained by the same tail's
reclaim. The native retirement pins ran on this rig at the PR 2 head and in
hosted CI at the merged heads. Repeat with
`cmake --build <build> --target LokaRetirementProbeWin32All`, then
`tests/win32/run-retirement-probe.ps1 -BuildDirectory <build> -OutputDirectory <dir> -Configuration Debug`
from a scheduled task in the guest's interactive session.

The one-extra-decoded-image threshold is provisional for each workload. Record
excess explicitly and obtain an evidence-based acceptance or admission redesign;
do not silently accept an excess or introduce an unmeasured cap. The modal trial
must verify no deletion while the outer Operation remains active, then recovery
at completion; final-exit recovery covers WM_QUIT's skipped tail.

### macOS measured acceptance

Run `LokaTestsMacOS`, including `testMacNativeRetirement*` and
`testMacCaptureRefusalReleasesBitmapOnce`, before measurement. Compare the PR 2
baseline with PR 3 on the same Tahoe rig, inputs and actions; record revisions,
OS/architecture, toolchain, preset and rig descriptor. Every macOS row below is
measured; the legacy Save selector is not (see below).

Record decoded image dimensions/bytes, burst length within one outer Operation,
modal path and dwell, baseline and candidate peak RSS and physical footprint,
peak queued ticket count (sample immediately before eligible completion), and
post-completion recovery of bytes and tickets. Use the existing testing ledger
access for ticket observation in an instrumented rig build; do not add a shipped
counter. RSS and footprint are separate measurements; do not label one as the
other. Record refusal behavior. Retention beyond the provisional extra-one-image
criterion requires an evidence-based acceptance decision before merge.

| Workload | Image sizes / burst / dwell | Baseline vs candidate peak RSS / footprint | Queued tickets at peak / recovery after completion | Refusal / decision |
|---|---|---|---|---|
| Scrapbook burst page flips | Scrapbook pages 1-4; 40 flips, one per outer Operation | Not resolvable at this image size: the per-flip change was 0 (median) in both revisions, for RSS and footprint alike | Baseline 0. Retirement: 1 queued at the end of every image-replacing turn, 0 at the next turn (30 flips per run, 3 runs) | No refusal. One extra image for less than one turn: accepted |
| SimpleViewer replace | 1024x768, then 800x600 32bpp. Returned through the existing dialog-result test access, because the out-of-process panel has no selection setter; SimpleViewer's own Flow loads each in a later tick | Both revisions: at the first sample after the replacing turn, footprint 23.3-23.9 MB with the 800x600 image shown; RSS varies by run with the panel services (85-101 MB) | Retirement: 1 held, 0 queued from the first sample after the replacing turn. The in-turn peak is not sampled | No refusal. No retention survives the replacing turn: accepted |
| Multiple replacements inside one outer Operation | Scrapbook, three clicks in one idle turn, 10 bursts | Not resolvable at this image size (0 median in both) | Baseline 0. Retirement: 3 queued at the end of the turn, 0 at the next turn in 30/30 bursts | No refusal. Exceeds the provisional criterion as on Win32: retention equals the images released within one outer Operation, recovered at its completion; the owner's acceptance of 2026-10-09 covers both rails |
| LazyView image generation replacement | The Win32 probe's LazyColumn: 9 resident items, each decoding its own 256x256 32bpp image (262,144 pixel bytes); 10 swaps, one per turn | Both revisions: +2.42-2.45 MB footprint and RSS after the edit and a settle inside the turn, old generation still alive; +0.14 to +0.25 MB of heap at the next turn | Retirement: 18 held and 0 queued at `post`, 9 held and 0 queued at the next turn, in 30/30 swaps | No refusal. Peaks equal; the old generation is drained by the same tail: accepted |
| Open panel dwell: deferred and synchronous fallback | One 1024x768 32bpp image (3,145,728 bytes) released 1 s into a 2 s dwell, cancelled afterwards. Deferred: the normal presenter, `Operation::hasActive()` false in the modal loop. Synchronous: presenter removed through the existing `MacDialogResultTestAccess`, `hasActive()` true | Baseline, both paths: -3.15 MB footprint and RSS at the release | Retirement, deferred: 1 queued at the release, then 0 and -3.15 MB within 1 s, on an eligible tick inside the modal loop. Retirement, synchronous: 1 queued and memory unchanged until dismiss, then 0 and -2.9 to -3.15 MB | No refusal. Disposal waits while the outer Operation is active and runs on eligible ticks when none is: accepted. One extra image for at most the dwell |
| Save panel dwell: deferred and synchronous fallback | As the Open panel, with the modern Save selector | As the Open panel | As the Open panel, in each of 3 runs | Accepted. The legacy Save selector (`runModalForDirectory:file:`, before 10.6) is not exercised: no supporting rig |

Evidence (2026-10-09): Tahoe, macOS 26.7.1 (25G241) x86_64, Xcode 26.5, Apple
clang 21.0.0, `macos-standalone-debug` (Debug), console user session.
Baseline = `bce6582e` (PR 2) with the probe sources and the probe CMake hunk
applied; candidate = this branch, whose macOS sources equal PR 3's except for
#1171's common `CommandSet` change, which no probe uses. Three interleaved runs
per revision; ledger counts were identical across runs. RSS is
`MACH_TASK_BASIC_INFO.resident_size` and footprint is
`TASK_VM_INFO.phys_footprint`, recorded separately. Ledger counts are the
evidence where the memory change is too small to resolve. At the candidate,
`LokaTestsMacOS` passed in full in the console session, and each
retirement pin passed alone. Final-exit recovery is pinned by
`testMacNativeRetirementFinalDrain`, not by these trials. Repeat with
`cmake --build <build> --target LokaRetirementProbeMacOSAll`, then run each
`LokaRetirementProbe{Scrapbook,Lazy,Dialog}MacOS.app/Contents/MacOS/...`
binary from an ssh session of the logged-in console user, with
`LOKA_RETIREMENT_PROBE_OUTPUT_DIR` set.

For both panel types verify retirement is refused while an outer Operation is
active and allowed on eligible ticks when none is active. Exercise the legacy
Save selector on a supporting rig; Tahoe's modern selector does not establish
that runtime branch. Verify final-exit recovery after App and Config destruction.

Reservation and cancellation cost O(1) per attempted acquisition. Publication
adds the existing Image allocations. Final release transfers O(1), allocation
free, per final copy/refusal. Completion checks O(1) and walks only its own
queued snapshot. Shutdown counts its own H held tickets once and processes at
most H + 1 snapshots; no foreign ledger is scanned.

The next resource kind supplies one rail static disposer, one consuming publish
adapter for its value type, and Reservation before native acquisition. Cancel,
transfer, completion, re-entrancy, and shutdown stay in the common procedure.
Do not add a per-kind queue, drain door, registry, or controller identity.

## Review risk profile

The owner explicitly approved six flags with this document as the strong design
note: a new cleanup path; lifetime spanning Boundary and Platform; ticket
pointers across the ImageRecord boundary; changed shutdown behavior; multiple
new members; and orphan containment as a new fallback. The common-first split
reduces simultaneous rail scope, not the mechanism's intrinsic review risk.
Always-on exclusion/refusal/transfer and revocation walls carry runtime safety;
audit-only checks diagnose unsupported lifetimes. Host pins and mutations cover
the common mechanism; they do not claim native modal safety or measured rail
retention acceptance.
