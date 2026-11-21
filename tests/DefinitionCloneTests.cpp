#include "DefinitionCloneTests.hpp"
#include "WindowRootRefusalTests.hpp"
#include "app/core/AppComposition.hpp"
#include "support/LokaAllocFailure.hpp"
#include "support/StandaloneMountTestSupport.hpp"
#include "support/TestVerify.hpp"
#include <cassert>
#include <cstdio>
#include "core/State.hpp"
#include "core/util/OwnedDef.hpp"
#include "app/PlatformContext.hpp"
#include "app/Menu.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/core/WindowDefinition.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Match.hpp"
#include "app/scene/Node.hpp"
#include "app/scene/node/Conditional.hpp"
#include "support/LimitedCloneProbe.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "scenarios/ObservedMainDefinition.hpp"

// Regression pin for the ConditionalDefinition dangling-branch fix (issue #47,
// docs/TODO.md "DSL definition lifetime safety"): ConditionalDefinition must own
// deep copies of its branch definitions so that stack-local/temporary branch
// definitions can be destroyed without leaving the conditional (or its clones)
// pointing at freed memory. Pre-fix code fails this under ASan.

// ============================================================
// Probe definition/node with liveness counters
// ============================================================

namespace DefinitionCloneTestSupport
{
  int g_probePropsAlive = 0;
  int g_probeNodesAlive = 0;
  int g_probeNodesCreated = 0;
  int g_limitedCloneBudget = -1;
  int g_limitedCloneCalls = 0;
} // namespace DefinitionCloneTestSupport

namespace
{
  using namespace DefinitionCloneTestSupport;

  class MarginRoot : public loka::app::scene::BoundaryNodeFor<MarginRoot>
  {
  public:
    typedef loka::app::scene::BoundaryPropsFor<MarginRoot> Props;
    explicit MarginRoot(const Props &props)
        : loka::app::scene::BoundaryNodeFor<MarginRoot>(props)
    {
    }
    virtual void composeNode(loka::app::scene::NodeComposition &) {}
  };

  struct OomCloneProbeDefinition : public CloneProbeDefinition
  {
    virtual loka::app::scene::NodeDefinitionBase *clone() const
    {
      return 0;
    }
  };

  struct NullWindowPlatformContext : public PlatformContext
  {
    NullWindowPlatformContext()
        : createWindowCalls(0),
          sawRootedScene(false)
    {
    }

    virtual App *createApp(AppConfigurable *, NativeModuleHandle, int) const
    {
      return 0;
    }

    virtual Window *createWindow(const WindowProps &props)
    {
      ++createWindowCalls;
      const loka::app::scene::Scene *scene = props.peekInitialScene();
      this->sawRootedScene = scene && scene->getRootDefinition();
      WindowProps delivered(props);
      loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> remaining(delivered.takeRootDefinition());
      LOKA_VERIFY(!remaining.get() && !delivered.rootDefinitionRefused());
      return 0;
    }

    virtual loka::app::scene::NodeContext *createNodeContext(loka::app::scene::Node *) const
    {
      return 0;
    }

    virtual bool openFile(const loka::file::File &, loka::platform::file::FileHandle &) const
    {
      return false;
    }

    virtual bool createImageFromBlob(const loka::core::resource::Blob &,
                                     std::size_t,
                                     std::size_t,
                                     loka::core::resource::Image &)
    {
      return false;
    }

    int createWindowCalls;
    bool sawRootedScene;
  };

  void verifyTransferableRoot(const WindowProps &props)
  {
    WindowProps copy(props);
    loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(copy.takeRootDefinition());
    LOKA_VERIFY(root.get());
  }

  bool cloneMatchPredicate(const int &value, void *userData)
  {
    const int *expected = static_cast<const int *>(userData);
    return expected && value == *expected;
  }
} // namespace

// ============================================================
// Tests
// ============================================================

void testOwnedDefOwnership()
{
  printf("\n==== [testOwnedDefOwnership] start ====\n");

  int baseline = g_probePropsAlive;
  {
    loka::core::OwnedDef<CloneProbeDefinition> owned(new CloneProbeDefinition());
    LOKA_VERIFY(owned.isSet());
    assert(owned.get() != 0);
    (void)baseline;
    assert(g_probePropsAlive == baseline + 1);

    CloneProbeDefinition *taken = owned.take();
    LOKA_VERIFY(!owned.isSet());
    assert(owned.get() == 0);
    delete taken;
    assert(g_probePropsAlive == baseline);

    owned.reset(new CloneProbeDefinition());
    CloneProbeDefinition *same = owned.get();
    owned.reset(same);
    assert(g_probePropsAlive == baseline + 1);
    owned.reset();
    LOKA_VERIFY(!owned.isSet());
    assert(g_probePropsAlive == baseline);
  }
  {
    loka::core::OwnedDef<CloneProbeDefinition> owned(new CloneProbeDefinition());
    assert(g_probePropsAlive == baseline + 1);
  }
  assert(g_probePropsAlive == baseline);
  {
    OomCloneProbeDefinition failedClone;
    loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> empty(failedClone.clone());
    LOKA_VERIFY(!empty.isSet());
    assert(empty.get() == 0);
  }
  assert(g_probePropsAlive == baseline);

  printf("==== [testOwnedDefOwnership] end ====\n");
}

void testConditionalDefinitionCloneOwnership()
{
  printf("\n==== [testConditionalDefinitionCloneOwnership] start ====\n");

  using loka::app::scene::ConditionalDefinition;
  using loka::app::scene::ConditionalProps;

  // --- the definition owns deep copies of its branch definitions ---
  // The branch definition is stack-local and destroyed before the conditional
  // is cloned or materialized; the owned clones must keep working.
  {
    loka::core::MutableState<bool> cond(true);
    ConditionalDefinition *conditional = 0;
    {
      CloneProbeDefinition trueBranch;
      conditional = new ConditionalDefinition(ConditionalProps(&cond, &trueBranch, 0));
    }
    // trueBranch is gone. Clone the conditional, then destroy the original:
    // the clone must own its own independent branch copy.
    loka::app::scene::NodeDefinitionBase *copy = conditional->clone();
    LOKA_VERIFY(copy != 0);
    delete conditional;

    int createdBefore = g_probeNodesCreated;
    loka::app::scene::IBranchSeatDefinition *copiedSeat = copy->asBranchSeatDefinition();
    assert(copiedSeat);
    loka::app::scene::Node *node = copiedSeat->branchDefinition(true)->create();
    assert(node != 0);
    (void)createdBefore;
    assert(g_probeNodesCreated > createdBefore); // true branch materialized from the owned clone
    loka::app::scene::DestroyHeapNode(node);
    delete copy;
  }

  // --- assignment deep-copies; self-assignment is safe ---
  {
    loka::core::MutableState<bool> cond(false);
    {
      CloneProbeDefinition falseBranch;
      ConditionalDefinition first(ConditionalProps(&cond, 0, &falseBranch));
      ConditionalDefinition second(ConditionalProps(&cond, 0, 0));
      second = first;
      ConditionalDefinition *same = &second;
      second = *same; // self-assignment must not free the owned branches

      loka::app::scene::Node *node = second.branchDefinition(false)->create();
      assert(node != 0);
      loka::app::scene::DestroyHeapNode(node);
    }
  }

  // --- ownership balance: every owned copy destroyed exactly once ---
  // A double delete or a leaked branch clone shows up here (and under ASan).
  assert(g_probePropsAlive == 0);
  assert(g_probeNodesAlive == 0);

  printf("==== [testConditionalDefinitionCloneOwnership] end ====\n");
}

void testMatchDefinitionClonePreservesArmsAndMatchers()
{
  loka::core::MutableState<int> selection(2);
  CloneProbeDefinition valueArm;
  CloneProbeDefinition predicateArm;
  CloneProbeDefinition otherwiseArm;
  int predicateValue = 2;
  loka::app::MatchDefinition<int> original = loka::app::Match(selection);
  original.arm(1, valueArm)
      .arm(&cloneMatchPredicate, &predicateValue, predicateArm)
      .otherwise(otherwiseArm);

  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> copy(original.clone());
  const bool copyIsSet = copy.isSet();
  LOKA_VERIFY(copyIsSet);
  loka::app::scene::IBranchSeatDefinition *copiedSeat =
      copy->asBranchSeatDefinition();
  LOKA_VERIFY(copiedSeat);
  const unsigned copiedArmCount = copiedSeat->armCount();
  const unsigned originalArmCount = original.armCount();
  LOKA_VERIFY(copiedArmCount == originalArmCount);
  assert(copiedSeat->branchSeatTypeId() == original.branchSeatTypeId());
  for (unsigned arm = 0; arm < originalArmCount; ++arm)
  {
    assert(copiedSeat->armDefinition(arm));
    assert(copiedSeat->armDefinition(arm) != original.armDefinition(arm));
  }
  unsigned originalArm = 0;
  unsigned copiedArm = 0;
  const bool originalSelected = original.selectArm(originalArm);
  const bool copySelected = copiedSeat->selectArm(copiedArm);
  LOKA_VERIFY(originalSelected && copySelected);
  assert(originalArm == 1 && copiedArm == originalArm);
}

void testConditionalDefinitionAssignmentPreservesPairOnSecondCloneFailure()
{
  printf("\n==== [testConditionalDefinitionAssignmentPreservesPairOnSecondCloneFailure] start ====\n");

  using loka::app::scene::ConditionalDefinition;
  using loka::app::scene::ConditionalProps;

  const int baseline = g_probePropsAlive;
  {
    loka::core::MutableState<bool> oldCondition(true);
    loka::core::MutableState<bool> newCondition(false);
    CloneProbeDefinition oldTrueBranch;
    CloneProbeDefinition oldFalseBranch;
    LimitedCloneProbeDefinition newTrueBranch;
    LimitedCloneProbeDefinition newFalseBranch;
    ConditionalDefinition target(ConditionalProps(&oldCondition, &oldTrueBranch, &oldFalseBranch));
    ConditionalDefinition source(ConditionalProps(&newCondition, &newTrueBranch, &newFalseBranch));

    loka::app::scene::NodeDefinitionBase *oldOwnedTrueDef = target.ownedTrueDef;
    loka::app::scene::NodeDefinitionBase *oldOwnedFalseDef = target.ownedFalseDef;
    const int aliveBeforeAssignment = g_probePropsAlive;

    g_limitedCloneBudget = 1;
    g_limitedCloneCalls = 0;
    target = source;
    g_limitedCloneBudget = -1;

    assert(g_limitedCloneCalls == 2);
    assert(target.props.condition == &oldCondition);
    (void)oldOwnedTrueDef;
    assert(target.ownedTrueDef == oldOwnedTrueDef);
    (void)oldOwnedFalseDef;
    assert(target.ownedFalseDef == oldOwnedFalseDef);
    assert(target.props.trueDef == target.ownedTrueDef);
    assert(target.props.falseDef == target.ownedFalseDef);
    (void)aliveBeforeAssignment;
    assert(g_probePropsAlive == aliveBeforeAssignment);
  }
  (void)baseline;
  assert(g_probePropsAlive == baseline);
  assert(g_probeNodesAlive == 0);

  printf("==== [testConditionalDefinitionAssignmentPreservesPairOnSecondCloneFailure] end ====\n");
}

void testConditionalDefinitionCloneReturnsNullOnSecondBranchFailure()
{
  printf("\n==== [testConditionalDefinitionCloneReturnsNullOnSecondBranchFailure] start ====\n");

  using loka::app::scene::ConditionalDefinition;
  using loka::app::scene::ConditionalProps;

  const int baseline = g_probePropsAlive;
  {
    loka::core::MutableState<bool> condition(true);
    LimitedCloneProbeDefinition trueBranch;
    LimitedCloneProbeDefinition falseBranch;
    ConditionalDefinition source(ConditionalProps(&condition, &trueBranch, &falseBranch));
    const int aliveBeforeClone = g_probePropsAlive;

    g_limitedCloneBudget = 1;
    g_limitedCloneCalls = 0;
    loka::app::scene::NodeDefinitionBase *copy = source.clone();
    g_limitedCloneBudget = -1;

    assert(g_limitedCloneCalls == 2);
    (void)copy;
    assert(copy == 0);
    (void)aliveBeforeClone;
    assert(g_probePropsAlive == aliveBeforeClone);
  }
  (void)baseline;
  assert(g_probePropsAlive == baseline);
  assert(g_probeNodesAlive == 0);

  printf("==== [testConditionalDefinitionCloneReturnsNullOnSecondBranchFailure] end ====\n");
}

void testNestableDefinitionAssignmentPreservesStableChildOnOomClone()
{
  printf("\n==== [testNestableDefinitionAssignmentPreservesStableChildOnOomClone] start ====\n");

  using loka::app::Box;
  using loka::app::BoxDefinition;

  CloneProbeDefinition originalChild;
  BoxDefinition stableTarget = Box().padding(10) << originalChild;
  BoxDefinition sourceWithBadChild;
  sourceWithBadChild.padding(20);
  sourceWithBadChild << new OomCloneProbeDefinition();

  stableTarget = sourceWithBadChild;

  LOKA_VERIFY(stableTarget.props.padding == loka::app::layout::LayoutGap::of(10));
  LOKA_VERIFY(stableTarget.childrenCount() == 1);
  LOKA_VERIFY(stableTarget.childrenHead() != 0);
  loka::app::scene::NodeDefinitionBase *childClone = stableTarget.childrenHead()->clone();
  assert(childClone != 0);
  delete childClone;

  printf("==== [testNestableDefinitionAssignmentPreservesStableChildOnOomClone] end ====\n");
}

void testNestableDefinitionCloneReturnsNullOnOomChildClone()
{
  printf("\n==== [testNestableDefinitionCloneReturnsNullOnOomChildClone] start ====\n");

  using loka::app::BoxDefinition;

  BoxDefinition sourceWithBadChild;
  sourceWithBadChild.padding(20);
  sourceWithBadChild << new OomCloneProbeDefinition();

  loka::app::scene::NodeDefinitionBase *copy = sourceWithBadChild.clone();
  (void)copy;
  assert(copy == 0);

  printf("==== [testNestableDefinitionCloneReturnsNullOnOomChildClone] end ====\n");
}

void testNodeCompositionLastDeclarationWins()
{
  using namespace loka::app;
  using namespace loka::app::scene;

  NodeComposition composition;
  BoxDefinition first = Box().testId("A");
  BoxDefinition second = Box().testId("B");
  composition.declare(first);
  LOKA_VERIFY(composition.root() != 0);
  LOKA_VERIFY(composition.root() != &first);
  LOKA_VERIFY(composition.root()->testIdValue() == "A");
  composition.declare(second);
  LOKA_VERIFY(composition.root() != 0);
  LOKA_VERIFY(composition.root() != &second);
  LOKA_VERIFY(composition.root()->testIdValue() == "B");
}

void testNodeCompositionSkipsOomClones()
{
  printf("\n==== [testNodeCompositionSkipsOomClones] start ====\n");

  {
    loka::app::scene::NodeComposition composition;
    CloneProbeDefinition stableRoot;
    OomCloneProbeDefinition failingDefinition;
    loka::app::scene::NodeDefinitionBase &failingBase = failingDefinition;

    composition.declare(stableRoot);
    loka::app::scene::NodeDefinitionBase *storedRoot = composition.root();
    (void)storedRoot;
    assert(storedRoot != 0);

    loka::app::scene::NodeDefinitionBase *grouped = composition.group(failingDefinition);
    (void)grouped;
    assert(grouped == 0);
    composition.declare(failingDefinition);
    LOKA_VERIFY(composition.root() == storedRoot);
    composition.declare(failingBase);
    LOKA_VERIFY(composition.root() == storedRoot);
    composition.declareTagged(42, failingBase);
    LOKA_VERIFY(composition.root() == storedRoot);
    composition.declareTagged(42, failingDefinition);
    LOKA_VERIFY(composition.root() == storedRoot);
  }
  assert(g_probePropsAlive == 0);
  assert(g_probeNodesAlive == 0);

  printf("==== [testNodeCompositionSkipsOomClones] end ====\n");
}

void testWindowPropsAssignmentPreservesOwnedSceneOnOomClone()
{
  printf("\n==== [testWindowPropsAssignmentPreservesOwnedSceneOnOomClone] start ====\n");

  WindowProps stableTarget;
  stableTarget.scene(CloneProbeDefinition());
  stableTarget.contentMargin(17);

  WindowProps sourceWithBadRoot;
  sourceWithBadRoot.scene(LimitedCloneProbeDefinition());
  sourceWithBadRoot.contentMargin(loka::app::StandardWindowMargin());

  g_limitedCloneBudget = 0;
  stableTarget = sourceWithBadRoot;
  LOKA_VERIFY(stableTarget.contentMarginValue == loka::app::layout::LayoutGap::of(17));
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> retained(stableTarget.takeRootDefinition());
  LOKA_VERIFY(retained.get());
  // Only the original target still allows a clone while the source budget is exhausted.
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> childClone(retained->clone());
  LOKA_VERIFY(childClone.get());
  g_limitedCloneBudget = -1;

  printf("==== [testWindowPropsAssignmentPreservesOwnedSceneOnOomClone] end ====\n");
}

void testWindowDefinitionCreateReturnsNullOnOomRootClone()
{
  printf("\n==== [testWindowDefinitionCreateReturnsNullOnOomRootClone] start ====\n");

  WindowDefinition<WindowProps> def;
  def.props.scene(LimitedCloneProbeDefinition());
  g_limitedCloneBudget = 0;

  NullWindowPlatformContext context;
  Window *window = def.create(&context);

  g_limitedCloneBudget = -1;
  (void)window;
  assert(window == 0);
  assert(context.createWindowCalls == 0);

  printf("==== [testWindowDefinitionCreateReturnsNullOnOomRootClone] end ====\n");
}

void testWindowDefinitionCreateTransfersSingleRootClone()
{
  printf("\n==== [testWindowDefinitionCreateTransfersSingleRootClone] start ====\n");

  WindowDefinition<WindowProps> def;
  def.props.scene(LimitedCloneProbeDefinition());

  NullWindowPlatformContext context;
  g_limitedCloneBudget = 1;
  g_limitedCloneCalls = 0;
  Window *window = def.create(&context);
  g_limitedCloneBudget = -1;

  (void)window;
  assert(window == 0);
  assert(g_limitedCloneCalls == 1);
  assert(context.createWindowCalls == 1);
  LOKA_VERIFY(context.sawRootedScene);

  printf("==== [testWindowDefinitionCreateTransfersSingleRootClone] end ====\n");
}

void testConditionalDefinitionCopyDegradesToEmptyOnCloneFailure()
{
  printf("\n==== [testConditionalDefinitionCopyDegradesToEmptyOnCloneFailure] start ====\n");

  using loka::app::scene::ConditionalDefinition;
  using loka::app::scene::ConditionalProps;

  const int baseline = g_probePropsAlive;
  {
    loka::core::MutableState<bool> condition(true);
    LimitedCloneProbeDefinition trueBranch;
    LimitedCloneProbeDefinition falseBranch;
    ConditionalDefinition source(ConditionalProps(&condition, &trueBranch, &falseBranch));

    // A C++98 copy constructor cannot fail; the pinned contract is that a
    // branch clone OOM degrades the copy to EMPTY (condition kept, both
    // branches null), never to a half pair.
    g_limitedCloneBudget = 1;
    ConditionalDefinition copy(source);
    g_limitedCloneBudget = -1;

    assert(copy.props.condition == &condition);
    assert(copy.props.trueDef == 0);
    assert(copy.props.falseDef == 0);
    assert(copy.ownedTrueDef == 0);
    assert(copy.ownedFalseDef == 0);
  }
  (void)baseline;
  assert(g_probePropsAlive == baseline);
  assert(g_probeNodesAlive == 0);

  printf("==== [testConditionalDefinitionCopyDegradesToEmptyOnCloneFailure] end ====\n");
}

void testWindowContentMarginRetention()
{
  using loka::app::layout::LayoutGap;
  NullPlatformContext context;
  LOKA_VERIFY(WindowProps().contentMarginValue == LayoutGap::unspecified());
  WindowDefinition<WindowProps> definition = WindowDef(WindowProps().contentMargin(23));
  loka::core::OwnedDef<WindowDefinitionBase> copy(definition.clone());
  LOKA_VERIFY(copy.get());
  Window *window = copy.get()->create(&context);
  LOKA_VERIFY(window && window->contentMargin() == LayoutGap::of(23));
  definition.props.contentMargin(0);
  LOKA_VERIFY(window->contentMargin() == LayoutGap::of(23));
  delete window;
  WindowProps assigned;
  assigned = WindowProps().contentMargin(loka::app::StandardWindowMargin());
  LOKA_VERIFY(assigned.contentMarginValue == loka::app::StandardWindowMargin());
  Window *named = context.createWindow(assigned);
  LOKA_VERIFY(named && named->contentMargin() == loka::app::StandardWindowMargin());
  delete named;
  WindowProps stable = WindowProps().contentMargin(17);
  WindowProps source;
  source.contentMargin(29).scene(
      loka::scenario_tests::ObservedMainDefinition<MarginRoot::Props, MarginRoot>(MarginRoot::Props(), 0));
  verifyTransferableRoot(source);
  loka::scenario_tests::testing::failObservedMainDefinitionClones(1);
  stable = source;
  loka::scenario_tests::testing::allowObservedMainDefinitionClones();
  LOKA_VERIFY(stable.contentMarginValue == LayoutGap::of(17));
  LOKA_VERIFY(stable.rootDefinitionRefused() && stable.takeRootDefinition() == 0);
  stable = source;
  LOKA_VERIFY(stable.contentMarginValue == LayoutGap::of(29));
  verifyTransferableRoot(stable);
}

namespace
{
  void verifyRefusedWindow(const WindowDefinition<WindowProps> &definition)
  {
    WindowRootRefusalTestSupport::CountingContext context;
    LOKA_VERIFY(definition.create(&context) == 0);
    LOKA_VERIFY(context.creates == 0);
  }

  loka::app::scene::Scene *probeScene()
  {
    return new loka::app::scene::Scene(new CloneProbeDefinition());
  }

  loka::app::scene::Scene *refusedScene()
  {
    typedef loka::scenario_tests::ObservedMainDefinition<MarginRoot::Props, MarginRoot> Definition;
    loka::scenario_tests::testing::failObservedMainDefinitionClones(1);
    return new loka::app::scene::Scene(Definition(MarginRoot::Props(), 0));
  }
} // namespace

void testWindowRootCaptureRefusal()
{
  WindowProps props;
  g_limitedCloneBudget = 0;
  props.scene(LimitedCloneProbeDefinition());
  g_limitedCloneBudget = -1;
  verifyRefusedWindow(WindowDef(props));
}

void testWindowRootCopyRefusal()
{
  WindowProps source;
  source.scene(LimitedCloneProbeDefinition());
  g_limitedCloneBudget = 0;
  WindowProps copy(source);
  g_limitedCloneBudget = -1;
  verifyRefusedWindow(WindowDef(copy));
}

void testWindowRootAssignmentRefusal()
{
  WindowProps source;
  source.scene(LimitedCloneProbeDefinition());
  WindowProps target;
  g_limitedCloneBudget = 0;
  target = source;
  g_limitedCloneBudget = -1;
  verifyRefusedWindow(WindowDef(target));
}

void testWindowRootConstructionRefusal()
{
  WindowProps source;
  source.scene(LimitedCloneProbeDefinition());
  g_limitedCloneBudget = 0;
  WindowDefinition<WindowProps> definition(source);
  g_limitedCloneBudget = -1;
  verifyRefusedWindow(definition);
}

void testWindowRootCloneRefusal()
{
  WindowDefinition<WindowProps> definition;
  definition.props.scene(LimitedCloneProbeDefinition());
  g_limitedCloneBudget = 0;
  loka::core::OwnedDef<WindowDefinitionBase> copy(definition.clone());
  g_limitedCloneBudget = -1;
  LOKA_VERIFY(!copy.isSet());
}

void testWindowRootResolutionRefusal()
{
  WindowDefinition<WindowProps> definition;
  definition.props.scene(LimitedCloneProbeDefinition());
  g_limitedCloneBudget = 0;
  verifyRefusedWindow(definition);
  g_limitedCloneBudget = -1;
}

void testWindowRootRefusalRecovery()
{
  WindowProps source;
  source.scene(OomCloneProbeDefinition())
      .title("Refused")
      .frame(1, 2, 3, 4)
      .contentMargin(23)
      .visible(false)
      .idlePolicy(loka::app::IdlePolicy::none())
      .onIdle(0, 0)
      .onKeyPress(0, 0);
  LOKA_VERIFY(source.takeRootDefinition() == 0);
  WindowProps copy(source);
  WindowProps target;
  target.scene(probeScene());
  target = copy;
  LOKA_VERIFY(!target.peekInitialScene() && target.width == 3 && !target.initialVisibility);
  verifyRefusedWindow(WindowDef(target));
  target.scene(CloneProbeDefinition()).scene(OomCloneProbeDefinition());
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(target.takeRootDefinition());
  LOKA_VERIFY(root.isSet() && !target.rootDefinitionRefused());
  target = source;
  target.scene(static_cast<loka::app::scene::Scene *>(0));
  NullPlatformContext context;
  Window *empty = WindowDef(target).create(&context);
  LOKA_VERIFY(empty && !empty->scene());
  delete empty;

  target = source;
  loka::app::scene::Scene *pending = probeScene();
  target.scene(pending).scene(pending).scene(OomCloneProbeDefinition());
  WindowProps limited;
  limited.scene(LimitedCloneProbeDefinition());
  g_limitedCloneBudget = 0;
  target = limited;
  g_limitedCloneBudget = -1;
  LOKA_VERIFY(target.peekInitialScene() == pending);
  WindowProps sibling(target);
  delete sibling.takeInitialScene();
  g_limitedCloneBudget = 0;
  sibling = limited;
  g_limitedCloneBudget = -1;
  verifyRefusedWindow(WindowDef(sibling));
  target.scene(OomCloneProbeDefinition());
  verifyRefusedWindow(WindowDef(target));
}

void testWindowSceneHandoffRefusal()
{
  WindowProps props;
  props.scene(refusedScene());
  LOKA_VERIFY(!props.peekInitialScene());
  verifyRefusedWindow(WindowDef(props));
  props.scene(CloneProbeDefinition()).scene(refusedScene());
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(props.takeRootDefinition());
  LOKA_VERIFY(root.isSet());

  loka::app::scene::Scene *pending = probeScene();
  props.scene(pending).scene(refusedScene());
  LOKA_VERIFY(props.peekInitialScene() == pending);
  WindowProps sibling(props);
  delete sibling.takeInitialScene();
  props.scene(refusedScene());
  verifyRefusedWindow(WindowDef(props));

  // Balance the allocator around all values that can allocate through the gate.
  loka::core::testing::failLokaAllocRaw("WindowProps", "InitialSceneHandoff", 2);
  {
    WindowProps target;
    pending = probeScene();
    target.scene(pending);
    WindowProps sharing(target);
    target.scene(pending);
    const int liveBeforeReplacement = g_probePropsAlive;
    target.scene(probeScene());
    LOKA_VERIFY(target.peekInitialScene() == pending);
    LOKA_VERIFY(g_probePropsAlive == liveBeforeReplacement);
    LOKA_VERIFY(sharing.takeInitialScene() == pending && !target.peekInitialScene());
    delete pending;

    target.scene(CloneProbeDefinition());
    loka::core::testing::failLokaAllocRaw("WindowProps", "InitialSceneHandoff", 1);
    target.scene(probeScene());
    verifyTransferableRoot(target);

    target.scene(static_cast<loka::app::scene::Scene *>(0));
    loka::core::testing::failLokaAllocRaw("WindowProps", "InitialSceneHandoff", 1);
    target.scene(probeScene());
    LOKA_VERIFY(target.rootDefinitionRefused());
    target.scene(refusedScene());
    LOKA_VERIFY(target.rootDefinitionRefused());
    target.scene(probeScene());
    LOKA_VERIFY(target.peekInitialScene() && !target.rootDefinitionRefused());
  }
  loka::core::testing::allowLokaAllocRaw();
}

void testWindowMixedRootDeclarations()
{
  NullPlatformContext context;
  AppComposition composition(&context);
  composition << WindowDef(WindowProps().scene(CloneProbeDefinition()).title("Good"));
  composition << WindowDef(WindowProps().scene(OomCloneProbeDefinition()));
  std::vector<AppComponent *> components = composition.build();
  LOKA_VERIFY(components.size() == 1);
  Window *window = components[0]->asWindow();
  LOKA_VERIFY(window && window->scene() && window->titleState().get().equals(loka::core::String::Literal("Good")));
  delete window;
}

namespace
{
  void observeIdle(Window *, double, void *data)
  {
    ++*static_cast<int *>(data);
  }
  bool observeKey(Window *, char key, void *data)
  {
    *static_cast<char *>(data) = key;
    return true;
  }
} // namespace

void testWindowRootSuccessfulCopies()
{
  int idleCalls = 0;
  char lastKey = 0;
  WindowProps source;
  source.scene(LimitedCloneProbeDefinition())
      .frame(11, 22, 333, 444)
      .title("Document")
      .contentMargin(7)
      .idlePolicy(loka::app::IdlePolicy::interval(0.1))
      .onIdle(&observeIdle, &idleCalls)
      .onKeyPress(&observeKey, &lastKey);
  WindowProps assigned;
  g_limitedCloneCalls = 0;
  g_limitedCloneBudget = 1;
  assigned = source;
  g_limitedCloneBudget = -1;
  LOKA_VERIFY(g_limitedCloneCalls == 1);
  WindowDefinition<WindowProps> definition(assigned);
  loka::core::OwnedDef<WindowDefinitionBase> copy(definition.clone());
  LOKA_VERIFY(copy.get());
  // Count declaration resolution before a renderer mounts and clones its own nodes.
  loka::testing::StandaloneMountTestPlatformContext context;
  g_limitedCloneCalls = 0;
  Window *window = copy->create(&context);
  LOKA_VERIFY(window && window->scene() && window->scene()->getRootDefinition() && g_limitedCloneCalls == 1);
  LOKA_VERIFY(window->frameState().get() == loka::core::Frame(11, 22, 333, 444));
  LOKA_VERIFY(window->titleState().get().equals(loka::core::String::Literal("Document")));
  LOKA_VERIFY(window->contentMargin() == loka::app::layout::LayoutGap::of(7));
  LOKA_VERIFY(window->handleIdle(0.1) && idleCalls == 1);
  LOKA_VERIFY(window->handleKeyPress('a') && lastKey == 'a');
  delete window;
}
