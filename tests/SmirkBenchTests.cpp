#include "SmirkBenchTests.hpp"

#include <fstream>
#include <iterator>

#include "../example/SmirkBench/src/MainNode.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/scene/Scene.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/context/NullAttributedTextContext.hpp"
#include "support/TestVerify.hpp"
#include "scenarios/SmirkBenchAttributedScenario.hpp"
#include "scenarios/SmirkBenchPlainEditorNode.hpp"
#include "scenarios/TextEditorSettleAudit.hpp"
#include "scenarios/SmirkBenchScenarioPresentation.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "testing/app/ComposableNodeTestAccess.hpp"

namespace
{
  loka::app::scene::Node *findNodeByTestId(loka::app::scene::Node *node, const char *testId)
  {
    if (!node)
    {
      return 0;
    }
    if (node->testId() == testId)
    {
      return node;
    }
    loka::app::scene::INestable *nestable = node->asNestable();
    for (loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0; child;
         child = child->nextInComposition)
    {
      loka::app::scene::Node *match = findNodeByTestId(child, testId);
      if (match)
      {
        return match;
      }
    }
    return 0;
  }

  void flushSmirkBench(loka::app::scene::Scene &scene)
  {
    if (scene.hasPendingInvalidation())
    {
      LOKA_VERIFY(scene.flushInvalidation());
    }
  }

  void drainSmirkBench(loka::app::scene::Scene &scene)
  {
    for (int attempt = 0; attempt < 4 && scene.hasPendingInvalidation(); ++attempt)
    {
      (void)scene.flushInvalidation();
    }
    assert(!scene.hasPendingInvalidation());
  }

  template <class NodeT> struct SmirkBenchFixtureFor
  {
    explicit SmirkBenchFixtureFor(bool addInitialFace = true)
        : model(640, 400, addInitialFace),
          platform(),
          scene(loka::app::scene::Boundary<NodeT>(typename NodeT::PropsType(&this->model))),
          mainNode(0)
    {
      this->scene.mount(&this->platform);
      loka::dsl::testing::SceneTestAccess::updateAttached(this->scene, true);
      this->mainNode =
          static_cast<NodeT *>(loka::dsl::testing::SceneTestAccess::rootBoundary(this->scene));
      LOKA_VERIFY(this->mainNode != 0);
    }

    ~SmirkBenchFixtureFor()
    {
      loka::dsl::testing::SceneTestAccess::unmount(this->scene);
    }

    smirkbench::SmirkModel model;
    NullScenePlatformController platform;
    loka::app::scene::Scene scene;
    NodeT *mainNode;
  };

  typedef SmirkBenchFixtureFor<smirkbench::MainNode> SmirkBenchFixture;
  typedef SmirkBenchFixtureFor<loka::scenario_tests::SmirkBenchEditorLineNode> SmirkBenchEditorFixture;

  template <class NodeT>
  loka::app::RectSurfaceNode *findSurface(SmirkBenchFixtureFor<NodeT> &fixture)
  {
    loka::app::scene::Node *node =
        findNodeByTestId(loka::dsl::testing::SceneTestAccess::rootNode(fixture.scene), "SmirkBench.Surface");
    return node ? node->asRectSurfaceNode() : 0;
  }

  loka::app::scene::LayoutState layoutState(short width, short height)
  {
    loka::app::scene::LayoutState state;
    state.width = width;
    state.height = height;
    return state;
  }

  loka::app::StackNode *findStack(SmirkBenchFixture &fixture, const char *testId)
  {
    loka::app::scene::Node *node =
        findNodeByTestId(loka::dsl::testing::SceneTestAccess::rootNode(fixture.scene), testId);
    return node ? node->asStackNode() : 0;
  }

  void verifyLandscapeRowSeats(SmirkBenchFixture &fixture)
  {
    loka::app::StackNode *row = findStack(fixture, "SmirkBench.Panels");
    LOKA_VERIFY(row != 0);
    LOKA_VERIFY(row->props.effectiveAxis() == loka::app::STACK_AXIS_ROW);
    LOKA_VERIFY(row->childrenCount() == 2);
    loka::app::scene::Node *navSeat = row->childrenHead();
    loka::app::scene::Node *surface = navSeat ? navSeat->nextInComposition : 0;
    LOKA_VERIFY(surface != 0);
    LOKA_VERIFY(surface->nextInComposition == 0);

    loka::app::layout::RowWidthConsultation widths(row->childrenHead(),
                                                   row->childrenCount(),
                                                   500,
                                                   4,
                                                   4,
                                                   row->props.rowUndeclaredWidth_,
                                                   0); // Heuristic-only probe, without a rail.
    const loka::app::layout::RowChildWidth nav = widths.next(navSeat);
    const loka::app::layout::RowChildWidth content = widths.next(surface);
    LOKA_VERIFY(nav.width() == 200);
    LOKA_VERIFY(nav.isLiveSeat());
    LOKA_VERIFY(content.width() == 296);
  }
  void verifySmirkAudit(const char *actualPath, const char *relativeExpected)
  {
    const std::string source(__FILE__);
    const std::string testsDirectory = source.substr(0, source.find_last_of("/\\"));
    const std::string expectedPath = testsDirectory + relativeExpected;
    std::ifstream actual(actualPath, std::ios::binary);
    std::ifstream expected(expectedPath.c_str(), std::ios::binary);
    LOKA_VERIFY(actual.good() && expected.good());
    const std::string actualBytes((std::istreambuf_iterator<char>(actual)), std::istreambuf_iterator<char>());
    const std::string expectedBytes((std::istreambuf_iterator<char>(expected)), std::istreambuf_iterator<char>());
    LOKA_VERIFY(actualBytes == expectedBytes);
    actual.close();
    expected.close();
    std::remove(actualPath);
  }
} // namespace

void testSmirkBenchOrientationFlipsAxesAndRetainsSurface()
{
  SmirkBenchFixture fixture;
  loka::app::scene::Node *surface =
      findNodeByTestId(loka::dsl::testing::SceneTestAccess::rootNode(fixture.scene), "SmirkBench.Surface");
  LOKA_VERIFY(surface != 0);
  LOKA_VERIFY(surface->asRectSurfaceNode() != 0);
  loka::app::StackNode *root = findStack(fixture, "SmirkBench.Panels");
  loka::app::StackNode *navPane = findStack(fixture, "SmirkBench.NavPane");
  LOKA_VERIFY(root != 0);
  LOKA_VERIFY(navPane != 0);
  LOKA_VERIFY(navPane->props.effectiveAxis() == loka::app::STACK_AXIS_COLUMN);
  verifyLandscapeRowSeats(fixture);

  // A portrait window flips both containers' axes. The Stack nodes and the
  // surface are retained (#555): only the axis props change. On the rails
  // the flip rides the resize that caused it, so this fixture asserts the
  // tree, and the seat pin below asserts the layout that follows.
  fixture.mainNode->refreshOrientationForTesting(loka::core::Frame(0, 0, 300, 500));
  flushSmirkBench(fixture.scene);
  LOKA_VERIFY(fixture.mainNode->orientationForTesting() == smirkbench::ORIENTATION_PORTRAIT);
  LOKA_VERIFY(findStack(fixture, "SmirkBench.Panels") == root);
  LOKA_VERIFY(findStack(fixture, "SmirkBench.NavPane") == navPane);
  LOKA_VERIFY(findNodeByTestId(loka::dsl::testing::SceneTestAccess::rootNode(fixture.scene), "SmirkBench.Surface")
              == surface);
  LOKA_VERIFY(root->props.effectiveAxis() == loka::app::STACK_AXIS_COLUMN);
  LOKA_VERIFY(navPane->props.effectiveAxis() == loka::app::STACK_AXIS_ROW);
  const int portraitNavClaim = loka::app::layout::preferredChildWidthForRow(root->childrenHead());
  LOKA_VERIFY(portraitNavClaim == -1);
  loka::app::scene::Node *addButtonNode =
      findNodeByTestId(loka::dsl::testing::SceneTestAccess::rootNode(fixture.scene), "SmirkBench.AddFace");
  LOKA_VERIFY(addButtonNode != 0);
  LOKA_VERIFY(addButtonNode->asButtonNode() != 0);

  // Back to landscape: same nodes, axes restored.
  fixture.mainNode->refreshOrientationForTesting(loka::core::Frame(0, 0, 500, 300));
  flushSmirkBench(fixture.scene);
  LOKA_VERIFY(findStack(fixture, "SmirkBench.Panels") == root);
  LOKA_VERIFY(findNodeByTestId(loka::dsl::testing::SceneTestAccess::rootNode(fixture.scene), "SmirkBench.Surface")
              == surface);
  LOKA_VERIFY(navPane->props.effectiveAxis() == loka::app::STACK_AXIS_COLUMN);
  verifyLandscapeRowSeats(fixture);
}

void testSmirkBenchOrientationDerivationAvoidsIdenticalWrites()
{
  SmirkBenchFixture fixture;
  fixture.mainNode->refreshOrientationForTesting(loka::core::Frame(0, 0, 300, 500));
  LOKA_VERIFY(fixture.mainNode->orientationForTesting() == smirkbench::ORIENTATION_PORTRAIT);
  LOKA_VERIFY(fixture.mainNode->consumeOrientationTrackerDirtForTesting());

  fixture.mainNode->refreshOrientationForTesting(loka::core::Frame(0, 0, 300, 500));
  LOKA_VERIFY(!fixture.mainNode->consumeOrientationTrackerDirtForTesting());

  // A square window is landscape; an unsized frame keeps the current value.
  fixture.mainNode->refreshOrientationForTesting(loka::core::Frame(0, 0, 400, 400));
  LOKA_VERIFY(fixture.mainNode->orientationForTesting() == smirkbench::ORIENTATION_LANDSCAPE);
  LOKA_VERIFY(fixture.mainNode->consumeOrientationTrackerDirtForTesting());

  fixture.mainNode->refreshOrientationForTesting(loka::core::Frame());
  LOKA_VERIFY(fixture.mainNode->orientationForTesting() == smirkbench::ORIENTATION_LANDSCAPE);
  LOKA_VERIFY(!fixture.mainNode->consumeOrientationTrackerDirtForTesting());
}

void testSmirkBenchSurfaceExtentTracksContentSeat()
{
  SmirkBenchFixture fixture(false);
  loka::app::RectSurfaceNode *surface = findSurface(fixture);
  LOKA_VERIFY(surface != 0);
  // Landscape 500x300: the surface fills the Row seat beside the 200 nav.
  // Extent delivery triggers onChange through model bounds. Those calls must
  // reuse the explicit fixture viewport, not restore Null's initial 100x20.
  fixture.platform.projectLayoutForTesting(loka::dsl::testing::SceneTestAccess::rootNode(fixture.scene),
                                           layoutState(500, 300));
  LOKA_VERIFY(findSurface(fixture) == surface);
  LOKA_VERIFY(fixture.mainNode->surfaceExtentForTesting() == loka::core::Frame(204, 0, 296, 300));
  const bool wideFaceAdded =
      fixture.model.addFaceForTesting(loka::app::RectSprite(30000, 30000, 24, 24), 1, 1);
  LOKA_VERIFY(wideFaceAdded);
  LOKA_VERIFY(fixture.model.faceForTesting(0).x + fixture.model.faceForTesting(0).width == 296);
  LOKA_VERIFY(fixture.model.faceForTesting(0).y + fixture.model.faceForTesting(0).height == 300);

  // The same seat again delivers nothing.
  const loka::core::Frame unchangedExtent = fixture.mainNode->surfaceExtentForTesting();
  const unsigned long beforeSameProjection = fixture.platform.onChangeCallCount();
  fixture.platform.projectLayoutForTesting(loka::dsl::testing::SceneTestAccess::rootNode(fixture.scene),
                                           layoutState(500, 300));
  LOKA_VERIFY(fixture.mainNode->surfaceExtentForTesting() == unchangedExtent);
  LOKA_VERIFY(fixture.platform.onChangeCallCount() == beforeSameProjection);

  // A wider landscape window widens the seat, and the faces follow.
  fixture.platform.projectLayoutForTesting(loka::dsl::testing::SceneTestAccess::rootNode(fixture.scene),
                                           layoutState(520, 300));
  LOKA_VERIFY(fixture.mainNode->surfaceExtentForTesting() == loka::core::Frame(204, 0, 316, 300));
  const bool widerFaceAdded =
      fixture.model.addFaceForTesting(loka::app::RectSprite(30000, 30000, 24, 24), 1, 1);
  LOKA_VERIFY(widerFaceAdded);
  LOKA_VERIFY(fixture.model.faceForTesting(1).x + fixture.model.faceForTesting(1).width == 316);
  LOKA_VERIFY(fixture.model.faceForTesting(1).y + fixture.model.faceForTesting(1).height == 300);

  // Portrait 300x500: the nav row (Button height + spacing = 44) sits above,
  // the surface takes the Column's remainder; the seat delivery reclamps.
  fixture.mainNode->refreshOrientationForTesting(loka::core::Frame(0, 0, 300, 500));
  drainSmirkBench(fixture.scene);
  fixture.platform.projectLayoutForTesting(loka::dsl::testing::SceneTestAccess::rootNode(fixture.scene),
                                           layoutState(300, 500));
  drainSmirkBench(fixture.scene);
  LOKA_VERIFY(findSurface(fixture) == surface);
  LOKA_VERIFY(fixture.mainNode->surfaceExtentForTesting() == loka::core::Frame(0, 44, 300, 456));
  LOKA_VERIFY(fixture.model.faceForTesting(0).x + fixture.model.faceForTesting(0).width <= 300);
  LOKA_VERIFY(fixture.model.faceForTesting(1).x + fixture.model.faceForTesting(1).width <= 300);
  const bool portraitFaceAdded =
      fixture.model.addFaceForTesting(loka::app::RectSprite(30000, 30000, 24, 24), 1, 1);
  LOKA_VERIFY(portraitFaceAdded);
  LOKA_VERIFY(fixture.model.faceForTesting(2).x + fixture.model.faceForTesting(2).width == 300);
  LOKA_VERIFY(fixture.model.faceForTesting(2).y + fixture.model.faceForTesting(2).height == 456);
}

void testSmirkModelReflectsRefusesAndReclamps()
{
  smirkbench::SmirkModel model(100, 80, false);
  LOKA_VERIFY(model.addFaceForTesting(loka::app::RectSprite(76, 10, 24, 24), 5, 2));
  model.advanceFrame(smirkbench::kFixedStepSeconds);
  LOKA_VERIFY(model.faceForTesting(0).x <= 76);
  LOKA_VERIFY(model.faceForTesting(0).x >= 0);
  LOKA_VERIFY(model.faceForTesting(0).y >= 0);
  LOKA_VERIFY(model.faceForTesting(0).y <= 56);
  LOKA_VERIFY(model.velocityXForTesting(0) < 0);

  smirkbench::SmirkModel cappedModel(100, 80, false);
  for (int i = 0; i < loka::app::RectSurfaceModel::kMaxRects; ++i)
  {
    LOKA_VERIFY(cappedModel.addFace());
  }
  LOKA_VERIFY(!cappedModel.addFace());
  LOKA_VERIFY(cappedModel.faceCount() == loka::app::RectSurfaceModel::kMaxRects);

  model.updateBounds(30, 30);
  for (int i = 0; i < model.faceCount(); ++i)
  {
    const loka::app::RectSprite &face = model.faceForTesting(i);
    LOKA_VERIFY(face.x >= 0);
    LOKA_VERIFY(face.y >= 0);
    LOKA_VERIFY(face.x + face.width <= 30);
    LOKA_VERIFY(face.y + face.height <= 30);
  }

  SmirkBenchFixture fixture;
  loka::app::scene::Node *addButtonNode =
      findNodeByTestId(loka::dsl::testing::SceneTestAccess::rootNode(fixture.scene), "SmirkBench.AddFace");
  LOKA_VERIFY(addButtonNode != 0);
  loka::app::ButtonNode *addButton = addButtonNode->asButtonNode();
  LOKA_VERIFY(addButton != 0);
  LOKA_VERIFY(addButton->props.onClick_ != 0);
  while (fixture.model.faceCount() < loka::app::RectSurfaceModel::kMaxRects)
  {
    addButton->props.onClick_->emit();
  }
  LOKA_VERIFY(addButton->props.enabled_ != 0);
  LOKA_VERIFY(!addButton->props.enabled_->get());
}

void testSmirkBenchAttributedEditorLine()
{
  using namespace loka;
  using namespace loka::app;
  SmirkBenchFixture ordinary;
  LOKA_VERIFY(findNodeByTestId(dsl::testing::SceneTestAccess::rootNode(ordinary.scene), "SmirkBench.EditorLine") == 0);
  verifyLandscapeRowSeats(ordinary);

  SmirkBenchEditorFixture fixture;
  LOKA_VERIFY(scene::ComposableNodeTestAccess::declaredStateCount(*ordinary.mainNode) == 8);
  LOKA_VERIFY(scene::ComposableNodeTestAccess::declaredStateCount(*fixture.mainNode) == 9);
  scene::Node *root = dsl::testing::SceneTestAccess::rootNode(fixture.scene);
  scene::Node *line = findNodeByTestId(root, "SmirkBench.EditorLine");
  scene::Node *wrap = findNodeByTestId(root, "SmirkBench.WrapFixture");
  scene::Node *box = findNodeByTestId(root, "SmirkBench.WrapBox");
  LOKA_VERIFY(line && line->asAttributedTextNode());
  LOKA_VERIFY(wrap && wrap->asAttributedTextNode());
  LOKA_VERIFY(box && box->asBoxNode() && box->asBoxNode()->props.effectiveWidth() == 28);
  LOKA_VERIFY(findNodeByTestId(root, "SmirkBench.PlainText") != 0);
  LOKA_VERIFY(findSurface(fixture) != 0);
  const AttributedString initial = Styled("var x = ", Bold) + Styled("1;", Italic);
  LOKA_VERIFY(line->asAttributedTextNode()->props.text_->get() == initial);
  LOKA_VERIFY(line->asAttributedTextNode()->props.text_->get().segmentCount() == 2);
  scene::NodeContext *context = line->getContext();
  LOKA_VERIFY(context != 0);
  fixture.platform.projectLayoutForTesting(root, layoutState(636, 400));
  const NullTextMeasurement &wrapped = static_cast<NullAttributedTextContext *>(wrap->getContext())->measurement();
  LOKA_VERIFY(wrapped.width() == 24 && wrapped.height() == 36 && wrapped.lineCount() == 2);

  // The very same checkpoint code runs on all native rails.
  {
    platform::file::FileHandle auditFile;
    auditFile.displayPath = core::String::Literal("_loka_smirkbench_attributed.audit");
    dsl::testing::ScenarioAuditFile audit(auditFile, "attributed-editor-line");
    dsl::SnapRecord record;
    LOKA_VERIFY(scenario_tests::AdvanceSmirkBenchEditor(2, &fixture.scene, audit, record)
                == scenario_tests::SCENARIO_ADVANCE_PENDING);
    LOKA_VERIFY(scenario_tests::AdvanceSmirkBenchEditor(3, &fixture.scene, audit, record)
                == scenario_tests::SCENARIO_ADVANCE_PENDING);
    drainSmirkBench(fixture.scene);
    LOKA_VERIFY(findNodeByTestId(root, "SmirkBench.EditorLine") == line);
    LOKA_VERIFY(line->getContext() == context);
    LOKA_VERIFY(line->asAttributedTextNode()->props.text_->get().segmentCount() == 3);
    LOKA_VERIFY(line->asAttributedTextNode()->props.text_->get()
                == Styled("var ", Bold) + Styled("x = ", TextStyle()) + Styled("1;", Italic));
    LOKA_VERIFY(findNodeByTestId(root, "SmirkBench.WrapFixture") == wrap);
    LOKA_VERIFY(scenario_tests::AdvanceSmirkBenchEditor(4, &fixture.scene, audit, record)
                == scenario_tests::SCENARIO_ADVANCE_PENDING);
    LOKA_VERIFY(scenario_tests::AdvanceSmirkBenchEditor(5, &fixture.scene, audit, record)
                == scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY);
    std::string status;
    LOKA_VERIFY(record.get("status", status) && status == dsl::SnapStatusOk());
    dsl::testing::scenario_audit_detail::TerminalEmitter terminal(&audit);
    LOKA_VERIFY(terminal.emit(dsl::testing::SCENARIO_AUDIT_SUCCEEDED, record));
  }
  verifySmirkAudit("_loka_smirkbench_attributed.audit", "/scenarios/expected/smirkbench/attributed-editor-line.audit");

  drainSmirkBench(ordinary.scene);
  {
    platform::file::FileHandle file;
    file.displayPath = core::String::Literal("_loka_smirkbench_startup.audit");
    dsl::testing::ScenarioAuditFile audit(file, "startup");
    dsl::SnapRecord record;
    dsl::FlowError error;
    LOKA_VERIFY(dsl::testing::SnapText("SmirkBench.FaceCount", "SmirkBench", "startup", 2, 1)
                    .run(&ordinary.scene, record, error)
                == dsl::FLOW_STEP_SUCCEEDED);
    scenario_tests::SmirkBenchEditorDriver driver(true, &audit);
    LOKA_VERIFY(driver.publishVerdict(record));
  }
  verifySmirkAudit("_loka_smirkbench_startup.audit", "/scenarios/desktop-expected/smirkbench/startup.audit");

  // Missing opt-in is a hard failed verdict, not a successful empty capture.
  {
    dsl::SnapRecord record;
    std::string status;
    platform::file::FileHandle absentFile;
    absentFile.displayPath = core::String::Literal("_loka_smirkbench_absent.audit");
    dsl::testing::ScenarioAuditFile absentAudit(absentFile, "attributed-editor-line");
    LOKA_VERIFY(scenario_tests::AdvanceSmirkBenchEditor(2, &ordinary.scene, absentAudit, record)
                == scenario_tests::SCENARIO_ADVANCE_DRIVER_COMPLETION_READY);
    LOKA_VERIFY(record.get("status", status) && status == dsl::SnapStatusError());
  }
  std::remove("_loka_smirkbench_absent.audit");
  const scene::BoundaryDefinition<smirkbench::MainProps, smirkbench::MainNode> ordinaryDefinition(
      smirkbench::MainProps(&ordinary.model));
  const scene::BoundaryDefinition<scenario_tests::SmirkBenchEditorLineProps, scenario_tests::SmirkBenchEditorLineNode>
      editorDefinition(scenario_tests::SmirkBenchEditorLineProps(&fixture.model));
  LOKA_VERIFY(!editorDefinition.isCompatibleWithNode(ordinary.mainNode));
  LOKA_VERIFY(!ordinaryDefinition.isCompatibleWithNode(fixture.mainNode));
  LOKA_VERIFY(!editorDefinition.applyPropsToNode(ordinary.mainNode));
  LOKA_VERIFY(!ordinaryDefinition.applyPropsToNode(fixture.mainNode));
  LOKA_VERIFY(ordinaryDefinition.applyPropsToNode(ordinary.mainNode));
  LOKA_VERIFY(editorDefinition.applyPropsToNode(fixture.mainNode));
  LOKA_VERIFY(scene::ComposableNodeTestAccess::declaredStateCount(*ordinary.mainNode) == 8);
  LOKA_VERIFY(scene::ComposableNodeTestAccess::declaredStateCount(*fixture.mainNode) == 9);
  LOKA_VERIFY(findNodeByTestId(dsl::testing::SceneTestAccess::rootNode(ordinary.scene), "SmirkBench.EditorLine") == 0);
  LOKA_VERIFY(findNodeByTestId(root, "SmirkBench.EditorLine") == line);
  LOKA_VERIFY(line->getContext() == context);
}

void testSmirkBenchPlainEditorQueueAudit()
{
  using namespace loka;
  using namespace app;
  typedef app::testing::SettleTrace<LineCursor> Trace;
  typedef scene::Reply<LineCursor> Reply;
  SmirkBenchFixtureFor<scenario_tests::SmirkBenchPlainEditorNode> fixture;
  drainSmirkBench(fixture.scene);
  scene::Node *node = findNodeByTestId(dsl::testing::SceneTestAccess::rootNode(fixture.scene), "SmirkBench.PlainEditor");
  LOKA_VERIFY(node && node->nodeTypeKey() == scene::NodeTypeToken<TextEditorNode>());
  TextEditorNode *editor = static_cast<TextEditorNode *>(node);
  const LineCursor first(editor->props.lines_->at(0).id, 0);
  const LineCursor last(first.line, 2);
  Trace &trace = Trace::instance();
  // Both request histories: a command trace left by an earlier test in the
  // same process would otherwise merge into this audit.
  app::testing::SettleTraceCapture<LineCursor>::clear();
  LOKA_VERIFY(fixture.mainNode->queueCaretRoundTripForTesting());
  drainSmirkBench(fixture.scene);
  LOKA_VERIFY(trace.size() == 1 && trace.at(0).count == 2);
  LOKA_VERIFY(trace.at(0).takes[0].requested() == first && trace.at(0).takes[1].requested() == last);
  LOKA_VERIFY(editor->props.cursorState()->get() == last && editor->props.moveCaretTo_.state()->get().isNone());
  // The literal audit pins ordering and all serialized fields independently of the formatter.
  platform::file::FileHandle location;
  location.displayPath = core::String::Literal("_loka_queue_settle.audit");
  {
    dsl::testing::ScenarioAuditFile audit(location, "queue");
    LOKA_VERIFY(scenario_tests::RecordTextEditorSettleAudit(12, trace, audit));
    trace.clear();
    app::testing::SettleTraceRow<LineCursor> empty(scene::SETTLE_PROPS, last);
    trace.append(empty);
    LOKA_VERIFY(trace.size() == 0);
    LOKA_VERIFY(scenario_tests::RecordTextEditorSettleAudit(13, trace, audit));
    app::testing::SettleTraceRow<LineCursor> refused(scene::SETTLE_INPUT, last);
    refused.count = 1;
    refused.takes[0] = Reply::Refused(first, EDITOR_STALE_ID);
    refused.seam[0] = EDITOR_STALE_ID;
    trace.append(refused);
    LOKA_VERIFY(scenario_tests::RecordTextEditorSettleAudit(14, trace, audit));
    trace.clear();
    app::testing::SettleTraceRow<LineCursor> delta(scene::SETTLE_ATTACH, first);
    delta.after = last;
    trace.append(delta);
    LOKA_VERIFY(scenario_tests::RecordTextEditorSettleAudit(15, trace, audit));
  }
  std::ifstream input("_loka_queue_settle.audit", std::ios::binary);
  const std::string actual((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const std::string expected =
      "loka_scenario_audit version=1 scenario=queue\n"
      "settle.12.0\tstimulus=props takes=2 before=1:1:0 after=1:1:2 take0=granted seam=0 reason=0 requested=1:1:0 applied=1:1:0 take1=granted seam=0 reason=0 requested=1:1:2 applied=1:1:2\n"
      "settle.14.0\tstimulus=input takes=1 before=1:1:2 after=1:1:2 take0=refused seam=2 reason=2 requested=1:1:0 applied=none\n"
      "settle.15.0\tstimulus=attach takes=0 before=1:1:0 after=1:1:2\n";
  LOKA_VERIFY(actual == expected);
  trace.clear();
  for (unsigned i = 0; i <= Trace::CAPACITY; ++i)
  {
    app::testing::SettleTraceRow<LineCursor> delta(scene::SETTLE_ATTACH, first);
    delta.after = last;
    trace.append(delta);
  }
  {
    dsl::testing::ScenarioAuditFile audit(location, "overflow");
    LOKA_VERIFY(!scenario_tests::RecordTextEditorSettleAudit(16, trace, audit));
  }
  trace.clear();
}
