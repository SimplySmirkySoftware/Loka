#include "testing/scene/SceneTestFlow.hpp"
#include "StartupScenarioTests.hpp"
#include "standalone/StandaloneScenarioSupport.hpp"

#include "support/TestVerify.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "../example/FloppyBird/src/GameModel.hpp"
#include "../example/FloppyBird/src/MainNode.hpp"
#include "../example/HelloWorld/src/MainNode.hpp"
#include "../example/MineSweeper/src/MainNode.hpp"
#include "../example/Tutorial/src/DoItYourselfNode.hpp"
#include "app/scene/Scene.hpp"
#include "core/util/OwnedDef.hpp"
#include "platform/file/FileHandle.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "scenarios/FloppyBirdScenarios.hpp"
#include "scenarios/HelloWorldProductionFrame.hpp"
#include "scenarios/MineSweeperScenarios.hpp"
#include "scenarios/StartupScenarios.hpp"

namespace
{
  std::string SourcePath(const char *relative)
  {
    const std::string sourceFile(__FILE__);
    const std::string unixMarker = "/tests/StartupScenarioTests.cpp";
    const std::string windowsMarker = "\\tests\\StartupScenarioTests.cpp";
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

  void RunStartupScenario(loka::scenario_tests::StartupExample example,
                          loka::app::scene::NodeDefinitionBase *root,
                          int width,
                          int height,
                          const char *expectedRelativePath,
                          floppybird::GameModel *floppyBirdGame)
  {
    const char *actualPath = "_loka_startup.audit";
    std::remove(actualPath);
    loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> ownedRoot(root);
    LOKA_VERIFY(ownedRoot.get() != 0);
    NullScenePlatformController platform;
    loka::app::scene::Scene scene(ownedRoot.take());
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    {
      loka::platform::file::FileHandle destination;
      destination.displayPath = loka::core::String::Utf8(actualPath, std::strlen(actualPath));
      loka::dsl::testing::ScenarioAuditFile audit(destination, "startup");
      assert(audit.isValid()); // loka-assert-ok: pure validity observation
      loka::scenario_tests::StartupScenario scenario(
          example, loka::scenario_tests::SCENARIO_COMPLETION_HOLD_FINAL_SCENE, &audit);
      loka::scenario_tests::CaptureContentBounds bounds;
      bounds.available = true;
      bounds.right = width;
      bounds.bottom = height;
      loka::dsl::SnapRecord record;
      for (long tick = 1; tick <= 2; ++tick)
      {
        if (floppyBirdGame)
        {
          floppyBirdGame->advanceFrame(loka_floppy_bird::kFixedStepSeconds);
        }
        const loka::scenario_tests::ScenarioAdvance advance =
            scenario.step(tick, &scene, bounds, record);
        LOKA_VERIFY(advance == (tick == 2
                                   ? loka::scenario_tests::SCENARIO_ADVANCE_FINAL_SCENE_HELD
                                   : loka::scenario_tests::SCENARIO_ADVANCE_PENDING));
      }
      LOKA_VERIFY(scenario.step(3, &scene, bounds, record)
                  == loka::scenario_tests::SCENARIO_ADVANCE_FINAL_SCENE_HELD);
    }

    const std::string actual = ReadBytes(actualPath);
    const std::string expectedPath = SourcePath(expectedRelativePath);
    const std::string expected = ReadBytes(expectedPath.c_str());
    LOKA_VERIFY(actual == expected);
    std::remove(actualPath);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }
} // namespace

void testHelloWorldStartupHoldsSettledInitialScreenAndMatchesAudit()
{
  helloworld::MainProps props;
  loka::app::scene::BoundaryDefinition<helloworld::MainProps, helloworld::MainNode> definition(props);
  const loka::scenario_tests::HelloWorldProductionFrame frame;
  RunStartupScenario(loka::scenario_tests::STARTUP_EXAMPLE_HELLO_WORLD,
                     definition.clone(),
                     frame.width(),
                     frame.height(),
                     "tests/scenarios/expected/helloworld/startup.audit",
                     0);
  std::printf("testHelloWorldStartupHoldsSettledInitialScreenAndMatchesAudit passed\n");
}

void testTutorialStartupHoldsSettledInitialScreenAndMatchesAudit()
{
  tutorial::DoItYourselfNode::PropsType props;
  loka::app::scene::BoundaryDefinition<tutorial::DoItYourselfNode::PropsType,
                                       tutorial::DoItYourselfNode>
      definition(props);
  RunStartupScenario(loka::scenario_tests::STARTUP_EXAMPLE_TUTORIAL,
                     definition.clone(),
                     360,
                     280,
                     "tests/scenarios/expected/tutorial/startup.audit",
                     0);
  std::printf("testTutorialStartupHoldsSettledInitialScreenAndMatchesAudit passed\n");
}

void testMineSweeperStartupHoldsSettledInitialScreenAndMatchesAudit()
{
  const minesweeper::MainProps props(loka::scenario_tests::MineSweeperScenarioSeed());
  loka::app::scene::BoundaryDefinition<minesweeper::MainProps, minesweeper::MainNode> definition(props);
  RunStartupScenario(loka::scenario_tests::STARTUP_EXAMPLE_MINESWEEPER,
                     definition.clone(),
                     220,
                     240,
                     "tests/scenarios/expected/minesweeper/startup.audit",
                     0);
  std::printf("testMineSweeperStartupHoldsSettledInitialScreenAndMatchesAudit passed\n");
}

void testFloppyBirdStartupHoldsSettledInitialScreenAndMatchesAudit()
{
  floppybird::GameModel game(loka::scenario_tests::FloppyBirdScenarioSeed());
  const floppybird::MainProps props(&game);
  loka::app::scene::BoundaryDefinition<floppybird::MainProps, floppybird::MainNode> definition(props);
  RunStartupScenario(loka::scenario_tests::STARTUP_EXAMPLE_FLOPPY_BIRD,
                     definition.clone(),
                     380,
                     340,
                     "tests/scenarios/expected/floppybird/startup.audit",
                     &game);
  std::printf("testFloppyBirdStartupHoldsSettledInitialScreenAndMatchesAudit passed\n");
}

void testStandaloneStartupRefusesOnlyUnresolvedAuditLocation()
{
  loka::platform::file::FileHandle unresolved;
  const char *message = loka::standalone_tests::StandaloneAuditRefusal(unresolved);
  LOKA_VERIFY(message != 0);
  LOKA_VERIFY(std::string(message)
              == "The audit sidecar LOG.TXT could not be resolved beside the application. "
                 "Move the application to a writable folder (for example, out of Downloads into Applications), "
                 "or remove the quarantine attribute, then try again.");

  loka::platform::file::FileHandle resolved;
  resolved.displayPath = loka::core::String::Literal("/missing-folder/LOG.TXT");
  resolved.kind = loka::file::File::KIND_FILE;
  // Resolution is a location fact, not proof that a later open/write succeeds.
  LOKA_VERIFY(loka::standalone_tests::StandaloneAuditRefusal(resolved) == 0);
  std::printf("testStandaloneStartupRefusesOnlyUnresolvedAuditLocation passed\n");
}
