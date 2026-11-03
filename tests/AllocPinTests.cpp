#ifdef LOKA_UPSTREAM_GAUGE_PIN
#include "support/UpstreamGaugePin.hpp"
#endif
// Allocation ratchet scenario: one steady-state UI interaction on the real
// HelloWorld MainNode, driven headlessly, must not exceed a fixed heap
// allocation budget (design ruling 2026-07-20).
//
// The interaction mirrors HelloWorld's probe-button click end to end:
//   handler body (String concat + String::FromInt + state sets)
//   -> tracker commit (propagation/notify/invalidate)
//   -> scene.flushInvalidation() (compose + platform apply).
// The platform controller below flattens every Text node's value at apply
// time through the same CollectUtf8 path a real platform context uses
// (apple/macos/src/context/MacTextContext.mm:34, Win32ButtonContext.cpp:260),
// so the lazy-rope flatten cost is part of the census.
//
// The budget is a CEILING, not a target: see the comment above
// kSteadyStateAllocBudget below and the PR that introduced this ratchet
// ("Pin steady-state per-interaction allocations with a ratchet budget")
// for the full census table and family breakdown.

#include "support/AllocCensus.hpp"
#include "app/core/App.hpp"
#include "platform/null/NullWindow.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "app/style/AttributedString.hpp"
#include "support/TestVerify.hpp"

#include "MainNode.hpp"
#include "../example/FloppyBird/src/MainNode.hpp"
#include "platform/null/NullScenePlatformController.hpp"

#include "app/nodes/Text.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/scene/Scene.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/StringUTF8.hpp"
#include "testing/scene/SceneTestFlow.hpp"

#include <cassert>
#include <cstdio>
#include <string>

namespace
{
  using loka::app::scene::BoundaryNode;
  using loka::app::scene::INestable;
  using loka::app::scene::Node;
  using loka::app::scene::Scene;

  // Platform controller that consumes applied scenes the way a real
  // platform does for the census-relevant part: every Text node's value is
  // flattened into a fresh std::string via loka::platform::CollectUtf8 on
  // each apply, which is exactly the shape of MacTextContext/Win32 apply.
  struct AllocPinPlatformController : public loka::app::scene::IPlatformController
  {
    AllocPinPlatformController()
        : changeCalls_(0),
          boundaryApplyCalls_(0),
          textsFlattened_(0),
          textBytes_(0)
    {
    }

    void flattenTextsIn(Node *node)
    {
      if (!node)
      {
        return;
      }
      loka::app::TextNode *text = node->asTextNode();
      if (text && text->props.text_)
      {
        std::string utf8;
        if (loka::platform::CollectUtf8(text->props.text_->get(), utf8))
        {
          ++textsFlattened_;
          textBytes_ += static_cast<unsigned long>(utf8.size());
        }
      }
      INestable *nestable = node->asNestable();
      if (nestable)
      {
        for (Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
        {
          flattenTextsIn(child);
        }
      }
    }

    virtual void onChange(Node *rootNode, loka::app::scene::NodeDirtyFlags, bool)
    {
      ++changeCalls_;
      flattenTextsIn(rootNode);
    }

    virtual void onBoundaryApply(Node *,
                                 BoundaryNode *boundary,
                                 const loka::app::scene::BoundaryLocalApplyInfo &,
                                 const loka::app::scene::PlatformApplyPlan &)
    {
      ++boundaryApplyCalls_;
      flattenTextsIn(boundary);
    }

    virtual void synchronize() {}
    virtual bool hasPendingSync() const
    {
      return false;
    }
    virtual void destroy() {}

    int changeCalls_;
    int boundaryApplyCalls_;
    unsigned long textsFlattened_;
    unsigned long textBytes_;
  };

  // One full UI interaction, phased for the census. Mirrors
  // ClickButtonByIdAndFlush (SceneTestFlow.hpp) without the Flow DSL so the
  // capture window contains only framework work.
  int runInteraction(Scene &scene, loka::app::ButtonNode *button, BoundaryNode *rootBoundary)
  {
    {
      loka::core::StateTrackerGuard guard(rootBoundary->tracker());
      allocpin::SetPhase(allocpin::PHASE_HANDLER);
      button->props.onClick_->emit();
      allocpin::SetPhase(allocpin::PHASE_COMMIT);
    } // guard dtor: tracker end() -> recompute/commit/deferred/invalidate
    allocpin::SetPhase(allocpin::PHASE_FLUSH);
    int flushes = 0;
    while (scene.hasPendingInvalidation() && flushes < 8)
    {
      scene.flushInvalidation();
      ++flushes;
    }
    // The interaction must drain fully inside the capture window: a bound hit
    // with work still pending would measure a partial interaction and let the
    // ratchet pass on an undercount. Steady-state HelloWorld settles in one or
    // two flushes; the bound is only a runaway guard, never the exit path.
    assert(!scene.hasPendingInvalidation() &&
           "interaction did not drain within the flush bound; allocation count is partial");
    allocpin::SetPhase(allocpin::PHASE_OUTSIDE);
    return flushes;
  }

  loka::app::ButtonNode *findButton(Scene &scene, const char *testId)
  {
    Node *root = loka::dsl::testing::SceneTestAccess::rootNode(scene);
    assert(root != 0);
    long idMatches = 0;
    long typedMatches = 0;
    loka::app::ButtonNode *button = 0;
    loka::dsl::testing::scene_test_detail::findNodeByIdRecursive<loka::app::ButtonNode>(
        root, std::string(testId), idMatches, typedMatches, button);
    assert(idMatches == 1 && typedMatches == 1 && button != 0);
    return button;
  }
} // namespace

namespace allocpin
{
  void RunZeroAllocPin()
  {
    using loka::app::scene::BoundaryDefinition;

    BoundaryDefinition<helloworld::MainProps, helloworld::MainNode> mainDef;
    loka::app::scene::NodeDefinitionBase *rootDefinition = mainDef.clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    AllocPinPlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    // Settle the mount before anything is measured.
    for (int i = 0; scene.hasPendingInvalidation() && i < 8; ++i)
    {
      scene.flushInvalidation();
    }

    BoundaryNode *rootBoundary = loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
    assert(rootBoundary != 0 && rootBoundary->tracker() != 0);

    // The probe button increments actionProbeCount_ and rebuilds
    // actionSummary_ via Literal + concat + FromInt (MainNode.cpp:239-242):
    // the canonical steady-state interaction.
    loka::app::ButtonNode *probeButton = findButton(scene, "HelloWorld.LeftPanel.ProbeButton");
    assert(probeButton->props.onClick_ != 0);

    // WARMUP (uncounted): run the full interaction path twice so one-time
    // lazy initialization is out of the way.
    runInteraction(scene, probeButton, rootBoundary);
    runInteraction(scene, probeButton, rootBoundary);

    const unsigned long textsBeforeCaptures = platform.textsFlattened_;

    // Capture 0: first measured interaction (may still contain stragglers).
    // Capture 1: second measured interaction = steady state; this is the
    // pinned quantity. Comparing 0 vs 1 separates warmup residue.
    // Single call site: the backtrace hash includes return addresses, so
    // both captured interactions must run through identical frames for the
    // cross-capture stack matching to work.
    int flushes[2];
    for (int captureIndex = 0; captureIndex < 2; ++captureIndex)
    {
      BeginCapture(captureIndex);
#ifdef LOKA_UPSTREAM_GAUGE_PIN
    const loka::core::UpstreamGauge upstreamBefore = upstreamPinSnapshot();
#endif
      flushes[captureIndex] = runInteraction(scene, probeButton, rootBoundary);
      EndCapture();
#ifdef LOKA_UPSTREAM_GAUGE_PIN
    upstreamPinCheck("HelloWorld", upstreamBefore, upstreamPinSnapshot(), 0, 0);
#endif
    }
#ifdef LOKA_UPSTREAM_GAUGE_PIN
    long idMatches = 0, typedMatches = 0;
    loka::app::TextNode *summary = 0;
    loka::dsl::testing::scene_test_detail::findNodeByIdRecursive<loka::app::TextNode>(
        loka::dsl::testing::SceneTestAccess::rootNode(scene),
        std::string("HelloWorld.LeftPanel.ActionSummary"), idMatches, typedMatches, summary);
    LOKA_VERIFY(idMatches == 1 && typedMatches == 1 && summary && summary->props.text_);
    LOKA_VERIFY(summary->props.text_->get().equals(
        loka::core::String::Literal("Enabled: yes / clicks: 4")));
    const bool settled = !scene.hasPendingInvalidation();
    LOKA_VERIFY(settled);
#endif
    const int flushes0 = flushes[0];
    const int flushes1 = flushes[1];

    std::printf("scenario: flushes per interaction = %d / %d, platform onChange=%d onBoundaryApply=%d\n",
                flushes0, flushes1, platform.changeCalls_, platform.boundaryApplyCalls_);
    std::printf("scenario: texts flattened during captures = %lu (bytes=%lu)\n",
                platform.textsFlattened_ - textsBeforeCaptures, platform.textBytes_);
    // The apply path must actually have flattened text during the captured
    // interactions, otherwise the census misses the flatten suspects.
    assert(platform.textsFlattened_ > textsBeforeCaptures);

    loka::dsl::testing::SceneTestAccess::unmount(scene);

    // THE RATCHET: a CEILING, not a target. This is not "the framework
    // should allocate this much" -- it is "the framework must not allocate
    // MORE than this for one steady-state UI interaction without someone
    // noticing." The budget below is the census recorded when this ratchet
    // was introduced (PR "Pin steady-state per-interaction allocations with
    // a ratchet budget", built from the census probe on
    // probe/zero-alloc-pin @ fffe20c): 47 allocations / 1483 bytes for one
    // probe-button click, steady state (second captured interaction).
    //
    // By family (full per-stack table and fix recipes in the PR body and
    // the design-table record it references):
    //   String value world (literal String nodes + their control blocks,
    //     ConcatString nodes, String::FromInt, the equality-gate flatten in
    //     String::compare before NodeState::set() commits): 28 allocs
    //   tracker/notify (StateBase::notifyHandlers vector snapshot,
    //     PushStateTracker's visiting_ Rb-tree node insert, the
    //     dirtyStates vector copy in PushStateTracker::end()): 6 allocs
    //   SceneDirector bookkeeping (SceneProjectionTransaction::enqueue,
    //     PendingUpdateRootAnalysis::recordSeenRoot vectors): 10 allocs
    // Several of the above fire twice per click: the default
    // flushViewDirtyImmediately() policy runs the whole pipeline once for
    // actionProbeCount_ (int) and once for actionSummary_ (String) inside
    // the same handler call, i.e. two cycles per click. That structural
    // multiplier is noted separately in the PR body and is routed to the
    // WR-4 scene-update redesign as formal input, not fixed here.
    //
    // Any PR that raises steady-state per-interaction allocations trips
    // this ratchet and must, in its OWN PR body, either justify raising the
    // budget below or avoid the extra allocation. Lowering the budget when
    // allocations are removed is encouraged and expected over time.
    enum
    {
      kSteadyStateAllocBudget = 47,
      kSteadyStateByteBudget = 1483
    };

    // Only the second captured interaction is asserted. The probe found
    // capture 0 and capture 1 identical run to run on this machine, but
    // capture 1 is the one documented as steady state (capture 0 can still
    // carry warmup stragglers in principle), so it is the quantity that
    // stays pinned if warmup behavior ever changes.
    const unsigned long steadyAllocs = CaptureAllocCount(1);
    const unsigned long steadyBytes = CaptureAllocBytes(1);
    const bool allocsOverBudget = steadyAllocs > static_cast<unsigned long>(kSteadyStateAllocBudget);
    const bool bytesOverBudget = steadyBytes > static_cast<unsigned long>(kSteadyStateByteBudget);

    // Print the per-stack census on failure only, so a tripped ratchet
    // immediately names the culprit without spamming a passing run.
    if (allocsOverBudget || bytesOverBudget)
    {
      std::printf("allocation ratchet TRIPPED: steady-state interaction used allocs=%lu (budget=%d)"
                  " bytes=%lu (budget=%d); per-stack census follows\n",
                  steadyAllocs, static_cast<int>(kSteadyStateAllocBudget), steadyBytes,
                  static_cast<int>(kSteadyStateByteBudget));
      PrintCensusReport();
    }

    assert(!allocsOverBudget && "allocation ratchet: steady-state UI interaction exceeded the allocation budget");
    assert(!bytesOverBudget && "allocation ratchet: steady-state UI interaction exceeded the byte budget");

    std::printf("allocation ratchet: PASS (allocs=%lu/%d bytes=%lu/%d)\n", steadyAllocs,
                static_cast<int>(kSteadyStateAllocBudget), steadyBytes, static_cast<int>(kSteadyStateByteBudget));
  }
  void RunFloppyBirdSurfaceAllocPin()
  {
    floppybird::SharedModel model;
    NullScenePlatformController platform;
    Scene scene(loka::app::scene::Boundary<floppybird::MainNode>(floppybird::MainProps(&model)));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    for (int i = 0; scene.hasPendingInvalidation() && i < 8; ++i)
      LOKA_VERIFY(scene.flushInvalidation());
    assert(!scene.hasPendingInvalidation());
    BoundaryNode *root = loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
    LOKA_VERIFY(root != 0);

    // Warm up two surface-only ticks, then measure one complete State update
    // and its Scene cycle. Score is deliberately unchanged.
    for (int tick = 0; tick < 3; ++tick)
    {
      loka::app::RectSurfaceModel next;
      next.rectCount = 1;
      next.rects[0] = loka::app::RectSprite(static_cast<short>(tick), 10, 8, 8);
      const loka::app::scene::testing::PaintBaselineStats before =
          loka::app::scene::testing::paintBaselineStats();
      if (tick == 2)
        BeginCapture(0);
      SetPhase(PHASE_COMMIT);
      {
        loka::core::StateTrackerGuard guard(root->tracker());
        model.surfaceModel_.set(next);
      }
      SetPhase(PHASE_FLUSH);
      if (scene.hasPendingInvalidation())
        LOKA_VERIFY(scene.flushInvalidation());
      if (tick == 2)
        EndCapture();
      assert(!scene.hasPendingInvalidation());
      LOKA_VERIFY(loka::app::scene::testing::paintBaselineStats().boundaryUpdateVisits
                      - before.boundaryUpdateVisits == 1);
    }
    const unsigned long allocations = CaptureAllocCount(0);
    const unsigned long bytes = CaptureAllocBytes(0);
    std::printf("FloppyBird surface tick baseline: allocations=%lu bytes=%lu\n", allocations, bytes);
    // Characterization of this Null surface-only tick: 6 allocations / 112 bytes.
    // Both halves are pinned so a later paint change that keeps the count but
    // grows the sizes is visible. Kept independent of the HelloWorld ceiling;
    // later PRs explicitly update this baseline.
    LOKA_VERIFY(allocations == 6);
    LOKA_VERIFY(bytes == 112);
    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }
  void RunFloppyBirdScoreAllocPin()
  {
    floppybird::SharedModel model;
    NullScenePlatformController platform;
    Scene scene(loka::app::scene::Boundary<floppybird::MainNode>(floppybird::MainProps(&model)));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    for (int i = 0; scene.hasPendingInvalidation() && i < 8; ++i)
      LOKA_VERIFY(scene.flushInvalidation());
    BoundaryNode *root = loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
    const loka::core::String next = loka::core::String::Literal("Score: 123456789012345678");
    BeginCapture(0);
    SetPhase(PHASE_COMMIT);
    {
      loka::core::StateTrackerGuard guard(root->tracker());
      model.scoreText_.set(next);
    }
    SetPhase(PHASE_FLUSH);
    if (scene.hasPendingInvalidation())
      LOKA_VERIFY(scene.flushInvalidation());
    EndCapture();
    assert(!scene.hasPendingInvalidation());
    const unsigned long allocations = CaptureAllocCount(0);
    const unsigned long bytes = CaptureAllocBytes(0);
    std::printf("FloppyBird score tick baseline: allocations=%lu bytes=%lu\n", allocations, bytes);
    // Distinct String comparison and seat verification materialize buffers.
    LOKA_VERIFY(allocations == 12);
    LOKA_VERIFY(bytes == 436);
    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }
} // namespace allocpin

namespace
{
  template <typename T> class TokenAllocProbe : public loka::core::State<T>
  {
  public:
    void reserveHandlerStorage()
    {
      this->handlers.reserve(2);
      this->deferredHandlers.reserve(2);
    }
  };

  void tokenAllocNoop(void *) {}

  template <typename T> void captureFirstObserverToken(bool deferred, unsigned long &first, unsigned long &later)
  {
    TokenAllocProbe<T> state;
    // Isolate the token from vector capacity allocation; registration remains real.
    state.reserveHandlerStorage();
    allocpin::BeginCapture(0);
    if (deferred)
      state.deferBind(&tokenAllocNoop, 0);
    else
      state.bind(&tokenAllocNoop, 0, false);
    allocpin::EndCapture();
    first = allocpin::CaptureAllocCount(0);
    allocpin::BeginCapture(1);
    state.bind(&tokenAllocNoop, 0, false);
    state.deferBind(&tokenAllocNoop, 0);
    allocpin::EndCapture();
    later = allocpin::CaptureAllocCount(1);
  }
}

void allocpin::RunStateLifetimeTokenAllocPin()
{
  using loka::core::MutableState;
  using loka::core::StateBase;
  enum { kStates = 32 };
  BeginCapture(0);
  for (int i = 0; i < kStates; ++i)
  {
    MutableState<int> state(i);
  }
  EndCapture();
  const unsigned long constructed = CaptureAllocCount(0);
  MutableState<int> source(7);
  source.bind(&tokenAllocNoop, 0, false);
  BeginCapture(1);
  for (int i = 0; i < kStates; ++i)
  {
    MutableState<int> copy(source);
  }
  EndCapture();
  const unsigned long copied = CaptureAllocCount(1);
  unsigned long first[4], later[4];
  captureFirstObserverToken<int>(false, first[0], later[0]);
  captureFirstObserverToken<int>(true, first[1], later[1]);
  captureFirstObserverToken<void>(false, first[2], later[2]);
  captureFirstObserverToken<void>(true, first[3], later[3]);

  MutableState<int> unobserved(0);
  loka::core::PushStateTracker tracker;
  BeginCapture(0);
  {
    loka::core::StateTrackerGuard guard(&tracker);
    unobserved.set(1);
    unobserved.set(2, true);
  }
  EndCapture();
  const unsigned long notified = CaptureAllocCount(0);
  BeginCapture(0);
  void *external = unobserved.retainExternalLifetimeToken();
  EndCapture();
  const unsigned long guarded = CaptureAllocCount(0);
  BeginCapture(1);
  void *second = unobserved.retainExternalLifetimeToken();
  StateBase::releaseExternalLifetimeToken(second);
  StateBase::releaseExternalLifetimeToken(external);
  EndCapture();
  const unsigned long reguarded = CaptureAllocCount(1);
  std::fprintf(stderr, "State tokens: construct %d=%lu copy %d=%lu; first bind int/void immediate/deferred=%lu/%lu/%lu/%lu; later=%lu/%lu/%lu/%lu; unobserved set=%lu; external=%lu then %lu\n",
      kStates, constructed, kStates, copied, first[0], first[1], first[2], first[3],
      later[0], later[1], later[2], later[3], notified, guarded, reguarded);
  LOKA_VERIFY(constructed == 0);
  LOKA_VERIFY(copied == 0);
  for (int i = 0; i < 4; ++i)
  {
    LOKA_VERIFY(first[i] == 1);
    LOKA_VERIFY(later[i] == 0);
  }
  LOKA_VERIFY(notified == 0);
  LOKA_VERIFY(guarded == 1);
  LOKA_VERIFY(reguarded == 0);
}

void allocpin::RunAttributedStringEqualsAllocPin()
{
  using namespace loka::app;
  AttributedString left;
  AttributedString right;
  for (int i = 0; i < 10; ++i)
  {
    left = left + Styled(loka::core::String("segment"), Bold);
    right = right + Styled(loka::core::String("segment"), Bold);
  }
  LOKA_VERIFY(left.valid() && right.valid());
  LOKA_VERIFY(left.segmentCount() == 10 && right.segmentCount() == 10);
  BeginCapture(0);
  const bool equal = left.equals(right);
  EndCapture();
  const unsigned long allocations = CaptureAllocCount(0);
  std::fprintf(stderr, "AttributedString equal 10-segment values: allocations=%lu\n", allocations);
  LOKA_VERIFY(equal);
  LOKA_VERIFY(allocations == 0);
}

namespace
{
  class FocusAllocRoot : public loka::app::scene::BoundaryNodeFor<FocusAllocRoot>
  {
  public:
    explicit FocusAllocRoot(const loka::app::scene::BoundaryPropsFor<FocusAllocRoot> &props)
        : loka::app::scene::BoundaryNodeFor<FocusAllocRoot>(props) {}
    virtual void composeNode(loka::app::scene::NodeComposition &) {}
  };
  class FocusAllocController : public NullScenePlatformController
  {
  public:
    unsigned reads;
    FocusAllocController() : reads(0) {}
    virtual bool readNativeFocus(loka::app::scene::NodeContext *&out)
    {
      ++this->reads;
      return NullScenePlatformController::readNativeFocus(out);
    }
  };
  class FocusAllocApp : public App
  {
  public:
    FocusAllocApp() : App(0)
    {
      this->group_ = new AppComponentGroup(std::vector<AppComponent *>());
    }
    void adopt(Window *window) { this->group_->adopt(window); }
    void settle() { this->flushWindowInvalidations(); }
    virtual void quit() {}
  };
}

void allocpin::RunFocusCompletionAllocPin()
{
  NullPlatformContext context;
  FocusAllocController controllers[8];
  FocusAllocApp app;
  for (unsigned i = 0; i < 8; ++i)
  {
    WindowProps props;
    props.scene(new loka::app::scene::Scene(loka::app::scene::Boundary<FocusAllocRoot>()));
    app.adopt(new NullWindow(&context, props, &controllers[i]));
    if (i != 2 && i != 7)
      continue;
    app.settle();
    app.reconcileAtCompletion();
    for (int capture = 0; capture != 2; ++capture)
    {
      unsigned before[8];
      for (unsigned n = 0; n <= i; ++n)
        before[n] = controllers[n].reads;
      BeginCapture(capture);
      for (int completion = 0; completion != 4; ++completion)
        app.reconcileAtCompletion();
      EndCapture();
      const unsigned long allocations = CaptureAllocCount(capture);
      std::fprintf(stderr, "Focus completion: windows=%u capture=%d completions=4 allocations=%lu\n",
                   i + 1, capture, allocations);
      for (unsigned n = 0; n <= i; ++n)
        LOKA_VERIFY(controllers[n].reads == before[n] + 4);
      LOKA_VERIFY(allocations == 0);
    }
  }
}
