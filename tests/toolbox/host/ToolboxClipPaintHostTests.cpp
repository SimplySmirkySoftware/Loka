#include "ToolboxHost.hpp"
#include "ToolboxNodeDispatch.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxDirtyReplay.hpp"
#include "ToolboxBuiltInSupport.hpp"
#include "ToolboxPlatformLayoutHandlers.hpp"
#include "context/ToolboxClipBoxContext.hpp"
#include "context/ToolboxCellContext.hpp"
#include "context/ToolboxTextContext.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include <cstdio>
#include <cstring>

namespace loka
{
  namespace testing
  {
    class ToolboxTextContextAccess
    {
    public:
      static Rect rect(const ToolboxTextContext &context)
      {
        return context.rect_;
      }
    };
  } // namespace testing
} // namespace loka
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
    if (!flagged)
    {
      if (kind == 0)
        static_cast<BoxNode *>(box)->props.childClip_ = layout::CHILD_CLIP_OWN_RECT;
      else
        static_cast<CardNode *>(box)->props.childClip_ = layout::CHILD_CLIP_OWN_RECT;
      f.layout(*box);
      LOKA_VERIFY(box->getContext());
      if (kind == 0)
        static_cast<BoxNode *>(box)->props.childClip_ = layout::CHILD_CLIP_NONE;
      else
        static_cast<CardNode *>(box)->props.childClip_ = layout::CHILD_CLIP_NONE;
      f.controller.observation_.reset();
    }
    f.layout(*box);
    LOKA_VERIFY(!f.controller.observation_.outsideRefusal().refused());
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
    if (push)
    {
      // Exercise the layout guard refusal and its next comparison in the same
      // fixture as the broad fallback drawing below.
      f.layout(box);
      Rect refused;
      LOKA_VERIFY(!static_cast<ToolboxClipBoxContext *>(box.getContext())->placement().query(refused));
      buttonSeat = f.seat();
      LayoutNode(button, buttonSeat, &f.controller, 0);
    }
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
    if (push)
    {
      const Frame origin;
      ToolboxViewportContentObservation next(ToolboxExpectedMotion(&origin, origin), false);
      ToolboxViewportContentObservation::Scope observation(f.controller.activeViewportContent_, next);
      f.layout(box);
      LOKA_VERIFY(next.complete().notTranslated >= 1);
    }
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
  /** Like ReplayEditor in the neighbouring unit fixture: the host controller
      omits prepareProjectedLayout, so project explicitly into the real context. */
  class ShrinkText : public TextNode
  {
  public:
    explicit ShrinkText(const char *label)
        : TextNode(Text(label).props)
    {
      this->props.blockStyle_ = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      ToolboxScenePlatformController *controller = static_cast<ToolboxScenePlatformController *>(platform);
      return controller->projectLayoutState(state) ? this->Node::layout(platform, state) : 0;
    }
  };
  class ShrinkButton : public ButtonNode
  {
  public:
    explicit ShrinkButton(const char *label)
        : ButtonNode(Button(label).props)
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      ToolboxScenePlatformController *controller = static_cast<ToolboxScenePlatformController *>(platform);
      return controller->projectLayoutState(state) ? this->Node::layout(platform, state) : 0;
    }
  };
  /** Host dispatch needs the same explicit layout projection as ShrinkText. */
  class PlacedEdit : public EditTextNode
  {
  public:
    explicit PlacedEdit(const NodeState<String> &text)
        : EditTextNode(EditTextProps(text))
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      ToolboxScenePlatformController *controller = static_cast<ToolboxScenePlatformController *>(platform);
      return controller->projectLayoutState(state) ? this->Node::layout(platform, state) : 0;
    }
  };
  void editPaintEvidence()
  {
    Fixture f;
    SetRect(&f.window.port.portRect, 0, 0, 220, 160);
    MutableState<String> value(String::Literal("Clipped edit"));
    NodeState<String> text(&value, 0);
    StackNode root(Column().props);
    BoxNode *viewportSeat = new BoxNode(Box().size(180, 62).props);
    ScrollViewNode *viewport = new ScrollViewNode(ScrollViewProps());
    StackNode *contents = new StackNode(Column().props);
    contents->addChild(new BoxNode(Box().size(150, 40).props));
    PlacedEdit *scrolled = new PlacedEdit(text);
    scrolled->setContext(new ToolboxEditTextContext(scrolled, &f.controller));
    contents->addChild(scrolled);
    viewport->addChild(contents);
    viewportSeat->addChild(viewport);
    root.addChild(viewportSeat);
    BoxNode *clip = new BoxNode(ClipBox().size(40, 24).props);
    BoxNode *wide = new BoxNode(Box().width(120).props);
    PlacedEdit *field = new PlacedEdit(text);
    ToolboxEditTextContext *context = new ToolboxEditTextContext(field, &f.controller);
    field->setContext(context);
    wide->addChild(field);
    clip->addChild(wide);
    root.addChild(clip);
    LayoutState state;
    state.x = state.y = 12;
    state.width = 196;
    state.height = 136;
    state.lineHeight = 14; // The production root render offer uses 14.
    state.spacing = 8;
    LayoutNode(&root, state, &f.controller, 0);
    const Rect laidOutChrome = context->chromeRect();
    const Rect clipRect = placement(*clip);
    const Rect expectedClip = {82, 12, 106, 52};
    equal(clipRect, expectedClip);
    const Rect port = f.window.port.portRect;
    LOKA_VERIFY(laidOutChrome.left - 2 >= port.left && laidOutChrome.top - 2 >= port.top);
    LOKA_VERIFY(laidOutChrome.right + 2 <= port.right && laidOutChrome.bottom + 2 <= port.bottom);
    const Rect paintRect = intersection(laidOutChrome, clipRect);
    LOKA_VERIFY(!EmptyRect(&paintRect));
    RenderNode(&root, &f.controller);
    LOKA_VERIFY(f.controller.editControls_.size() == 2);
    std::size_t index = 0;
    LOKA_VERIFY(f.controller.editControls_.find(context, index));
    const ToolboxNativePlacement applied = f.controller.editControls_[index].placement;
    Rect full = laidOutChrome;
    ++full.left;
    full.top += 2;
    --full.right;
    --full.bottom;
    equal(applied.full(), full);
    Rect visible;
    LOKA_VERIFY(applied.queryVisible(visible));
    equal(visible, intersection(applied.full(), clipRect));
    LOKA_VERIFY(visible.right < applied.full().right);
    equal(context->chromeRect(), laidOutChrome);
    const TEHandle te = f.controller.editControls_[index].te;
    equal((**te).viewRect, applied.full());
    const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
    const PaintAnswer clean = context->queryPaintDamage(query);
    LOKA_VERIFY(clean.kind == PAINT_ANSWER_EXACT);
    LOKA_VERIFY(clean.damage.x == paintRect.left && clean.damage.y == paintRect.top);
    LOKA_VERIFY(clean.damage.width == 0 && clean.damage.height == 0);
    // Paint debt is empty after presentation; applied native visibility is not.
    value.set(String::Literal("Changed without rendering"));
    const PaintAnswer changed = context->queryPaintDamage(query);
    LOKA_VERIFY(changed.kind == PAINT_ANSWER_EXACT);
    LOKA_VERIFY(changed.damage.x == paintRect.left && changed.damage.y == paintRect.top);
    LOKA_VERIFY(changed.damage.width == paintRect.right - paintRect.left);
    LOKA_VERIFY(changed.damage.height == paintRect.bottom - paintRect.top);
    RenderNode(&root, &f.controller);
    const PaintAnswer repainted = context->queryPaintDamage(query);
    LOKA_VERIFY(repainted.kind == PAINT_ANSWER_EXACT);
    LOKA_VERIFY(repainted.damage.width == 0 && repainted.damage.height == 0);
    equal(f.controller.editControls_[index].placement.visible(), visible);
    std::printf("edit evidence full=(%d,%d,%d,%d) visible=(%d,%d,%d,%d) "
                "clip=(%d,%d,%d,%d) paint=(%d,%d,%d,%d) clean=(%d,%d,%d,%d)\n",
                applied.full().left,
                applied.full().top,
                applied.full().right,
                applied.full().bottom,
                visible.left,
                visible.top,
                visible.right,
                visible.bottom,
                clipRect.left,
                clipRect.top,
                clipRect.right,
                clipRect.bottom,
                paintRect.left,
                paintRect.top,
                paintRect.right,
                paintRect.bottom,
                clean.damage.x,
                clean.damage.y,
                clean.damage.width,
                clean.damage.height);
    f.controller.clearControls();
    f.controller.releaseNodeContexts(&root);
  }
  /** Exercise real Row dispatch and PlainDisplay together, with no synthetic widths. */
  void shrinkSweep(const char *mode)
  {
    const bool ancestor = !std::strcmp(mode, "shrink-clip") || !std::strcmp(mode, "shrink-frame");
    const bool refused = !std::strcmp(mode, "shrink-refused");
    const bool frame = !std::strcmp(mode, "shrink-frame");
    Fixture f;
    BoxNode box(ancestor ? ClipBox().height(1).props : Box().props);
    StackNode *row = new StackNode(Row().naturalWidth().props);
    ButtonNode *back = new ShrinkButton("< Back");
    TextNode *title = new ShrinkText("A title longer than the available toolbar space");
    ButtonNode *main = new ShrinkButton("Main");
    back->setContext(new ToolboxButtonContext(back, &f.controller));
    title->setContext(new ToolboxTextContext(title, &f.controller));
    main->setContext(new ToolboxButtonContext(main, &f.controller));
    row->addChild(back);
    row->addChild(title);
    row->addChild(main);
    box.addChild(row);
    short a = 0, t = 0, b = 0;
    LOKA_VERIFY(f.controller.queryNaturalWidth(back, a));
    LOKA_VERIFY(f.controller.queryNaturalWidth(title, t));
    LOKA_VERIFY(f.controller.queryNaturalWidth(main, b));
    const int gap = 8;
    const int fixed = a + b + 2 * gap;
    for (int offer = fixed + t; offer >= fixed + 1; --offer)
    {
      LayoutState state = f.seat();
      state.width = static_cast<short>(offer);
      state.spacing = gap;
      state.inputs = NODE_DIRTY_LAYOUT;
      if (frame)
        for (unsigned i = 0; i != 6; ++i)
          LOKA_VERIFY(
              f.controller.projectionParentScopes_.pushClip(ProjectionParentScope(0, 0, 0, Frame(0, 0, 300, 300))));
      LayoutNode(&box, state, &f.controller, 0);
      ToolboxTextContext &context = *static_cast<ToolboxTextContext *>(title->getContext());
      const Rect seat = loka::testing::ToolboxTextContextAccess::rect(context);
      if (ancestor)
      {
        Rect placed;
        LOKA_VERIFY(static_cast<ToolboxClipBoxContext *>(box.getContext())->placement().query(placed) == !frame);
      }
      LOKA_VERIFY(seat.right - seat.left == offer - fixed);
      LOKA_VERIFY(seat.left == f.seat().x + a + gap);
      RenderNode(back, &f.controller);
      RenderNode(main, &f.controller);
      toolbox_host::draws.clear();
      toolbox_host::pascalDraws.clear();
      // Render the title separately for the allocation-refusal axis: both of
      // its own paint clip regions must be refused, not an ancestor's regions.
      toolbox_host::failRegions = refused ? 2 : 0;
      if (refused)
        RenderNode(title, &f.controller);
      else
        RenderNode(&box, &f.controller);
      LOKA_VERIFY(toolbox_host::failRegions == 0);
      LOKA_VERIFY(!toolbox_host::pascalDraws.empty());
      // Buttons use DrawControl; the sole Pascal text draw is the title.
      const std::string &bytes = toolbox_host::pascalDraws.front();
      unsigned char nativeText[256];
      LOKA_VERIFY(bytes.size() <= 255);
      nativeText[0] = static_cast<unsigned char>(bytes.size());
      std::memcpy(nativeText + 1, bytes.data(), bytes.size());
      const int advance = StringWidth(nativeText);
      const unsigned char dots[] = {3, '.', '.', '.'};
      LOKA_VERIFY(advance <= offer - fixed);
      LOKA_VERIFY(context.visibleWidth() == advance);
      if (offer - fixed < StringWidth(dots))
        LOKA_VERIFY(bytes.empty());
      if (refused)
        equal(toolbox_host::draws.front().clip, broad);
      if (ancestor)
        LOKA_VERIFY(toolbox_host::draws.front().clip.bottom == (frame ? seat.bottom : f.seat().y + 1));
      {
        LOKA_VERIFY(f.controller.buttonControls_.size() == 2);
        const Rect left = f.controller.buttonControls_[0].control->rect;
        const Rect right = f.controller.buttonControls_[1].control->rect;
        LOKA_VERIFY(left.left == f.seat().x && left.right - left.left == a);
        LOKA_VERIFY(seat.left - left.right == gap);
        LOKA_VERIFY(right.left - seat.right == gap && right.right - right.left == b);
        LOKA_VERIFY(right.right == f.seat().x + offer);
      }
      if (frame)
        for (unsigned i = 0; i != 6; ++i)
          LOKA_VERIFY(f.controller.projectionParentScopes_.pop());
      equal(toolbox_host::currentClip(), broad);
    }
    f.controller.clearControls();
    f.controller.releaseNodeContexts(&box);
  }
  void multipleTitles(unsigned count)
  {
    Fixture f;
    StackNode row(Row().naturalWidth().props);
    const char *labels[] = {"Short", "A distinctly longer label", "Tiny", "The longest title in this narrow row"};
    TextNode *texts[4];
    short natural[4];
    int sum = 0;
    for (unsigned i = 0; i < count; ++i)
    {
      texts[i] = new ShrinkText(labels[i]);
      texts[i]->setContext(new ToolboxTextContext(texts[i], &f.controller));
      row.addChild(texts[i]);
      LOKA_VERIFY(f.controller.queryNaturalWidth(texts[i], natural[i]));
      sum += natural[i];
    }
    // Independent integer oracle: enumerate caps rather than water-filling.
    for (int budget = sum - 1; budget >= static_cast<int>(count); --budget)
    {
      int cap = 1, used = 0;
      for (int candidate = 1; candidate <= sum; ++candidate)
      {
        int total = 0;
        for (unsigned i = 0; i < count; ++i)
          total += natural[i] < candidate ? natural[i] : candidate;
        if (total > budget)
          break;
        cap = candidate;
        used = total;
      }
      int remainder = budget - used;
      LayoutState offer = f.seat();
      offer.spacing = 8;
      offer.width = static_cast<short>(budget + (count - 1) * 8);
      offer.inputs = NODE_DIRTY_LAYOUT;
      LayoutNode(&row, offer, &f.controller, 0);
      int x = f.seat().x;
      for (unsigned i = 0; i < count; ++i)
      {
        int expected = natural[i] < cap ? natural[i] : cap;
        if (natural[i] > cap && remainder > 0)
        {
          ++expected;
          --remainder;
        }
        ToolboxTextContext &context = *static_cast<ToolboxTextContext *>(texts[i]->getContext());
        const Rect rect = loka::testing::ToolboxTextContextAccess::rect(context);
        LOKA_VERIFY(rect.left == x && rect.right - rect.left == expected);
        toolbox_host::pascalDraws.clear();
        RenderNode(texts[i], &f.controller);
        LOKA_VERIFY(toolbox_host::pascalDraws.size() == 1);
        if (natural[i] <= cap)
          LOKA_VERIFY(toolbox_host::pascalDraws.front() == labels[i]);
        x += expected + 8;
      }
      LOKA_VERIFY(remainder == 0);
    }
    f.controller.releaseNodeContexts(&row);
  }
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
  if (!std::strcmp(mode, "edit-paint-evidence"))
    editPaintEvidence();
  else if (!std::strcmp(mode, "multi-two") || !std::strcmp(mode, "multi-four"))
    multipleTitles(!std::strcmp(mode, "multi-two") ? 2 : 4);
  else if (!std::strncmp(mode, "shrink-", 7))
    shrinkSweep(mode);
  else if (!std::strcmp(mode, "box"))
  {
    walk(0, true);
    editPaintEvidence();
  }
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
