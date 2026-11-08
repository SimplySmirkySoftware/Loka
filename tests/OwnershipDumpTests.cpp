#include "OwnershipDumpTests.hpp"

#include "support/TestVerify.hpp"
#include "platform/null/NullPlatformContext.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "../example/HelloWorld/src/MyAppConfig.hpp"
#include "../example/HelloWorld/src/ProductionAppConfig.hpp"
#include "../example/MineSweeper/src/MainNode.hpp"
#include "app/PlatformContext.hpp"
#include "app/core/App.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/core/Window.hpp"
#include "app/nodes/nestable/BoundarySection.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/scene/Scene.hpp"
#include "app/scene/composition/NodeComposition.hpp"
#include "app/scene/state/NodeState.hpp"
#include "core/State.hpp"
#include "core/StateTracker.hpp"
#include "support/RecordingPlatformController.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/PolicyScope.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "testing/scene/OwnershipDump.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace
{
  struct OwnershipDumpPayload
  {
  };

  struct ParkedSeatProbeRecord
  {
  };

  struct OwnershipDumpScenario
  {
    OwnershipDumpScenario(bool firstHolder = true, bool secondHolder = false)
        : held(),
          arenaState(),
          dirtySource(0),
          parkedCondition(true),
          creatorOwner(0),
          showCreator(true),
          useConditional(false),
          nestedBoundaryBranch(false),
          parkedSeatFixturePhase(-1),
          showOriginalSeat(true),
          showShiftedSeat(false),
          parkedSeatOuterCondition(true),
          parkedSeatInnerCondition(true),
          parkedSeatShiftedCondition(true),
          createHeld(true),
          declareFirstHolder(firstHolder),
          declareSecondHolder(secondHolder),
          showFirstHolder(true),
          showSecondHolder(true),
          createStates(true),
          releaseCount(0)
    {
    }

    loka::core::Held<OwnershipDumpPayload> held;
    loka::app::scene::NodeState<int> arenaState;
    loka::core::MutableState<int> dirtySource;
    loka::core::MutableState<bool> parkedCondition;
    loka::app::scene::IStateOwner *creatorOwner;
    loka::core::MutableState<bool> showCreator;
    bool useConditional;
    bool nestedBoundaryBranch;
    int parkedSeatFixturePhase;
    loka::core::MutableState<bool> showOriginalSeat;
    loka::core::MutableState<bool> showShiftedSeat;
    loka::core::MutableState<bool> parkedSeatOuterCondition;
    loka::core::MutableState<bool> parkedSeatInnerCondition;
    loka::core::MutableState<bool> parkedSeatShiftedCondition;
    ParkedSeatProbeRecord shiftedFalseRecord;
    bool createHeld;
    // Which holders the fixture declares is fixed at construction; the Show
    // States below only remove a declared holder later (the declaration is
    // frozen at mount, so a State must not decide what gets declared).
    bool declareFirstHolder;
    bool declareSecondHolder;
    loka::core::MutableState<bool> showFirstHolder;
    loka::core::MutableState<bool> showSecondHolder;
    bool createStates;
    int releaseCount;
  };

  OwnershipDumpScenario *g_ownershipDumpScenario = 0;

  void verifyOwnershipDump(const std::string &actual,
                           const std::string &expected)
  {
    if (actual == expected)
    {
      return;
    }
    std::fprintf(stderr,
                 "ownership dump mismatch\n--- expected ---\n%s--- actual ---\n%s",
                 expected.c_str(),
                 actual.c_str());
    std::abort();
  }

  void collectMineSweeperMineCoordinates(
      loka::app::scene::Node *node,
      int &cellIndex,
      std::vector<int> &coordinates)
  {
    if (!node)
    {
      return;
    }
    if (node->propsTypeId() == minesweeper::MineCellProps::staticTypeId())
    {
      minesweeper::MineCellNode *cell =
          static_cast<minesweeper::MineCellNode *>(node);
      if (cell->props.isMine)
      {
        coordinates.push_back(cellIndex);
      }
      ++cellIndex;
    }
    loka::app::scene::INestable *nestable = node->asNestable();
    if (!nestable)
    {
      return;
    }
    for (loka::app::scene::Node *child = nestable->childrenHead();
         child;
         child = child->nextInComposition)
    {
      collectMineSweeperMineCoordinates(child, cellIndex, coordinates);
    }
  }

  std::vector<int> captureMineSweeperMineCoordinates(
      loka::app::scene::Scene &scene)
  {
    std::vector<int> coordinates;
    int cellIndex = 0;
    collectMineSweeperMineCoordinates(
        loka::dsl::testing::SceneTestAccess::rootNode(scene),
        cellIndex,
        coordinates);
    LOKA_VERIFY(cellIndex == 64);
    LOKA_VERIFY(coordinates.size() == 10);
    return coordinates;
  }

  void findMineSweeperNewGameButton(loka::app::scene::Node *node,
                                    loka::app::ButtonNode *&button,
                                    int &matches)
  {
    if (!node)
    {
      return;
    }
    if (node->nodeTypeKey() ==
        loka::app::scene::NodeTypeToken<loka::app::ButtonNode>())
    {
      button = static_cast<loka::app::ButtonNode *>(node);
      ++matches;
    }
    loka::app::scene::INestable *nestable = node->asNestable();
    for (loka::app::scene::Node *child =
             nestable ? nestable->childrenHead() : 0;
         child;
         child = child->nextInComposition)
    {
      findMineSweeperNewGameButton(child, button, matches);
    }
  }

  std::vector<std::vector<int> > captureMineSweeperGameSequence(
      unsigned long seed,
      int gameCount)
  {
    using namespace loka::app::scene;
    const minesweeper::MainProps mainProps(seed);
    NodeDefinition<minesweeper::MainProps, minesweeper::MainNode>
        mainDefinition(mainProps);
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::NodeDefinitionBase *rootDefinition =
        mainDefinition.clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    std::vector<std::vector<int> > games;
    for (int game = 0; game < gameCount; ++game)
    {
      games.push_back(captureMineSweeperMineCoordinates(scene));
      if (game + 1 == gameCount)
      {
        continue;
      }

      BoundaryNode *wrapper =
          loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
      assert(wrapper);
      Node *root = loka::dsl::testing::SceneTestAccess::rootNode(scene);
      assert(root);
      loka::app::ButtonNode *button = 0;
      int buttonMatches = 0;
      findMineSweeperNewGameButton(root, button, buttonMatches);
      LOKA_VERIFY(buttonMatches == 1 && button != 0);
      {
        loka::core::StateTrackerGuard guard(wrapper->tracker());
        button->props.onClick_->emit();
      }
      LOKA_VERIFY(scene.flushInvalidation());
    }
    return games;
  }

  void releaseOwnershipDumpPayload(OwnershipDumpPayload *payload)
  {
    assert(g_ownershipDumpScenario);
    ++g_ownershipDumpScenario->releaseCount;
    delete payload;
  }

  struct OwnershipDumpProbeTypeTag
  {
  };

  class OwnershipDumpProbeNode;

  struct OwnershipDumpProbeProps
      : public loka::app::scene::NodePropsBase<OwnershipDumpProbeProps>
  {
    typedef OwnershipDumpProbeTypeTag TypeTag;
    typedef OwnershipDumpProbeNode NodeType;

    enum Role
    {
      ROLE_CREATE = 0,
      ROLE_HOLD
    };

    explicit OwnershipDumpProbeProps(Role value = ROLE_HOLD)
        : role(value)
    {
    }

    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
      {
        return false;
      }
      const OwnershipDumpProbeProps &other =
          static_cast<const OwnershipDumpProbeProps &>(rhs);
      return this->role < other.role;
    }

    Role role;
  };

  class OwnershipDumpProbeNode : public loka::app::scene::ComposableNode
  {
  public:
    typedef OwnershipDumpProbeTypeTag TypeTag;
    OwnershipDumpProbeProps props;

    explicit OwnershipDumpProbeNode(const OwnershipDumpProbeProps &value)
        : loka::app::scene::ComposableNode(),
          props(value)
    {
    }

    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<OwnershipDumpProbeNode>();
    }

  protected:
    virtual void composeWithContext(
        loka::app::scene::ComponentContext &context,
        loka::app::scene::ComposeEvent event)
    {
      assert(g_ownershipDumpScenario);
      if (event != loka::app::scene::COMPOSE_EVENT_ATTACH)
      {
        return;
      }
      OwnershipDumpScenario &scenario = *g_ownershipDumpScenario;
      loka::app::scene::NodeComposition composition;
      composition.setContext(&context);
      if (this->props.role == OwnershipDumpProbeProps::ROLE_CREATE)
      {
        scenario.creatorOwner = context.stateOwner();
        if (scenario.createStates && !scenario.arenaState.isValid())
        {
          composition.declareStates().state(scenario.arenaState, 17);
          context.stateOwner()->adoptState(
              new loka::core::MutableState<int>(23));
        }
        if (scenario.createHeld && !scenario.held.isValid())
        {
          scenario.held = composition.hold(
              new OwnershipDumpPayload(),
              &releaseOwnershipDumpPayload);
          if (!scenario.held.isValid())
          {
            std::abort();
          }
        }
        return;
      }
      const loka::core::Held<OwnershipDumpPayload> acquired =
          composition.hold(scenario.held);
      if (!acquired.isValid())
      {
        std::abort();
      }
    }
  };

  typedef loka::app::scene::NodeDefinition<OwnershipDumpProbeProps,
                                           OwnershipDumpProbeNode>
      OwnershipDumpProbeDefinition;

  struct ParkedSeatProbeTypeTag
  {
  };

  class ParkedSeatProbeNode;

  struct ParkedSeatProbeProps
      : public loka::app::scene::NodePropsBase<ParkedSeatProbeProps>
  {
    typedef ParkedSeatProbeTypeTag TypeTag;
    typedef ParkedSeatProbeNode NodeType;

    ParkedSeatProbeProps(int birthValue = 0,
                         ParkedSeatProbeRecord *recordValue = 0)
        : birth(birthValue),
          record(recordValue)
    {
    }

    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
      {
        return false;
      }
      const ParkedSeatProbeProps &other =
          static_cast<const ParkedSeatProbeProps &>(rhs);
      if (this->birth != other.birth)
      {
        return this->birth < other.birth;
      }
      return this->record < other.record;
    }

    int birth;
    ParkedSeatProbeRecord *record;
  };

  class ParkedSeatProbeNode : public loka::app::scene::Node
  {
  public:
    typedef ParkedSeatProbeTypeTag TypeTag;
    ParkedSeatProbeProps props;

    explicit ParkedSeatProbeNode(const ParkedSeatProbeProps &value)
        : loka::app::scene::Node(),
          props(value),
          birth_(value.birth)
    {
    }

    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<ParkedSeatProbeNode>();
    }

    int birth() const
    {
      return this->birth_;
    }

  private:
    int birth_;
  };

  typedef loka::app::scene::NodeDefinition<ParkedSeatProbeProps,
                                           ParkedSeatProbeNode>
      ParkedSeatProbeDefinition;

  ParkedSeatProbeNode *findLiveParkedSeatProbe(
      loka::app::scene::Node *node,
      ParkedSeatProbeRecord *record)
  {
    if (!node)
    {
      return 0;
    }
    if (node->nodeTypeKey() ==
        loka::app::scene::NodeTypeToken<ParkedSeatProbeNode>())
    {
      ParkedSeatProbeNode *probe = static_cast<ParkedSeatProbeNode *>(node);
      if (probe->props.record == record)
      {
        return probe;
      }
    }
    loka::app::scene::INestable *nestable = node->asNestable();
    for (loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0;
         child;
         child = child->nextInComposition)
    {
      ParkedSeatProbeNode *found = findLiveParkedSeatProbe(child, record);
      if (found)
      {
        return found;
      }
    }
    return 0;
  }

  class OwnershipDumpNestedBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<OwnershipDumpNestedBoundaryNode>
      OwnershipDumpNestedBoundaryProps;

  class OwnershipDumpNestedBoundaryNode : public loka::app::scene::BoundaryNodeFor<OwnershipDumpNestedBoundaryNode>
  {
  public:
    explicit OwnershipDumpNestedBoundaryNode(const OwnershipDumpNestedBoundaryProps &props)
        : loka::app::scene::BoundaryNodeFor<OwnershipDumpNestedBoundaryNode>(props)
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      loka::app::Fragment root;
      loka::app::Section creator(4111);
      creator << OwnershipDumpProbeDefinition(
          OwnershipDumpProbeProps(OwnershipDumpProbeProps::ROLE_CREATE));
      root << creator;
      composition.declare(root);
    }
  };

  class OwnershipDumpRootNode;
  typedef loka::app::scene::BoundaryPropsFor<OwnershipDumpRootNode>
      OwnershipDumpRootProps;

  class OwnershipDumpRootNode : public loka::app::scene::BoundaryNodeFor<OwnershipDumpRootNode>
  {
  public:
    explicit OwnershipDumpRootNode(const OwnershipDumpRootProps &props)
        : loka::app::scene::BoundaryNodeFor<OwnershipDumpRootNode>(props)
    {
      assert(g_ownershipDumpScenario);
    }

    virtual void composeNode(
        loka::app::scene::NodeComposition &composition)
    {
      assert(g_ownershipDumpScenario);
      OwnershipDumpScenario &scenario = *g_ownershipDumpScenario;
      loka::app::Fragment root;
      if (scenario.parkedSeatFixturePhase == 0)
      {
        ParkedSeatProbeDefinition oldTrue((ParkedSeatProbeProps(101)));
        ParkedSeatProbeDefinition oldFalse((ParkedSeatProbeProps(102)));
        loka::app::scene::ConditionalDefinition inner(
            (loka::app::scene::ConditionalProps(
                &scenario.parkedSeatInnerCondition, &oldTrue, &oldFalse)));
        loka::app::FragmentDefinition outerTrueBranch;
        outerTrueBranch << inner;
        loka::app::FragmentDefinition emptyOuterBranch;
        loka::app::scene::ConditionalDefinition outer(
            (loka::app::scene::ConditionalProps(
                &scenario.parkedSeatOuterCondition,
                &outerTrueBranch,
                &emptyOuterBranch)));
        loka::app::PolicyScopeDefinition original;
        original.destroyOnDetach() << outer;
        root << (loka::app::Show(scenario.showOriginalSeat) << original);
        ParkedSeatProbeDefinition slotSpacer((ParkedSeatProbeProps(200)));
        ParkedSeatProbeDefinition secondSlotSpacer((ParkedSeatProbeProps(200)));
        ParkedSeatProbeDefinition shiftedTrue((ParkedSeatProbeProps(201)));
        ParkedSeatProbeDefinition shiftedFalse(
            (ParkedSeatProbeProps(202, &scenario.shiftedFalseRecord)));
        loka::app::scene::ConditionalDefinition shifted(
            (loka::app::scene::ConditionalProps(
                &scenario.parkedSeatShiftedCondition,
                &shiftedTrue,
                &shiftedFalse)));
        loka::app::PolicyScopeDefinition shiftedScope;
        shiftedScope.destroyOnDetach() << slotSpacer << secondSlotSpacer << shifted;
        root << (loka::app::Show(scenario.showShiftedSeat) << shiftedScope);
        composition.declare(root);
        return;
      }
      {
        loka::app::Section creator(4101);
        creator << OwnershipDumpProbeDefinition(
            OwnershipDumpProbeProps(OwnershipDumpProbeProps::ROLE_CREATE))
                       .tag(4191);
        if (scenario.declareFirstHolder)
        {
          loka::app::Section holder(4102);
          holder << OwnershipDumpProbeDefinition(
              OwnershipDumpProbeProps(OwnershipDumpProbeProps::ROLE_HOLD));
          loka::app::PolicyScopeDefinition heldScope;
          heldScope.destroyOnDetach() << holder;
          creator << (loka::app::Show(scenario.showFirstHolder) << heldScope);
        }
        if (scenario.declareSecondHolder)
        {
          loka::app::Section holder(4103);
          holder << OwnershipDumpProbeDefinition(
              OwnershipDumpProbeProps(OwnershipDumpProbeProps::ROLE_HOLD));
          loka::app::PolicyScopeDefinition heldScope;
          heldScope.destroyOnDetach() << holder;
          creator << (loka::app::Show(scenario.showSecondHolder) << heldScope);
        }
        if (scenario.useConditional)
        {
          loka::app::FragmentDefinition emptyBranch;
          if (scenario.nestedBoundaryBranch)
          {
            loka::app::FragmentDefinition trueBranch;
            trueBranch << loka::app::scene::Boundary<
                OwnershipDumpNestedBoundaryNode>();
            loka::app::scene::ConditionalDefinition conditional(
                (loka::app::scene::ConditionalProps(
                    &scenario.parkedCondition, &trueBranch, &emptyBranch)));
            root << conditional;
          }
          else
          {
            loka::app::scene::ConditionalDefinition conditional(
                (loka::app::scene::ConditionalProps(
                    &scenario.parkedCondition, &creator, &emptyBranch)));
            root << conditional;
          }
        }
        else
        {
          loka::app::PolicyScopeDefinition creatorScope;
          creatorScope.destroyOnDetach() << creator;
          root << (loka::app::Show(scenario.showCreator) << creatorScope);
        }
      }
      composition.declare(root);
    }

    virtual void declareDirtySources(
        loka::app::scene::DirtySourceRegistrar &registrar)
    {
      if (g_ownershipDumpScenario->useConditional)
        registrar.markDirtyOnChange(&g_ownershipDumpScenario->dirtySource, loka::app::scene::NODE_DIRTY_PROPS);
    }
  };

  class OwnershipDumpTestApp : public App
  {
  public:
    OwnershipDumpTestApp()
        : App(0)
    {
    }

    virtual void quit()
    {
    }

    void install(Window *window)
    {
      this->group_ =
          new AppComponentGroup(std::vector<AppComponent *>(1, window));
      this->setActiveWindow(window);
    }
  };

  class OwnershipDumpPlatformContext : public PlatformContext
  {
  public:
    virtual App *createApp(AppConfigurable *, NativeModuleHandle, int) const
    {
      return 0;
    }

    virtual Window *createWindow(const WindowProps &)
    {
      return 0;
    }

    virtual loka::app::scene::NodeContext *createNodeContext(
        loka::app::scene::Node *) const
    {
      return 0;
    }

    virtual bool openFile(
        const loka::file::File &,
        loka::platform::file::FileHandle &) const
    {
      return false;
    }

    virtual bool createImageFromBlob(
        const loka::core::resource::Blob &,
        std::size_t,
        std::size_t,
        loka::core::resource::Image &)
    {
      return false;
    }
  };

  loka::app::scene::Scene *createFixtureScene(
      SceneTestSupport::RecordingPlatformController &platform)
  {
    loka::app::scene::BoundaryDefinition<OwnershipDumpRootProps,
                                         OwnershipDumpRootNode> root;
    loka::app::scene::Scene *scene = new loka::app::scene::Scene(root);
    scene->mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(*scene, true);
    loka::app::scene::BoundaryNode *boundary =
        loka::dsl::testing::SceneTestAccess::rootBoundary(*scene);
    if (!boundary)
    {
      std::abort();
    }
    boundary->setTestId("MainView");
    return scene;
  }

  void flushFixtureSeats(loka::app::scene::Scene &scene)
  {
    scene.requestInvalidate(loka::app::scene::NODE_DIRTY_CHILD);
    LOKA_VERIFY(scene.flushInvalidation());
  }

  std::vector<int> captureHelloWorldRandomMenu(loka::app::scene::Scene &scene)
  {
    const loka::app::MenuBarDefinition *bar = scene.menuBar();
    LOKA_VERIFY(bar);
    loka::app::MenuDefinition *randomMenu = 0;
    for (loka::app::MenuDefinition *menu = bar->menusHead();
         menu;
         menu = menu->nextInComposition)
    {
      if (menu->title.equals(loka::core::String::Literal("Random")))
      {
        randomMenu = menu;
        break;
      }
    }
    LOKA_VERIFY(randomMenu != 0);

    LOKA_VERIFY(randomMenu->itemsHead()->onClickState);
    randomMenu->itemsHead()->onClickState->emit();
    std::vector<int> order;
    for (loka::app::MenuItemDefinition *item = randomMenu->itemsHead();
         item;
         item = item->nextInComposition)
    {
      for (int label = 1; label <= 6; ++label)
      {
        const loka::core::String expected =
            loka::core::String::Literal("Random ") +
            loka::core::String::FromInt(label);
        if (item->titleState && item->titleState->get().equals(expected))
        {
          order.push_back(label);
          break;
        }
      }
    }
    LOKA_VERIFY(order.size() == 6);
    return order;
  }

  std::vector<std::vector<int> > captureHelloWorldRandomMenuSequence(
      unsigned long seed,
      int menuCount)
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene((loka::app::scene::Boundary<helloworld::MainNode>(helloworld::MainProps(seed))));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    std::vector<std::vector<int> > sequence;
    for (int i = 0; i < menuCount; ++i)
    {
      sequence.push_back(captureHelloWorldRandomMenu(scene));
    }
    return sequence;
  }

  void verifyHelloWorldMenuOrder(const std::vector<int> &actual,
                                 const int *expected)
  {
    LOKA_VERIFY(actual.size() == 6);
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
      LOKA_VERIFY(actual[i] == expected[i]);
    }
  }
} // namespace

void testOwnershipDumpPinsRepresentativeHelloWorld()
{
  using namespace loka::app::scene;
  const std::vector<std::vector<int> > firstSequence =
      captureHelloWorldRandomMenuSequence(0x13579BDFUL, 3);
  const std::vector<std::vector<int> > repeatedSequence =
      captureHelloWorldRandomMenuSequence(0x13579BDFUL, 3);
  const std::vector<std::vector<int> > differentSeedSequence =
      captureHelloWorldRandomMenuSequence(0x2468ACE0UL, 1);
  LOKA_VERIFY(firstSequence == repeatedSequence);
  LOKA_VERIFY(firstSequence[0] != differentSeedSequence[0]);
  const int expectedFirst[6] = {3, 5, 4, 1, 6, 2};
  const int expectedSecond[6] = {3, 4, 6, 1, 5, 2};
  const int expectedThird[6] = {5, 2, 4, 6, 1, 3};
  verifyHelloWorldMenuOrder(firstSequence[0], expectedFirst);
  verifyHelloWorldMenuOrder(firstSequence[1], expectedSecond);
  verifyHelloWorldMenuOrder(firstSequence[2], expectedThird);

  BoundaryDefinition<helloworld::MainProps, helloworld::MainNode> mainDefinition;
  SceneTestSupport::RecordingPlatformController platform;
  loka::app::scene::NodeDefinitionBase *rootDefinition = mainDefinition.clone();
  LOKA_VERIFY(rootDefinition != 0);
  Scene scene(rootDefinition);
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  const std::string expected("scene\n"
                             "  boundary\n"
                             "    states: 17 (arena 17, heap 0)\n"
                             "    observed: 9\n"
                             "    own-input: 1\n");
  verifyOwnershipDump(
      loka::dsl::testing::OwnershipDump::dump(scene), expected);
}

void testHelloWorldProductionSeedAdapterPassesDerivedSeed()
{
  const std::time_t wallClockSeconds =
      static_cast<std::time_t>(0x13579BDFUL);
  const HelloWorldMenuSeed seed =
      HelloWorldMenuSeed::FromWallClock(wallClockSeconds);
  LOKA_VERIFY(seed.value() == 0x13579BDFUL);

  NullPlatformContext context;
  HelloWorldProductionAppConfig production(&context, seed);
  HelloWorldAppConfig expected(&context, seed.value());
  AppComposition productionComposition(&context), expectedComposition(&context);
  production.compose(productionComposition);
  expected.compose(expectedComposition);
  std::vector<AppComponent *> actualWindows = productionComposition.build();
  std::vector<AppComponent *> expectedWindows = expectedComposition.build();
  LOKA_VERIFY(actualWindows.size() == 1 && expectedWindows.size() == 1);
  Window *actualWindow = actualWindows[0]->asWindow();
  Window *expectedWindow = expectedWindows[0]->asWindow();
  LOKA_VERIFY(actualWindow && expectedWindow);
  const int first[6] = {3, 5, 4, 1, 6, 2};
  for (int i = 0; i < 3; ++i)
  {
    const std::vector<int> actual = captureHelloWorldRandomMenu(*actualWindow->scene());
    LOKA_VERIFY(actual == captureHelloWorldRandomMenu(*expectedWindow->scene()));
    if (i == 0) verifyHelloWorldMenuOrder(actual, first);
  }
  delete actualWindow;
  delete expectedWindow;
}

void testOwnershipDumpPinsMineSweeperSections()
{
  using namespace loka::app::scene;
  // The seed is a complete input fact: it pins both the initial mine
  // coordinates and every board reached by the owned New Game sequence.
  const std::vector<std::vector<int> > firstSequence =
      captureMineSweeperGameSequence(0x13579BDFU, 3);
  const std::vector<std::vector<int> > repeatedSequence =
      captureMineSweeperGameSequence(0x13579BDFU, 3);
  const std::vector<std::vector<int> > differentSeedSequence =
      captureMineSweeperGameSequence(0x2468ACE0U, 1);
  LOKA_VERIFY(firstSequence == repeatedSequence);
  LOKA_VERIFY(firstSequence[0] != differentSeedSequence[0]);

  NodeDefinition<minesweeper::MainProps, minesweeper::MainNode> mainDefinition(
      minesweeper::MainProps(0x13579BDFU));
  SceneTestSupport::RecordingPlatformController platform;
  loka::app::scene::NodeDefinitionBase *rootDefinition = mainDefinition.clone();
  LOKA_VERIFY(rootDefinition != 0);
  Scene scene(rootDefinition);
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  // The #270 ownership flip: each cell's presentation resident now lives in
  // its own owner-scope box; the parent's four hand-declared arrays are gone.
  // The boundary owns the bank
  // key; every cell box holds exactly one gate-allocated heap resident.
  std::string expected("scene\n"
                       "  boundary\n"
                       "    boundary\n"
                       "      states: 1 (arena 1, heap 0)\n"
                       "      observed: 65\n");
  for (int i = 0; i < 64; ++i)
  {
    std::ostringstream row;
    row << "      section(" << (100 + i) << ")\n"
        << "        states: 1 (arena 0, heap 1)\n";
    expected += row.str();
  }
  verifyOwnershipDump(
      loka::dsl::testing::OwnershipDump::dump(scene), expected);
}

void testOwnershipDumpPinsMineSweeperNewGameRetiresCells()
{
  using namespace loka::app::scene;
  NodeDefinition<minesweeper::MainProps, minesweeper::MainNode> mainDefinition(
      minesweeper::MainProps(0x13579BDFU));
  SceneTestSupport::RecordingPlatformController platform;
  loka::app::scene::NodeDefinitionBase *rootDefinition = mainDefinition.clone();
  LOKA_VERIFY(rootDefinition != 0);
  Scene scene(rootDefinition);
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  loka::app::scene::BoundaryNode *wrapper =
      loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
  assert(wrapper);

  // A new game changes the Keyed generation, retiring the entire board:
  // all 64 old boxes -- residents included -- are replaced by 64
  // fresh covered cells. Old rows are reclaimed by the next drain. Two real
  // button clicks, not direct handler calls: the second click only works if
  // the boundary's original binding remains armed across seat replacements.
  for (int round = 0; round < 2; ++round)
  {
    loka::app::scene::Node *root =
        loka::dsl::testing::SceneTestAccess::rootNode(scene);
    assert(root);
    long idMatches = 0;
    long typedMatches = 0;
    loka::app::ButtonNode *button = 0;
    loka::dsl::testing::scene_test_detail::findNodeByIdRecursive<
        loka::app::ButtonNode>(root,
                               std::string("MineSweeper.NewGameButton"),
                               idMatches,
                               typedMatches,
                               button);
    LOKA_VERIFY(idMatches == 1 && button != 0);
    {
      loka::core::StateTrackerGuard guard(wrapper->tracker());
      button->props.onClick_->emit();
    }
    assert(scene.hasPendingInvalidation() &&
           "retired cell boxes must wait for the drain");
    LOKA_VERIFY(scene.flushInvalidation() &&
                "returned cell slots admit the next board");

    const int baseKey = 100;
    std::string expected("scene\n"
                         "  boundary\n"
                         "    boundary\n"
                         "      states: 1 (arena 1, heap 0)\n"
                         "      observed: 65\n");
    for (int i = 0; i < 64; ++i)
    {
      std::ostringstream row;
      row << "      section(" << (baseKey + i) << ")\n"
          << "        states: 1 (arena 0, heap 1)\n";
      expected += row.str();
    }
    verifyOwnershipDump(
        loka::dsl::testing::OwnershipDump::dump(scene), expected);
  }
}

void testOwnershipDumpPinsFullVocabulary()
{
  OwnershipDumpScenario scenario;
  g_ownershipDumpScenario = &scenario;
  {
    OwnershipDumpPlatformContext context;
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene *scene = createFixtureScene(platform);
    WindowProps props;
    props.scene(scene);
    OwnershipDumpTestApp app;
    app.install(new Window(&context, props));

    const std::string expected(
        "app\n"
        "  window[0]\n"
        "    scene\n"
        "      boundary \"MainView\"\n"
        "        observed: 2\n"
        "        section(4101)\n"
        "          states: 2 (arena 1, heap 1)\n"
        "          held#1 count=2 held-by [section(4101) x1, section(4102) x1]\n"
        "          section(4102)\n"
        "            held#1 count=2 held-by [section(4101) x1, section(4102) x1]\n");
    verifyOwnershipDump(
        loka::dsl::testing::OwnershipDump::dump(app), expected);
  }
  assert(scenario.releaseCount == 1);
  g_ownershipDumpScenario = 0;
}

void testOwnershipDumpHeldByNamesSurvivingOwner()
{
  OwnershipDumpScenario scenario(true, true);
  scenario.createStates = false;
  g_ownershipDumpScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene *scene = createFixtureScene(platform);
    if (!scenario.creatorOwner)
    {
      std::abort();
    }
    scenario.creatorOwner->detachHeldResources();
    {
      loka::core::StateTrackerGuard guard(loka::dsl::testing::SceneTestAccess::rootBoundary(*scene)->tracker());
      scenario.showFirstHolder.set(false);
    }
    flushFixtureSeats(*scene);

    const std::string expected(
        "scene\n"
        "  boundary \"MainView\"\n"
        "    observed: 3\n"
        "    section(4101)\n"
        "      section(4103)\n"
        "        held#1 count=1 held-by [section(4103) x1]\n");
    verifyOwnershipDump(
        loka::dsl::testing::OwnershipDump::dump(*scene), expected);
    delete scene;
  }
  assert(scenario.releaseCount == 1);
  g_ownershipDumpScenario = 0;
}

void testOwnershipDumpIsDeterministic()
{
  OwnershipDumpScenario scenario;
  g_ownershipDumpScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene *scene = createFixtureScene(platform);
    const std::string first =
        loka::dsl::testing::OwnershipDump::dump(*scene);
    const std::string second =
        loka::dsl::testing::OwnershipDump::dump(*scene);
    verifyOwnershipDump(first, second);
    delete scene;
  }
  assert(scenario.releaseCount == 1);
  g_ownershipDumpScenario = 0;
}

void testOwnershipDumpWalksParkedBranches()
{
  OwnershipDumpScenario scenario(false);
  scenario.useConditional = true;

  g_ownershipDumpScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene *scene = createFixtureScene(platform);

    // Flip the condition: the section-bearing branch parks. Parking is a
    // retained detach, and detach removes slots (axiom 14) -- the branch
    // keeps its states warm for re-entry, but its hold drops, the last drop
    // queues the releaser, and cross-tick retention re-acquires through the
    // owner. A dump that only walked childrenHead() would render the parked
    // states as unowned, which is the exact lie this tool exists to remove.
    // The condition is a registered branch-seat source, so set() itself
    // drives the seat apply that parks the branch; the park-time drop queues
    // the releaser on the boundary pool inside that same run.
    {
      loka::core::StateTrackerGuard guard(loka::dsl::testing::SceneTestAccess::rootBoundary(*scene)->tracker());
      scenario.parkedCondition.set(false);
    }
    assert(scenario.releaseCount == 0 &&
           "the drop queues the releaser; nothing may fire at the park site");

    const std::string parkedExpected(
        "scene\n"
        "  boundary \"MainView\"\n"
        "    pending-releases: 1\n"
        "    observed: 2\n"
        "    parked\n"
        "      section(4101)\n"
        "        states: 2 (arena 1, heap 1)\n");
    verifyOwnershipDump(
        loka::dsl::testing::OwnershipDump::dump(*scene), parkedExpected);

    LOKA_VERIFY(!scene->flushInvalidation());
    assert(scenario.releaseCount == 1 &&
           "the queued releaser runs at the drain, on the owner's clock");

    // Re-entry (set(true)) is deliberately not exercised here: flipping back
    // currently loses the seat's branch entirely -- the parked section is
    // taken, reconciliation fails, and the replacement never materializes --
    // independent of the park-time drop. That is a branch-seat defect in the
    // frozen scene-update area, filed separately with this fixture named as
    // the reproducer.
    delete scene;
  }
  assert(scenario.releaseCount == 1 &&
         "the parked payload released exactly once, at the drain");
  g_ownershipDumpScenario = 0;
}

void testOwnershipDumpAdoptsParkedNestedBoundaryReleases()
{
  OwnershipDumpScenario scenario(false);
  scenario.useConditional = true;
  scenario.nestedBoundaryBranch = true;
  g_ownershipDumpScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene *scene = createFixtureScene(platform);
    assert(scenario.held.isValid());

    // The last drop happens inside the branch being parked, so the releaser
    // first lands on the nested Boundary's pool -- a clock the live-tree
    // drain walk will never reach again. The parking boundary must adopt it.
    {
      loka::core::StateTrackerGuard guard(loka::dsl::testing::SceneTestAccess::rootBoundary(*scene)->tracker());
      scenario.parkedCondition.set(false);
    }
    assert(scenario.releaseCount == 0 &&
           "adoption re-queues; nothing may fire at the park site");

    const std::string parkedExpected(
        "scene\n"
        "  boundary \"MainView\"\n"
        "    pending-releases: 1\n"
        "    observed: 2\n"
        "    parked\n"
        "      boundary\n"
        "        section(4111)\n"
        "          states: 2 (arena 1, heap 1)\n");
    verifyOwnershipDump(
        loka::dsl::testing::OwnershipDump::dump(*scene), parkedExpected);

    LOKA_VERIFY(!scene->flushInvalidation());
    assert(scenario.releaseCount == 1 &&
           "the adopted releaser runs at the live boundary's drain");
    delete scene;
  }
  assert(scenario.releaseCount == 1);
  g_ownershipDumpScenario = 0;
}

void testOwnershipDumpRemovedOuterSeatRetiresNestedParkedBranch()
{
  OwnershipDumpScenario scenario;
  scenario.parkedSeatFixturePhase = 0;
  scenario.createHeld = false;
  scenario.createStates = false;
  g_ownershipDumpScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene *scene = createFixtureScene(platform);

    {
      loka::core::StateTrackerGuard guard(loka::dsl::testing::SceneTestAccess::rootBoundary(*scene)->tracker());
      scenario.parkedSeatInnerCondition.set(false);
    }
    const std::string parked =
        loka::dsl::testing::OwnershipDump::dump(*scene);
    // Observations stay outside assert(): under NDEBUG the whole expression
    // would vanish and the fixture would stop discriminating.
    const bool innerParked = parked.find("parked\n") != std::string::npos;
    LOKA_VERIFY(innerParked && "the inner seat must park its outgoing branch before removal");

    {
      loka::core::StateTrackerGuard guard(loka::dsl::testing::SceneTestAccess::rootBoundary(*scene)->tracker());
      scenario.showOriginalSeat.set(false);
    }
    flushFixtureSeats(*scene);
    const std::string removed =
        loka::dsl::testing::OwnershipDump::dump(*scene);
    const bool parkedSurvived = removed.find("parked\n") != std::string::npos;
    LOKA_VERIFY(!parkedSurvived && "removing the outer seat must retire its nested parked branch");
    delete scene;
  }
  g_ownershipDumpScenario = 0;
}

void testOwnershipDumpShiftedSlotDoesNotReuseNestedParkedBranch()
{
  OwnershipDumpScenario scenario;
  scenario.parkedSeatFixturePhase = 0;
  scenario.createHeld = false;
  scenario.createStates = false;
  g_ownershipDumpScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene *scene = createFixtureScene(platform);

    {
      loka::core::StateTrackerGuard guard(loka::dsl::testing::SceneTestAccess::rootBoundary(*scene)->tracker());
      scenario.parkedSeatInnerCondition.set(false);
    }
    {
      loka::core::StateTrackerGuard guard(loka::dsl::testing::SceneTestAccess::rootBoundary(*scene)->tracker());
      scenario.showOriginalSeat.set(false);
      scenario.showShiftedSeat.set(true);
    }
    flushFixtureSeats(*scene);

    {
      loka::core::StateTrackerGuard guard(loka::dsl::testing::SceneTestAccess::rootBoundary(*scene)->tracker());
      scenario.parkedSeatShiftedCondition.set(false);
    }
    ParkedSeatProbeNode *shiftedFalse = findLiveParkedSeatProbe(
        loka::dsl::testing::SceneTestAccess::rootNode(*scene),
        &scenario.shiftedFalseRecord);
    const bool shiftedOwnsItsBranch = shiftedFalse != 0 && shiftedFalse->birth() == 202;
    LOKA_VERIFY(shiftedOwnsItsBranch && "a shifted anonymous seat must materialize its own branch, not the orphan");
    delete scene;
  }
  g_ownershipDumpScenario = 0;
}

void testOwnershipDumpShowsPendingReleaseUntilDrain()
{
  OwnershipDumpScenario scenario(false);
  scenario.createStates = false;
  g_ownershipDumpScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene *scene = createFixtureScene(platform);
    {
      loka::core::StateTrackerGuard guard(loka::dsl::testing::SceneTestAccess::rootBoundary(*scene)->tracker());
      scenario.showCreator.set(false);
    }

    const std::string pendingExpected(
        "scene\n"
        "  boundary \"MainView\"\n"
        "    pending-releases: 1\n"
        "    observed: 1\n");
    verifyOwnershipDump(
        loka::dsl::testing::OwnershipDump::dump(*scene), pendingExpected);
    LOKA_VERIFY(!scene->flushInvalidation());
    const std::string drainedExpected(
        "scene\n"
        "  boundary \"MainView\"\n"
        "    observed: 1\n");
    verifyOwnershipDump(
        loka::dsl::testing::OwnershipDump::dump(*scene), drainedExpected);
    delete scene;
  }
  assert(scenario.releaseCount == 1);
  g_ownershipDumpScenario = 0;
}
