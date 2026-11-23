#include "testing/scene/SceneTestFlow.hpp"
#include "FloppyBirdScenarioTests.hpp"

#include "support/TestVerify.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../example/FloppyBird/src/GameModel.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/Window.hpp"
#include "app/scene/Scene.hpp"
#include "core/util/OwnedDef.hpp"
#include "platform/file/FileHandle.hpp"
#include "platform/null/NullApp.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "scenarios/FloppyBirdScenarios.hpp"
#include "scenarios/ObservedMainDefinition.hpp"
#include "standalone/FloppyBirdStandaloneFlowAppConfig.hpp"
#include "StandaloneLoopLaunchTests.hpp"
#include "WindowRootRefusalTests.hpp"
#include "testing/scene/ScenarioAudit.hpp"

namespace
{
  class RecordingFloppyBirdAudit : public loka::dsl::testing::ScenarioAuditSink
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
    const std::string unixMarker = "/tests/FloppyBirdScenarioTests.cpp";
    const std::string windowsMarker = "\\tests\\FloppyBirdScenarioTests.cpp";
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

  loka::app::scene::NodeDefinitionBase *CloneFloppyBirdRoot(floppybird::GameModel &game)
  {
    const floppybird::MainProps props(&game);
    loka::app::scene::BoundaryDefinition<floppybird::MainProps, floppybird::MainNode> definition(props);
    return definition.clone();
  }

  void AdvanceToCompletion(loka::scenario_tests::FloppyBirdScenario &scenario,
                           floppybird::GameModel &game,
                           loka::app::scene::Scene &scene,
                           const loka::scenario_tests::CaptureContentBounds &bounds,
                           loka::dsl::SnapRecord &record,
                           loka::scenario_tests::ScenarioAdvance expected)
  {
    for (long tick = 1; tick <= 273; ++tick)
    {
      game.advanceFrame(loka_floppy_bird::kFixedStepSeconds);
      const loka::scenario_tests::ScenarioAdvance actual = scenario.step(tick, &scene, game, bounds, record);
      if (tick < 273)
      {
        if (actual != loka::scenario_tests::SCENARIO_ADVANCE_PENDING)
        {
          std::fprintf(stderr, "FloppyBird scenario became terminal at tick %ld\n", tick);
        }
        LOKA_VERIFY(actual == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
      }
      else
      {
        LOKA_VERIFY(actual == expected);
      }
    }
  }

  void VerifyRecordString(const loka::dsl::SnapRecord &record, const char *key, const char *expected)
  {
    std::string value;
    LOKA_VERIFY(record.get(key, value));
    if (value != expected)
    {
      std::fprintf(stderr,
                   "FloppyBird record mismatch for %s: expected '%s', got '%s'\n",
                   key ? key : "(null)",
                   expected ? expected : "(null)",
                   value.c_str());
    }
    LOKA_VERIFY(value == expected);
  }
} // namespace

void testFloppyBirdFixedStepFlapsDriveSeededGame()
{
  floppybird::GameModel game(loka::scenario_tests::FloppyBirdScenarioSeed());
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(CloneFloppyBirdRoot(game));
  LOKA_VERIFY(root.get() != 0);
  NullScenePlatformController platform;
  loka::app::scene::Scene scene(root.take());
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  RecordingFloppyBirdAudit audit;
  loka::scenario_tests::FloppyBirdScenario scenario(
      loka::scenario_tests::SCENARIO_COMPLETION_DRIVER_OWNED, &audit);
  loka::scenario_tests::CaptureContentBounds bounds;
  bounds.available = true;
  bounds.right = 380;
  bounds.bottom = 340;
  loka::dsl::SnapRecord record;
  AdvanceToCompletion(scenario,
                      game,
                      scene,
                      bounds,
                      record,
                      loka::scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY);
  LOKA_VERIFY(scenario.publishVerdict(record));

  VerifyRecordString(record, "test", "FloppyBird");
  VerifyRecordString(record, "step", "fixed-step-flaps");
  VerifyRecordString(record, "node", "FloppyBird.Surface");
  VerifyRecordString(record, "fixed_step_seconds", "1/60");
  VerifyRecordString(record, "flap_ticks", "2,40,78,116,154");
  VerifyRecordString(record, "game_over_tick", "213");
  VerifyRecordString(record, "game_over_hold_ticks", "60");
  VerifyRecordString(record,
                     "surface.rects",
                     "130,0,24,39;130,111,24,129;303,0,24,81;303,153,24,87;72,233,18,14");
  LOKA_VERIFY(game.gameState() == loka_floppy_bird::GAME_DEAD);
  LOKA_VERIFY(game.score() == 1);
  loka::app::scene::Scene *checkedScene = 0;
  loka::dsl::FlowError textError;
  LOKA_VERIFY(loka::dsl::testing::CheckText("FloppyBird.Score", "Game Over - Score: 1")
                  .run(&scene, checkedScene, textError)
              == loka::dsl::FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(audit.steps.size() == 13);
  LOKA_VERIFY(audit.steps[1].name() == "flap-1");
  LOKA_VERIFY(audit.steps[4].name() == "verify-seeded-first-pipe");
  LOKA_VERIFY(audit.steps[9].name() == "verify-final-checkpoint");
  LOKA_VERIFY(audit.steps[10].name() == "verify-game-over");
  LOKA_VERIFY(audit.steps[11].name() == "verify-game-over-held");
  LOKA_VERIFY(audit.terminals.size() == 1);
  LOKA_VERIFY(audit.terminals[0] == loka::dsl::testing::SCENARIO_AUDIT_SUCCEEDED);
  LOKA_VERIFY(audit.verdicts.size() == 1);

  loka::dsl::testing::SceneTestAccess::unmount(scene);
  std::printf("testFloppyBirdFixedStepFlapsDriveSeededGame passed\n");
}

void testFloppyBirdGameModelResetReplaysSeededScenario()
{
  floppybird::GameModel game(loka::scenario_tests::FloppyBirdScenarioSeed());
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(CloneFloppyBirdRoot(game));
  LOKA_VERIFY(root.get() != 0);
  NullScenePlatformController platform;
  loka::app::scene::Scene scene(root.take());
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  loka::scenario_tests::CaptureContentBounds bounds;
  loka::dsl::SnapRecord firstRecord;
  {
    loka::scenario_tests::FloppyBirdScenario first(
        loka::scenario_tests::SCENARIO_COMPLETION_DRIVER_OWNED, 0);
    AdvanceToCompletion(first,
                        game,
                        scene,
                        bounds,
                        firstRecord,
                        loka::scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY);
  }

  loka::dsl::testing::SceneTestAccess::updateAttached(scene, false);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  game.reset(loka::scenario_tests::FloppyBirdScenarioSeed());
  loka::app::scene::Scene *checkedScene = 0;
  loka::dsl::FlowError textError;
  LOKA_VERIFY(loka::dsl::testing::CheckText("FloppyBird.Score", "Score: 0").run(&scene, checkedScene, textError)
              == loka::dsl::FLOW_STEP_SUCCEEDED);

  loka::dsl::SnapRecord secondRecord;
  {
    loka::scenario_tests::FloppyBirdScenario second(
        loka::scenario_tests::SCENARIO_COMPLETION_DRIVER_OWNED, 0);
    AdvanceToCompletion(second,
                        game,
                        scene,
                        bounds,
                        secondRecord,
                        loka::scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY);
  }

  std::string firstSurface;
  std::string secondSurface;
  LOKA_VERIFY(firstRecord.get("surface.rects", firstSurface));
  LOKA_VERIFY(secondRecord.get("surface.rects", secondSurface));
  LOKA_VERIFY(secondSurface == firstSurface);

  loka::dsl::testing::SceneTestAccess::unmount(scene);
  std::printf("testFloppyBirdGameModelResetReplaysSeededScenario passed\n");
}

void testFloppyBirdFixedStepFlapsHoldFinalSceneAndMatchAudit()
{
  const char *actualPath = "_loka_floppybird_fixed_step_flaps.audit";
  std::remove(actualPath);
  floppybird::GameModel game(loka::scenario_tests::FloppyBirdScenarioSeed());
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(CloneFloppyBirdRoot(game));
  LOKA_VERIFY(root.get() != 0);
  NullScenePlatformController platform;
  loka::app::scene::Scene scene(root.take());
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  {
    loka::platform::file::FileHandle destination;
    destination.displayPath = loka::core::String::Utf8(actualPath, std::strlen(actualPath));
    loka::dsl::testing::ScenarioAuditFile audit(destination, "fixed-step-flaps");
    assert(audit.isValid()); // loka-assert-ok: pure validity observation
    loka::scenario_tests::FloppyBirdScenario scenario(
        loka::scenario_tests::SCENARIO_COMPLETION_HOLD_FINAL_SCENE, &audit);
    loka::scenario_tests::CaptureContentBounds bounds;
    bounds.available = true;
    bounds.right = 380;
    bounds.bottom = 340;
    loka::dsl::SnapRecord record;
    AdvanceToCompletion(scenario,
                        game,
                        scene,
                        bounds,
                        record,
                        loka::scenario_tests::SCENARIO_ADVANCE_FINAL_SCENE_HELD);
    LOKA_VERIFY(scenario.step(274, &scene, game, bounds, record)
                == loka::scenario_tests::SCENARIO_ADVANCE_FINAL_SCENE_HELD);
  }

  const std::string actual = ReadBytes(actualPath);
  const std::string expectedPath =
      SourcePath("tests/scenarios/expected/floppybird/fixed-step-flaps.audit");
  const std::string expected = ReadBytes(expectedPath.c_str());
  LOKA_VERIFY(actual == expected);
  std::remove(actualPath);

  loka::dsl::testing::SceneTestAccess::unmount(scene);
  std::printf("testFloppyBirdFixedStepFlapsHoldFinalSceneAndMatchesAudit passed\n");
}

void testFloppyBirdDifferentSeedRefusesFixedCheckpointAudit()
{
  const char *actualPath = "_loka_floppybird_different_seed.audit";
  std::remove(actualPath);
  floppybird::GameModel game(0x2468ACE0UL);
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(CloneFloppyBirdRoot(game));
  LOKA_VERIFY(root.get() != 0);
  NullScenePlatformController platform;
  loka::app::scene::Scene scene(root.take());
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  {
    loka::platform::file::FileHandle destination;
    destination.displayPath = loka::core::String::Utf8(actualPath, std::strlen(actualPath));
    loka::dsl::testing::ScenarioAuditFile audit(destination, "fixed-step-flaps");
    assert(audit.isValid()); // loka-assert-ok: pure validity observation
    loka::scenario_tests::FloppyBirdScenario scenario(
        loka::scenario_tests::SCENARIO_COMPLETION_HOLD_FINAL_SCENE, &audit);
    loka::scenario_tests::CaptureContentBounds bounds;
    loka::dsl::SnapRecord record;
    for (long tick = 1; tick <= 44; ++tick)
    {
      game.advanceFrame(loka_floppy_bird::kFixedStepSeconds);
      const loka::scenario_tests::ScenarioAdvance advance = scenario.step(tick, &scene, game, bounds, record);
      if (advance != loka::scenario_tests::SCENARIO_ADVANCE_PENDING)
      {
        break;
      }
    }
    VerifyRecordString(record, "status", loka::dsl::SnapStatusError());
  }

  const std::string actual = ReadBytes(actualPath);
  const std::string expectedPath =
      SourcePath("tests/scenarios/expected/floppybird/fixed-step-flaps.audit");
  const std::string expected = ReadBytes(expectedPath.c_str());
  LOKA_VERIFY(actual != expected);
  assert(actual.find("status=failed") != std::string::npos);
  assert(actual.find("terminal status=failed") != std::string::npos);
  std::remove(actualPath);

  loka::dsl::testing::SceneTestAccess::unmount(scene);
  std::printf("testFloppyBirdDifferentSeedRefusesFixedCheckpointAudit passed\n");
}

void testFloppyBirdStandaloneFlowWritesExpectedAudit()
{
  const char *actualPath = "_loka_floppybird_standalone_flow.audit";
  std::remove(actualPath);
  {
    loka::platform::file::FileHandle auditFile;
    auditFile.displayPath = loka::core::String::Literal(actualPath);
    NullPlatformContext context;
    loka::standalone_tests::FloppyBirdStandaloneFlowAppConfig config(&context, &auditFile);
    NullApp app(&config);
    config.setApp(&app);

    AppComposition composition(&context);
    config.compose(composition);
    std::vector<AppComponent *> components = composition.build();
    LOKA_VERIFY(components.size() == 1);
    Window *window = components[0] ? components[0]->asWindow() : 0;
    LOKA_VERIFY(window != 0);
    LOKA_VERIFY(window->scene() != 0);
    for (int tick = 0; tick < 275; ++tick)
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
      SourcePath("tests/scenarios/expected/floppybird/fixed-step-flaps.audit");
  const std::string expected = ReadBytes(expectedPath.c_str());
  LOKA_VERIFY(actual == expected);
  std::remove(actualPath);
  std::printf("testFloppyBirdStandaloneFlowWritesExpectedAudit passed\n");
}

void testFloppyBirdStandaloneLoopCloseBeforeFirstIdleSucceeds()
{
  StandaloneLoopLaunchTestSupport::verifyCloseBeforeFirstIdle<
      loka::standalone_tests::FloppyBirdStandaloneFlowAppConfig>("_loka_floppybird_loop_close.audit");
}

void testFloppyBirdStandaloneDeclarationRefusalIsWindowless()
{
  WindowRootRefusalTestSupport::verifyDeclarationRefusal<loka::standalone_tests::FloppyBirdStandaloneFlowAppConfig>(
      "_loka_floppybird_declaration_refusal.audit");
}

void testFloppyBirdStandaloneMountRefusalFailsClosed()
{
  const char *auditPath = "_loka_floppybird_standalone_mount_refusal.audit";
  std::remove(auditPath);
  std::FILE *diagnostics = std::tmpfile();
  LOKA_VERIFY(diagnostics != 0);

  {
    loka::platform::file::FileHandle auditFile;
    auditFile.displayPath = loka::core::String::Literal(auditPath);
    NullPlatformContext context;
    loka::standalone_tests::FloppyBirdStandaloneFlowAppConfig config(&context, &auditFile, diagnostics);
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
                == "Loka FloppyBird standalone startup failed: "
                   "MainNode was not mounted after 5 idle ticks.\n");
  }

  LOKA_VERIFY(std::fclose(diagnostics) == 0);
  std::remove(auditPath);
  std::printf("testFloppyBirdStandaloneMountRefusalFailsClosed passed\n");
}
