#include "ToolboxHost.hpp"
#include "ToolboxNodeDispatch.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxDirtyReplay.hpp"
#include "ToolboxBuiltInSupport.hpp"
#include "ToolboxPlatformLayoutHandlers.hpp"
#include "context/ToolboxClipBoxContext.hpp"
#include "context/ToolboxCellContext.hpp"
#include "context/ToolboxTextContext.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include <cstdio>
#include <cstring>

using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::core;
namespace loka
{
  namespace testing
  {
    void failToolboxClipBoxAllocations(unsigned);
  }
} // namespace loka
namespace
{
  const Rect broad = {0, 0, 300, 300};
  void equal(const Rect &a, const Rect &b)
  {
    if (!EqualRect(&a, &b))
      std::fprintf(stderr,
                   "rect actual=(%d,%d,%d,%d) expected=(%d,%d,%d,%d)\n",
                   a.left,
                   a.top,
                   a.right,
                   a.bottom,
                   b.left,
                   b.top,
                   b.right,
                   b.bottom);
    LOKA_VERIFY(EqualRect(&a, &b));
  }
  Rect intersection(const Rect &a, const Rect &b)
  {
    Rect out;
    SectRect(&a, &b, &out);
    return out;
  }
  struct Fixture
  {
    ToolboxWindow window;
    HostPortScope port;
    ToolboxScenePlatformController controller;
    Fixture()
        : window(),
          port(window.window()),
          controller(&window)
    {
      LOKA_VERIFY(RegisterToolboxBuiltInSupport(this->controller));
      LOKA_VERIFY(RegisterToolboxPlatformLayoutHandlers(this->controller.gapLayoutRegistry));
      ClipRect(&broad);
    }
    LayoutState seat() const
    {
      LayoutState state;
      state.x = 20;
      state.y = 30;
      state.width = 80;
      state.height = 100;
      state.spacing = 0;
      return state;
    }
    void layout(Node &node)
    {
      LayoutState state = this->seat();
      LayoutNode(&node, state, &this->controller, 0);
    }
  };
  Rect placement(Node &node)
  {
    Rect rect;
    LOKA_VERIFY(node.getContext());
    LOKA_VERIFY(static_cast<ToolboxClipBoxContext *>(node.getContext())->placement().query(rect));
    return rect;
  }
  /** A projected leaf deliberately paints beyond its reported extent. */
  class Ink : public Node, public IProjectedLayoutNode
  {
  public:
    Rect frameClip;
    unsigned draws;
    Ink()
        : frameClip(),
          draws(0)
    {
    }
    virtual IProjectedLayoutNode *asProjectedLayoutNode()
    {
      return this;
    }
    virtual short layoutProjected(IPlatformController *, LayoutState &state)
    {
      state.y = static_cast<short>(state.y + 150);
      return 120;
    }
    virtual void render(IPlatformController *platform)
    {
      ToolboxScenePlatformController &controller = *static_cast<ToolboxScenePlatformController *>(platform);
      controller.intersectWithProjectionClip(broad, this->frameClip);
      ++this->draws;
      DrawText("ink", 0, 3);
    }
  };
  void inkIs(const Ink &ink, const Rect &expected)
  {
    LOKA_VERIFY(ink.draws > 0 && !toolbox_host::draws.empty());
    equal(toolbox_host::draws.back().clip, expected);
    equal(ink.frameClip, expected);
  }
  void walk(int kind, bool flagged)
  {
    Fixture f;
    BoxProps bp = Box().size(50, 40).props;
    CardProps cp;
    bp.childClip_ = cp.childClip_ = flagged ? layout::CHILD_CLIP_OWN_RECT : layout::CHILD_CLIP_NONE;
    NestableNode *box =
        kind == 0 ? static_cast<NestableNode *>(new BoxNode(bp)) : static_cast<NestableNode *>(new CardNode(cp));
    Ink *ink = new Ink();
    box->addChild(ink);
    f.layout(*box);
    // Move only the box's next placement; the paint reads the latest fact.
    LayoutState next = f.seat();
    next.x = 35;
    next.y = 45;
    LayoutNode(box, next, &f.controller, 0);
    const Rect expected = flagged ? placement(*box) : broad;
    RenderNode(box, &f.controller);
    inkIs(*ink, expected);
    equal(toolbox_host::currentClip(), broad);
    LOKA_VERIFY(f.controller.projectionParentScopes_.activeDepth() == 0);
    delete box;
  }
  void nested()
  {
    Fixture f;
    BoxNode outer(Box().size(50, 40).clipChildren().props);
    BoxNode *inner = new BoxNode(Box().size(100, 90).clipChildren().props);
    Ink *ink = new Ink();
    inner->addChild(ink);
    outer.addChild(inner);
    f.layout(outer);
    // A later inner placement crosses each of the outer edges. Its own fact
    // must remain intact while the paint frame intersects the parent.
    LayoutState next = f.seat();
    next.x = 10;
    next.y = 20;
    next.width = 120;
    LayoutNode(inner, next, &f.controller, 0);
    RenderNode(&outer, &f.controller);
    inkIs(*ink, intersection(placement(outer), placement(*inner)));
  }
  void overflow(bool dirty)
  {
    Fixture f;
    BoxNode outer(Box().clipChildren().props);
    BoxNode *inner = new BoxNode(Box().height(25).props);
    Ink *ink = new Ink();
    inner->addChild(ink);
    outer.addChild(inner);
    if (dirty)
    {
      f.controller.rootNode_ = &outer;
      const Rect damage = {5, 5, 250, 250};
      ToolboxRenderDirtyInCompositionOrder(f.controller, damage);
      equal(toolbox_host::draws.back().clip, intersection(placement(outer), damage));
      equal(ink->frameClip, placement(outer));
      LOKA_VERIFY(f.controller.debugStats_.renderCalls == 1);
      f.controller.rootNode_ = 0;
    }
    else
    {
      f.layout(outer);
      LOKA_VERIFY(placement(outer).bottom == 55);
      RenderNode(&outer, &f.controller);
      inkIs(*ink, placement(outer));
    }
    equal(toolbox_host::currentClip(), broad);
  }
  void scrolled()
  {
    Fixture f;
    MutableState<Frame> fact(Frame(0, 9, 0, 0));
    NodeState<Frame> viewport(&fact, 0);
    ScrollViewNode scroll((ScrollViewProps(viewport)));
    BoxNode *box = new BoxNode(Box().width(50).clipChildren().props);
    Ink *ink = new Ink();
    box->addChild(ink);
    scroll.addChild(box);
    f.layout(scroll);
    const Rect boxRect = placement(*box);
    LOKA_VERIFY(boxRect.top == 21 && fact.get().y == 9);
    RenderNode(&scroll, &f.controller);
    inkIs(*ink, intersection(boxRect, f.controller.viewportScroll().contentRect));
    f.controller.releaseNodeContexts(&scroll);
  }
  void viewportOrUnit(bool unit)
  {
    Fixture f;
    BoxNode outer(Box().clipChildren().props);
    BoxNode *inner = new BoxNode(Box().height(25).props);
    Ink *ink = new Ink();
    if (unit)
    {
      ZStackNode *stack = new ZStackNode(ZStack().props);
      stack->addChild(new Ink());
      stack->addChild(ink);
      inner->addChild(stack);
    }
    else
    {
      ScrollViewNode *scroll = new ScrollViewNode(ScrollViewProps());
      scroll->addChild(ink);
      BoxNode *tall = new BoxNode(Box().height(100).props);
      tall->addChild(scroll);
      inner->addChild(tall);
    }
    outer.addChild(inner);
    f.layout(outer);
    RenderNode(&outer, &f.controller);
    const Rect expected =
        unit ? placement(outer) : intersection(placement(outer), f.controller.viewportScroll().contentRect);
    inkIs(*ink, expected);
    f.controller.releaseNodeContexts(&outer);
  }
  class PlacedButton : public ButtonNode
  {
  public:
    PlacedButton()
        : ButtonNode(Button("Visible").props)
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      return this->getContext()->layout(platform, state);
    }
  };
  void fallback(const char *mode)
  {
    Fixture f;
    BoxNode box(Box().size(50, 40).clipChildren().props);
    Ink *ink = new Ink();
    box.addChild(ink);
    PlacedButton *button = new PlacedButton();
    button->setContext(new ToolboxButtonContext(button, &f.controller));
    box.addChild(button);
    // Place the button independently so its visible layout rect remains useful
    // even though the preceding deliberately overflowing leaf is much taller.
    if (!std::strcmp(mode, "none-context"))
      loka::testing::failToolboxClipBoxAllocations(1);
    f.layout(box);
    loka::testing::failToolboxClipBoxAllocations(0);
    LayoutState buttonSeat = f.seat();
    LayoutNode(button, buttonSeat, &f.controller, 0);
    if (!std::strcmp(mode, "none-placement"))
      static_cast<ToolboxClipBoxContext *>(box.getContext())->setPlacement(ToolboxClipPlacement());
    RenderNode(button, &f.controller);
    LOKA_VERIFY(f.controller.buttonControls_.size() == 1);
    f.controller.buttonControls_[0].control->calls.clear();
    f.controller.buttonControls_[0].usedThisFrame = false;
    toolbox_host::draws.clear();
    const bool push = !std::strcmp(mode, "push");
    if (push)
      for (unsigned i = 0; i != 6; ++i)
        LOKA_VERIFY(
            f.controller.projectionParentScopes_.pushClip(ProjectionParentScope(0, 0, 0, Frame(0, 0, 300, 300))));
    if (!std::strcmp(mode, "region"))
      toolbox_host::failRegions = 1;
    RenderNode(&box, &f.controller);
    toolbox_host::failRegions = 0;
    LOKA_VERIFY(ink->draws == 1);
    equal(ink->frameClip, broad);
    // The first QuickDraw text draw is the custom child (Button may draw too).
    equal(toolbox_host::draws.front().clip, broad);
    LOKA_VERIFY(f.controller.buttonControls_.size() == 1);
    ControlRef control = f.controller.buttonControls_[0].control;
    LOKA_VERIFY(control->visible && f.controller.buttonControls_[0].usedThisFrame);
    bool drewControl = false;
    for (std::size_t i = 0; i != control->calls.size(); ++i)
    {
      LOKA_VERIFY(control->calls[i].operation != "hide");
      if (control->calls[i].operation == "draw")
        drewControl = true;
    }
    LOKA_VERIFY(drewControl);
    if (push)
      for (unsigned i = 0; i != 6; ++i)
        LOKA_VERIFY(f.controller.projectionParentScopes_.pop());
    equal(toolbox_host::currentClip(), broad);
    f.controller.clearControls();
    f.controller.releaseNodeContexts(&box);
  }
  void ribbon()
  {
    Fixture f;
    RibbonGroupNode ribbon((RibbonGroupProps()));
    Ink *ink = new Ink();
    ribbon.addChild(ink);
    // Ribbon handlers may own their own context; a clip-shaped sentinel makes
    // accidental renderClipBox routing observable without relying on UB.
    ToolboxClipBoxContext *context = new ToolboxClipBoxContext();
    const Rect small = {20, 20, 40, 40};
    context->setPlacement(ToolboxClipPlacement(small));
    ribbon.setContext(context);
    RenderNode(&ribbon, &f.controller);
    inkIs(*ink, broad);
  }
  class HitInk : public Ink
  {
    ToolboxCellContext &cell_;
    ToolboxTextContext &text_;
    State<String> &value_;

  public:
    HitInk(ToolboxCellContext &cell, ToolboxTextContext &text, State<String> &value)
        : cell_(cell),
          text_(text),
          value_(value)
    {
    }
    virtual void render(IPlatformController *platform)
    {
      Ink::render(platform);
      ToolboxScenePlatformController &c = *static_cast<ToolboxScenePlatformController *>(platform);
      c.recordButtonHit(broad, 0, 0, 0, 0);
      c.recordCellHit(broad, 0, 0, &this->cell_, &this->value_);
      c.recordTextHit(broad, 0, 0, &this->value_, 0, false, 300, &this->text_);
    }
  };
  void hits()
  {
    Fixture f;
    State<String> value(String("hit"));
    CellNode cell((CellProps()));
    TextNode text(Text("hit").props);
    ToolboxCellContext cellContext(&cell, &f.controller);
    ToolboxTextContext textContext(&text, &f.controller);
    BoxNode box(Box().size(50, 40).clipChildren().props);
    box.addChild(new HitInk(cellContext, textContext, value));
    f.layout(box);
    RenderNode(&box, &f.controller);
    LOKA_VERIFY(f.controller.buttonHits().size() == 1);
    LOKA_VERIFY(f.controller.cellHits().size() == 1);
    LOKA_VERIFY(f.controller.textHits().size() == 1);
    const Rect expected = placement(box);
    equal(f.controller.buttonHits()[0].rect, expected);
    equal(f.controller.cellHits()[0].rect, expected);
    equal(f.controller.textHits()[0].rect, expected);
  }
} // namespace
int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  toolbox_host::reset();
  const char *mode = argv[1];
  if (!std::strcmp(mode, "box"))
    walk(0, true);
  else if (!std::strcmp(mode, "card"))
    walk(1, true);
  else if (!std::strcmp(mode, "unflagged"))
  {
    walk(0, false);
    walk(1, false);
  }
  else if (!std::strcmp(mode, "nested"))
    nested();
  else if (!std::strcmp(mode, "overflow"))
    overflow(false);
  else if (!std::strcmp(mode, "dirty"))
    overflow(true);
  else if (!std::strcmp(mode, "scrolled"))
    scrolled();
  else if (!std::strcmp(mode, "viewport"))
    viewportOrUnit(false);
  else if (!std::strcmp(mode, "unit"))
    viewportOrUnit(true);
  else if (!std::strcmp(mode, "ribbon"))
    ribbon();
  else if (!std::strcmp(mode, "hits"))
    hits();
  else
    fallback(mode);
  std::printf("Toolbox clip paint %s passed\n", mode);
}
