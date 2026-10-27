#include "testing/scene/SceneTestFlow.hpp"
#include "TutorialScenarioTests.hpp"

#include "support/TestVerify.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/StringUTF8.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../example/Tutorial/src/Step4Node.hpp"
#include "../example/Tutorial/src/Step5Node.hpp"
#include "app/scene/Scene.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/Window.hpp"
#include "core/util/OwnedDef.hpp"
#include "platform/file/FileHandle.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/NullApp.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "scenarios/TutorialScenarios.hpp"
#include "scenarios/ObservedMainDefinition.hpp"
#include "standalone/TutorialStandaloneFlowAppConfig.hpp"
#include "support/MenuPresentationVerify.hpp"
#include "support/StandaloneMountTestSupport.hpp"
#include "testing/scene/ScenarioAudit.hpp"

namespace
{
  class RecordingTutorialAudit : public loka::dsl::testing::ScenarioAuditSink
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
    const std::string unixMarker = "/tests/TutorialScenarioTests.cpp";
    const std::string windowsMarker = "\\tests\\TutorialScenarioTests.cpp";
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

  void VerifyRecordInt(const loka::dsl::SnapRecord &record, const char *key, long expected)
  {
    long actual = -1;
    const bool found = record.getInt(key, actual);
    LOKA_VERIFY(found);
    LOKA_VERIFY(actual == expected);
  }

  loka::app::scene::NodeDefinitionBase *CloneTutorialRoot()
  {
    tutorial::Step4Node::PropsType props;
    loka::app::scene::BoundaryDefinition<tutorial::Step4Node::PropsType, tutorial::Step4Node> definition(props);
    return definition.clone();
  }

  void AdvanceToCompletion(loka::scenario_tests::TutorialScenario &scenario,
                           loka::app::scene::Scene &scene,
                           loka::scenario_tests::CaptureContentBounds &bounds,
                           loka::dsl::SnapRecord &record,
                           loka::scenario_tests::ScenarioAdvance expected)
  {
    LOKA_VERIFY(scenario.step(2, &scene, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
    LOKA_VERIFY(scenario.step(32, &scene, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
    LOKA_VERIFY(scenario.step(62, &scene, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
    LOKA_VERIFY(scenario.step(92, &scene, bounds, record) == loka::scenario_tests::SCENARIO_ADVANCE_PENDING);
    LOKA_VERIFY(scenario.step(122, &scene, bounds, record) == expected);
  }
} // namespace

void testTutorialIncrementSummaryToggleDrivesUiCommands()
{
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(CloneTutorialRoot());
  LOKA_VERIFY(root.get() != 0);
  NullScenePlatformController platform;
  loka::app::scene::Scene scene(root.take());
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  RecordingTutorialAudit audit;
  loka::scenario_tests::TutorialScenario scenario(loka::scenario_tests::SCENARIO_COMPLETION_DRIVER_OWNED, &audit);
  loka::scenario_tests::CaptureContentBounds bounds;
  bounds.available = true;
  bounds.right = 360;
  bounds.bottom = 280;
  loka::dsl::SnapRecord record;
  AdvanceToCompletion(
      scenario, scene, bounds, record, loka::scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY);
  LOKA_VERIFY(scenario.publishVerdict(record));

  std::string value;
  LOKA_VERIFY(record.get("test", value) && value == "Tutorial");
  LOKA_VERIFY(record.get("step", value) && value == "increment-summary-toggle");
  LOKA_VERIFY(record.get("node", value) && value == "Tutorial.Step4.Summary");
  LOKA_VERIFY(record.get("text.value", value) && value == "Items: 2");
  LOKA_VERIFY(record.get("summary_toggle_round_trip", value) && value == "true");
  VerifyRecordInt(record, "increment_count", 2);
  VerifyRecordInt(record, "tick", 122);
  VerifyRecordInt(record, "crop_right", 360);
  VerifyRecordInt(record, "crop_bottom", 280);
  LOKA_VERIFY(audit.steps.size() == 10);
  LOKA_VERIFY(audit.steps[1].name() == "increment-to-one");
  LOKA_VERIFY(audit.steps[3].name() == "increment-to-two");
  LOKA_VERIFY(audit.steps[5].name() == "hide-summary");
  LOKA_VERIFY(audit.steps[6].name() == "verify-summary-hidden");
  LOKA_VERIFY(audit.steps[7].name() == "show-summary");
  LOKA_VERIFY(audit.terminals.size() == 1);
  LOKA_VERIFY(audit.terminals[0] == loka::dsl::testing::SCENARIO_AUDIT_SUCCEEDED);
  LOKA_VERIFY(audit.verdicts.size() == 1);

  loka::dsl::testing::SceneTestAccess::unmount(scene);
  std::printf("testTutorialIncrementSummaryToggleDrivesUiCommands passed\n");
}

void testTutorialIncrementSummaryToggleHoldsFinalSceneAndMatchesAudit()
{
  const char *actualPath = "_loka_tutorial_increment_summary_toggle.audit";
  std::remove(actualPath);
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(CloneTutorialRoot());
  LOKA_VERIFY(root.get() != 0);
  NullScenePlatformController platform;
  loka::app::scene::Scene scene(root.take());
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  {
    loka::platform::file::FileHandle destination;
    destination.displayPath = loka::core::String::Utf8(actualPath, std::strlen(actualPath));
    loka::dsl::testing::ScenarioAuditFile audit(destination, "increment-summary-toggle");
    const bool auditValid = audit.isValid();
    LOKA_VERIFY(auditValid);
    loka::scenario_tests::TutorialScenario scenario(loka::scenario_tests::SCENARIO_COMPLETION_HOLD_FINAL_SCENE,
                                                     &audit);
    loka::scenario_tests::CaptureContentBounds bounds;
    bounds.available = true;
    bounds.right = 360;
    bounds.bottom = 280;
    loka::dsl::SnapRecord record;
    AdvanceToCompletion(scenario, scene, bounds, record, loka::scenario_tests::SCENARIO_ADVANCE_FINAL_SCENE_HELD);
    LOKA_VERIFY(scenario.step(123, &scene, bounds, record)
                == loka::scenario_tests::SCENARIO_ADVANCE_FINAL_SCENE_HELD);
  }

  const std::string actual = ReadBytes(actualPath);
  const std::string expectedPath =
      SourcePath("tests/scenarios/expected/tutorial/increment-summary-toggle.audit");
  const std::string expected = ReadBytes(expectedPath.c_str());
  LOKA_VERIFY(actual == expected);
  std::remove(actualPath);

  loka::dsl::testing::SceneTestAccess::unmount(scene);
  std::printf("testTutorialIncrementSummaryToggleHoldsFinalSceneAndMatchesAudit passed\n");
}

void testTutorialStandaloneMenuUsesExampleDeclaration()
{
  const char *auditPath = "_loka_tutorial_standalone_menu.audit";
  std::remove(auditPath);
  {
    loka::platform::file::FileHandle auditFile;
    auditFile.displayPath = loka::core::String::Literal(auditPath);
    loka::standalone_tests::TutorialStandaloneFlowAppConfig standalone(0, &auditFile);
    loka::app::MenuBarDefinition expectedMenu;
    loka::app::MenuComposition expectedComposition(&expectedMenu);
    tutorial::DeclareTutorialMenu(expectedComposition);
    expectedComposition.finish();
    loka::app::MenuBarDefinition standaloneMenu;
    loka::testing::ComposeMenuBar(standalone, standaloneMenu);

    LOKA_VERIFY(!expectedMenu.empty());
    LOKA_VERIFY(loka::testing::MenuPresentationsEqual(expectedMenu, standaloneMenu));
  }
  std::remove(auditPath);
  std::printf("testTutorialStandaloneMenuUsesExampleDeclaration passed\n");
}

void testTutorialStandaloneFlowWritesExpectedAudit()
{
  const char *actualPath = "_loka_tutorial_standalone_flow.audit";
  std::remove(actualPath);
  {
    loka::platform::file::FileHandle auditFile;
    auditFile.displayPath = loka::core::String::Literal(actualPath);
    NullPlatformContext context;
    loka::standalone_tests::TutorialStandaloneFlowAppConfig config(&context, &auditFile);
    NullApp app(&config);
    config.setApp(&app);

    AppComposition composition(&context);
    config.compose(composition);
    std::vector<AppComponent *> components = composition.build();
    LOKA_VERIFY(components.size() == 1);
    Window *window = components[0] ? components[0]->asWindow() : 0;
    LOKA_VERIFY(window != 0);
    LOKA_VERIFY(window->scene() != 0);
    for (int tick = 0; tick < 125; ++tick)
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
      SourcePath("tests/scenarios/expected/tutorial/increment-summary-toggle.audit");
  const std::string expected = ReadBytes(expectedPath.c_str());
  LOKA_VERIFY(actual == expected);
  std::remove(actualPath);
  std::printf("testTutorialStandaloneFlowWritesExpectedAudit passed\n");
}

void testTutorialStandaloneMountRefusalFailsClosed()
{
  const char *auditPath = "_loka_tutorial_standalone_mount_refusal.audit";
  std::remove(auditPath);
  std::FILE *diagnostics = std::tmpfile();
  LOKA_VERIFY(diagnostics != 0);

  {
    loka::platform::file::FileHandle auditFile;
    auditFile.displayPath = loka::core::String::Literal(auditPath);
    loka::testing::StandaloneMountTestPlatformContext context;
    loka::standalone_tests::TutorialStandaloneFlowAppConfig config(&context, &auditFile, diagnostics);
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
                == "Loka Tutorial standalone startup failed: "
                   "MainNode was not mounted after 5 idle ticks.\n");

    for (std::size_t i = 0; i < components.size(); ++i)
    {
      delete components[i];
    }
  }

  LOKA_VERIFY(std::fclose(diagnostics) == 0);
  std::remove(auditPath);
  std::printf("testTutorialStandaloneMountRefusalFailsClosed passed\n");
}

namespace
{
  template <class T> T *TutorialNode(loka::app::scene::Scene &scene, const char *id)
  {
    long matches = 0;
    long typed = 0;
    T *node = 0;
    loka::dsl::testing::scene_test_detail::findNodeByIdRecursive<T>(
        loka::dsl::testing::SceneTestAccess::rootNode(scene), id, matches, typed, node);
    LOKA_VERIFY(matches == 1 && typed == 1 && node != 0);
    return node;
  }

  void PressTutorialButton(loka::app::scene::Scene &scene, const char *id)
  {
    loka::app::ButtonNode *button = TutorialNode<loka::app::ButtonNode>(scene, id);
    {
      loka::core::StateTrackerGuard guard(loka::dsl::testing::SceneTestAccess::rootBoundary(scene)->tracker());
      button->props.onClick_->emit();
    }
    scene.flushInvalidation();
    scene.flushInvalidation();
  }

  void VerifyTutorialRows(loka::app::scene::Node *node, int &rows)
  {
    if (!node)
    {
      return;
    }
    loka::app::TextNode *text = node->asTextNode();
    if (text && text->props.text_)
    {
      std::string value;
      LOKA_VERIFY(loka::platform::CollectUtf8(text->props.text_->get(), value));
      if (value.compare(0, 5, "Item ") == 0)
      {
        ++rows;
        std::string expected;
        LOKA_VERIFY(loka::platform::CollectUtf8(
            loka::core::String::Literal("Item ") + loka::core::String::FromInt(rows), expected));
        LOKA_VERIFY(value == expected);
      }
    }
    loka::app::scene::INestable *nestable = node->asNestable();
    if (nestable)
    {
      for (loka::app::scene::Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
      {
        VerifyTutorialRows(child, rows);
      }
    }
  }

  void VerifyTutorialContents(loka::app::scene::Scene &scene,
                              int expectedRows,
                              const char *summary,
                              const char *summaryId = "Tutorial.Step4.Summary")
  {
    int rows = 0;
    VerifyTutorialRows(loka::dsl::testing::SceneTestAccess::rootNode(scene), rows);
    LOKA_VERIFY(rows == expectedRows);
    long matches = 0;
    long typed = 0;
    loka::app::TextNode *text = 0;
    loka::dsl::testing::scene_test_detail::findNodeByIdRecursive<loka::app::TextNode>(
        loka::dsl::testing::SceneTestAccess::rootNode(scene), summaryId, matches, typed, text);
    if (summary)
    {
      LOKA_VERIFY(matches == 1 && typed == 1 && text != 0);
      LOKA_VERIFY(text->props.text_->get().equals(loka::core::String::Literal(summary)));
    }
    else
    {
      LOKA_VERIFY(matches == 0 && text == 0);
    }
  }
} // namespace

void testTutorialListCapacityAndVisibility()
{
  NullScenePlatformController platform;
  loka::app::scene::Scene scene(CloneTutorialRoot());
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  VerifyTutorialContents(scene, 0, "Items: 0");
  for (int count = 1; count <= 100; ++count)
  {
    loka::app::ButtonNode *add = TutorialNode<loka::app::ButtonNode>(scene, "Tutorial.Step4.AddItemButton");
    LOKA_VERIFY(add->props.enabled_ && add->props.enabled_->get());
    PressTutorialButton(scene, "Tutorial.Step4.AddItemButton");
    std::string summary;
    LOKA_VERIFY(loka::platform::CollectUtf8(loka::core::String::Literal("Items: ") + loka::core::String::FromInt(count)
                                                + loka::core::String::Literal(count == 100 ? " (full)" : ""),
                                            summary));
    VerifyTutorialContents(scene, count, summary.c_str());
    if (count == 2 || count == 100)
    {
      PressTutorialButton(scene, "Tutorial.Step4.ToggleSummaryButton");
      VerifyTutorialContents(scene, 0, 0);
      PressTutorialButton(scene, "Tutorial.Step4.ToggleSummaryButton");
      VerifyTutorialContents(scene, count, summary.c_str());
    }
  }
  loka::app::ButtonNode *add = TutorialNode<loka::app::ButtonNode>(scene, "Tutorial.Step4.AddItemButton");
  LOKA_VERIFY(add->props.enabled_ && !add->props.enabled_->get());
  // Inject the event even though the UI is disabled: the action must guard capacity too.
  PressTutorialButton(scene, "Tutorial.Step4.AddItemButton");
  VerifyTutorialContents(scene, 100, "Items: 100 (full)");
  LOKA_VERIFY(!add->props.enabled_->get());
  loka::dsl::testing::SceneTestAccess::unmount(scene);
  std::printf("testTutorialListCapacityAndVisibility passed\n");
}

void testTutorialScrollListCapacityAndVisibility()
{
  NullScenePlatformController platform;
  loka::app::scene::Scene scene(loka::app::scene::Boundary<tutorial::Step5Node>().clone());
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  VerifyTutorialContents(scene, 0, "Items: 0", "Tutorial.Step5.Summary");
  for (int count = 1; count <= 100; ++count)
  {
    loka::app::ButtonNode *add = TutorialNode<loka::app::ButtonNode>(scene, "Tutorial.Step5.AddItemButton");
    LOKA_VERIFY(add->props.enabled_ && add->props.enabled_->get());
    PressTutorialButton(scene, "Tutorial.Step5.AddItemButton");
    std::string summary;
    LOKA_VERIFY(loka::platform::CollectUtf8(loka::core::String::Literal("Items: ") + loka::core::String::FromInt(count)
                                                + loka::core::String::Literal(count == 100 ? " (full)" : ""),
                                            summary));
    VerifyTutorialContents(scene, count, summary.c_str(), "Tutorial.Step5.Summary");
    if (count == 2 || count == 100)
    {
      PressTutorialButton(scene, "Tutorial.Step5.ToggleSummaryButton");
      VerifyTutorialContents(scene, 0, 0, "Tutorial.Step5.Summary");
      PressTutorialButton(scene, "Tutorial.Step5.ToggleSummaryButton");
      VerifyTutorialContents(scene, count, summary.c_str(), "Tutorial.Step5.Summary");
    }
  }
  loka::app::ButtonNode *add = TutorialNode<loka::app::ButtonNode>(scene, "Tutorial.Step5.AddItemButton");
  LOKA_VERIFY(add->props.enabled_ && !add->props.enabled_->get());
  // Inject the event even though the UI is disabled: the action must guard capacity too.
  PressTutorialButton(scene, "Tutorial.Step5.AddItemButton");
  VerifyTutorialContents(scene, 100, "Items: 100 (full)", "Tutorial.Step5.Summary");
  LOKA_VERIFY(!add->props.enabled_->get());
  loka::app::ScrollViewNode *scroll =
      TutorialNode<loka::app::scene::Node>(scene, "Tutorial.Step5.ScrollView")->asScrollViewNode();
  LOKA_VERIFY(scroll != 0);
  int rows = 0;
  VerifyTutorialRows(scroll, rows);
  LOKA_VERIFY(rows == 100);
  LOKA_VERIFY(scroll->props.viewport_.isValid());
  // Null's heightless layout reports the full content extent, as in ScrollViewScopeTests.
  loka::app::scene::LayoutState layout;
  layout.width = 360;
  layout.lineHeight = 10;
  int extent = 0;
  {
    loka::core::StateTrackerGuard guard(loka::dsl::testing::SceneTestAccess::rootBoundary(scene)->tracker());
    extent = platform.projectLayoutForTesting(scroll, layout);
  }
  LOKA_VERIFY(extent >= 100 * layout.lineHeight);
  LOKA_VERIFY(platform.scrollViewShortRangeRefusalCount() == 0);
  layout.height = 280;
  {
    loka::core::StateTrackerGuard guard(loka::dsl::testing::SceneTestAccess::rootBoundary(scene)->tracker());
    platform.projectLayoutForTesting(loka::dsl::testing::SceneTestAccess::rootNode(scene), layout);
  }
  const loka::core::Frame viewport = scroll->props.viewport_.state()->get();
  LOKA_VERIFY(viewport.height > 0 && viewport.height < 280);
  LOKA_VERIFY(extent > viewport.height);
  // Simulate the rail's scroll publication, then hide and restore the whole branch.
  scroll->publishOrigin(0, extent - viewport.height);
  scene.flushInvalidation();
  LOKA_VERIFY(scroll->requestedOrigin().y == extent - viewport.height);
  PressTutorialButton(scene, "Tutorial.Step5.ToggleSummaryButton");
  VerifyTutorialContents(scene, 0, 0, "Tutorial.Step5.Summary");
  long matches = 0;
  long typed = 0;
  loka::app::scene::Node *hiddenScroll = 0;
  loka::dsl::testing::scene_test_detail::findNodeByIdRecursive<loka::app::scene::Node>(
      loka::dsl::testing::SceneTestAccess::rootNode(scene), "Tutorial.Step5.ScrollView", matches, typed, hiddenScroll);
  LOKA_VERIFY(matches == 0 && hiddenScroll == 0);
  PressTutorialButton(scene, "Tutorial.Step5.ToggleSummaryButton");
  VerifyTutorialContents(scene, 100, "Items: 100 (full)", "Tutorial.Step5.Summary");
  scroll = TutorialNode<loka::app::scene::Node>(scene, "Tutorial.Step5.ScrollView")->asScrollViewNode();
  rows = 0;
  VerifyTutorialRows(scroll, rows);
  LOKA_VERIFY(rows == 100);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
  std::printf("testTutorialScrollListCapacityAndVisibility passed\n");
}
