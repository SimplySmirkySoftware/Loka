#include "support/WindowAdmissionTestApp.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "ScenarioReelTests.hpp"

#include "support/TestVerify.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "../example/HelloWorld/src/MainNode.hpp"
#include "../example/MineSweeper/src/MainNode.hpp"
#include "app/core/Window.hpp"
#include "app/core/AppComposition.hpp"
#include "app/scene/Scene.hpp"
#include "core/LokaAlloc.hpp"
#include "core/util/OwnedDef.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/StringUTF8.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/null/NullApp.hpp"
#include "platform/null/NullWindow.hpp"
#include "scenarios/HelloWorldScenarios.hpp"
#include "scenarios/HelloWorldScenarioPresentation.hpp"
#include "scenarios/MineSweeperScenarios.hpp"
#include "scenarios/ObservedMainDefinition.hpp"
#include "scenarios/ScenarioReel.hpp"
#include "scenarios/ScenarioLoopAppConfig.hpp"
#include "scenarios/StartupScenarios.hpp"
#include "standalone/StandaloneScenarioSupport.hpp"
#include "testing/app/SceneManagerTestAccess.hpp"

namespace
{
  const char *const kProbeCells[] = {"startup", "first", "second"};

  struct ReelDriverLifetime
  {
    ReelDriverLifetime() : identity(0), stops(0), destructions(0) {}
    const void *identity;
    int stops;
    int destructions;
  };

  struct ReelDriverObservation
  {
    ReelDriverObservation() : constructions(0), stops(0), destructions(0), detaches(0) {}
    int constructions;
    int stops;
    int destructions;
    int detaches;
    std::string constructedCell;
    ReelDriverLifetime drivers[2];
  };

  ReelDriverObservation g_reelDriverObservation;

  class ReelProbeScenario
  {
  public:
    ReelProbeScenario(const std::string &cell, loka::scenario_tests::ScenarioCompletionPolicy,
                      loka::dsl::testing::ScenarioAuditSink *)
        : lifetime_(g_reelDriverObservation.drivers[g_reelDriverObservation.constructions])
    {
      this->lifetime_.identity = this;
      ++g_reelDriverObservation.constructions;
      g_reelDriverObservation.constructedCell = cell;
    }
    ~ReelProbeScenario()
    {
      ++this->lifetime_.destructions;
      ++g_reelDriverObservation.destructions;
    }
    void stop()
    {
      if (this->lifetime_.stops == 0)
      {
        ++this->lifetime_.stops;
        ++g_reelDriverObservation.stops;
      }
    }
    bool publishVerdict(const loka::dsl::SnapRecord &) { return true; }
    loka::scenario_tests::ScenarioAdvance step(
        long, loka::app::scene::Scene *, const loka::scenario_tests::CaptureContentBounds &,
        loka::dsl::SnapRecord &)
    {
      return loka::scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY;
    }

  private:
    ReelDriverLifetime &lifetime_;
  };

  void ObserveReelDriverAtDetach(void *userData)
  {
    loka::app::scene::Scene *scene = static_cast<loka::app::scene::Scene *>(userData);
    if (!scene->getAttachedState()->get())
    {
      ++g_reelDriverObservation.detaches;
      LOKA_VERIFY(g_reelDriverObservation.constructions == 2);
      LOKA_VERIFY(g_reelDriverObservation.stops == 1);
      LOKA_VERIFY(g_reelDriverObservation.destructions == 0);
    }
  }

  int g_scenarioRearmAllocationRefusals = 0;

  void *scenarioRearmRefusingBackendAlloc(
      std::size_t size, const loka::core::LokaAllocationSite &site)
  {
    (void)size;
    (void)site;
    ++g_scenarioRearmAllocationRefusals;
    return 0;
  }

  void scenarioRearmDelegatingBackendFree(
      void *ptr, const loka::core::LokaAllocationSite &site)
  {
    (void)site;
    delete[] static_cast<char *>(ptr);
  }

  // Same detach-triggered refusal mechanism as the A-1b REFUTE B1 probe:
  // replacement-driver allocation succeeds before fresh Scene composition fails.
  void RefuseScenarioCompositionAtDetach(void *userData)
  {
    loka::app::scene::Scene *scene = static_cast<loka::app::scene::Scene *>(userData);
    if (!scene->getAttachedState()->get())
      loka::core::LokaAllocSetBackend(
          &scenarioRearmRefusingBackendAlloc, &scenarioRearmDelegatingBackendFree);
  }

  void RequestDetachOnRearmAttachment(void *userData)
  {
    loka::app::scene::Scene *scene = static_cast<loka::app::scene::Scene *>(userData);
    if (scene->getAttachedState()->get())
      scene->getWindow()->sceneManager()->requestDetach();
  }

  class ScenarioLoopAdmissionTestApp : public NullApp
  {
  public:
    explicit ScenarioLoopAdmissionTestApp(Window &window) : NullApp(0)
    {
      this->group_ = new AppComponentGroup(std::vector<AppComponent *>(1, &window));
    }
    virtual ~ScenarioLoopAdmissionTestApp() { this->group_->build(); }
    void flush() { this->flushWindowInvalidations(); }
  };

  class NativeTitleObserverProbeWindow : public Window
  {
  public:
    NativeTitleObserverProbeWindow(PlatformContext *context,
                                   const WindowProps &props,
                                   int *notifications)
        : Window(context, props),
          notifications_(notifications)
    {
      this->observeNativeState(
          this->displayTitleState(), &NativeTitleObserverProbeWindow::TitleChanged, this);
    }

  private:
    static void TitleChanged(void *userData)
    {
      NativeTitleObserverProbeWindow *self =
          static_cast<NativeTitleObserverProbeWindow *>(userData);
      if (self && self->notifications_)
      {
        ++*self->notifications_;
      }
    }

    int *notifications_;
  };

  struct StandaloneScenarioRailObservation
  {
    StandaloneScenarioRailObservation()
        : stops(0),
          destructions(0)
    {
    }

    int stops;
    int destructions;
  };

  class StandaloneScenarioRailProbe
  {
  public:
    explicit StandaloneScenarioRailProbe(StandaloneScenarioRailObservation *observation)
        : observation_(observation),
          stopped_(false)
    {
    }

    ~StandaloneScenarioRailProbe()
    {
      ++this->observation_->destructions;
    }

    void stop()
    {
      if (!this->stopped_)
      {
        this->stopped_ = true;
        ++this->observation_->stops;
      }
    }

  private:
    StandaloneScenarioRailObservation *observation_;
    bool stopped_;
  };

  struct AttachmentTransactionObservation
  {
    AttachmentTransactionObservation()
        : manager(0),
          window(0),
          calls(0),
          allCallsInsideSeatApply(true)
    {
    }

    SceneManager *manager;
    Window *window;
    int calls;
    bool allCallsInsideSeatApply;
  };

  void ObserveAttachmentTransaction(void *userData)
  {
    AttachmentTransactionObservation *observation =
        static_cast<AttachmentTransactionObservation *>(userData);
    ++observation->calls;
    observation->allCallsInsideSeatApply =
        observation->allCallsInsideSeatApply &&
        loka::app::testing::SceneManagerTestAccess::applying(*observation->manager) ==
            observation->window->scene();
  }

  loka::scenario_tests::ScenarioCellTable ProbeCells()
  {
    return loka::scenario_tests::ScenarioCellTable(kProbeCells, 3);
  }

  std::string SourcePath(const char *relative)
  {
    const std::string sourceFile(__FILE__);
    const std::string unixMarker = "/tests/ScenarioReelTests.cpp";
    const std::string windowsMarker = "\\tests\\ScenarioReelTests.cpp";
    std::string::size_type marker = sourceFile.rfind(unixMarker);
    if (marker == std::string::npos)
    {
      marker = sourceFile.rfind(windowsMarker);
    }
    assert(marker != std::string::npos); // loka-assert-ok: fixture path derivation
    return sourceFile.substr(0, marker) + "/" + (relative ? relative : "");
  }

  /** Shared reel cells registered for one example, in file order. */
  std::vector<std::string> RegistryCells(const char *example)
  {
    std::vector<std::string> cells;
    const std::string path = SourcePath("tests/scenarios/scenarios.txt");
    std::FILE *input = std::fopen(path.c_str(), "rb");
    LOKA_VERIFY(input != 0);
    if (!input)
    {
      return cells;
    }
    char line[256];
    while (std::fgets(line, sizeof(line), input))
    {
      std::string entry(line);
      const std::string::size_type end = entry.find_last_not_of("\r\n");
      entry = end == std::string::npos ? std::string() : entry.substr(0, end + 1);
      const std::string::size_type space = entry.find(' ');
      if (space == std::string::npos)
      {
        continue;
      }
      // Real ADB input belongs to the Toolbox driver, never the shared reel.
      if (entry == "helloworld bmi-tab")
        continue;
      if (entry.substr(0, space) == example)
      {
        cells.push_back(entry.substr(space + 1));
      }
    }
    LOKA_VERIFY(std::fclose(input) == 0);
    return cells;
  }

  void VerifyTableMatchesRegistry(const char *example, const loka::scenario_tests::ScenarioCellTable &table)
  {
    const std::vector<std::string> registered = RegistryCells(example);
    LOKA_VERIFY(!registered.empty());
    LOKA_VERIFY(table.size() == registered.size());
    // The reel opens with the shared startup cell so every pass begins on the
    // example's settled initial screen.
    LOKA_VERIFY(table.at(0) != 0);
    LOKA_VERIFY(loka::scenario_tests::IsStartupScenario(table.at(0)));
    for (std::size_t i = 0; i < registered.size(); ++i)
    {
      LOKA_VERIFY(table.contains(registered[i]));
    }
  }

  loka::app::scene::NodeDefinitionBase *CloneMineSweeperRoot()
  {
    const minesweeper::MainProps props(loka::scenario_tests::MineSweeperScenarioSeed());
    loka::scenario_tests::ObservedMainDefinition<minesweeper::MainProps, minesweeper::MainNode> definition(props, 0);
    return definition.clone();
  }

  loka::app::scene::NodeDefinitionBase *CloneHelloWorldRoot()
  {
    const helloworld::MainProps props;
    loka::scenario_tests::ObservedMainDefinition<helloworld::MainProps, helloworld::MainNode> definition(props, 0);
    return definition.clone();
  }

  /** Runs one MineSweeper new-game-twice rail against a mounted window and
      returns the board it settled on. */
  std::string RunMineSweeperBoardCell(Window &window)
  {
    loka::scenario_tests::MineSweeperScenario scenario(loka::scenario_tests::SCENARIO_COMPLETION_DRIVER_OWNED, 0);
    loka::scenario_tests::CaptureContentBounds bounds;
    loka::dsl::SnapRecord record;
    loka::scenario_tests::ScenarioAdvance advance = loka::scenario_tests::SCENARIO_ADVANCE_PENDING;
    for (long tick = 1; tick <= 64 && advance == loka::scenario_tests::SCENARIO_ADVANCE_PENDING; ++tick)
    {
      LOKA_VERIFY(window.scene() != 0);
      advance = scenario.step(tick, window.scene(), bounds, record);
    }
    LOKA_VERIFY(advance == loka::scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY);
    std::string board;
    LOKA_VERIFY(record.get("board.mines", board));
    return board;
  }

  void CollectMineIndices(loka::app::scene::Node *node, int &cellIndex, std::string &out)
  {
    if (!node)
    {
      return;
    }
    if (node->propsTypeId() == minesweeper::MineCellProps::staticTypeId())
    {
      minesweeper::MineCellNode *cell = static_cast<minesweeper::MineCellNode *>(node);
      if (cell->props.isMine)
      {
        if (!out.empty())
        {
          out += ',';
        }
        char indexText[16];
        // snprintf, not sprintf: the macOS toolchain deprecates sprintf and
        // the warning floor is -Werror. Matches MineSweeperScenarios.cpp.
        ::snprintf(indexText, sizeof(indexText), "%d", cellIndex);
        out += indexText;
      }
      ++cellIndex;
    }
    loka::app::scene::INestable *nestable = node->asNestable();
    for (loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0;
         child;
         child = child->nextInComposition)
    {
      CollectMineIndices(child, cellIndex, out);
    }
  }

  /** The board a MineSweeper scene is currently showing, read the way the rail
      reads it: off the composed MineCell props, not off any reel bookkeeping. */
  std::string SceneBoard(loka::app::scene::Scene *scene)
  {
    std::string mines;
    if (!scene)
    {
      return mines;
    }
    loka::app::scene::Node *boardNode = 0;
    loka::dsl::FlowError error;
    if (loka::dsl::testing::LookupNodeById<loka::app::scene::Node>(scene, "MineSweeper.Board", boardNode, error)
        != loka::dsl::FLOW_STEP_SUCCEEDED)
    {
      return mines;
    }
    int cellIndex = 0;
    CollectMineIndices(boardNode, cellIndex, mines);
    return mines;
  }

  /** What one reel tick is worth recording. Two flavours because one example
      can be watched by name alone and the other has a board worth comparing. */
  typedef std::string (*ReelFrameFn)(const char *cell, Window &window);

  std::string CellFrame(const char *cell, Window &window)
  {
    (void)window;
    return std::string(cell ? cell : "");
  }

  std::string CellAndBoardFrame(const char *cell, Window &window)
  {
    return std::string(cell ? cell : "") + "|" + SceneBoard(window.scene());
  }

  /** Pumps a reel until it retires its bounded cycle budget, recording every
      distinct frame it passes through so one pass can be compared to the next.
      Sampling happens before the tick, so the reel's retirement never lands an
      odd trailing frame in the sequence. */
  template <class InteractionScenario>
  std::vector<std::string> PumpReel(loka::scenario_tests::ScenarioReel<InteractionScenario> &reel,
                                    Window &window,
                                    long maxTicks,
                                    ReelFrameFn frame,
                                    std::vector<std::string> *operatorTitles = 0)
  {
    WindowAdmissionTestApp admission(window);
    std::vector<std::string> frames;
    for (long tick = 0; tick < maxTicks && !reel.finished(); ++tick)
    {
      const std::string current = frame(reel.cell(), window);
      if (frames.empty() || frames[frames.size() - 1] != current)
      {
        frames.push_back(current);
      }
      if (operatorTitles)
      {
        std::string operatorTitle;
        LOKA_VERIFY(loka::platform::CollectUtf8(reel.operatorTitle(), operatorTitle));
        if (operatorTitles->empty() || (*operatorTitles)[operatorTitles->size() - 1] != operatorTitle)
        {
          operatorTitles->push_back(operatorTitle);
        }
      }
      reel.tick(&window, &admission, 0.1, window.getTracker());
    }
    return frames;
  }
} // namespace

void testScenarioReelPositionWrapsAndCountsCycles()
{
  loka::scenario_tests::ScenarioReelPosition position(ProbeCells(), 2);

  LOKA_VERIFY(!position.exhausted());
  LOKA_VERIFY(std::string(position.cell()) == "startup");
  LOKA_VERIFY(position.completedCycles() == 0);

  position.advance();
  LOKA_VERIFY(std::string(position.cell()) == "first");
  position.advance();
  LOKA_VERIFY(std::string(position.cell()) == "second");
  LOKA_VERIFY(position.completedCycles() == 0);

  // The wrap is what counts a cycle, so the reel is endless by construction
  // rather than by a separate "keep going" flag.
  position.advance();
  LOKA_VERIFY(std::string(position.cell()) == "startup");
  LOKA_VERIFY(position.completedCycles() == 1);
  LOKA_VERIFY(!position.exhausted());

  position.advance();
  position.advance();
  position.advance();
  LOKA_VERIFY(position.completedCycles() == 2);
  LOKA_VERIFY(position.exhausted());
  LOKA_VERIFY(position.cell() == 0);

  // A budget of 0 is the shipping case: it never retires.
  loka::scenario_tests::ScenarioReelPosition endless(ProbeCells(), 0);
  for (int i = 0; i < 64; ++i)
  {
    endless.advance();
  }
  LOKA_VERIFY(!endless.exhausted());
  LOKA_VERIFY(endless.completedCycles() > 0);

  // An empty table never claims to have a cell to run.
  loka::scenario_tests::ScenarioReelPosition empty(loka::scenario_tests::ScenarioCellTable(0, 0), 0);
  LOKA_VERIFY(empty.exhausted());
  LOKA_VERIFY(empty.cell() == 0);

  std::printf("testScenarioReelPositionWrapsAndCountsCycles passed\n");
}

void testScenarioReelCellTablesMatchSharedRegistry()
{
  VerifyTableMatchesRegistry("minesweeper", loka::scenario_tests::MineSweeperReelCells());
  VerifyTableMatchesRegistry("helloworld", loka::scenario_tests::HelloWorldReelCells());

  // The registration predicate answers from the same table, so a cell cannot
  // be registered for shared execution without joining the reel.
  LOKA_VERIFY(loka::scenario_tests::IsMineSweeperScenario("new-game-twice"));
  LOKA_VERIFY(loka::scenario_tests::IsMineSweeperScenario("seeded-reveal"));
  LOKA_VERIFY(!loka::scenario_tests::IsMineSweeperScenario("startup"));
  LOKA_VERIFY(loka::scenario_tests::IsHelloWorldScenario("toggle-action-probe"));
  LOKA_VERIFY(loka::scenario_tests::IsHelloWorldScenario("bmi-roundtrip"));
  LOKA_VERIFY(!loka::scenario_tests::IsHelloWorldScenario("startup"));
  LOKA_VERIFY(!loka::scenario_tests::IsHelloWorldScenario("bmi-tab"));
  LOKA_VERIFY(!loka::scenario_tests::HelloWorldReelCells().contains("bmi-tab"));

  std::printf("testScenarioReelCellTablesMatchSharedRegistry passed\n");
}

void testScenarioSceneRearmRebuildsExampleStateFromDefinition()
{
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(CloneMineSweeperRoot());
  LOKA_VERIFY(root.get() != 0);
  NullPlatformContext context;
  WindowProps windowProps;
  windowProps.scene(new loka::app::scene::Scene(root.take()));
  NullWindow window(&context, windowProps);
  WindowAdmissionTestApp admission(window);
  LOKA_VERIFY(window.scene() != 0);
  loka::app::scene::Scene *const installedScene = window.scene();
  loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
  const std::size_t mountedNodeCount = window.scene()->liveNodeCount();
  LOKA_VERIFY(mountedNodeCount > 0);

  // Two New Games leave the board somewhere the seed's initial board is not,
  // so a re-arm that only rewound bookkeeping would fail the next rail's
  // verify-initial-board step instead of reproducing this board.
  const std::string firstBoard = RunMineSweeperBoardCell(window);

  AttachmentTransactionObservation attachmentObservation;
  attachmentObservation.manager = window.sceneManager();
  attachmentObservation.window = &window;
  window.scene()->getAttachedState()->bind(
      &ObserveAttachmentTransaction, &attachmentObservation, false);
  LOKA_VERIFY(loka::scenario_tests::RearmScenarioScene(&window, &admission));
  window.scene()->getAttachedState()->unbind(
      &ObserveAttachmentTransaction, &attachmentObservation);
  LOKA_VERIFY(attachmentObservation.calls == 2);
  LOKA_VERIFY(attachmentObservation.allCallsInsideSeatApply);
  LOKA_VERIFY(window.scene() != 0);
  LOKA_VERIFY(window.scene() == installedScene);
  LOKA_VERIFY(window.scene()->liveNodeCount() == mountedNodeCount);

  const std::string secondBoard = RunMineSweeperBoardCell(window);
  LOKA_VERIFY(secondBoard == firstBoard);

  // A window without a mounted scene declines instead of substituting one.
  LOKA_VERIFY(!loka::scenario_tests::RearmScenarioScene(0, &admission));

  std::printf("testScenarioSceneRearmRebuildsExampleStateFromDefinition passed\n");
}

void testScenarioReelRunsEveryMineSweeperCellEveryCycle()
{
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(CloneMineSweeperRoot());
  LOKA_VERIFY(root.get() != 0);
  NullPlatformContext context;
  WindowProps windowProps;
  windowProps.scene(new loka::app::scene::Scene(root.take()));
  NullWindow window(&context, windowProps);
  LOKA_VERIFY(window.scene() != 0);
  loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
  const std::size_t mountedNodeCount = window.scene()->liveNodeCount();

  loka::scenario_tests::ScenarioReel<loka::scenario_tests::MineSweeperScenario> reel(
      loka::scenario_tests::MineSweeperReelCells(),
      loka::scenario_tests::STARTUP_EXAMPLE_MINESWEEPER,
      &loka::scenario_tests::MakeMineSweeperDriverErrorRecord,
      2602,
      0.05,
      2);

  const std::vector<std::string> frames = PumpReel(reel, window, 4096, &CellAndBoardFrame);

  LOKA_VERIFY(reel.finished());
  LOKA_VERIFY(reel.completedCycles() == 2);
  // The reel must not merely advance: a device left running has to replay the
  // same reel, so the boards the second pass puts on screen have to be the
  // boards the first pass put there. A re-arm that skipped the real teardown
  // would keep whichever board the previous cell finished on and diverge here.
  LOKA_VERIFY(!frames.empty());
  LOKA_VERIFY(frames.size() % 2 == 0);
  const std::size_t half = frames.size() / 2;
  for (std::size_t i = 0; i < half; ++i)
  {
    LOKA_VERIFY(frames[i] == frames[half + i]);
  }
  // The first pass really did visit all three cells in registry order.
  LOKA_VERIFY(frames[0].find("startup|") == 0);
  bool sawNewGameTwice = false;
  bool sawSeededReveal = false;
  for (std::size_t i = 0; i < half; ++i)
  {
    sawNewGameTwice = sawNewGameTwice || frames[i].find("new-game-twice|") == 0;
    sawSeededReveal = sawSeededReveal || frames[i].find("seeded-reveal|") == 0;
  }
  LOKA_VERIFY(sawNewGameTwice);
  LOKA_VERIFY(sawSeededReveal);
  // Five re-arms happened across those two passes and the tree is the size it
  // was at mount, so the reel is rebuilding rather than accumulating.
  LOKA_VERIFY(window.scene()->liveNodeCount() == mountedNodeCount);

  std::printf("testScenarioReelRunsEveryMineSweeperCellEveryCycle passed\n");
}

void testScenarioReelRunsEveryHelloWorldCellEveryCycle()
{
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(CloneHelloWorldRoot());
  LOKA_VERIFY(root.get() != 0);
  NullPlatformContext context;
  WindowProps windowProps;
  windowProps.scene(new loka::app::scene::Scene(root.take()));
  NullWindow window(&context, windowProps);
  LOKA_VERIFY(window.scene() != 0);
  loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
  const std::size_t mountedNodeCount = window.scene()->liveNodeCount();

  loka::scenario_tests::ScenarioReel<loka::scenario_tests::HelloWorldScenario> reel(
      loka::scenario_tests::HelloWorldReelCells(),
      loka::scenario_tests::STARTUP_EXAMPLE_HELLO_WORLD,
      &loka::scenario_tests::MakeHelloWorldDriverErrorRecord,
      2402,
      0.05,
      2);

  reel.decorateOperatorTitle(loka::core::String::Literal("LokaSample"));
  std::vector<std::string> operatorTitles;
  const std::vector<std::string> visited = PumpReel(reel, window, 4096, &CellFrame, &operatorTitles);

  LOKA_VERIFY(reel.finished());
  LOKA_VERIFY(reel.completedCycles() == 2);
  LOKA_VERIFY(visited.size() == 6);
  LOKA_VERIFY(visited[0] == "startup");
  LOKA_VERIFY(visited[1] == "toggle-action-probe");
  LOKA_VERIFY(visited[2] == "bmi-roundtrip");
  LOKA_VERIFY(visited[3] == "startup");
  LOKA_VERIFY(visited[4] == "toggle-action-probe");
  LOKA_VERIFY(visited[5] == "bmi-roundtrip");
  LOKA_VERIFY(operatorTitles.size() == 6);
  LOKA_VERIFY(operatorTitles[0] == "LokaSample - startup (cycle 1)");
  LOKA_VERIFY(operatorTitles[1] == "LokaSample - toggle-action-probe (cycle 1)");
  LOKA_VERIFY(operatorTitles[2] == "LokaSample - bmi-roundtrip (cycle 1)");
  LOKA_VERIFY(operatorTitles[3] == "LokaSample - startup (cycle 2)");
  LOKA_VERIFY(operatorTitles[4] == "LokaSample - toggle-action-probe (cycle 2)");
  LOKA_VERIFY(operatorTitles[5] == "LokaSample - bmi-roundtrip (cycle 2)");
  LOKA_VERIFY(window.scene()->liveNodeCount() == mountedNodeCount);

  std::printf("testScenarioReelRunsEveryHelloWorldCellEveryCycle passed\n");
}

void testScenarioReelCompositionRefusalIsTerminal()
{
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(CloneHelloWorldRoot());
  LOKA_VERIFY(root.get() != 0);
  NullPlatformContext context;
  WindowProps props;
  props.scene(new loka::app::scene::Scene(root.take()));
  NullWindow window(&context, props);
  WindowAdmissionTestApp app(window);
  // Drain initial viewport publication before arming the rearm-only refusal.
  app.flush();
  loka::app::scene::Scene *scene = window.scene();
  const std::size_t mountedNodeCount = scene->liveNodeCount();
  loka::scenario_tests::ScenarioReel<loka::scenario_tests::HelloWorldScenario> reel(
      loka::scenario_tests::HelloWorldReelCells(),
      loka::scenario_tests::STARTUP_EXAMPLE_HELLO_WORLD,
      &loka::scenario_tests::MakeHelloWorldDriverErrorRecord, 2402, 0.0, 2);

  g_scenarioRearmAllocationRefusals = 0;
  scene->getAttachedState()->bind(&RefuseScenarioCompositionAtDetach, scene, false);
  loka::scenario_tests::ScenarioReelResult result = loka::scenario_tests::SCENARIO_REEL_RUNNING;
  for (int tick = 0; tick < 512 && !g_scenarioRearmAllocationRefusals; ++tick)
    result = reel.tick(&window, &app, 0.1, window.getTracker());
  loka::core::LokaAllocSetBackend(0, 0);
  scene->getAttachedState()->unbind(&RefuseScenarioCompositionAtDetach, scene);

  const bool composedAfterRefusal = loka::dsl::testing::SceneTestAccess::composed(*scene);
  std::printf("rearm refusal: refusals=%d pending=%d composed=%d finished=%d cycles=%ld\n",
              g_scenarioRearmAllocationRefusals, window.sceneManager()->hasPendingWork(),
              composedAfterRefusal, reel.finished(), reel.completedCycles());
  LOKA_VERIFY(g_scenarioRearmAllocationRefusals == 1);
  LOKA_VERIFY(!composedAfterRefusal);
  LOKA_VERIFY(result == loka::scenario_tests::SCENARIO_REEL_FAILED);
  LOKA_VERIFY(window.sceneManager()->hasPendingWork());
  LOKA_VERIFY(reel.finished());
  LOKA_VERIFY(std::string(reel.cell()) == "startup");
  LOKA_VERIFY(reel.operatorTitle().equals(loka::core::String::Literal("startup (cycle 1)")));
  LOKA_VERIFY(reel.completedCycles() == 0);

  // No second request and no explicit white-flag invalidation: admission alone
  // retries the refused renewal on the same installed Scene, but cannot
  // restart the failed reel.
  app.flush();
  LOKA_VERIFY(window.scene() == scene);
  const bool composedAfterRetry = loka::dsl::testing::SceneTestAccess::composed(*scene);
  const std::size_t recoveredNodeCount = scene->liveNodeCount();
  LOKA_VERIFY(composedAfterRetry);
  LOKA_VERIFY(!window.sceneManager()->hasPendingWork());
  LOKA_VERIFY(recoveredNodeCount == mountedNodeCount);
  for (int tick = 0; tick < 3; ++tick)
    LOKA_VERIFY(reel.tick(&window, &app, 0.1, window.getTracker()) ==
                loka::scenario_tests::SCENARIO_REEL_FAILED);
  LOKA_VERIFY(reel.finished());
  LOKA_VERIFY(reel.completedCycles() == 0);
  std::printf("rearm recovery: pending=%d composed=%d finished=%d cycles=%ld\n",
              window.sceneManager()->hasPendingWork(),
              loka::dsl::testing::SceneTestAccess::composed(*scene),
              reel.finished(), reel.completedCycles());
  std::printf("testScenarioReelCompositionRefusalIsTerminal passed\n");
}

void testSceneRearmRefusalPreservesNewerDetachRequest()
{
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(CloneHelloWorldRoot());
  LOKA_VERIFY(root.get() != 0);
  NullPlatformContext context;
  WindowProps props;
  props.scene(new loka::app::scene::Scene(root.take()));
  NullWindow window(&context, props);
  WindowAdmissionTestApp app(window);
  // Drain initial viewport publication before arming the rearm-only refusal.
  app.flush();
  loka::app::scene::Scene *scene = window.scene();
  g_scenarioRearmAllocationRefusals = 0;
  scene->getAttachedState()->bind(&RefuseScenarioCompositionAtDetach, scene, false);
  scene->getAttachedState()->bind(&RequestDetachOnRearmAttachment, scene, false);
  window.sceneManager()->requestRearm();
  app.flush();
  loka::core::LokaAllocSetBackend(0, 0);
  scene->getAttachedState()->unbind(&RefuseScenarioCompositionAtDetach, scene);
  scene->getAttachedState()->unbind(&RequestDetachOnRearmAttachment, scene);
  LOKA_VERIFY(g_scenarioRearmAllocationRefusals == 1);
  LOKA_VERIFY(window.sceneManager()->hasPendingWork());
  app.flush();
  LOKA_VERIFY(!scene->getAttachedState()->get());
  LOKA_VERIFY(!window.sceneManager()->hasPendingWork());
  const bool composedAfterDetach = loka::dsl::testing::SceneTestAccess::composed(*scene);
  LOKA_VERIFY(!composedAfterDetach);
  std::printf("testSceneRearmRefusalPreservesNewerDetachRequest passed\n");
}

void testScenarioLoopCompositionRefusalQuits()
{
  NullPlatformContext context;
  typedef loka::scenario_tests::ScenarioLoopAppConfig<
      loka::scenario_tests::HelloWorldScenarioPresentation,
      loka::scenario_tests::HelloWorldScenario> HelloWorldLoopConfig;
  HelloWorldLoopConfig config(&context, HelloWorldMenuSeed::FromWallClock(0x13579BDFUL),
      loka::scenario_tests::HelloWorldReelCells(),
      loka::scenario_tests::STARTUP_EXAMPLE_HELLO_WORLD,
      &loka::scenario_tests::MakeHelloWorldDriverErrorRecord, 2402, 0.0, 2);
  AppComposition composition(&context);
  config.compose(composition);
  std::vector<AppComponent *> components = composition.build();
  LOKA_VERIFY(components.size() == 1);
  Window *window = components[0]->asWindow();
  LOKA_VERIFY(window != 0);
  {
    ScenarioLoopAdmissionTestApp app(*window);
    // Drain initial viewport publication before arming the rearm-only refusal.
    app.flush();
    config.setApp(&app);
    loka::app::scene::Scene *scene = window->scene();
    g_scenarioRearmAllocationRefusals = 0;
    scene->getAttachedState()->bind(&RefuseScenarioCompositionAtDetach, scene, false);
    for (int tick = 0; tick < 512 && !g_scenarioRearmAllocationRefusals; ++tick)
      LOKA_VERIFY(window->handleIdle(0.1));
    loka::core::LokaAllocSetBackend(0, 0);
    scene->getAttachedState()->unbind(&RefuseScenarioCompositionAtDetach, scene);
    LOKA_VERIFY(g_scenarioRearmAllocationRefusals == 1);
    std::printf("loop refusal: quit=%d pending=%d\n",
                app.quitRequested(), window->sceneManager()->hasPendingWork());
    LOKA_VERIFY(app.quitRequested());
    LOKA_VERIFY(window->sceneManager()->hasPendingWork());
    app.flush();
    const bool composedAfterRetry = loka::dsl::testing::SceneTestAccess::composed(*scene);
    LOKA_VERIFY(composedAfterRetry);
    LOKA_VERIFY(!window->sceneManager()->hasPendingWork());
    LOKA_VERIFY(app.quitRequested());
    for (int tick = 0; tick < 3; ++tick)
      LOKA_VERIFY(window->handleIdle(0.1));
    LOKA_VERIFY(app.quitRequested());
    LOKA_VERIFY(window->displayTitleState().get().equals(
        loka::core::String::Literal("LokaSample - startup (cycle 1)")));
    config.setApp(0);
  }
  delete window;
  std::printf("testScenarioLoopCompositionRefusalQuits passed\n");
}

void testScenarioWindowDisplayTitlePreservesLogicalTitleAndDropsNativeObserver()
{
  NullPlatformContext context;

  typedef loka::scenario_tests::ScenarioLoopAppConfig<
      loka::scenario_tests::HelloWorldScenarioPresentation,
      loka::scenario_tests::HelloWorldScenario>
      HelloWorldLoopConfig;
  HelloWorldLoopConfig config(&context,
                              HelloWorldMenuSeed::FromWallClock(0x13579BDFUL),
                              loka::scenario_tests::HelloWorldReelCells(),
                              loka::scenario_tests::STARTUP_EXAMPLE_HELLO_WORLD,
                              &loka::scenario_tests::MakeHelloWorldDriverErrorRecord,
                              2402,
                              10.0,
                              2);
  AppComposition composition(&context);
  config.compose(composition);
  std::vector<AppComponent *> components = composition.build();
  LOKA_VERIFY(components.size() == 1);
  Window *loopWindow = components.empty() ? 0 : components[0]->asWindow();
  LOKA_VERIFY(loopWindow != 0);
  if (loopWindow)
  {
    LOKA_VERIFY(loopWindow->titleState().get().equals(loka::core::String::Literal("LokaSample")));
    LOKA_VERIFY(loopWindow->displayTitleState().get().equals(
        loka::core::String::Literal("LokaSample - startup (cycle 1)")));
    {
      loka::core::StateTrackerGuard guard(loopWindow->getTracker());
      loopWindow->titleState().set(loka::core::String::Literal("LokaSample*"));
    }
    LOKA_VERIFY(loopWindow->titleState().get().equals(loka::core::String::Literal("LokaSample*")));
    LOKA_VERIFY(loopWindow->handleIdle(0.01));
    LOKA_VERIFY(loopWindow->titleState().get().equals(loka::core::String::Literal("LokaSample*")));
    LOKA_VERIFY(loopWindow->displayTitleState().get().equals(
        loka::core::String::Literal("LokaSample* - startup (cycle 1)")));
  }
  for (std::size_t i = 0; i < components.size(); ++i)
  {
    delete components[i];
  }

  loka::scenario_tests::ScenarioReelTitle title("startup", 0);
  title.decorateBeforeProjection(loka::core::String::Literal("LokaSample"));

  WindowProps props;
  props.title("LokaSample").displayTitleState(title.state());
  int nativeTitleNotifications = 0;
  {
    NativeTitleObserverProbeWindow window(&context, props, &nativeTitleNotifications);
    LOKA_VERIFY(window.titleState().get().equals(loka::core::String::Literal("LokaSample")));
    LOKA_VERIFY(window.displayTitleState().get().equals(
        loka::core::String::Literal("LokaSample - startup (cycle 1)")));

    {
      loka::core::StateTrackerGuard guard(window.getTracker());
      window.titleState().set(loka::core::String::Literal("LokaSample*"));
    }
    LOKA_VERIFY(window.titleState().get().equals(loka::core::String::Literal("LokaSample*")));
    LOKA_VERIFY(nativeTitleNotifications == 0);

    title.synchronizeProductionTitle(window.titleState().get(), window.getTracker());
    LOKA_VERIFY(window.titleState().get().equals(loka::core::String::Literal("LokaSample*")));
    LOKA_VERIFY(window.displayTitleState().get().equals(
        loka::core::String::Literal("LokaSample* - startup (cycle 1)")));
    LOKA_VERIFY(nativeTitleNotifications == 1);
  }

  // The reel-owned display State outlives the platform Window. Publishing the
  // next cell must not call the observer whose userData was the retired Window.
  title.publish("second", 0, 0);
  LOKA_VERIFY(nativeTitleNotifications == 1);

  std::printf("testScenarioWindowDisplayTitlePreservesLogicalTitleAndDropsNativeObserver passed\n");
}

void testScenarioReelDriverAllocationRefusalRetiresInsteadOfWedging()
{
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(CloneHelloWorldRoot());
  LOKA_VERIFY(root.get() != 0);
  NullPlatformContext context;
  WindowProps windowProps;
  windowProps.scene(new loka::app::scene::Scene(root.take()));
  NullWindow window(&context, windowProps);
  WindowAdmissionTestApp admission(window);
  LOKA_VERIFY(window.scene() != 0);
  loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);

  loka::scenario_tests::ScenarioReel<loka::scenario_tests::HelloWorldScenario> reel(
      loka::scenario_tests::HelloWorldReelCells(),
      loka::scenario_tests::STARTUP_EXAMPLE_HELLO_WORLD,
      &loka::scenario_tests::MakeHelloWorldDriverErrorRecord,
      2402,
      0.0,
      0);

  // The first driver already exists. Refuse its replacement at the first
  // re-arm; an endless reel can finish only by surfacing this failure.
  loka::scenario_tests::testing::failScenarioReelDriverAllocations(1);
  for (long tick = 0; tick < 512 && !reel.finished(); ++tick)
  {
    reel.tick(&window, &admission, 0.1, window.getTracker());
  }
  loka::scenario_tests::testing::allowScenarioReelDriverAllocations();

  LOKA_VERIFY(reel.finished());
  LOKA_VERIFY(std::string(reel.cell()) == "startup");
  LOKA_VERIFY(reel.completedCycles() == 0);
  LOKA_VERIFY(reel.operatorTitle().equals(loka::core::String::Literal("startup (cycle 1)")));
  LOKA_VERIFY(window.scene() != 0);
  LOKA_VERIFY(window.scene()->liveNodeCount() > 0);

  std::printf("testScenarioReelDriverAllocationRefusalRetiresInsteadOfWedging passed\n");
}

void testScenarioReelReplacementPreservesDriverAndStopsBeforeDetach()
{
  using namespace loka::scenario_tests;
  for (int refuse = 0; refuse != 3; ++refuse)
  {
    loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(CloneHelloWorldRoot());
    LOKA_VERIFY(root.get() != 0);
    NullPlatformContext context;
    WindowProps props;
    props.scene(new loka::app::scene::Scene(root.take()));
    NullWindow window(&context, props);
    WindowAdmissionTestApp app(window);
    // Drain initial viewport publication before arming the rearm-only refusal.
    app.flush();
    g_reelDriverObservation = ReelDriverObservation();
    window.scene()->getAttachedState()->bind(&ObserveReelDriverAtDetach, window.scene(), false);
    {
      ScenarioReel<ReelProbeScenario> reel(
          ScenarioCellTable(kProbeCells + 1, refuse ? 1 : 2), STARTUP_EXAMPLE_HELLO_WORLD,
          &MakeHelloWorldDriverErrorRecord, 2402, 0.0, refuse ? 0 : 1);
      LOKA_VERIFY(reel.tick(&window, &app, 0.1, window.getTracker()) == SCENARIO_REEL_RUNNING);
      const void *const installedIdentity = g_reelDriverObservation.drivers[0].identity;
      LOKA_VERIFY(installedIdentity != 0);
      if (refuse == 1)
        testing::failScenarioReelDriverAllocations(1);
      if (refuse == 2)
      {
        g_scenarioRearmAllocationRefusals = 0;
        window.scene()->getAttachedState()->bind(&RefuseScenarioCompositionAtDetach, window.scene(), false);
      }
      const ScenarioReelResult result = reel.tick(&window, &app, 0.1, window.getTracker());
      testing::allowScenarioReelDriverAllocations();
      if (refuse == 2)
      {
        loka::core::LokaAllocSetBackend(0, 0);
        window.scene()->getAttachedState()->unbind(&RefuseScenarioCompositionAtDetach, window.scene());
        LOKA_VERIFY(g_scenarioRearmAllocationRefusals == 1);
      }
      LOKA_VERIFY(reel.completedCycles() == 0);
      if (refuse)
      {
        LOKA_VERIFY(result == SCENARIO_REEL_FAILED);
        LOKA_VERIFY(reel.tick(&window, &app, 0.1, window.getTracker()) == SCENARIO_REEL_FAILED);
        LOKA_VERIFY(g_reelDriverObservation.drivers[0].identity == installedIdentity);
        LOKA_VERIFY(g_reelDriverObservation.drivers[0].destructions == 0);
        LOKA_VERIFY(g_reelDriverObservation.drivers[0].stops == (refuse == 2 ? 1 : 0));
        if (refuse == 2)
        {
          LOKA_VERIFY(g_reelDriverObservation.constructions == 2);
          LOKA_VERIFY(g_reelDriverObservation.detaches == 1);
          LOKA_VERIFY(g_reelDriverObservation.drivers[1].stops == 1);
          LOKA_VERIFY(g_reelDriverObservation.drivers[1].destructions == 1);
        }
        else
        {
          LOKA_VERIFY(g_reelDriverObservation.constructions == 1);
          LOKA_VERIFY(g_reelDriverObservation.detaches == 0);
        }
        LOKA_VERIFY(std::string(reel.cell()) == "first");
        LOKA_VERIFY(reel.operatorTitle().equals(loka::core::String::Literal("first (cycle 1)")));
        LOKA_VERIFY(reel.finished());
      }
      else
      {
        LOKA_VERIFY(result == SCENARIO_REEL_RUNNING);
        LOKA_VERIFY(g_reelDriverObservation.drivers[0].stops == 1);
        LOKA_VERIFY(g_reelDriverObservation.drivers[0].destructions == 1);
        LOKA_VERIFY(g_reelDriverObservation.drivers[1].destructions == 0);
        LOKA_VERIFY(g_reelDriverObservation.constructedCell == "second");
        LOKA_VERIFY(g_reelDriverObservation.detaches == 1);
        LOKA_VERIFY(std::string(reel.cell()) == "second");
        LOKA_VERIFY(reel.operatorTitle().equals(loka::core::String::Literal("second (cycle 1)")));
        LOKA_VERIFY(!reel.finished());
        LOKA_VERIFY(reel.tick(&window, &app, 0.1, window.getTracker()) == SCENARIO_REEL_RUNNING);
        LOKA_VERIFY(reel.tick(&window, &app, 0.1, window.getTracker()) == SCENARIO_REEL_COMPLETE);
        LOKA_VERIFY(reel.tick(&window, &app, 0.1, window.getTracker()) == SCENARIO_REEL_COMPLETE);
        LOKA_VERIFY(reel.finished());
        LOKA_VERIFY(reel.completedCycles() == 1);
        LOKA_VERIFY(reel.cell() == 0);
        LOKA_VERIFY(g_reelDriverObservation.constructions == 2);
        LOKA_VERIFY(g_reelDriverObservation.stops == 2);
        LOKA_VERIFY(g_reelDriverObservation.detaches == 1);
        LOKA_VERIFY(reel.operatorTitle().equals(loka::core::String::Literal("second (cycle 1)")));
      }
      window.scene()->getAttachedState()->unbind(&ObserveReelDriverAtDetach, window.scene());
    }
    LOKA_VERIFY(g_reelDriverObservation.stops == g_reelDriverObservation.constructions);
    LOKA_VERIFY(g_reelDriverObservation.destructions == g_reelDriverObservation.constructions);
  }
  std::printf("testScenarioReelReplacementPreservesDriverAndStopsBeforeDetach passed\n");
}

void testStandaloneRunControlRearmsCompletedSceneWithoutQuittingApp()
{
  const loka::scenario_tests::ScenarioCellTable cells(kProbeCells + 1, 2);
  NullPlatformContext context;
  NullApp completedApp(0);
  loka::standalone_tests::StandaloneRunControl completed(
      "LoopProbe",
      cells,
      0,
      loka::standalone_tests::StandaloneRunControl::REARM_COMPLETED_SCENE);
  WindowProps completedWindowProps;
  completedWindowProps.title("Loka Probe").displayTitleState(
      completed.displayTitleState("Loka Probe"));
  NullWindow completedWindow(&context, completedWindowProps);
  completed.setApp(&completedApp);
  LOKA_VERIFY(completed.advance(true)
              == loka::standalone_tests::StandaloneRunControl::ADVANCE_MOUNTED);
  LOKA_VERIFY(completed.tick() == 1);

  loka::dsl::SnapRecord record;
  LOKA_VERIFY(completed.observeScenarioAdvance(
      loka::scenario_tests::SCENARIO_ADVANCE_FINAL_SCENE_HELD, record, &completedWindow));
  LOKA_VERIFY(!completedApp.quitRequested());
  LOKA_VERIFY(std::string(completed.nextScenarioName()) == "second");
  completed.completeSceneRearm(true, &completedWindow);
  LOKA_VERIFY(completed.tick() == 0);
  LOKA_VERIFY(!completed.failed());
  LOKA_VERIFY(completedWindow.displayTitleState().get().equals(
      loka::core::String::Literal("Loka Probe - second (cycle 1)")));

  NullApp userClosedApp(0);
  loka::standalone_tests::StandaloneRunControl userClosed(
      "LoopProbe",
      cells,
      0,
      loka::standalone_tests::StandaloneRunControl::REARM_COMPLETED_SCENE);
  userClosed.setApp(&userClosedApp);
  LOKA_VERIFY(!userClosedApp.quitRequested());
  LOKA_VERIFY(!userClosed.failed());

  std::FILE *diagnostics = std::tmpfile();
  LOKA_VERIFY(diagnostics != 0);
  NullApp failedApp(0);
  loka::standalone_tests::StandaloneRunControl failed(
      "LoopProbe",
      cells,
      diagnostics,
      loka::standalone_tests::StandaloneRunControl::REARM_COMPLETED_SCENE);
  failed.setApp(&failedApp);
  LOKA_VERIFY(failed.observeScenarioAdvance(
      loka::scenario_tests::SCENARIO_ADVANCE_FINAL_SCENE_HELD, record, &completedWindow));
  failed.completeSceneRearm(false, &completedWindow);
  LOKA_VERIFY(failed.failed());
  LOKA_VERIFY(failedApp.quitRequested());
  if (diagnostics)
  {
    LOKA_VERIFY(std::fclose(diagnostics) == 0);
  }

  std::printf("testStandaloneRunControlRearmsCompletedSceneWithoutQuittingApp passed\n");
}

void testStandaloneScenarioRailReplacementIsFailureAtomic()
{
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(CloneMineSweeperRoot());
  LOKA_VERIFY(root.get() != 0);
  NullPlatformContext context;
  WindowProps windowProps;
  windowProps.scene(new loka::app::scene::Scene(root.take()));
  NullWindow window(&context, windowProps);
  WindowAdmissionTestApp admission(window);
  LOKA_VERIFY(window.scene() != 0);
  loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
  AttachmentTransactionObservation attachmentObservation;
  attachmentObservation.manager = window.sceneManager();
  attachmentObservation.window = &window;
  window.scene()->getAttachedState()->bind(
      &ObserveAttachmentTransaction, &attachmentObservation, false);

  StandaloneScenarioRailObservation first;
  StandaloneScenarioRailObservation second;
  {
    loka::standalone_tests::StandaloneScenarioRail<StandaloneScenarioRailProbe> rail(
        new StandaloneScenarioRailProbe(&first));
    LOKA_VERIFY(rail.isValid());
    LOKA_VERIFY(!rail.replaceAndRearmScene(0, &window, &admission));
    LOKA_VERIFY(first.stops == 0);
    LOKA_VERIFY(first.destructions == 0);
    LOKA_VERIFY(attachmentObservation.calls == 0);

    LOKA_VERIFY(rail.replaceAndRearmScene(
        new StandaloneScenarioRailProbe(&second), &window, &admission));
    LOKA_VERIFY(first.stops == 1);
    LOKA_VERIFY(first.destructions == 1);
    LOKA_VERIFY(second.stops == 0);
    LOKA_VERIFY(second.destructions == 0);
    LOKA_VERIFY(attachmentObservation.calls == 2);
    LOKA_VERIFY(attachmentObservation.allCallsInsideSeatApply);
  }
  window.scene()->getAttachedState()->unbind(
      &ObserveAttachmentTransaction, &attachmentObservation);
  LOKA_VERIFY(second.stops == 1);
  LOKA_VERIFY(second.destructions == 1);

  std::printf("testStandaloneScenarioRailReplacementIsFailureAtomic passed\n");
}

void testStandaloneSceneRearmAllocationRefusalDoesNotCommitNextPass()
{
  const loka::scenario_tests::ScenarioCellTable cells(kProbeCells + 1, 2);
  std::FILE *diagnostics = std::tmpfile();
  LOKA_VERIFY(diagnostics != 0);
  NullPlatformContext context;
  NullApp app(0);
  loka::standalone_tests::StandaloneRunControl runControl(
      "LoopProbe",
      cells,
      diagnostics,
      loka::standalone_tests::StandaloneRunControl::REARM_COMPLETED_SCENE);
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(CloneMineSweeperRoot());
  LOKA_VERIFY(root.get() != 0);
  WindowProps windowProps;
  windowProps.title("Loka Probe")
      .displayTitleState(runControl.displayTitleState("Loka Probe"))
      .scene(new loka::app::scene::Scene(root.take()));
  NullWindow window(&context, windowProps);
  WindowAdmissionTestApp admission(window);
  runControl.setApp(&app);
  LOKA_VERIFY(window.scene() != 0);
  loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
  const std::size_t mountedNodeCount = window.scene()->liveNodeCount();
  LOKA_VERIFY(mountedNodeCount > 0);
  LOKA_VERIFY(runControl.advance(true)
              == loka::standalone_tests::StandaloneRunControl::ADVANCE_MOUNTED);

  loka::dsl::SnapRecord record;
  LOKA_VERIFY(runControl.observeScenarioAdvance(
      loka::scenario_tests::SCENARIO_ADVANCE_FINAL_SCENE_HELD, record, &window));
  const loka::core::String titleBefore = window.displayTitleState().get();

  StandaloneScenarioRailObservation first;
  StandaloneScenarioRailObservation second;
  {
    loka::standalone_tests::StandaloneScenarioRail<StandaloneScenarioRailProbe> rail(
        new StandaloneScenarioRailProbe(&first));
    AttachmentTransactionObservation attachmentObservation;
    attachmentObservation.manager = window.sceneManager();
    attachmentObservation.window = &window;
    window.scene()->getAttachedState()->bind(
        &ObserveAttachmentTransaction, &attachmentObservation, false);

    // The refusal count is the positive control: the fresh attach really
    // reached the allocation gate instead of failing for an unrelated reason.
    g_scenarioRearmAllocationRefusals = 0;
    loka::core::LokaAllocSetBackend(
        &scenarioRearmRefusingBackendAlloc, &scenarioRearmDelegatingBackendFree);
    const bool rearmed = rail.replaceAndRearmScene(
        new StandaloneScenarioRailProbe(&second), &window, &admission);
    loka::core::LokaAllocSetBackend(0, 0);

    LOKA_VERIFY(!rearmed);
    LOKA_VERIFY(g_scenarioRearmAllocationRefusals > 0);
    LOKA_VERIFY(first.stops == 1);
    LOKA_VERIFY(first.destructions == 1);
    LOKA_VERIFY(second.stops == 0);
    LOKA_VERIFY(second.destructions == 0);
    LOKA_VERIFY(attachmentObservation.calls == 2);
    LOKA_VERIFY(attachmentObservation.allCallsInsideSeatApply);

    runControl.completeSceneRearm(rearmed, &window);
    LOKA_VERIFY(runControl.failed());
    LOKA_VERIFY(app.quitRequested());
    LOKA_VERIFY(runControl.tick() == 1);
    LOKA_VERIFY(std::string(runControl.nextScenarioName()) == "second");
    LOKA_VERIFY(window.displayTitleState().get().equals(titleBefore));

    // Restore the backend and externally drive the white-flag retry. This
    // proves the failed re-arm left a recoverable uncomposed Scene rather than
    // corrupting its root-definition owner.
    window.scene()->requestInvalidate(loka::app::scene::NODE_DIRTY_CHILD);
    (void)window.scene()->flushInvalidation();
    LOKA_VERIFY(window.scene()->liveNodeCount() == mountedNodeCount);

    window.scene()->getAttachedState()->unbind(
        &ObserveAttachmentTransaction, &attachmentObservation);
  }
  LOKA_VERIFY(second.stops == 1);
  LOKA_VERIFY(second.destructions == 1);
  if (diagnostics)
  {
    LOKA_VERIFY(std::fclose(diagnostics) == 0);
  }

  std::printf("testStandaloneSceneRearmAllocationRefusalDoesNotCommitNextPass passed\n");
}
