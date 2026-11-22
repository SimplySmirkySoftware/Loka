#include "MineSweeperScenarioTests.hpp"

#include "support/TestVerify.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../example/MineSweeper/src/MainNode.hpp"
#include "app/scene/Scene.hpp"
#include "core/util/OwnedDef.hpp"
#include "platform/file/FileHandle.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "scenarios/MineSweeperScenarios.hpp"
#include "standalone/MineSweeperStandaloneFlowAppConfig.hpp"
#include "WindowRootRefusalTests.hpp"
#include "platform/null/NullApp.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/null/NullWindow.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/Window.hpp"
#include "scenarios/ObservedMainDefinition.hpp"
#include "scenarios/SceneScenarioDriver.hpp"
#include "testing/scene/ScenarioAudit.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace
{
  class RecordingMineSweeperAudit : public loka::dsl::testing::ScenarioAuditSink
  {
  public:
    virtual bool recordStep(const loka::dsl::testing::ScenarioStepTerminal &record)
    {
      this->steps.push_back(record);
      return true;
    }

    virtual bool recordMatch(const loka::dsl::testing::ScenarioMatchSelection &)
    {
      return true;
    }

    virtual bool recordSubstep(const loka::dsl::testing::ScenarioSubstepTerminal &)
    {
      return true;
    }

    virtual bool recordTerminal(loka::dsl::testing::ScenarioAuditTerminalStatus status)
    {
      this->terminals.push_back(status);
      return true;
    }

    virtual bool recordVerdict(const loka::dsl::SnapRecord &record)
    {
      this->verdicts.push_back(record);
      return true;
    }

    std::vector<loka::dsl::testing::ScenarioStepTerminal> steps;
    std::vector<loka::dsl::SnapRecord> verdicts;
    std::vector<loka::dsl::testing::ScenarioAuditTerminalStatus> terminals;
  };

  std::string SourcePath(const char *relative)
  {
    const std::string sourceFile(__FILE__);
    const std::string unixMarker = "/tests/MineSweeperScenarioTests.cpp";
    const std::string windowsMarker = "\\tests\\MineSweeperScenarioTests.cpp";
    std::string::size_type marker = sourceFile.rfind(unixMarker);
    if (marker == std::string::npos)
    {
      marker = sourceFile.rfind(windowsMarker);
    }
    assert(marker != std::string::npos);
    return sourceFile.substr(0, marker) + "/" + (relative ? relative : "");
  }

  std::string ReadBytes(const char *path)
  {
    std::string content;
    FILE *input = std::fopen(path, "rb");
    LOKA_VERIFY(input != 0);
    if (!input)
    {
      return content;
    }
    char buffer[512];
    std::size_t count = 0;
    while ((count = std::fread(buffer, 1, sizeof(buffer), input)) != 0)
    {
      content.append(buffer, count);
    }
    LOKA_VERIFY(std::fclose(input) == 0);
    return content;
  }

  loka::app::scene::NodeDefinitionBase *CloneMineSweeperRoot(unsigned long seed)
  {
    const minesweeper::MainProps props(seed);
    loka::app::scene::BoundaryDefinition<minesweeper::MainProps, minesweeper::MainNode> definition(props);
    return definition.clone();
  }

  void AdvanceToCompletion(loka::scenario_tests::MineSweeperScenario &scenario,
                           loka::app::scene::Scene &scene,
                           loka::scenario_tests::CaptureContentBounds &bounds,
                           loka::dsl::SnapRecord &record,
                           loka::scenario_tests::ScenarioAdvance expected)
  {
    LOKA_VERIFY(scenario.step(2, &scene, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
    LOKA_VERIFY(scenario.step(7, &scene, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
    LOKA_VERIFY(scenario.step(12, &scene, bounds, record) == expected);
  }

  void VerifyRecordString(const loka::dsl::SnapRecord &record, const char *key, const char *expected)
  {
    std::string value;
    LOKA_VERIFY(record.get(key, value));
    LOKA_VERIFY(value == expected);
  }
} // namespace

void testMineSweeperNewGameKeyNeverRepeats()
{
  minesweeper::MainNode *main = 0;
  loka::scenario_tests::ObservedMainDefinition<minesweeper::MainProps, minesweeper::MainNode> definition(
      minesweeper::MainProps(loka::scenario_tests::MineSweeperScenarioSeed()), &main);
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(definition.clone());
  LOKA_VERIFY(root.get() != 0);
  NullScenePlatformController platform;
  loka::app::scene::Scene scene(root.take());
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  LOKA_VERIFY(main != 0);

  long idMatches = 0;
  long typedMatches = 0;
  loka::app::ButtonNode *button = 0;
  loka::dsl::testing::scene_test_detail::findNodeByIdRecursive<loka::app::ButtonNode>(
      loka::dsl::testing::SceneTestAccess::rootNode(scene),
      std::string("MineSweeper.NewGameButton"), idMatches, typedMatches, button);
  LOKA_VERIFY(idMatches == 1 && typedMatches == 1 && button != 0);

  int keys[4];
  keys[0] = main->gameGeneration();
  for (int round = 1; round < 4; ++round)
  {
    {
      loka::core::StateTrackerGuard guard(main->tracker());
      button->props.onClick_->emit();
    }
    keys[round] = main->gameGeneration();
    scene.flushInvalidation();
  }
  for (int i = 0; i < 4; ++i)
  {
    for (int j = i + 1; j < 4; ++j)
    {
      LOKA_VERIFY(keys[i] != keys[j]);
    }
  }

  loka::dsl::testing::SceneTestAccess::unmount(scene);
  std::printf("testMineSweeperNewGameKeyNeverRepeats passed\n");
}

void testMineSweeperNewGameTwiceDrivesOwnerEmitter()
{
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(
      CloneMineSweeperRoot(loka::scenario_tests::MineSweeperScenarioSeed()));
  LOKA_VERIFY(root.get() != 0);
  NullScenePlatformController platform;
  loka::app::scene::Scene scene(root.take());
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  RecordingMineSweeperAudit audit;
  loka::scenario_tests::MineSweeperScenario scenario(
      loka::scenario_tests::SCENARIO_COMPLETION_DRIVER_OWNED, &audit);
  loka::scenario_tests::CaptureContentBounds bounds;
  bounds.available = true;
  bounds.right = 220;
  bounds.bottom = 240;
  loka::dsl::SnapRecord record;
  AdvanceToCompletion(
      scenario, scene, bounds, record, loka::scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY);
  LOKA_VERIFY(scenario.publishVerdict(record));

  VerifyRecordString(record, "test", "MineSweeper");
  VerifyRecordString(record, "step", "new-game-twice");
  VerifyRecordString(record, "node", "MineSweeper.Board");
  VerifyRecordString(record, "board.initial_mines", "3,4,20,22,37,45,50,55,56,59");
  VerifyRecordString(record, "board.after_new_game_1_mines", "1,6,16,23,27,30,34,44,45,53");
  VerifyRecordString(record, "board.after_new_game_2_mines", "3,26,42,49,50,56,59,60,62,63");
  VerifyRecordString(record, "board.mines", "3,26,42,49,50,56,59,60,62,63");
  LOKA_VERIFY(audit.steps.size() == 6);
  LOKA_VERIFY(audit.steps[1].name() == "start-first-new-game");
  LOKA_VERIFY(audit.steps[3].name() == "start-second-new-game");
  LOKA_VERIFY(audit.terminals.size() == 1);
  LOKA_VERIFY(audit.terminals[0] == loka::dsl::testing::SCENARIO_AUDIT_SUCCEEDED);
  LOKA_VERIFY(audit.verdicts.size() == 1);

  loka::dsl::testing::SceneTestAccess::unmount(scene);
  std::printf("testMineSweeperNewGameTwiceDrivesOwnerEmitter passed\n");
}

void testMineSweeperNewGameTwiceHoldsFinalSceneAndMatchesAudit()
{
  const char *actualPath = "_loka_minesweeper_new_game_twice.audit";
  std::remove(actualPath);
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(
      CloneMineSweeperRoot(loka::scenario_tests::MineSweeperScenarioSeed()));
  LOKA_VERIFY(root.get() != 0);
  NullScenePlatformController platform;
  loka::app::scene::Scene scene(root.take());
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  {
    loka::platform::file::FileHandle destination;
    destination.displayPath = loka::core::String::Utf8(actualPath, std::strlen(actualPath));
    loka::dsl::testing::ScenarioAuditFile audit(destination, "new-game-twice");
    assert(audit.isValid()); // loka-assert-ok: pure validity observation
    loka::scenario_tests::MineSweeperScenario scenario(
        loka::scenario_tests::SCENARIO_COMPLETION_HOLD_FINAL_SCENE, &audit);
    loka::scenario_tests::CaptureContentBounds bounds;
    bounds.available = true;
    bounds.right = 220;
    bounds.bottom = 240;
    loka::dsl::SnapRecord record;
    AdvanceToCompletion(
        scenario, scene, bounds, record, loka::scenario_tests::SCENARIO_ADVANCE_FINAL_SCENE_HELD);
    LOKA_VERIFY(scenario.step(63, &scene, bounds, record)
                == loka::scenario_tests::SCENARIO_ADVANCE_FINAL_SCENE_HELD);
  }

  const std::string actual = ReadBytes(actualPath);
  const std::string expectedPath =
      SourcePath("tests/scenarios/expected/minesweeper/new-game-twice.audit");
  const std::string expected = ReadBytes(expectedPath.c_str());
  LOKA_VERIFY(actual == expected);
  std::remove(actualPath);

  loka::dsl::testing::SceneTestAccess::unmount(scene);
  std::printf("testMineSweeperNewGameTwiceHoldsFinalSceneAndMatchesAudit passed\n");
}

void testMineSweeperSeededRevealDrivesCells()
{
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(
      CloneMineSweeperRoot(loka::scenario_tests::MineSweeperScenarioSeed()));
  LOKA_VERIFY(root.get() != 0);
  NullPlatformContext context;
  WindowProps windowProps;
  windowProps.scene(new loka::app::scene::Scene(root.take()));
  NullWindow window(&context, windowProps);
  LOKA_VERIFY(window.scene() != 0);
  loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);

  RecordingMineSweeperAudit audit;
  loka::scenario_tests::SceneScenarioDriver<loka::scenario_tests::MineSweeperScenario> driver(
      false,
      loka::scenario_tests::STARTUP_EXAMPLE_MINESWEEPER,
      "seeded-reveal",
      &loka::scenario_tests::MakeMineSweeperDriverErrorRecord,
      2602,
      &audit);
  loka::scenario_tests::CaptureContentBounds bounds;
  bounds.available = true;
  bounds.right = 220;
  bounds.bottom = 240;
  loka::dsl::SnapRecord record;

  LOKA_VERIFY(driver.step(2, &window, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
  LOKA_VERIFY(driver.step(7, &window, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
  LOKA_VERIFY(driver.step(12, &window, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
  LOKA_VERIFY(driver.step(17, &window, bounds, record)
              == loka::scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY);
  LOKA_VERIFY(driver.publishVerdict(record));

  VerifyRecordString(record, "step", "seeded-reveal");
  VerifyRecordString(record, "blank_cell", "0,0");
  VerifyRecordString(record, "blank_value", "space");
  VerifyRecordString(record, "numbered_cell", "0,2");
  VerifyRecordString(record, "numbered_value", "1");
  VerifyRecordString(record, "mine_cell", "0,3");
  VerifyRecordString(record, "mine_value", "X");
  LOKA_VERIFY(audit.steps.size() == 8);
  LOKA_VERIFY(audit.steps[1].name() == "reveal-blank-cell");
  LOKA_VERIFY(audit.steps[3].name() == "reveal-numbered-cell");
  LOKA_VERIFY(audit.steps[5].name() == "reveal-mine-cell");
  LOKA_VERIFY(audit.terminals.size() == 1);
  LOKA_VERIFY(audit.terminals[0] == loka::dsl::testing::SCENARIO_AUDIT_SUCCEEDED);

  std::printf("testMineSweeperSeededRevealDrivesCells passed\n");
}

void testMineSweeperDifferentSeedRefusesFixedBoardAudit()
{
  const char *actualPath = "_loka_minesweeper_different_seed.audit";
  std::remove(actualPath);
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(CloneMineSweeperRoot(0x2468ACE0UL));
  LOKA_VERIFY(root.get() != 0);
  NullScenePlatformController platform;
  loka::app::scene::Scene scene(root.take());
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  {
    loka::platform::file::FileHandle destination;
    destination.displayPath = loka::core::String::Utf8(actualPath, std::strlen(actualPath));
    loka::dsl::testing::ScenarioAuditFile audit(destination, "new-game-twice");
    assert(audit.isValid()); // loka-assert-ok: pure validity observation
    loka::scenario_tests::MineSweeperScenario scenario(
        loka::scenario_tests::SCENARIO_COMPLETION_HOLD_FINAL_SCENE, &audit);
    loka::scenario_tests::CaptureContentBounds bounds;
    loka::dsl::SnapRecord record;
    LOKA_VERIFY(scenario.step(2, &scene, bounds, record)
                == loka::scenario_tests::SCENARIO_ADVANCE_FINAL_SCENE_HELD);
    VerifyRecordString(record, "status", loka::dsl::SnapStatusError());
  }

  const std::string actual = ReadBytes(actualPath);
  const std::string expectedPath =
      SourcePath("tests/scenarios/expected/minesweeper/new-game-twice.audit");
  const std::string expected = ReadBytes(expectedPath.c_str());
  LOKA_VERIFY(actual != expected);
  assert(actual.find("status=failed") != std::string::npos);
  assert(actual.find("terminal status=failed") != std::string::npos);
  std::remove(actualPath);

  loka::dsl::testing::SceneTestAccess::unmount(scene);
  std::printf("testMineSweeperDifferentSeedRefusesFixedBoardAudit passed\n");
}

void testMineSweeperStandaloneFlowWritesExpectedAudit()
{
  const char *actualPath = "_loka_minesweeper_standalone_flow.audit";
  std::remove(actualPath);
  {
    loka::platform::file::FileHandle auditFile;
    auditFile.displayPath = loka::core::String::Literal(actualPath);
    NullPlatformContext context;
    loka::standalone_tests::MineSweeperStandaloneFlowAppConfig config(&context, &auditFile);
    NullApp app(&config);
    config.setApp(&app);

    AppComposition composition(&context);
    config.compose(composition);
    std::vector<AppComponent *> components = composition.build();
    LOKA_VERIFY(components.size() == 1);
    Window *window = components[0] ? components[0]->asWindow() : 0;
    LOKA_VERIFY(window != 0);
    LOKA_VERIFY(window->scene() != 0);
    for (int tick = 0; tick < 65; ++tick)
    {
      LOKA_VERIFY(window->handleIdle(0.1));
    }
    LOKA_VERIFY(config.exitCode() == 0);
    LOKA_VERIFY(!app.quitRequested());

    for (std::size_t i = 0; i < components.size(); ++i)
    {
      delete components[i];
    }
  }

  const std::string actual = ReadBytes(actualPath);
  const std::string expectedPath =
      SourcePath("tests/scenarios/expected/minesweeper/new-game-twice.audit");
  const std::string expected = ReadBytes(expectedPath.c_str());
  LOKA_VERIFY(actual == expected);
  std::remove(actualPath);
  std::printf("testMineSweeperStandaloneFlowWritesExpectedAudit passed\n");
}

void testMineSweeperStandaloneDeclarationRefusalIsWindowless()
{
  WindowRootRefusalTestSupport::verifyDeclarationRefusal<loka::standalone_tests::MineSweeperStandaloneFlowAppConfig>(
      "_loka_minesweeper_declaration_refusal.audit");
}

void testMineSweeperStandaloneMountRefusalFailsClosed()
{
  const char *auditPath = "_loka_minesweeper_standalone_mount_refusal.audit";
  std::remove(auditPath);
  std::FILE *diagnostics = std::tmpfile();
  LOKA_VERIFY(diagnostics != 0);

  {
    loka::platform::file::FileHandle auditFile;
    auditFile.displayPath = loka::core::String::Literal(auditPath);
    NullPlatformContext context;
    loka::standalone_tests::MineSweeperStandaloneFlowAppConfig config(&context, &auditFile, diagnostics);
    NullApp app(&config);
    config.setApp(&app);

    Window *window = WindowRootRefusalTestSupport::launchWithRefusedMount(app);
    for (int tick = 0; tick < 8; ++tick)
    {
      LOKA_VERIFY(window->handleIdle(0.1));
    }

    LOKA_VERIFY(app.quitRequested());
    LOKA_VERIFY(config.exitCode() != 0);
    LOKA_VERIFY(std::fflush(diagnostics) == 0);
    LOKA_VERIFY(std::fseek(diagnostics, 0, SEEK_SET) == 0);
    char buffer[256];
    const std::size_t count = std::fread(buffer, 1, sizeof(buffer), diagnostics);
    const std::string output(buffer, count);
    LOKA_VERIFY(output
                == "Loka MineSweeper standalone startup failed: "
                   "MainNode was not mounted after 5 idle ticks.\n");
  }

  LOKA_VERIFY(std::fclose(diagnostics) == 0);
  std::remove(auditPath);
  std::printf("testMineSweeperStandaloneMountRefusalFailsClosed passed\n");
}
