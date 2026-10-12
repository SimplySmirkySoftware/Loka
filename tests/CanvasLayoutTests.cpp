#include "CanvasLayoutTests.hpp"
#include "support/TestVerify.hpp"
#include <climits>
#include "app/nodes/nestable/Canvas.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/Match.hpp"
#include "app/nodes/nestable/Keyed.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/scene/Scene.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using loka::core::Frame;

  class CanvasProbe : public Node, public IProjectedLayoutNode
  {
  public:
    CanvasProbe()
        : calls(0),
          geometry()
    {
    }
    virtual IProjectedLayoutNode *asProjectedLayoutNode()
    {
      return this;
    }
    virtual short layoutProjected(IPlatformController *, LayoutState &state)
    {
      ++this->calls;
      this->geometry = state;
      return state.y;
    }
    int calls;
    LayoutState geometry;
  };

  void populate(CanvasNode &canvas, CanvasProbe **probes, int count)
  {
    for (int i = 0; i < count; ++i)
    {
      probes[i] = new CanvasProbe();
      canvas.addChild(probes[i]);
    }
  }

  LayoutState input()
  {
    LayoutState state;
    state.width = 200;
    state.height = 160;
    return state;
  }

  void moveViewport(loka::core::MutableState<Frame> &viewport, const Frame &frame)
  {
    loka::core::PushStateTracker tracker;
    tracker.addState(&viewport);
    loka::core::StateTrackerGuard guard(&tracker);
    viewport.set(frame);
  }

  /** The modifier belongs to Show, not all definitions. Detected through a
      member-pointer template argument rather than expression SFINAE, which
      GCC 4.0/4.2 (Xcode 3.2.6) cannot evaluate in an array bound. */
  template <typename T> struct HasDestroyOnDetach
  {
    template <typename M, M> struct Signature;
    template <typename U> static char probe(Signature<U &(U::*)(), &U::destroyOnDetach> *);
    template <typename U> static long probe(...);
    enum
    {
      value = sizeof(probe<T>(0)) == sizeof(char)
    };
  };
  typedef char ShowHasModifier[HasDestroyOnDetach<ShowDefinition>::value ? 1 : -1];
  typedef char CanvasHasNoModifier[!HasDestroyOnDetach<CanvasDefinition>::value ? 1 : -1];
  typedef char MatchHasNoModifier[!HasDestroyOnDetach<MatchDefinition<int> >::value ? 1 : -1];
  typedef char KeyedHasNoModifier[!HasDestroyOnDetach<KeyedDefinition<int> >::value ? 1 : -1];

  int constructions = 0;
  class CountedButton : public ButtonNode
  {
  public:
    typedef ButtonTypeTag TypeTag;
    explicit CountedButton(const ButtonProps &p)
        : ButtonNode(p)
    {
      ++constructions;
    }
  };

  class CanvasOwner : public BoundaryNodeFor<CanvasOwner>
  {
  public:
    typedef BoundaryPropsFor<CanvasOwner> Props;
    explicit CanvasOwner(const Props &p)
        : BoundaryNodeFor<CanvasOwner>(p),
          viewport(),
          shown(),
          composeCalls(0)
    {
      this->state(this->viewport, Frame(0, 0, 200, 160));
      this->state(this->shown, true);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      ++this->composeCalls;
      composition.declare(Show(*this->shown.state()).destroyOnDetach()
                          << (Canvas(100, 20, this->viewport.state())
                              << NodeDefinition<ButtonProps, CountedButton>(ButtonProps().text("canvas"))));
    }
    NodeState<Frame> viewport;
    NodeState<bool> shown;
    int composeCalls;
  };
} // namespace

void testCanvasVerticalVisibleRangeAndViewportMovement()
{
  NullScenePlatformController platform;
  loka::core::MutableState<Frame> viewport(Frame(0, 0, 200, 160));
  CanvasNode canvas(CanvasProps(200, 20, &viewport));
  CanvasProbe *probes[100];
  populate(canvas, probes, 100);
  LOKA_VERIFY(platform.projectLayoutForTesting(&canvas, input()) == 2000);
  for (int i = 0; i < 100; ++i)
  {
    LOKA_VERIFY(probes[i]->calls == (i <= 8 ? 1 : 0));
    if (i <= 8)
    {
      LOKA_VERIFY(probes[i]->geometry.x == 0);
      LOKA_VERIFY(probes[i]->geometry.y == i * 20);
    }
  }
  moveViewport(viewport, Frame(0, 1000, 200, 160));
  LOKA_VERIFY(platform.projectLayoutForTesting(&canvas, input()) == 2000);
  for (int i = 0; i < 100; ++i)
  {
    LOKA_VERIFY(probes[i]->calls == (i <= 8 || (i >= 50 && i <= 58) ? 1 : 0));
    if (i >= 50 && i <= 58)
      LOKA_VERIFY(probes[i]->geometry.y == i * 20 - 1000);
  }
}

void testCanvasWrapUsesDeclarationOrderAndRowRange()
{
  NullScenePlatformController platform;
  loka::core::MutableState<Frame> viewport(Frame(0, 20, 300, 25));
  CanvasProps props(80, 20, &viewport);
  props.wrap = 3;
  CanvasNode canvas(props);
  CanvasProbe *probes[100];
  populate(canvas, probes, 100);
  LOKA_VERIFY(platform.projectLayoutForTesting(&canvas, input()) == 680);
  for (int i = 0; i < 100; ++i)
  {
    LOKA_VERIFY(probes[i]->calls == (i >= 3 && i < 9 ? 1 : 0));
    if (i >= 3 && i < 9)
    {
      LOKA_VERIFY(probes[i]->geometry.x == i % 3 * 80);
      LOKA_VERIFY(probes[i]->geometry.y == i / 3 * 20 - 20);
      LOKA_VERIFY(probes[i]->geometry.width == 80 && probes[i]->geometry.height == 20);
    }
  }
}

void testCanvasHorizontalMirrorsVertical()
{
  NullScenePlatformController platform;
  loka::core::MutableState<Frame> viewport(Frame(0, 0, 160, 200));
  CanvasProps props(20, 200, &viewport);
  props.axis = STACK_AXIS_ROW;
  CanvasNode canvas(props);
  CanvasProbe *probes[100];
  populate(canvas, probes, 100);
  LOKA_VERIFY(platform.projectLayoutForTesting(&canvas, input()) == 200);
  for (int i = 0; i < 100; ++i)
  {
    LOKA_VERIFY(probes[i]->calls == (i <= 8 ? 1 : 0));
    if (i <= 8)
      LOKA_VERIFY(probes[i]->geometry.x == i * 20 && probes[i]->geometry.y == 0);
  }
  moveViewport(viewport, Frame(1000, 0, 160, 200));
  LOKA_VERIFY(platform.projectLayoutForTesting(&canvas, input()) == 200);
  for (int i = 50; i <= 58; ++i)
    LOKA_VERIFY(probes[i]->calls == 1 && probes[i]->geometry.x == i * 20 - 1000);
}

void testCanvasReportsFullExtentToColumn()
{
  NullScenePlatformController platform;
  loka::core::MutableState<Frame> viewport(Frame(0, 1000, 200, 160));
  StackNode column((StackProps(STACK_AXIS_COLUMN)));
  CanvasNode *canvas = new CanvasNode(CanvasProps(200, 20, &viewport));
  CanvasProbe *probes[100];
  populate(*canvas, probes, 100);
  CanvasProbe *tail = new CanvasProbe();
  column.addChild(canvas);
  column.addChild(tail);
  LayoutState state = input();
  state.x = 7;
  state.y = 11;
  LOKA_VERIFY(platform.projectLayoutForTesting(&column, state) == 2011);
  LOKA_VERIFY(tail->geometry.y == 2011);
  LOKA_VERIFY(probes[50]->geometry.x == 7 && probes[50]->geometry.y == 11);
}

void testCanvasRefusesNarrowingAndRecovers()
{
  NullScenePlatformController platform;
  loka::core::MutableState<Frame> viewport(Frame(-40000, 0, 50000, 160));
  CanvasNode canvas(CanvasProps(200, 20, &viewport));
  CanvasProbe *probes[1];
  populate(canvas, probes, 1);
  platform.projectLayoutForTesting(&canvas, input());
  LOKA_VERIFY(canvas.layoutStatus() == CANVAS_LAYOUT_SHORT_RANGE_REFUSED);
  LOKA_VERIFY(probes[0]->calls == 0);
  moveViewport(viewport, Frame(INT_MIN, 0, INT_MAX, 160));
  platform.projectLayoutForTesting(&canvas, input());
  LOKA_VERIFY(canvas.layoutStatus() == CANVAS_LAYOUT_INT_RANGE_REFUSED);
  LOKA_VERIFY(probes[0]->calls == 0);
  moveViewport(viewport, Frame(0, 0, 200, 160));
  platform.projectLayoutForTesting(&canvas, input());
  LOKA_VERIFY(canvas.layoutStatus() == CANVAS_LAYOUT_READY && probes[0]->calls == 1);
}

void testCanvasEmptyInvalidAndCrossAxisViewports()
{
  NullScenePlatformController platform;
  loka::core::MutableState<Frame> viewport(Frame(100, 0, 5, 1));
  CanvasProps props(20, 20, &viewport);
  props.wrap = 10;
  CanvasNode canvas(props);
  CanvasProbe *probes[10];
  populate(canvas, probes, 10);
  platform.projectLayoutForTesting(&canvas, input());
  for (int i = 0; i < 10; ++i)
    LOKA_VERIFY(probes[i]->calls == (i == 5 ? 1 : 0));
  moveViewport(viewport, Frame(0, 0, 0, 160));
  LOKA_VERIFY(platform.projectLayoutForTesting(&canvas, input()) == 20);
  LOKA_VERIFY(probes[0]->calls == 0);
  moveViewport(viewport, Frame(0, 10000, 200, 160));
  platform.projectLayoutForTesting(&canvas, input());
  LOKA_VERIFY(probes[0]->calls == 0);
  canvas.props.wrap = 0;
  platform.projectLayoutForTesting(&canvas, input());
  LOKA_VERIFY(canvas.layoutStatus() == CANVAS_LAYOUT_INVALID_INPUT);
}

void testShowDestroyOnDetachAndCanvasLiveViewport()
{
  constructions = 0;
  NullScenePlatformController platform;
  Scene scene((Boundary<CanvasOwner>()));
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  CanvasOwner *owner = static_cast<CanvasOwner *>(loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
  LOKA_VERIFY(owner && owner->composeCalls == 1);
  const bool ledgerFact1 = constructions == 1 && platform.ledger().size() == 1;
  LOKA_VERIFY(ledgerFact1);
  const unsigned long changes = platform.onChangeCallCount();
  owner->viewport.set(Frame(0, 1, 200, 160));
  if (scene.hasPendingInvalidation())
    scene.flushInvalidation();
  LOKA_VERIFY(platform.onChangeCallCount() > changes && owner->composeCalls == 1);
  owner->shown.set(false);
  scene.flushInvalidation();
  platform.drainNativeRetirements();
  const bool ledgerFact2 = 0 == platform.ledger().size();
  LOKA_VERIFY(ledgerFact2);
  owner->shown.set(true);
  scene.flushInvalidation();
  const bool ledgerFact3 = constructions == 2 && platform.ledger().size() == 1;
  LOKA_VERIFY(ledgerFact3);
  LOKA_VERIFY(owner->composeCalls == 1);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}

void testShowDestroyModifierCopiesTrueArmOnly()
{
  loka::core::MutableState<bool> condition(true);
  ShowDefinition original = Show(condition).destroyOnDetach();
  ShowDefinition copied(original);
  ShowDefinition assigned = Show(condition);
  assigned = original;
  loka::core::OwnedDef<NodeDefinitionBase> cloned(original.clone());
  LOKA_VERIFY(cloned.get() != 0);
  LOKA_VERIFY(copied.armPolicies(1).destroyOnDetach);
  LOKA_VERIFY(assigned.armPolicies(1).destroyOnDetach);
  LOKA_VERIFY(cloned.get()->asBranchSeatDefinition()->armPolicies(1).destroyOnDetach);
  LOKA_VERIFY(!original.armPolicies(0).destroyOnDetach);
  IBranchPolicyScopeDefinition *scope = original.armDefinition(1)->asBranchPolicyScopeDefinition();
  LOKA_VERIFY(scope == 0);
  ShowDefinition retained = Show(condition);
  LOKA_VERIFY(original.armPolicies(1).destroyOnDetach !=
              retained.armPolicies(1).destroyOnDetach);
  ConditionalDefinition conditional(ConditionalProps(&condition, 0, 0));
  LOKA_VERIFY(conditional.armPolicies(1).destroyOnDetach !=
              original.armPolicies(1).destroyOnDetach);
  LOKA_VERIFY(conditional.branchCondition() == retained.branchCondition());
  LOKA_VERIFY(conditional.armPolicies(1).destroyOnDetach ==
              retained.armPolicies(1).destroyOnDetach);
  LOKA_VERIFY(!original.armPolicies(1).deliverWhileDetached);
}

void testCanvasSubtractsLargeWorldOriginBeforeNarrowing()
{
  NullScenePlatformController platform;
  loka::core::MutableState<Frame> viewport(Frame(0, 70001, 200, 160));
  CanvasNode canvas(CanvasProps(200, 20, &viewport));
  CanvasProbe *probes[3600];
  populate(canvas, probes, 3600);
  LOKA_VERIFY(platform.projectLayoutForTesting(&canvas, input()) == 72000);
  for (int i = 0; i < 3600; ++i)
  {
    LOKA_VERIFY(probes[i]->calls == (i >= 3500 && i <= 3508 ? 1 : 0));
    if (i >= 3500 && i <= 3508)
      LOKA_VERIFY(probes[i]->geometry.y == i * 20 - 70001);
  }
  LOKA_VERIFY(canvas.layoutStatus() == CANVAS_LAYOUT_READY);
  moveViewport(viewport, Frame(0, INT_MAX, 200, 160));
  platform.projectLayoutForTesting(&canvas, input());
  LOKA_VERIFY(canvas.layoutStatus() == CANVAS_LAYOUT_INT_RANGE_REFUSED);
  LOKA_VERIFY(probes[3500]->calls == 1);
}

void testCanvasHorizontalWrapAndEmptyContent()
{
  NullScenePlatformController platform;
  loka::core::MutableState<Frame> viewport(Frame(20, 0, 25, 300));
  CanvasProps props(20, 80, &viewport);
  props.axis = STACK_AXIS_ROW;
  props.wrap = 3;
  CanvasNode canvas(props);
  LOKA_VERIFY(platform.projectLayoutForTesting(&canvas, input()) == 0);
  CanvasProbe *probes[10];
  populate(canvas, probes, 10);
  LOKA_VERIFY(platform.projectLayoutForTesting(&canvas, input()) == 240);
  for (int i = 0; i < 10; ++i)
  {
    LOKA_VERIFY(probes[i]->calls == (i >= 3 && i < 9 ? 1 : 0));
    if (i >= 3 && i < 9)
      LOKA_VERIFY(probes[i]->geometry.x == i / 3 * 20 - 20 && probes[i]->geometry.y == i % 3 * 80);
  }
}
