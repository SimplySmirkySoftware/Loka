#include "support/LayoutY.hpp"
#include "app/layout/ColumnLayout.hpp"
#include "context/ToolboxTextContext.hpp"
#include "Script.h"
#include "app/layout/RowLayout.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/TestVerify.hpp"
#include "support/LokaAllocFailure.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include <cstdio>
#include <climits>
#include "app/nodes/ImageView.hpp"
#include <cstring>
#include "support/LifecycleFactTestAccess.hpp"
#include "platform/String.hpp"

void testToolboxRowSpacing(const char *mode);

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  class WidthNode;
  typedef BoundaryPropsFor<WidthNode> WidthProps;
  class WidthNode : public StdCompositionBoundaryNodeBase<WidthProps>
  {
  public:
    typedef WidthProps::TypeTag TypeTag;
    explicit WidthNode(const WidthProps &p) : StdCompositionBoundaryNodeBase<WidthProps>(p)
    { this->state(this->title, String::Literal("Flag mode: off")); }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Column().TEST_ID("column") << Button(this->title.state()).TEST_ID("button")
          << EditText(this->title).TEST_ID("edit") << PopupMenu().TEST_ID("popup")
          << Text(this->title.state()).TEST_ID("text")
          << (Text(this->title.state()).TEST_ID("wrapped") + BlockStyle().wrap(TEXT_WRAP_WORD))
          << (Text(this->title.state()).TEST_ID("ellipsis") + BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS))
          << (Row().TEST_ID("row") << Button(this->title.state()).TEST_ID("row-button")));
    }
    NodeState<String> title;
  };
  Node *lookup(Scene &scene, const char *id)
  {
    Node *node = 0; loka::dsl::FlowError error;
    LOKA_VERIFY(loka::dsl::testing::LookupNodeById<Node>(&scene, id, node, error) == loka::dsl::FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(node != 0);
    return node;
  }
  LayoutState seat(short width)
  {
    LayoutState s; s.x = 10; s.y = 20; s.width = width; s.height = 24; s.spacing = 0;
    return s;
  }
  struct LayoutProbe
  {
    ToolboxScenePlatformController &controller;
    Node *target;
    short used;
    LayoutProbe(ToolboxScenePlatformController &c, Node *n) : controller(c), target(n), used(0) {}
    static int child(void *data, Node *node, const LayoutState &offer)
    {
      LayoutProbe &p = *static_cast<LayoutProbe *>(data);
      LayoutState s = offer;
      if (node == p.target)
      {
        p.used = node->context->layout(&p.controller, s);
        node->context->render(&p.controller);
      }
      return s.y;
    }
  };
}
namespace loka { namespace testing {
  class ToolboxTextContextAccess
  {
  public:
    static bool ready(const ToolboxTextContext &c) { return c.projection_.valid(); }
    static bool known(const ToolboxTextContext &c) { return c.presented_.isKnown(); }
  };
} }

namespace
{
  void naturalImagePins()
  {
    ToolboxWindow window;
    ToolboxScenePlatformController controller(&window);
    ToolboxScenePlatformController portless(0);
    int native = 0;
    MutableState<resource::Image> imageState(resource::Image::FromNative(&native, 640, 10, 0, 0));
    ImageViewProps props;
    props.image(&imageState).attr(ImageViewAttr().sizePolicy(IMAGE_VIEW_SIZE_INTRINSIC));
    ImageViewNode image(props);
    const int widths = toolbox_host::widths;
    const int metrics = toolbox_host::metrics;
    ToolboxScenePlatformController *controllers[] = {&controller, &portless};
    for (unsigned i = 0; i < 2; ++i)
    {
      image.props = props;
      imageState.set(resource::Image::FromNative(&native, 640, 10, 0, 0));
      short width = 123;
      LOKA_VERIFY(controllers[i]->queryNaturalWidth(&image, width));
      LOKA_VERIFY(width == 640);
      imageState.set(resource::Image::FromNative(&native, SHRT_MAX, 10, 0, 0));
      LOKA_VERIFY(controllers[i]->queryNaturalWidth(&image, width));
      LOKA_VERIFY(width == SHRT_MAX);
      for (unsigned refusal = 0; refusal < 7; ++refusal)
      {
        image.props = props;
        imageState.set(resource::Image::FromNative(&native, 640, 10, 0, 0));
        switch (refusal)
        {
        case 0: image.props.width_ = 100; break;
        case 1: image.props.attr_.sizePolicyValue_ = IMAGE_VIEW_SIZE_AUTO; break;
        case 2: image.props.attr_.sizePolicyValue_ = IMAGE_VIEW_SIZE_FILL_PARENT; break;
        case 3: imageState.set(resource::Image::Empty()); break;
        case 4: imageState.set(resource::Image::FromNative(&native, 0, 10, 0, 0)); break;
        case 5: imageState.set(resource::Image::FromNative(&native, static_cast<int>(SHRT_MAX) + 1, 10, 0, 0)); break;
        case 6: image.props.image_ = 0; break;
        }
        width = 123;
        LOKA_VERIFY(!controllers[i]->queryNaturalWidth(&image, width));
        LOKA_VERIFY(width == 123);
      }
    }
    LOKA_VERIFY(toolbox_host::widths == widths);
    LOKA_VERIFY(toolbox_host::metrics == metrics);
    std::puts("Natural image width pins passed");
  }

  class RefusingPlainString : public loka::platform::String
  {
  public:
    virtual bool appendUtf8(std::string &out) const { out += "partial"; return false; }
  };

  void plainPins(ToolboxScenePlatformController &controller, Scene &scene, WidthNode &root,
                 const char *mode)
  {
    using loka::testing::ToolboxTextContextAccess;
    const bool wraps = std::strcmp(mode, "encoding-wrap") == 0;
    const bool ellipsis = std::strcmp(mode, "encoding-ellipsis") == 0;
    Node *node = lookup(scene, wraps ? "wrapped" : ellipsis ? "ellipsis" : "text");
    ToolboxTextContext *context = new ToolboxTextContext(node->asTextNode(), &controller);
    node->setContext(context);
    const std::string sources[] = {
      "\xC0\xAF" "A", "\n\x80" "B", "\xE2\x28\xA1", "A\x80" "B",
      std::string("\0\r\n\t", 4), std::string(254, 'a') + "\xC3\xA9",
      std::string(), "\xC3\xA9"
    };
    const std::string expected[] = {
      "??A", "\n?B", "?(?", "A?B", std::string("\0\r\n\t", 4),
      std::string(254, 'a') + "\x8E", std::string(), "\x8E"
    };
    for (unsigned test = 0; test < sizeof(sources) / sizeof(sources[0]); ++test)
    {
      { StateTrackerGuard guard(root.tracker()); root.title.set(String(sources[test])); }
      LayoutState offer = seat(wraps ? 2000 : 0);
      toolbox_host::reset();
      const unsigned reads = toolbox_host::scriptReads;
      context->layout(&controller, offer);
      LOKA_VERIFY(toolbox_host::scriptReads == reads + 1);
      context->render(&controller);
      std::string drawn;
      for (std::size_t i = 0; i < toolbox_host::draws.size(); ++i)
        drawn += toolbox_host::draws[i].bytes;
      std::string wanted = expected[test];
      if (wraps)
      {
        if (test == 1) { wanted = "?B"; LOKA_VERIFY(toolbox_host::draws.size() == 2); }
        if (test == 4) { wanted = std::string("\0\t", 2); LOKA_VERIFY(toolbox_host::draws.size() == 3); }
      }
      LOKA_VERIFY(drawn == wanted);
      if (!wraps)
      {
        bool measured = false;
        for (std::size_t i = 0; i < toolbox_host::widthPayloads.size(); ++i)
          if (toolbox_host::widthPayloads[i] == wanted) measured = true;
        LOKA_VERIFY(measured);
      }
      LOKA_VERIFY(ToolboxTextContextAccess::ready(*context));
      context->repaint();
      context->visibleWidth();
      offer = seat(wraps ? 1800 : 2000);
      offer.inputs = NODE_DIRTY_NONE;
      context->layout(&controller, offer);
      context->repaint();
      LOKA_VERIFY(toolbox_host::scriptReads == reads + 1);
    }
    std::string repeated;
    for (unsigned i = 0; i < 255; ++i) repeated += "\xC3\xA9";
    { StateTrackerGuard guard(root.tracker()); root.title.set(String(repeated)); }
    LayoutState exact = seat(wraps ? 2000 : 0);
    toolbox_host::reset();
    context->layout(&controller, exact);
    context->render(&controller);
    LOKA_VERIFY(toolbox_host::draws.size() == 1);
    LOKA_VERIFY(toolbox_host::draws[0].bytes == std::string(255, static_cast<char>(0x8E)));
    repeated += "\xC3\xA9";
    { StateTrackerGuard guard(root.tracker()); root.title.set(String(repeated)); }
    LayoutState offer = seat(wraps ? 2000 : 0);
    toolbox_host::reset();
    const short width = context->layout(&controller, offer);
    context->render(&controller);
    LOKA_VERIFY(toolbox_host::draws[0].bytes == std::string(255, static_cast<char>(0x8E)));
    LOKA_VERIFY(wraps || width == 1020);
    if (wraps)
    {
      LOKA_VERIFY(toolbox_host::draws.size() == 2);
      LOKA_VERIFY(toolbox_host::draws[1].bytes == "\x8E");
      // A native start beyond signed-short range must rebase the pointer.
      { StateTrackerGuard guard(root.tracker()); root.title.set(String(std::string(33000, 'a') + "Z")); }
      offer = seat(2000);
      toolbox_host::reset();
      context->layout(&controller, offer);
      context->render(&controller);
      LOKA_VERIFY(toolbox_host::draws.size() == 130);
      LOKA_VERIFY(toolbox_host::draws.back().bytes == std::string(105, 'a') + "Z");
      bool measuredTail = false;
      for (std::size_t i = 0; i < toolbox_host::widthPayloads.size(); ++i)
        if (toolbox_host::widthPayloads[i] == std::string(105, 'a') + "Z") measuredTail = true;
      LOKA_VERIFY(measuredTail);
    }
    else if (ellipsis)
    {
      offer = seat(1016);
      toolbox_host::reset();
      context->layout(&controller, offer);
      context->render(&controller);
      LOKA_VERIFY(toolbox_host::draws[0].bytes == std::string(251, static_cast<char>(0x8E)) + "...");
      const std::string malformed[] = {"A\x80" "BCDEF", "\xC0\xAF" "BCDEF", "\xC3\xA9" "BCDEFG"};
      const std::string prefixes[] = {"A?", "??", "\x8E" "B"};
      for (unsigned i = 0; i < 3; ++i)
      {
        { StateTrackerGuard guard(root.tracker()); root.title.set(String(malformed[i])); }
        offer = seat(20);
        toolbox_host::reset();
        context->layout(&controller, offer);
        context->render(&controller);
        LOKA_VERIFY(toolbox_host::draws[0].bytes == prefixes[i] + "...");
        LOKA_VERIFY(context->visibleWidth() == 20);
      }
    }
    else
    {
      // The real context doors used by the scene's non-wrapped text-change
      // and redrawTextHit entries; the full scene controller is not host-built.
      LOKA_VERIFY(ToolboxTextContextAccess::known(*context));
      { StateTrackerGuard guard(root.tracker()); root.title.set(String::Literal("\xC3\xA9")); }
      toolbox_host::reset();
      unsigned reads = toolbox_host::scriptReads;
      LOKA_VERIFY(context->visibleWidth() == 4);
      // A successful source change keeps the paint history of the last painted value.
      LOKA_VERIFY(ToolboxTextContextAccess::known(*context));
      LOKA_VERIFY(toolbox_host::scriptReads == reads + 1);
      context->repaint();
      LOKA_VERIFY(toolbox_host::draws[0].bytes == "\x8E");
      LOKA_VERIFY(toolbox_host::scriptReads == reads + 1);
      { StateTrackerGuard guard(root.tracker()); root.title.set(String::Literal("\xE2\x80\xA6")); }
      toolbox_host::reset();
      context->repaint();
      LOKA_VERIFY(toolbox_host::draws[0].bytes == "\xC9");
      LOKA_VERIFY(toolbox_host::scriptReads == reads + 2);
      const String refused(Managed<loka::platform::String>::Wrap(new RefusingPlainString()));
      { StateTrackerGuard guard(root.tracker()); root.title.set(refused); }
      toolbox_host::reset();
      LOKA_VERIFY(context->visibleWidth() == 0);
      context->repaint();
      LOKA_VERIFY(toolbox_host::draws.empty());
      LOKA_VERIFY(!ToolboxTextContextAccess::ready(*context));
      LOKA_VERIFY(!ToolboxTextContextAccess::known(*context));
      { StateTrackerGuard guard(root.tracker()); root.title.set(String(std::string(100, 'x'))); }
      loka::core::testing::failLokaAllocRaw("TextLineBreaker", "Table", 1);
      offer = seat(1000);
      LOKA_VERIFY(context->layout(&controller, offer) == 0);
      LOKA_VERIFY(!ToolboxTextContextAccess::ready(*context));
      loka::core::testing::failLokaAllocRaw("TextLineBreaker", "Table", 1);
      context->repaint();
      LOKA_VERIFY(toolbox_host::draws.empty());
      loka::core::testing::allowLokaAllocRaw();
      // Paint-only recovery: a refused build keeps placement, so the next
      // text change repaints through the same doors without a layout pass.
      { StateTrackerGuard guard(root.tracker()); root.title.set(String::Literal("ok")); }
      toolbox_host::reset();
      context->repaint();
      LOKA_VERIFY(!toolbox_host::draws.empty() && toolbox_host::draws[0].bytes == "ok");
      offer = seat(1000);
      context->layout(&controller, offer);
      LOKA_VERIFY(ToolboxTextContextAccess::ready(*context));
      LifecycleFactTestAccess::MarkSubtreeRetired(node);
      LifecycleFactTestAccess::DeliverFacts(node);
      LOKA_VERIFY(context->visibleWidth() == 0);
      context->repaint();
      LOKA_VERIFY(!ToolboxTextContextAccess::ready(*context));
    }
    std::puts("Plain projection pins passed");
  }
}

int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  const char *mode = argv[1];
  if (std::strcmp(mode, "natural-image") == 0)
  {
    naturalImagePins();
    return 0;
  }
  if (std::strncmp(mode, "spacing-", 8) == 0)
  {
    testToolboxRowSpacing(mode);
    return 0;
  }
  ToolboxWindow window; ToolboxScenePlatformController controller(&window);
  Scene scene((Boundary<WidthNode>(WidthProps())));
  scene.mount(&controller);
  typedef loka::dsl::testing::SceneTestAccess Access;
  Access::updateAttached(scene, true);
  WidthNode *root = static_cast<WidthNode *>(Access::rootBoundary(scene));
  if (std::strcmp(mode, "encoding-plain") == 0 || std::strcmp(mode, "encoding-wrap") == 0
      || std::strcmp(mode, "encoding-ellipsis") == 0)
  {
    plainPins(controller, scene, *root, mode);
    return 0;
  }
  Node *button = lookup(scene, "button"), *edit = lookup(scene, "edit"), *popup = lookup(scene, "popup");
  Node *rowButton = lookup(scene, "row-button");
  button->setContext(new ToolboxButtonContext(button->asButtonNode(), &controller));
  edit->setContext(new ToolboxEditTextContext(edit->asEditTextNode(), &controller));
  popup->setContext(new ToolboxPopupMenuContext(popup->asPopupMenuNode(), &controller));
  rowButton->setContext(new ToolboxButtonContext(rowButton->asButtonNode(), &controller));
  // A leaf's exit is independent of the enclosing container's gap.
  Node *leaves[] = {button, edit, popup};
  for (unsigned i = 0; i != 3; ++i)
  {
    LayoutState plain = seat(80), spaced = plain;
    spaced.spacing = 9;
    const short width = leaves[i]->context->layout(&controller, plain);
    LOKA_VERIFY(leaves[i]->context->layout(&controller, spaced) == width);
    LOKA_VERIFY(spaced.y == plain.y && spaced.y > 20);
  }
  const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
  if (std::strcmp(mode, "encoding-button") == 0)
  {
    const String label = String::Literal("Open\xE2\x80\xA6 \xC3\xA9");
    const std::string expected("Open\xC9 \x8E", 7);
    HostControl control;
    control.owner = window.window();
    std::string installed;
    toolbox_host::controlTitles.clear();
    LOKA_VERIFY(ReconcileToolboxButtonControl(&control, label, 0, installed));
    LOKA_VERIFY(toolbox_host::controlTitles.size() == 1);
    LOKA_VERIFY(toolbox_host::controlTitles[0] == expected);
    LOKA_VERIFY(installed == "Open\xE2\x80\xA6 \xC3\xA9");
    LOKA_VERIFY(ReconcileToolboxButtonControl(&control, label, 0, installed));
    LOKA_VERIFY(toolbox_host::controlTitles.size() == 1);
    { StateTrackerGuard guard(root->tracker()); root->title.set(label); }
    LayoutState offer = seat(0);
    const short measured = rowButton->context->layout(&controller, offer);
    Str255 encoded = {7, 'O', 'p', 'e', 'n', 0xC9, ' ', 0x8E};
    LOKA_VERIFY(measured == StringWidth(encoded) + 16);
  }
  else if (std::strcmp(mode, "encoding-popup") == 0)
  {
    String longLabel = String::Literal("");
    for (unsigned i = 0; i < 128; ++i)
      longLabel = String::Concat(longLabel, String::Literal("\xC3\xA9"));
    longLabel = String::Concat(longLabel, String::Literal(";X"));
    loka::Vector<String> items;
    items.push_back(longLabel);
    MutableState<int> selected(0);
    ToolboxPopupMenuContext &context = *static_cast<ToolboxPopupMenuContext *>(popup->context);
    context.updateData(&items, &selected, WriteSeat<int>(), 0, 0);
    Rect face; SetRect(&face, 10, 20, 250, 44);
    context.updateRect(face, 16);
    toolbox_host::reset();
    context.draw();
    const std::string expected = std::string(128, static_cast<char>(0x8E)) + ";X";
    LOKA_VERIFY(!toolbox_host::draws.empty());
    LOKA_VERIFY(toolbox_host::draws[0].bytes == expected);
    LOKA_VERIFY(toolbox_host::pascalDraws.size() == 1);
    LOKA_VERIFY(toolbox_host::pascalDraws[0] == expected);
    toolbox_host::menuAppends.clear(); toolbox_host::menuSets.clear();
    const Point click = {25, 15};
    LOKA_VERIFY(context.handleMouseDown(click, 0));
    LOKA_VERIFY(toolbox_host::menuAppends.size() == 1);
    LOKA_VERIFY(toolbox_host::menuAppends[0] == " ");
    LOKA_VERIFY(toolbox_host::menuSets.size() == 1);
    LOKA_VERIFY(toolbox_host::menuSets[0] == expected);
    LOKA_VERIFY(toolbox_host::disposedMenuItems.size() == 1);
    LOKA_VERIFY(toolbox_host::disposedMenuItems[0] == expected);
  }
  else if (std::strcmp(mode, "encoding-text") == 0)
  {
    const String label = String::Literal("Open\xE2\x80\xA6 \xC3\xA9");
    { StateTrackerGuard guard(root->tracker()); root->title.set(label); }
    const char *ids[] = {"text", "wrapped"};
    for (unsigned i = 0; i < 2; ++i)
    {
      Node *text = lookup(scene, ids[i]);
      text->setContext(new ToolboxTextContext(text->asTextNode(), &controller));
      LayoutState offer = seat(0);
      toolbox_host::reset();
      LOKA_VERIFY(text->context->layout(&controller, offer) == TextWidth("Open\xC9 \x8E", 0, 7));
      text->context->render(&controller);
      LOKA_VERIFY(!toolbox_host::draws.empty());
      LOKA_VERIFY(toolbox_host::draws[0].bytes == "Open\xC9 \x8E");
      LOKA_VERIFY(static_cast<ToolboxTextContext *>(text->context)->visibleWidth()
          == TextWidth("Open\xC9 \x8E", 0, 7));
    }
    Node *ellipsis = lookup(scene, "ellipsis");
    ellipsis->setContext(new ToolboxTextContext(ellipsis->asTextNode(), &controller));
    LayoutState narrow = seat(32);
    toolbox_host::reset();
    ellipsis->context->layout(&controller, narrow);
    ellipsis->context->render(&controller);
    LOKA_VERIFY(toolbox_host::draws.size() == 1);
    LOKA_VERIFY(toolbox_host::draws[0].bytes == "Open\xC9 \x8E");
  }
  else if (std::strncmp(mode, "column-", 7) == 0)
  {
    Node *target = std::strcmp(mode, "column-button") == 0 ? button :
        std::strcmp(mode, "column-edit") == 0 ? edit : popup;
    LayoutProbe p(controller, target);
    layout::computeColumnLayoutResultY(lookup(scene, "column")->asStackNode(),
                                       seat(240),
                                       &p,
                                       LayoutProbe::child,
                                       0,
                                       &loka::testing::acceptShortLayoutY);
    std::printf("%s: used=%d outer=%d expected=240\n", mode, p.used,
        toolbox_host::controlRect.right - toolbox_host::controlRect.left); std::fflush(stdout);
    LOKA_VERIFY(p.used == 240);
    LOKA_VERIFY(toolbox_host::controlRect.right - toolbox_host::controlRect.left == 240);
  }
  else if (std::strcmp(mode, "natural") == 0)
  {
    Node *nodes[] = {button, edit, popup};
    const int widths[] = {controller.measureTextWidth(root->title.get()) + 16, 120, 120};
    const int insets[] = {0, 3, 8};
    for (int offer = 0; offer >= -1; --offer)
      for (int i = 0; i < 3; ++i)
      {
        LayoutState s = seat(static_cast<short>(offer));
        LOKA_VERIFY(nodes[i]->context->layout(&controller, s) == widths[i]);
        nodes[i]->context->render(&controller);
        LOKA_VERIFY(toolbox_host::controlRect.right - toolbox_host::controlRect.left == widths[i] + insets[i]);
      }
  }
  else
  {
    const bool natural = std::strcmp(mode, "natural-title") == 0;
    const bool clipped = std::strcmp(mode, "clipped-title") == 0;
    const short width = natural ? 0 : std::strcmp(mode, "equal-title") == 0
        ? static_cast<short>(controller.measureTextWidth(root->title.get()) + 16) : 240;
    if (clipped) controller.projectionClip.right = 5;
    LayoutProbe p(controller, rowButton);
    if (natural)
      LayoutProbe::child(&p, rowButton, seat(0));
    else
      layout::computeRowLayoutResultY(lookup(scene, "row")->asStackNode(),
                                      seat(width),
                                      layout::RowLayoutMetrics(),
                                      loka::app::layout::RailSpacing(12, 4, 9),
                                      &controller,
                                      &p,
                                      LayoutProbe::child,
                                      0,
                                      0);
    ToolboxButtonContext *context = static_cast<ToolboxButtonContext *>(rowButton->context);
    const Rect before = toolbox_host::controlRect;
    { StateTrackerGuard guard(root->tracker()); root->title.set(String::Literal("Flag mode: on")); }
    const PaintAnswer answer = context->queryPaintDamage(query);
    std::printf("%s: answer=%d rect-width=%d\n", mode, answer.kind, before.right - before.left); std::fflush(stdout);
    LOKA_VERIFY(answer.kind == (natural ? PAINT_ANSWER_REFUSED : PAINT_ANSWER_EXACT));
    if (!natural && !clipped)
    {
      LOKA_VERIFY(answer.damage.x == before.left && answer.damage.y == before.top);
      LOKA_VERIFY(answer.damage.width == before.right - before.left);
      LOKA_VERIFY(answer.damage.height == before.bottom - before.top);
      LayoutState s = seat(width); context->layout(&controller, s); context->draw(&controller);
      LOKA_VERIFY(EqualRect(&before, &toolbox_host::controlRect));
    }
  }
  Access::unmount(scene);
  std::puts("Control width pin passed");
}
