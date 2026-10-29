#include "support/LokaAllocFailure.hpp"
#include "StartupRedrawTests.hpp"
#include "support/TestVerify.hpp"
#include <cassert>
#include <cstdio>

// ---------------------------------------------------------------------------
// Startup redraw count tests
//
// Portable model of the ToolboxWindow invalidation state machine.
// No Mac headers needed — purely tests the flag-based draw decisions that
// control how many full draws happen when a Classic window first appears.
//
// Before fix: 3+ full draws on startup
// After fix:  1 full draw on startup
// ---------------------------------------------------------------------------

namespace
{
  struct StartupRedrawSimulator
  {
    bool needsInvalidate_;
    bool skipNextUpdateDraw_;
    int fullDrawCount_;

    StartupRedrawSimulator()
        : needsInvalidate_(false),
          skipNextUpdateDraw_(false),
          fullDrawCount_(0)
    {
    }

    // Models requestInvalidateWithReason (called from onChange with fullRebuild)
    void requestInvalidate()
    {
      needsInvalidate_ = true;
    }

    // Models draw() — direct draw, does not touch invalidation flags
    void draw()
    {
      ++fullDrawCount_;
    }

    // Models flushInvalidate() — only draws if needsInvalidate_ is true
    void flushInvalidate()
    {
      if (!needsInvalidate_)
      {
        return;
      }
      needsInvalidate_ = false;
      skipNextUpdateDraw_ = true;
      draw();
    }

    // Models updateEvt handling
    void handleUpdateEvt()
    {
      if (skipNextUpdateDraw_)
      {
        skipNextUpdateDraw_ = false;
        return;
      }
      draw();
    }
  };

  struct ToolboxFollowupRedrawSimulator
  {
    bool skipNextUpdateDraw_;
    int drawCount_;

    ToolboxFollowupRedrawSimulator()
        : skipNextUpdateDraw_(false),
          drawCount_(0)
    {
    }

    void draw()
    {
      ++drawCount_;
    }

    void flushInvalidateWithLegacySkip()
    {
      skipNextUpdateDraw_ = true;
      draw();
    }

    void flushInvalidateWithoutSkip()
    {
      draw();
    }

    void handleUpdateEvt()
    {
      if (skipNextUpdateDraw_)
      {
        skipNextUpdateDraw_ = false;
        return;
      }
      draw();
    }
  };

  struct ToolboxChildDirtyInvalidationSimulator
  {
    enum InvalidateMode
    {
      INVALIDATE_NONE = 0,
      INVALIDATE_RECT = 1,
      INVALIDATE_FULL = 2
    };

    static InvalidateMode chooseInvalidateMode(bool hasRootNode, bool fullRebuild, bool hasChildDirty)
    {
      if (!hasRootNode || fullRebuild || hasChildDirty)
      {
        return INVALIDATE_FULL;
      }
      return INVALIDATE_RECT;
    }
  };
} // namespace

void testStartupRedrawCount_Before()
{
  printf("\n==== [testStartupRedrawCount_Before] start ====\n");

  // Simulate the CURRENT ToolboxApp::run() startup sequence:
  //   open()                    -> NewWindow(visible=true), queues updateEvt
  //   ensureSceneMounted()      -> onChange(INITIAL, fullRebuild=true) -> requestInvalidate
  //   draw()                    -> explicit draw (does NOT clear needsInvalidate_)
  //   --- event loop ---
  //   nullEvent -> flushInvalidate()  -> draws again because needsInvalidate_ still set
  //   updateEvt from NewWindow        -> skipNextUpdateDraw_ consumed by flushInvalidate
  //   recompose fires requestInvalidate -> another flushInvalidate -> draws again

  StartupRedrawSimulator sim;

  // Phase 1: startup, before event loop
  sim.requestInvalidate(); // ensureSceneMounted -> onChange(fullRebuild=true)
  sim.draw();              // explicit draw() call at ToolboxApp.cpp:54

  // Phase 2: first event-loop iteration (nullEvent)
  sim.flushInvalidate(); // needsInvalidate_ was still true -> draws again

  // Phase 3: updateEvt from NewWindow(visible=true)
  sim.handleUpdateEvt(); // skipNextUpdateDraw_ was set by flushInvalidate -> skipped

  // Phase 4: recompose cycle fires another requestInvalidate during startup
  sim.requestInvalidate(); // dynamic boundary state change during first compose cycle
  sim.flushInvalidate();   // draws again

  printf("  Before: fullDrawCount = %d (expected >= 3)\n", sim.fullDrawCount_);
  assert(sim.fullDrawCount_ >= 3);

  printf("==== [testStartupRedrawCount_Before] PASSED ====\n");
}

void testStartupRedrawCount_After()
{
  printf("\n==== [testStartupRedrawCount_After] start ====\n");

  // Simulate the FIXED startup sequence:
  //   open()
  //   ensureSceneMounted()      -> onChange(fullRebuild=true) -> requestInvalidate
  //   flushInvalidate()         -> clears needsInvalidate_, draws, sets skipNextUpdateDraw_
  //   --- event loop ---
  //   updateEvt from NewWindow  -> skipped by skipNextUpdateDraw_
  //   no excess requestInvalidate because dynamic root downgrade prevents fullRebuild

  StartupRedrawSimulator sim;

  // Phase 1: startup, before event loop
  sim.requestInvalidate(); // ensureSceneMounted -> onChange(fullRebuild=true)
  sim.flushInvalidate();   // FIXED: use flushInvalidate instead of raw draw()
                           // clears needsInvalidate_, draws, sets skipNextUpdateDraw_

  // Phase 2: updateEvt from NewWindow(visible=true)
  sim.handleUpdateEvt(); // skipNextUpdateDraw_ is set -> skipped

  // Phase 3: if recompose fires, dynamic root boundary downgrade means
  // fullRebuild=false, so only rect invalidation occurs (not modelled here
  // as full draw), and needsInvalidate_ stays false.

  printf("  After: fullDrawCount = %d (expected 1)\n", sim.fullDrawCount_);
  assert(sim.fullDrawCount_ == 1);

  printf("==== [testStartupRedrawCount_After] PASSED ====\n");
}

void testToolboxChildDirtyInvalidationPrefersFullRedraw()
{
  printf("\n==== [testToolboxChildDirtyInvalidationPrefersFullRedraw] start ====\n");

  const ToolboxChildDirtyInvalidationSimulator::InvalidateMode childDirtyMode =
      ToolboxChildDirtyInvalidationSimulator::chooseInvalidateMode(true, false, true);
  const ToolboxChildDirtyInvalidationSimulator::InvalidateMode propsOnlyMode =
      ToolboxChildDirtyInvalidationSimulator::chooseInvalidateMode(true, false, false);

  printf("  child dirty mode = %d (expected full)\n", static_cast<int>(childDirtyMode));
  printf("  props only mode = %d (expected rect)\n", static_cast<int>(propsOnlyMode));

  assert(childDirtyMode == ToolboxChildDirtyInvalidationSimulator::INVALIDATE_FULL);
  assert(propsOnlyMode == ToolboxChildDirtyInvalidationSimulator::INVALIDATE_RECT);

  printf("==== [testToolboxChildDirtyInvalidationPrefersFullRedraw] PASSED ====\n");
}

void testToolboxManualInvalidateDoesNotSkipFollowupUpdateDraw()
{
  printf("\n==== [testToolboxManualInvalidateDoesNotSkipFollowupUpdateDraw] start ====\n");

  ToolboxFollowupRedrawSimulator legacy;
  legacy.flushInvalidateWithLegacySkip();
  legacy.handleUpdateEvt();
  printf("  legacy drawCount = %d (expected 1)\n", legacy.drawCount_);
  assert(legacy.drawCount_ == 1);

  ToolboxFollowupRedrawSimulator fixed;
  fixed.flushInvalidateWithoutSkip();
  fixed.handleUpdateEvt();
  printf("  fixed drawCount = %d (expected 2)\n", fixed.drawCount_);
  assert(fixed.drawCount_ == 2);

  printf("==== [testToolboxManualInvalidateDoesNotSkipFollowupUpdateDraw] PASSED ====\n");
}

// ---------------------------------------------------------------------------
// Auto control-id policy (issue #120)
//
// Portable model, same philosophy as the redraw simulator above: the
// allocator is plain C++ shared with ToolboxScenePlatformController.
// The first sequence below is exactly the one that used to collide —
// the controller re-announced the explicit base every frame while
// contexts allocated lazily and cached their id, so the first control
// revealed by a Show flip received an id that was still live.
// ---------------------------------------------------------------------------

#include "../apple/toolbox/src/ToolboxControlIdAllocator.hpp"
#include "../apple/toolbox/src/ToolboxEditControlLedger.hpp"
#include "../apple/toolbox/src/ToolboxFocusedEditIndex.hpp"

namespace
{
  struct ToolboxEditBindingProbe
  {
    explicit ToolboxEditBindingProbe(int value)
        : id(value)
    {
    }

    int id;
  };

  struct ToolboxOwnedEditBindingProbe
  {
    ToolboxOwnedEditBindingProbe(void *context, void *state, int nativeHandle)
        : ownerContext(context),
          text(state),
          handle(nativeHandle)
    {
    }

    void *ownerContext;
    void *text;
    int handle;
  };

  struct ToolboxEditSyncProbe
  {
    ToolboxEditSyncProbe()
        : count(0),
          firstHandle(0),
          secondHandle(0)
    {
    }

    void synchronize(ToolboxOwnedEditBindingProbe &binding)
    {
      if (count == 0)
      {
        firstHandle = binding.handle;
      }
      else if (count == 1)
      {
        secondHandle = binding.handle;
      }
      ++count;
    }

    int count;
    int firstHandle;
    int secondHandle;
  };
}

void testToolboxAutoControlIdsNeverReissueLiveIds()
{
  ToolboxControlIdAllocator ids(128);

  ids.raiseBaseAbove(0); // frame 1, no explicit tags
  const short outerAdd = ids.allocate();
  const short toggle = ids.allocate();

  ids.raiseBaseAbove(0); // frame 2: cached contexts skip allocation
  const short revealedAdd = ids.allocate(); // Show reveal
  (void)toggle;
  (void)revealedAdd;
  assert(revealedAdd != outerAdd && revealedAdd != toggle &&
         "a control revealed after re-announcing the base must not receive a live id");

  // Recycling: a released id may be reissued, live ids stay reserved.
  ids.release(outerAdd);
  const short reused = ids.allocate();
  assert(reused == outerAdd);
  const short another = ids.allocate();
  (void)another;
  assert(another != toggle && another != reused && "live ids stay reserved");

  // A late explicit tag lifts the base; freed ids below it are discarded.
  ids.release(reused);
  ids.raiseBaseAbove(1000);
  const short afterRaise = ids.allocate();
  assert(afterRaise >= 1001 && "freed ids below a raised base are not reissued");
  ids.release(afterRaise);
  LOKA_VERIFY(ids.allocate() == afterRaise);
}

void testToolboxAutoControlIdsSaturateAtShortMax()
{
  // An explicit tag one below SHRT_MAX leaves exactly one fresh id.
  ToolboxControlIdAllocator ids(128);
  ids.raiseBaseAbove(32766);
  const short last = ids.allocate();
  assert(last == 32767 && "the last fresh id below SHRT_MAX is still issued");
  const short exhausted = ids.allocate();
  assert(exhausted == 0 &&
         "an exhausted fresh range saturates to the never-live sentinel");
  LOKA_VERIFY(ids.allocate() == 0 &&
         "saturation never wraps the counter back into the live range");

  // Recycling keeps working past exhaustion; the sentinel never enters the pool.
  ids.release(exhausted);
  ids.release(last);
  LOKA_VERIFY(ids.allocate() == last && "released live ids are still recycled");
  LOKA_VERIFY(ids.allocate() == 0 && "the sentinel is not recycled as an id");

  // An explicit tag at SHRT_MAX exhausts immediately and never lowers the base.
  ToolboxControlIdAllocator edge(128);
  edge.raiseBaseAbove(32767);
  LOKA_VERIFY(edge.allocate() == 0 && "no fresh ids above an explicit SHRT_MAX tag");
  edge.raiseBaseAbove(32767);
  LOKA_VERIFY(edge.allocate() == 0 && "re-announcing SHRT_MAX never lowers the base");
}

void testToolboxFocusedEditSurvivesLowerBindingErase()
{
  std::vector<ToolboxEditBindingProbe> bindings;
  bindings.push_back(ToolboxEditBindingProbe(1));
  bindings.push_back(ToolboxEditBindingProbe(2));
  ToolboxFocusedEditIndex focus;
  focus.focus(1);
  assert(focus.resolve(bindings) && focus.resolve(bindings)->id == 2);

  focus.erase(0);
  bindings.erase(bindings.begin());

  assert(focus.resolve(bindings) && focus.resolve(bindings)->id == 2 &&
         "erasing a lower edit binding must preserve focus on the surviving binding");

  focus.erase(0);
  bindings.erase(bindings.begin());
  assert(!focus.resolve(bindings) && "erasing the focused binding must clear focus");
}

void testToolboxEditControlDetachRetiresExactContext()
{
  // Portable/isomorphic pin for the controller-owned Toolbox ledger. Native
  // TEDeactivate/queue behavior is covered by Retro68 build + MAME runtime.
  int sharedTextState = 0;
  int retiringContext = 0;
  int survivingContext = 0;
  ToolboxEditControlLedger<ToolboxOwnedEditBindingProbe, void> editControls;
  LOKA_VERIFY(editControls.prepareAppend());
  editControls.commitAppend(ToolboxOwnedEditBindingProbe(&retiringContext, &sharedTextState, 101));
  LOKA_VERIFY(editControls.prepareAppend());
  editControls.commitAppend(ToolboxOwnedEditBindingProbe(&survivingContext, &sharedTextState, 202));
  editControls.focus(1);

  std::size_t retiringIndex = 0;
  const bool retiredExactContext = editControls.find(&retiringContext, retiringIndex);
  (void)retiredExactContext;
  assert(retiredExactContext && editControls[retiringIndex].handle == 101 &&
         "detach must synchronously retire the exact context's native edit binding");
  editControls.erase(retiringIndex);

  assert(editControls.size() == 1 && editControls[0].ownerContext == &survivingContext &&
         editControls[0].text == &sharedTextState && editControls[0].handle == 202 &&
         "a live EditText sharing the same State must retain its own binding");
  assert(editControls.focused() // loka-assert-ok: Pure index lookup.
         && editControls.focused()->handle == 202
         && "retiring a lower binding must preserve focus on the surviving control");

  std::size_t survivingIndex = 0;
  // find() fills survivingIndex; erased under NDEBUG it would leave the
  // initial 0 and erase whichever binding happens to sit first.
  LOKA_VERIFY(editControls.find(&survivingContext, survivingIndex));
  editControls.erase(survivingIndex);
  assert(!editControls.focused() && // loka-assert-ok: focused only queries the index.
         "retiring the focused context must synchronously clear native edit focus");
}

void testToolboxEditControlSharedStateChangeFansOut()
{
  int sharedTextState = 0;
  int unrelatedTextState = 0;
  int firstContext = 0;
  int secondContext = 0;
  int unrelatedContext = 0;
  ToolboxEditControlLedger<ToolboxOwnedEditBindingProbe, void> editControls;
  LOKA_VERIFY(editControls.prepareAppend());
  editControls.commitAppend(ToolboxOwnedEditBindingProbe(&firstContext, &sharedTextState, 101));
  LOKA_VERIFY(editControls.prepareAppend());
  editControls.commitAppend(ToolboxOwnedEditBindingProbe(&secondContext, &sharedTextState, 202));
  LOKA_VERIFY(editControls.prepareAppend());
  editControls.commitAppend(ToolboxOwnedEditBindingProbe(&unrelatedContext, &unrelatedTextState, 303));

  ToolboxEditSyncProbe sync;
  const std::size_t synchronizedCount = editControls.forEachTextBinding(
      &sharedTextState, &sync, &ToolboxEditSyncProbe::synchronize);

  (void)synchronizedCount;
  assert(synchronizedCount == 2 && sync.count == 2 && sync.firstHandle == 101 && sync.secondHandle == 202 &&
         "a shared text state change must synchronize every live native edit binding");
}

/** Candidate allocation, commit, cancellation, and spare capacity are distinct doors. */
void testToolboxEditControlPreparedStorage()
{
  using namespace loka::core::testing;
  failLokaAllocRaw("ToolboxEditControlLedger", "Rows", 0);
  {
    ToolboxEditControlLedger<ToolboxOwnedEditBindingProbe, void> rows;
    int owners[33];
    int text = 0;
    LOKA_VERIFY(rows.prepareAppend());
    failLokaAllocRaw("ToolboxEditControlLedger", "Rows", 1);
    rows.commitAppend(ToolboxOwnedEditBindingProbe(&owners[0], &text, 101));
    LOKA_VERIFY(lokaAllocRawAttempts() == 0);
    rows.focus(0);
    for (int i = 1; i != 4; ++i)
    {
      LOKA_VERIFY(rows.prepareAppend());
      rows.commitAppend(ToolboxOwnedEditBindingProbe(&owners[i], &text, 101 + i));
    }
    LOKA_VERIFY(lokaAllocRawAttempts() == 0);
    LOKA_VERIFY(!rows.prepareAppend());
    LOKA_VERIFY(rows.size() == 4 && rows.focused()->handle == 101);
    LOKA_VERIFY(rows[3].ownerContext == &owners[3]);
    failLokaAllocRaw("ToolboxEditControlLedger", "Rows", 0);
    LOKA_VERIFY(rows.prepareAppend());
    rows.cancel();
    LOKA_VERIFY(rows.size() == 4 && rows.focused()->handle == 101);
    LOKA_VERIFY(lokaAllocRawLive() == 1);
    failLokaAllocRaw("ToolboxEditControlLedger", "Rows", 0);
    for (int i = 4; i != 33; ++i)
    {
      LOKA_VERIFY(rows.prepareAppend());
      rows.commitAppend(ToolboxOwnedEditBindingProbe(&owners[i], &text, 101 + i));
    }
    LOKA_VERIFY(lokaAllocRawAttempts() == 4);
    LOKA_VERIFY(rows.size() == 33 && rows.focused()->handle == 101);
    rows.erase(0);
    LOKA_VERIFY(!rows.focused() && rows[0].handle == 102);
    rows.clear();
    LOKA_VERIFY(rows.empty());
  }
  LOKA_VERIFY(lokaAllocRawLive() == 0);
  allowLokaAllocRaw();
}

/** Geometric growth uses three allocations for sixteen rows, not one per append. */
void testToolboxEditControlCapacityReuse()
{
  using namespace loka::core::testing;
  failLokaAllocRaw("ToolboxEditControlLedger", "Rows", 0);
  {
    ToolboxEditControlLedger<ToolboxOwnedEditBindingProbe, void> rows;
    int owners[16];
    int text = 0;
    for (int i = 0; i != 16; ++i)
    {
      LOKA_VERIFY(rows.prepareAppend());
      rows.commitAppend(ToolboxOwnedEditBindingProbe(&owners[i], &text, i));
    }
    LOKA_VERIFY(lokaAllocRawAttempts() == 3);
    LOKA_VERIFY(rows.size() == 16);
  }
  LOKA_VERIFY(lokaAllocRawLive() == 0);
  allowLokaAllocRaw();
}
