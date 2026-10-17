#include "testing/scene/SceneTestFlow.hpp"
#include "HelloWorldScenarioTests.hpp"

#include "support/TestVerify.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../example/HelloWorld/src/MainNode.hpp"
#include "../example/HelloWorld/src/ProductionAppConfig.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/Window.hpp"
#include "app/scene/Scene.hpp"
#include "core/util/OwnedDef.hpp"
#include "platform/null/NullApp.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/null/NullWindow.hpp"
#include "scenarios/HelloWorldProductionFrame.hpp"
#include "scenarios/ObservedMainDefinition.hpp"
#include "scenarios/VehiclePresentationVerify.hpp"
#include "scenarios/HelloWorldScenarioPresentation.hpp"
#include "scenarios/HelloWorldScenarios.hpp"
#include "scenarios/SceneScenarioDriver.hpp"
#include "standalone/HelloWorldStandaloneFlowAppConfig.hpp"
#include "support/MenuPresentationVerify.hpp"
#include "support/StandaloneMountTestSupport.hpp"
#include "testing/scene/ScenarioAudit.hpp"

namespace
{
  class HelloWorldScenarioPlatform : public loka::app::scene::IPlatformController
  {
  public:
    virtual void onChange(loka::app::scene::Node *, loka::app::scene::NodeDirtyFlags, bool) {}

    virtual void synchronize() {}

    virtual bool hasPendingSync() const
    {
      return false;
    }

    virtual void destroy() {}
  };

  class RecordingHelloWorldAudit : public loka::dsl::testing::ScenarioAuditSink
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

  void VerifyRecordInt(const loka::dsl::SnapRecord &record, const char *key, long expected)
  {
    long actual = -1;
    const bool available = record.getInt(key, actual);
    LOKA_VERIFY(available);
    LOKA_VERIFY(actual == expected);
  }
  void verifyHelloWorldSceneMenus(NullPlatformContext *context,
                                 AppConfigurable &example,
                                 AppConfigurable &standalone)
  {
    AppComposition exampleComposition(context), standaloneComposition(context);
    example.compose(exampleComposition);
    standalone.compose(standaloneComposition);
    std::vector<AppComponent *> exampleComponents = exampleComposition.build();
    std::vector<AppComponent *> standaloneComponents = standaloneComposition.build();
    LOKA_VERIFY(exampleComponents.size() == 1 && standaloneComponents.size() == 1);
    Window *exampleWindow = exampleComponents[0]->asWindow();
    Window *standaloneWindow = standaloneComponents[0]->asWindow();
    LOKA_VERIFY(exampleWindow && standaloneWindow);
    const loka::app::MenuBarDefinition *exampleMenu = exampleWindow->scene()->menuBar();
    const loka::app::MenuBarDefinition *standaloneMenu = standaloneWindow->scene()->menuBar();
    LOKA_VERIFY(exampleMenu && standaloneMenu && !exampleMenu->empty());
    LOKA_VERIFY(loka::testing::MenuPresentationsEqual(*exampleMenu, *standaloneMenu));
    // Startup seeds must reach both Scene owners, not only their configs.
    exampleMenu->menuAt(4)->itemsHead()->onClickState->emit();
    standaloneMenu->menuAt(4)->itemsHead()->onClickState->emit();
    LOKA_VERIFY(loka::testing::MenuPresentationsEqual(*exampleMenu, *standaloneMenu));
    delete exampleWindow;
    delete standaloneWindow;
  }
} // namespace

void testHelloWorldVehiclePresentationUsesExampleDeclaration()
{
  NullPlatformContext context;
  const HelloWorldMenuSeed menuSeed = HelloWorldMenuSeed::FromWallClock(1234567);
  HelloWorldProductionAppConfig production(&context, menuSeed);
  loka::scenario_tests::HelloWorldScenarioPresentation vehicle(&context, menuSeed);
  loka::scenario_tests::VerifyVehiclePresentation(&context, production, vehicle, true);
  verifyHelloWorldSceneMenus(&context, production, vehicle);
  std::printf("testHelloWorldVehiclePresentationUsesExampleDeclaration passed\n");
}

void testHelloWorldToggleActionProbeDrivesOwnerCommands()
{
  helloworld::MainProps props;
  loka::app::scene::BoundaryDefinition<helloworld::MainProps, helloworld::MainNode> definition(props);
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(definition.clone());
  LOKA_VERIFY(root.get() != 0);
  HelloWorldScenarioPlatform platform;
  loka::app::scene::Scene scene(root.take());
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  RecordingHelloWorldAudit audit;
  loka::scenario_tests::HelloWorldScenario scenario(loka::scenario_tests::SCENARIO_COMPLETION_DRIVER_OWNED, &audit);
  loka::scenario_tests::CaptureContentBounds bounds;
  bounds.available = true;
  const loka::scenario_tests::HelloWorldProductionFrame productionFrame;
  bounds.right = productionFrame.width();
  bounds.bottom = productionFrame.height();
  loka::dsl::SnapRecord record;

  LOKA_VERIFY(scenario.step(1, &scene, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
  LOKA_VERIFY(scenario.step(2, &scene, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
  LOKA_VERIFY(scenario.step(32, &scene, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
  LOKA_VERIFY(scenario.step(62, &scene, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
  LOKA_VERIFY(scenario.step(92, &scene, bounds, record)
              == loka::scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY);
  LOKA_VERIFY(scenario.publishVerdict(record));
  LOKA_VERIFY(scenario.step(93, &scene, bounds, record)
              == loka::scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY);

  std::string value;
  LOKA_VERIFY(record.get("test", value) && value == "HelloWorld");
  LOKA_VERIFY(record.get("step", value) && value == "toggle-action-probe");
  LOKA_VERIFY(record.get("node", value) && value == "HelloWorld.LeftPanel.ActionSummary");
  LOKA_VERIFY(record.get("text.value", value) && value == "Enabled: no / clicks: 1");
  LOKA_VERIFY(record.get("disabled_probe_ignored", value) && value == "true");
  VerifyRecordInt(record, "tick", 92);
  VerifyRecordInt(record, "crop_left", 0);
  VerifyRecordInt(record, "crop_top", 0);
  VerifyRecordInt(record, "crop_right", 420);
  VerifyRecordInt(record, "crop_bottom", 330);
  LOKA_VERIFY(audit.steps.size() == 8);
  LOKA_VERIFY(audit.steps[1].name() == "probe-enabled-action");
  LOKA_VERIFY(audit.steps[3].name() == "toggle-probe-disabled");
  LOKA_VERIFY(audit.steps[5].name() == "probe-disabled-action");
  LOKA_VERIFY(audit.terminals.size() == 1);
  LOKA_VERIFY(audit.terminals[0] == loka::dsl::testing::SCENARIO_AUDIT_SUCCEEDED);
  LOKA_VERIFY(audit.verdicts.size() == 1);

  loka::dsl::testing::SceneTestAccess::unmount(scene);
  std::printf("testHelloWorldToggleActionProbeDrivesOwnerCommands passed\n");
}

void testHelloWorldToggleActionProbeHoldsFinalScene()
{
  helloworld::MainProps props;
  loka::app::scene::BoundaryDefinition<helloworld::MainProps, helloworld::MainNode> definition(props);
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(definition.clone());
  LOKA_VERIFY(root.get() != 0);
  HelloWorldScenarioPlatform platform;
  loka::app::scene::Scene scene(root.take());
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  RecordingHelloWorldAudit audit;
  loka::scenario_tests::HelloWorldScenario scenario(loka::scenario_tests::SCENARIO_COMPLETION_HOLD_FINAL_SCENE, &audit);
  loka::scenario_tests::CaptureContentBounds bounds;
  bounds.available = true;
  const loka::scenario_tests::HelloWorldProductionFrame productionFrame;
  bounds.right = productionFrame.width();
  bounds.bottom = productionFrame.height();
  loka::dsl::SnapRecord record;

  LOKA_VERIFY(scenario.step(2, &scene, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
  LOKA_VERIFY(scenario.step(32, &scene, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
  LOKA_VERIFY(scenario.step(62, &scene, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
  LOKA_VERIFY(scenario.step(92, &scene, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_FINAL_SCENE_HELD);
  LOKA_VERIFY(scenario.step(93, &scene, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_FINAL_SCENE_HELD);

  std::string value;
  LOKA_VERIFY(record.get("text.value", value) && value == "Enabled: no / clicks: 1");
  LOKA_VERIFY(audit.terminals.size() == 1);
  LOKA_VERIFY(audit.terminals[0] == loka::dsl::testing::SCENARIO_AUDIT_SUCCEEDED);
  LOKA_VERIFY(audit.verdicts.size() == 1);

  loka::dsl::testing::SceneTestAccess::unmount(scene);
  std::printf("testHelloWorldToggleActionProbeHoldsFinalScene passed\n");
}

void testHelloWorldBmiRoundtripDrivesEditTextInput()
{
  helloworld::MainProps props;
  loka::app::scene::BoundaryDefinition<helloworld::MainProps, helloworld::MainNode> definition(props);
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(definition.clone());
  LOKA_VERIFY(root.get() != 0);
  NullPlatformContext context;
  WindowProps windowProps;
  windowProps.scene(new loka::app::scene::Scene(root.take()));
  NullWindow window(&context, windowProps);
  LOKA_VERIFY(window.scene() != 0);
  loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);

  RecordingHelloWorldAudit audit;
  loka::scenario_tests::SceneScenarioDriver<loka::scenario_tests::HelloWorldScenario> driver(
      false,
      loka::scenario_tests::STARTUP_EXAMPLE_HELLO_WORLD,
      "bmi-roundtrip",
      &loka::scenario_tests::MakeHelloWorldDriverErrorRecord,
      2402,
      &audit);
  loka::scenario_tests::CaptureContentBounds bounds;
  bounds.available = true;
  const loka::scenario_tests::HelloWorldProductionFrame productionFrame;
  bounds.right = productionFrame.width();
  bounds.bottom = productionFrame.height();
  loka::dsl::SnapRecord record;

  LOKA_VERIFY(driver.step(2, &window, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
  LOKA_VERIFY(driver.step(32, &window, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
  LOKA_VERIFY(driver.step(62, &window, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
  LOKA_VERIFY(driver.step(92, &window, bounds, record)
              == loka::scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY);
  LOKA_VERIFY(driver.publishVerdict(record));

  std::string value;
  LOKA_VERIFY(record.get("step", value) && value == "bmi-roundtrip");
  LOKA_VERIFY(record.get("node", value) && value == "HelloWorld.Bmi/Text[4]");
  LOKA_VERIFY(record.get("text.value", value) && value == "BMI: 25.00");
  LOKA_VERIFY(record.get("invalid_input_result", value) && value == "BMI: --");
  LOKA_VERIFY(audit.steps.size() == 8);
  LOKA_VERIFY(audit.steps[3].name() == "enter-invalid-height");
  LOKA_VERIFY(audit.steps[4].name() == "verify-invalid-input");
  LOKA_VERIFY(audit.terminals.size() == 1);
  LOKA_VERIFY(audit.terminals[0] == loka::dsl::testing::SCENARIO_AUDIT_SUCCEEDED);

  std::printf("testHelloWorldBmiRoundtripDrivesEditTextInput passed\n");
}

void testHelloWorldStandaloneMenuMatchesExample()
{
  NullPlatformContext context;
  HelloWorldAppConfig example(&context, 0x13579BDFUL);
  loka::standalone_tests::HelloWorldStandaloneFlowAppConfig standalone(&context);
  verifyHelloWorldSceneMenus(&context, example, standalone);

  std::printf("testHelloWorldStandaloneMenuMatchesExample passed\n");
}

void testHelloWorldStandaloneWindowMatchesExample()
{
  NullPlatformContext context;
  HelloWorldAppConfig example(&context, 0x13579BDFUL);
  loka::standalone_tests::HelloWorldStandaloneFlowAppConfig standalone(&context);
  AppComposition exampleComposition(&context), standaloneComposition(&context);
  example.compose(exampleComposition);
  standalone.compose(standaloneComposition);
  std::vector<AppComponent *> exampleComponents = exampleComposition.build();
  std::vector<AppComponent *> standaloneComponents = standaloneComposition.build();
  LOKA_VERIFY(exampleComponents.size() == 1 && standaloneComponents.size() == 1);
  Window *exampleWindow = exampleComponents[0]->asWindow();
  Window *standaloneWindow = standaloneComponents[0]->asWindow();
  LOKA_VERIFY(exampleWindow && standaloneWindow);
  LOKA_VERIFY(standaloneWindow->contentMargin() == exampleWindow->contentMargin());
  LOKA_VERIFY(exampleWindow->contentMargin() == loka::app::layout::LayoutGap::of(0));
  LOKA_VERIFY(standaloneWindow->frameState().get().width == exampleWindow->frameState().get().width);
  LOKA_VERIFY(standaloneWindow->frameState().get().height == exampleWindow->frameState().get().height);
  delete exampleWindow;
  delete standaloneWindow;

  std::printf("testHelloWorldStandaloneWindowMatchesExample passed\n");
}

void testHelloWorldStandaloneMountRefusalFailsClosed()
{
  const char *auditPath = "_loka_hello_standalone_mount_refusal.log";
  std::remove(auditPath);
  std::FILE *diagnostics = std::tmpfile();
  LOKA_VERIFY(diagnostics != 0);

  {
    loka::platform::file::FileHandle auditFile;
    auditFile.displayPath = loka::core::String::Literal(auditPath);
    loka::testing::StandaloneMountTestPlatformContext context;
    loka::standalone_tests::HelloWorldStandaloneFlowAppConfig config(&context, &auditFile, diagnostics);
    LOKA_VERIFY(config.exitCode() == 0);
    NullApp app(&config);
    config.setApp(&app);

    loka::scenario_tests::testing::failObservedMainDefinitionClones(1);
    AppComposition composition(&context);
    config.compose(composition);
    std::vector<AppComponent *> components = composition.build();
    loka::scenario_tests::testing::allowObservedMainDefinitionClones();

    LOKA_VERIFY(components.size() == 1);
    Window *window = components[0] ? components[0]->asWindow() : 0;
    LOKA_VERIFY(window != 0);
    LOKA_VERIFY(window->visibilityState().get());
    LOKA_VERIFY(window->displayTitleState().get().equals(
        loka::core::String::Literal(
            "Loka HelloWorld Standalone Flow - toggle-action-probe (cycle 1)")));
    LOKA_VERIFY(window->scene() == 0);
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
                == "Loka HelloWorld standalone startup failed: "
                   "MainNode was not mounted after 5 idle ticks.\n");

    for (std::size_t i = 0; i < components.size(); ++i)
    {
      delete components[i];
    }
  }

  LOKA_VERIFY(std::fclose(diagnostics) == 0);
  std::remove(auditPath);
  std::printf("testHelloWorldStandaloneMountRefusalFailsClosed passed\n");
}
