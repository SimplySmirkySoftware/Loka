#include <Script.h>
#include "ToolboxInputDoor.hpp"
#include "context/ToolboxTextContext.hpp"
#include "context/ToolboxAttributedTextContext.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/TestVerify.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/String.hpp"
#include <cstdio>
#include <cstring>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  class PaintNode;
  typedef BoundaryPropsFor<PaintNode> PaintProps;
  class PaintNode : public StdCompositionBoundaryNodeBase<PaintProps>
  {
  public:
    typedef PaintProps::TypeTag TypeTag;
    explicit PaintNode(const PaintProps &p) : StdCompositionBoundaryNodeBase<PaintProps>(p)
    {
      this->state(this->text, String::Literal("V"));
      this->state(this->enabled, true);
      this->state(this->style, TextStyle(Red));
      this->state(this->selection, 0);
      this->state(this->attributed, AttributedString(Styled("V", Bold)));
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Column() << Cell(this->text.state()).TEST_ID("cell")
                         << (Text(this->text.state()) + this->style.state()).TEST_ID("text")
                         << EditText(this->text).TEST_ID("edit")
                         << Button(this->text.state()).enabled(this->enabled.state()).TEST_ID("button")
                         << PopupMenu().selectedIndex(this->selection).enabled(this->enabled.state()).TEST_ID("popup")
                         << AttributedText(this->attributed.state()).TEST_ID("attributed"));
    }
    NodeState<TextStyle> style;
    NodeState<String> text;
    NodeState<bool> enabled;
    NodeState<int> selection;
    NodeState<AttributedString> attributed;
  };
  class RefusingUtf8 : public loka::platform::String
  {
  public:
    virtual bool appendUtf8(std::string &out) const
    {
      out.append("par");
      return false;
    }
  };
  LayoutState Seat()
  {
    LayoutState seat;
    seat.x = 10; seat.y = 40; seat.width = 80; seat.height = 24; seat.lineHeight = 16; seat.spacing = 0;
    return seat;
  }
  void ExactEmpty(ToolboxProjectedNodeContext &context, const PaintQuery &query)
  {
    const PaintAnswer answer = context.queryPaintDamage(query);
    std::printf("answer kind=%d reason=%d damage=%dx%d\n", answer.kind, answer.reason,
                answer.damage.width, answer.damage.height);
    std::fflush(stdout);
    LOKA_VERIFY(answer.kind == PAINT_ANSWER_EXACT);
    LOKA_VERIFY(answer.damage.width == 0 && answer.damage.height == 0);
  }
  void Draw(ToolboxProjectedNodeContext &context, ToolboxScenePlatformController &controller, const char *mode,
            HostControl &control, std::string &label)
  {
    if (std::strcmp(mode, "button-repaint") == 0)
      static_cast<ToolboxButtonContext &>(context).repaint(&control, label);
    else if (std::strcmp(mode, "text-repaint") == 0)
      static_cast<ToolboxTextContext &>(context).repaint();
    else if (std::strcmp(mode, "popup-repaint") == 0)
      static_cast<ToolboxPopupMenuContext &>(context).repaint();
    else
      context.render(&controller);
  }
}
int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  const char *mode = argv[1];
  const std::string id(mode, std::strcspn(mode, "-"));
  ToolboxWindow window;
  HostPortScope port(window.window());
  ToolboxScenePlatformController controller(&window);
  Scene scene((Boundary<PaintNode>(PaintProps())));
  scene.mount(&controller);
  typedef loka::dsl::testing::SceneTestAccess Access;
  Access::updateAttached(scene, true);
  PaintNode &root = *static_cast<PaintNode *>(Access::rootBoundary(scene));
  Node *node = 0;
  loka::dsl::FlowError error;
  LOKA_VERIFY(loka::dsl::testing::LookupNodeById<Node>(&scene, id.c_str(), node, error));
  ToolboxProjectedNodeContext *context = 0;
  if (id == "cell") context = new ToolboxCellContext(node->asCellNode(), &controller);
  if (id == "text") context = new ToolboxTextContext(node->asTextNode(), &controller);
  if (id == "button") context = new ToolboxButtonContext(node->asButtonNode(), &controller);
  if (id == "popup") context = new ToolboxPopupMenuContext(node->asPopupMenuNode(), &controller);
  if (id == "attributed") context = new ToolboxAttributedTextContext(node->asAttributedTextNode(), &controller);
  if (id == "edit") context = new ToolboxEditTextContext(node->asEditTextNode(), &controller);
  LOKA_VERIFY(context);
  node->setContext(context);
  if (std::strcmp(mode, "edit-fallback-frame") == 0)
  {
    ToolboxEditTextContext *edit = static_cast<ToolboxEditTextContext *>(context);
    const Rect outer = {0, 0, 20, 80};
    const Rect text = {2, 1, 19, 79};
    const Rect frameClip = {0, 0, 1, 80};
    SetRect(&controller.projectionClip, 0, 0, 40, 20);
    edit->updateData(root.text.state());
    edit->updateRect(outer, text, 1, 16);
    edit->draw(&controller);
    LOKA_VERIFY(controller.editControls_.size() == 1);
    const TEHandle te = controller.editControls_[0].te;
    LOKA_VERIFY(EqualRect(&(**te).destRect, &text));
    LOKA_VERIFY(EqualRect(&(**te).viewRect, &text));
    controller.retireEditTextControl(edit, NATIVE_HINT_EAGER_RELEASE);
    controller.projectionClip = frameClip;
    edit->updateRect(outer, text, 1, 16);
    edit->draw(&controller);
    LOKA_VERIFY(controller.editControls_.empty());
    controller.publishFixturePressOrder();
    const Point framePoint = {0, 10};
    const Point clippedPoint = {5, 10};
    LOKA_VERIFY(controller.isPointInEdit(framePoint));
    LOKA_VERIFY(!controller.isPointInEdit(clippedPoint));
    Access::unmount(scene);
    std::puts("Clipped text keeps the visible frame fallback hit");
    return 0;
  }
  if (std::strcmp(mode, "text-ink") == 0)
  {
    window.port.fgColor = greenColor;
    const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
    LayoutState seat = Seat();
    node->layout(&controller, seat);
    toolbox_host::reset();
    context->render(&controller);
    LOKA_VERIFY(toolbox_host::draws.size() == 1);
    LOKA_VERIFY(toolbox_host::draws.back().ink == redColor);
    LOKA_VERIFY(window.port.fgColor == greenColor);
    const unsigned before = toolbox_host::invalidations;
    {
      StateTrackerGuard guard(root.tracker());
      root.style.set(TextStyle());
    }
    scene.flushInvalidation();
    LOKA_VERIFY(toolbox_host::invalidations > before);
    LOKA_VERIFY(Access::lastApplyPlan(scene).hasLayoutWork());
    seat = Seat();
    node->layout(&controller, seat);
    toolbox_host::reset();
    context->render(&controller);
    LOKA_VERIFY(toolbox_host::draws.size() == 1);
    LOKA_VERIFY(toolbox_host::draws.back().ink == blackColor);
    LOKA_VERIFY(window.port.fgColor == greenColor);
    const TextStyle styles[] = {TextStyle(Blue), TextStyle(), TextStyle(Red)};
    const long inks[] = {blueColor, blackColor, redColor};
    for (unsigned i = 0; i < 3; ++i)
    {
      const TextDefinitionWithAttr replacement = Text(root.text.state()) + styles[i];
      LOKA_VERIFY(replacement.applyPropsToNode(node));
      LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
      const NodeDirtyFlags inputs = node->takeLayoutInputs();
      LOKA_VERIFY((inputs & NODE_DIRTY_LAYOUT) != 0);
      node->requeueLayoutInputs(inputs);
      const unsigned beforeStatic = toolbox_host::invalidations;
      // Retained props apply has completed; exercise the real rail completion.
      BoundaryLocalApplyInfo info;
      info.hasLayoutWork = true;
      info.paintKind = LOCAL_APPLY_PAINT_GENERIC;
      PlatformApplyPlan plan;
      plan.layoutChanged = true;
      plan.paintKind = PlatformApplyPlan::PAINT_LOCAL;
      plan.setPrimaryRoot(&root);
      controller.onBoundaryApply(&root, &root, info, plan);
      LOKA_VERIFY(toolbox_host::invalidations > beforeStatic);
      seat = Seat();
      node->layout(&controller, seat);
      toolbox_host::reset();
      context->render(&controller);
      LOKA_VERIFY(toolbox_host::draws.size() == 1);
      LOKA_VERIFY(toolbox_host::draws.back().ink == inks[i]);
      LOKA_VERIFY(window.port.fgColor == greenColor);
    }
    // Both cached wrapped lines and the capped/ellipsis path reach the ink door.
    for (unsigned modeIndex = 0; modeIndex < 2; ++modeIndex)
    {
      const BlockStyle block =
          modeIndex == 0 ? BlockStyle().wrap(TEXT_WRAP_CHAR) : BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
      const TextDefinitionWithAttr replacement = Text("MMMMMMMM") + Blue + block;
      LOKA_VERIFY(replacement.applyPropsToNode(node));
      seat = Seat();
      seat.width = 20;
      node->layout(&controller, seat);
      toolbox_host::reset();
      context->render(&controller);
      LOKA_VERIFY(!toolbox_host::draws.empty());
      if (modeIndex == 0)
        LOKA_VERIFY(toolbox_host::draws.size() > 1);
      else
        LOKA_VERIFY(toolbox_host::draws.back().bytes.find("...") != std::string::npos);
      for (unsigned i = 0; i < toolbox_host::draws.size(); ++i)
        LOKA_VERIFY(toolbox_host::draws[i].ink == blueColor);
      LOKA_VERIFY(window.port.fgColor == greenColor);
    }
    Access::unmount(scene);
    return 0;
  }
  if (std::strcmp(mode, "cell-encoding") == 0)
  {
    { StateTrackerGuard guard(root.tracker()); root.text.set(String::Literal("Open\xE2\x80\xA6 \xC3\xA9")); }
    LayoutState natural = Seat(); natural.width = 0;
    toolbox_host::reset();
    Str255 expected = {7, 'O', 'p', 'e', 'n', 0xC9, ' ', 0x8E};
    LOKA_VERIFY(context->layout(&controller, natural) == StringWidth(expected));
    context->render(&controller);
    LOKA_VERIFY(toolbox_host::pascalDraws.size() == 1);
    LOKA_VERIFY(toolbox_host::pascalDraws[0] == std::string("Open\xC9 \x8E", 7));
    Access::unmount(scene);
    std::puts("Cell encoding pin passed");
    return 0;
  }
  toolbox_host::reset();
  LayoutState seat = Seat();
  context->layout(&controller, seat);
  HostControl control;
  control.owner = window.window();
  std::string installedLabel;
  const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
  Draw(*context, controller, mode, control, installedLabel);
  ExactEmpty(*context, query);
  if (std::strcmp(mode, "edit-refused") == 0)
  {
    LOKA_VERIFY(controller.editControls_.size() == 1);
    TEHandle te = controller.editControls_[0].te;
    LOKA_VERIFY((**te).text == "V");
    {
      StateTrackerGuard guard(root.tracker());
      root.text.set(String::FromPlatform(Managed<loka::platform::String>::Wrap(new RefusingUtf8())));
    }
    Draw(*context, controller, mode, control, installedLabel);
    const PaintAnswer refused = context->queryPaintDamage(query);
    std::printf("refused conversion: TE=%s lastText=%s answer=%d reason=%d damage=%dx%d\n",
        (**te).text.c_str(), "certificate",
        refused.kind, refused.reason, refused.damage.width, refused.damage.height);
    std::fflush(stdout);
    LOKA_VERIFY((**te).text == "V" && controller.editControls_[0].installed.holds(String("V")));
    LOKA_VERIFY(refused.kind == PAINT_ANSWER_REFUSED && refused.reason == PAINT_REFUSED_HISTORY_UNKNOWN);
    // Replay also must not certify a logical value that the TE never installed.
    static_cast<ToolboxEditTextContext *>(context)->repaint(ToolboxEditPresentation(te, controller.editControls_[0].installed));
    LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
    { StateTrackerGuard guard(root.tracker()); root.text.set(String::Literal("W")); }
    Draw(*context, controller, mode, control, installedLabel);
    LOKA_VERIFY((**te).text == "W");
    ExactEmpty(*context, query);
    // A successful conversion alone is not proof: replay may precede sync.
    { StateTrackerGuard guard(root.tracker()); root.text.set(String::Literal("X")); }
    static_cast<ToolboxEditTextContext *>(context)->repaint(ToolboxEditPresentation(te, controller.editControls_[0].installed));
    LOKA_VERIFY((**te).text == "W");
    LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
    // Concatenation exercises the non-borrowing conversion path, then empty
    // text exercises the no-platform-handle representation.
    { StateTrackerGuard guard(root.tracker()); root.text.set(String::Concat(String::Literal("V"), String::Literal("W"))); }
    Draw(*context, controller, mode, control, installedLabel);
    LOKA_VERIFY((**te).text == "VW");
    ExactEmpty(*context, query);
    { StateTrackerGuard guard(root.tracker()); root.text.set(String()); }
    Draw(*context, controller, mode, control, installedLabel);
    LOKA_VERIFY((**te).text.empty());
    ExactEmpty(*context, query);
    { StateTrackerGuard guard(root.tracker()); root.text.set(String::Literal("\xE6\xBC\xA2\xE5\xAD\x97")); }
    Draw(*context, controller, mode, control, installedLabel);
    LOKA_VERIFY((**te).text == "??");
    ExactEmpty(*context, query);
    ToolboxEditTextContext *edit = static_cast<ToolboxEditTextContext *>(context);
    controller.editControls_[0].installed.revoke();
    const int sets = toolbox_host::sets;
    const std::size_t rows = controller.editControls_.size();
    edit->repaint(ToolboxEditPresentation(te, controller.editControls_[0].installed));
    LOKA_VERIFY(context->queryPaintDamage(query).reason == PAINT_REFUSED_HISTORY_UNKNOWN);
    LOKA_VERIFY(toolbox_host::sets == sets && controller.editControls_.size() == rows);
    // Repair from authoritative State records the outer chrome without any notification.
    controller.activateEditControl(0);
    { StateTrackerGuard guard(root.tracker()); root.text.set(String::Literal("caf\xC3\xA9")); }
    controller.syncEditTextFromState(controller.editControls_[0]);
    toolbox_host::systemScript = 1;
    toolbox_host::failSets = 1;
    controller.pendingDirtyRects_.clear();
    LOKA_VERIFY(ToolboxInputDoor::keyDown(controller, 'x'));
    LOKA_VERIFY(!controller.editControls_[0].installed.holds(root.text.get()));
    LOKA_VERIFY(context->queryPaintDamage(query).reason == PAINT_REFUSED_HISTORY_UNKNOWN);
    LOKA_VERIFY(!controller.pendingDirtyRects_.empty());
    LOKA_VERIFY(EqualRect(&controller.pendingDirtyRects_.back(), &edit->chromeRect()));
    toolbox_host::systemScript = 0;
    controller.syncEditTextFromState(controller.editControls_[0]);
    const Rect partialRepair = {40, 10, 45, 30};
    { ToolboxPaintClip clip(partialRepair);
      edit->repaint(ToolboxEditPresentation(te, controller.editControls_[0].installed)); }
    LOKA_VERIFY(context->queryPaintDamage(query).reason == PAINT_REFUSED_HISTORY_UNKNOWN);
    edit->repaint(ToolboxEditPresentation(te, controller.editControls_[0].installed));
    ExactEmpty(*context, query);
    // Draw fallback through the same Pascal encoding door as other leaves.
    controller.retireEditTextControl(edit, NATIVE_HINT_EAGER_RELEASE);
    toolbox_host::failNew = 1; toolbox_host::pascalDraws.clear();
    edit->draw(&controller);
    LOKA_VERIFY(toolbox_host::pascalDraws.size() == 1);
    LOKA_VERIFY(toolbox_host::pascalDraws[0] == std::string("caf\x8E", 4));
    Access::unmount(scene);
    std::puts("Refused conversion paint pin passed");
    return 0;
  }
  const Rect partial = {40, 10, 45, 30};
  std::printf("%s unchanged partial\n", mode); std::fflush(stdout);
  { ToolboxPaintClip clip(partial); Draw(*context, controller, mode, control, installedLabel); }
  ExactEmpty(*context, query);
  if (id == "attributed")
  {
    // The host controller has no render traversal. Mirror production replay's
    // layout-then-render sequence, including its unchanged layout inputs.
    std::puts("attributed unchanged replay layout"); std::fflush(stdout);
    {
      ToolboxPaintClip clip(partial);
      seat = Seat(); seat.inputs = NODE_DIRTY_NONE;
      context->layout(&controller, seat);
      context->render(&controller);
    }
    ExactEmpty(*context, query);
    const char *changes[] = {"rebuilt equal value", "moved x", "moved y", "changed width"};
    for (unsigned i = 0; i < sizeof(changes) / sizeof(changes[0]); ++i)
    {
      std::printf("attributed %s\n", changes[i]); std::fflush(stdout);
      seat = Seat(); seat.inputs = NODE_DIRTY_NONE;
      if (i == 0) seat.inputs = NODE_DIRTY_PROPS;
      if (i == 1) ++seat.x;
      if (i == 2) ++seat.y;
      if (i == 3) ++seat.width;
      {
        ToolboxPaintClip clip(partial);
        context->layout(&controller, seat);
        const PaintAnswer laidOut = context->queryPaintDamage(query);
        LOKA_VERIFY(laidOut.kind == PAINT_ANSWER_REFUSED
            && laidOut.reason == PAINT_REFUSED_HISTORY_UNKNOWN);
        context->render(&controller);
      }
      const PaintAnswer replayed = context->queryPaintDamage(query);
      LOKA_VERIFY(replayed.kind == PAINT_ANSWER_REFUSED
          && replayed.reason == PAINT_REFUSED_HISTORY_UNKNOWN);
      // Restore the same full-draw baseline for each independent negative.
      seat = Seat(); seat.inputs = NODE_DIRTY_NONE;
      context->layout(&controller, seat);
      context->render(&controller);
      ExactEmpty(*context, query);
    }
  }
  // Change the drawer's value without changing placement. AttributedText must
  // rebuild its table via layout, which conservatively revokes the old fact.
  {
    StateTrackerGuard guard(root.tracker());
    root.text.set(String::Literal("W"));
    root.selection.set(1);
    root.attributed.set(AttributedString(Styled("W", Bold)));
  }
  if (id == "attributed" || (id == "button" && std::strcmp(mode, "button") == 0))
  {
    seat = Seat(); seat.inputs = NODE_DIRTY_PROPS; context->layout(&controller, seat);
  }
  std::printf("%s changed partial\n", mode); std::fflush(stdout);
  { ToolboxPaintClip clip(partial); Draw(*context, controller, mode, control, installedLabel); }
  const PaintAnswer changed = context->queryPaintDamage(query);
  LOKA_VERIFY(changed.kind == PAINT_ANSWER_REFUSED);
  LOKA_VERIFY(changed.reason == PAINT_REFUSED_HISTORY_UNKNOWN);
  Draw(*context, controller, mode, control, installedLabel);
  ExactEmpty(*context, query);
  // Native button enabled and popup face enabled are independent value inputs.
  if (id == "button" || id == "popup")
  {
    { StateTrackerGuard guard(root.tracker()); root.enabled.set(false); }
    { ToolboxPaintClip clip(partial); Draw(*context, controller, mode, control, installedLabel); }
    const PaintAnswer disabled = context->queryPaintDamage(query);
    LOKA_VERIFY(disabled.kind == PAINT_ANSWER_REFUSED && disabled.reason == PAINT_REFUSED_HISTORY_UNKNOWN);
    Draw(*context, controller, mode, control, installedLabel);
    ExactEmpty(*context, query);
  }
  toolbox_host::failRegions = 2;
  Draw(*context, controller, mode, control, installedLabel);
  LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
  toolbox_host::failRegions = 0;
  Draw(*context, controller, mode, control, installedLabel);
  ExactEmpty(*context, query);
  Access::unmount(scene);
  std::puts("Partial paint pins passed");
}
