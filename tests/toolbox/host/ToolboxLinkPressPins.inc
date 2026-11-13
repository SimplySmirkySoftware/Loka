#include "context/ToolboxAttributedTextContext.hpp"
#include "context/ToolboxTextContext.hpp"
#include "context/ToolboxCellContext.hpp"
#include "context/ToolboxClipBoxContext.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "support/WindowAdmissionTestApp.hpp"

/** P3 is a guard note, not a pin: no reentry hook violates the closed native interval. */
namespace link_press
{
  Point point(short x, short y = 13)
  {
    const Point p = {y, x};
    return p;
  }
  AttributedString value()
  {
    const LinkTarget x(String("x")), y(String("y"));
    return Linked("aa", TextStyle(), x) + Linked("bb", TextStyle().italic(true), x) + Styled("  ", TextStyle())
           + Linked("cc", TextStyle(), y) + Styled("  ", TextStyle()) + Linked("dd", TextStyle(), x);
  }
  class PlacedLink : public ToolboxAttributedTextContext
  {
    short x_;

  public:
    PlacedLink(AttributedTextNode *node, ToolboxScenePlatformController *controller, short x = 12)
        : ToolboxAttributedTextContext(node, controller),
          x_(x)
    {
    }
    virtual short layout(IPlatformController *controller, LayoutState &state)
    {
      const short origin = state.x;
      state.x = this->x_;
      state.y = 12;
      state.width = 120;
      state.inputs = NODE_DIRTY_NONE;
      return ToolboxAttributedTextContext::layout(controller, state) + this->x_ - origin;
    }
  };
  struct Fixture : press_door::Fixture
  {
    MutableState<LinkTarget> result;
    NodeState<LinkTarget> box;
    AttributedTextNode *node;
    unsigned writes;
    explicit Fixture(const AttributedString &text = value())
        : result(LinkTarget()),
          box(&result, 0),
          node(0),
          writes(0)
    {
      this->result.bind(&changed, this, false);
      this->node = this->add(text);
      this->render();
    }
    ~Fixture()
    {
      this->result.unbind(&changed, this);
    }
    static void changed(void *data)
    {
      Fixture &f = *static_cast<Fixture *>(data);
      LOKA_VERIFY(f.controller.borrowPhase().open());
      ++f.writes;
    }
    AttributedTextNode *add(const AttributedString &text, short x = 12)
    {
      AttributedTextNode *n = new AttributedTextNode(AttributedText(text).activatedLink(this->box).props);
      n->setContext(new PlacedLink(n, &this->controller, x));
      this->stack.addChild(n);
      NotifySubtreeNodeAttached(n);
      return n;
    }
    ToolboxAttributedTextContext *context()
    {
      return static_cast<ToolboxAttributedTextContext *>(this->node->getContext());
    }
    bool click(Point down, Point up, bool held = false)
    {
      toolbox_host::mouse = toolbox_host::MouseScript();
      toolbox_host::mouse.release = up;
      if (held)
      {
        toolbox_host::mouse.held.push_back(point(250, 250));
        toolbox_host::mouse.held.push_back(up);
      }
      return ToolboxInputDoor::mouseDown(this->controller, down);
    }
    Point segment(unsigned segment)
    {
      for (short x = 12; x < 132; ++x)
      {
        ToolboxLinkHit hit;
        if (this->context()->prepareLinkHit(point(x), hit) == TOOLBOX_HIT_LINK && hit.firstSegment <= segment
            && hit.lastSegment >= segment)
          return point(x);
      }
      LOKA_VERIFY(false && "linked segment must have an advance cell");
      return point(10);
    }
    void replace(const AttributedString &text)
    {
      this->node->props = AttributedText(text).activatedLink(this->box).props;
      this->context()->onPropsApplied();
      this->node->requeueLayoutInputs(static_cast<NodeDirtyFlags>(NODE_DIRTY_PROPS | NODE_DIRTY_LAYOUT));
      this->render();
    }
  };
  void mouseScript()
  {
    toolbox_host::mouse = toolbox_host::MouseScript();
    toolbox_host::mouse.held.push_back(point(20));
    toolbox_host::mouse.held.push_back(point(30));
    toolbox_host::mouse.release = point(40);
    Point sample;
    LOKA_VERIFY(toolbox_host::Button());
    LOKA_VERIFY(WaitMouseUp());
    GetMouse(&sample);
    LOKA_VERIFY(sample.h == 20 && toolbox_host::Button());
    LOKA_VERIFY(WaitMouseUp());
    GetMouse(&sample);
    LOKA_VERIFY(sample.h == 30 && toolbox_host::Button());
    LOKA_VERIFY(!WaitMouseUp());
    GetMouse(&sample);
    LOKA_VERIFY(sample.h == 40 && !toolbox_host::Button() && toolbox_host::mouse.calls == 3);
  }
  void basic()
  {
    Fixture f;
    Point first = f.segment(0), same = f.segment(1), other = f.segment(3), separated = f.segment(5);
    LOKA_VERIFY(f.click(first, first));
    LOKA_VERIFY(f.click(first, first));
    LOKA_VERIFY(f.writes == 2 && f.result.get() == LinkTarget(String("x")));
    LOKA_VERIFY(f.click(first, same, true) && f.writes == 3);
    LOKA_VERIFY(toolbox_host::mouse.calls == 3);
    LOKA_VERIFY(!f.click(first, other) && !f.click(first, separated) && f.writes == 3);
    LOKA_VERIFY(!f.click(first, point(250, 250)) && f.writes == 3);
    f.replace(Linked("a", TextStyle(), LinkTarget(String("x"))) + Linked("b", TextStyle(), LinkTarget(String("x"))));
    const Point a = f.segment(0);
    const Point b = point(a.h + TextWidth("a", 0, 1));
    LOKA_VERIFY(b.h > a.h && f.click(a, b) && f.writes == 4);
    std::puts("[pin] P1/P2/P5 forced activation, same/different/separated runs, A2 and off-text passed");
  }
  void parkedAndUnknown()
  {
    Fixture f;
    const Point first = f.segment(0);
    f.focus();
    // A real props application drops placement/presentation; layout restores
    // placement, but deliberately skip paint to isolate unknown presentation.
    f.context()->onPropsApplied();
    LayoutState seat;
    f.context()->layout(&f.controller, seat);
    ToolboxLinkHit hit;
    LOKA_VERIFY(f.context()->prepareLinkHit(first, hit) == TOOLBOX_HIT_LINK);
    LOKA_VERIFY(!f.click(first, first, true) && toolbox_host::mouse.calls == 0 && f.writes == 0);
    f.expectFocus(0);
    f.render();
    LOKA_VERIFY(f.click(first, first, true) && toolbox_host::mouse.calls != 0);
    NotifySubtreeNodeDetached(f.node);
    f.focus();
    const int measures = toolbox_host::widths;
    LOKA_VERIFY(!f.click(first, first) && toolbox_host::mouse.calls == 0 && f.writes == 1);
    LOKA_VERIFY(toolbox_host::widths == measures);
    f.expectFocus(f.field.getContext());
    std::puts("[pin] P6/P11 parked DOWN and unknown-presentation qualification before tracking passed");
  }
  void rows()
  {
    Fixture f(Styled("plain", TextStyle()));
    LOKA_VERIFY(f.controller.linkHits().empty());
    const String refused = String::FromPlatform(Managed<loka::platform::String>::Wrap(new RefusingUtf8()));
    f.replace(Linked(refused, TextStyle(), LinkTarget(String("x"))));
    LOKA_VERIFY(f.controller.linkHits().empty());
    f.replace(value());
    LOKA_VERIFY(f.controller.linkHits().size() == 1);
    const Rect miss = {200, 200, 210, 210};
    ToolboxRenderDirtyInCompositionOrder(f.controller, miss);
    LOKA_VERIFY(f.controller.linkHits().size() == 1);
    LOKA_VERIFY(f.click(f.segment(0), f.segment(0)));
    AttributedTextNode *later = f.add(value());
    f.render();
    LOKA_VERIFY(f.controller.linkHits().size() == 2);
    ToolboxRenderDirtyInCompositionOrder(f.controller, miss);
    LOKA_VERIFY(f.controller.linkHits().size() == 2);
    PressRow order;
    LOKA_VERIFY(f.controller.selectedPress(point(13), order) == later->getContext());
    LOKA_VERIFY(order.kind == PressRow::LINK_HIT && order.index == 1);
    LOKA_VERIFY(!f.click(f.segment(0), point(250, 250)));
    f.controller.pressLedger_.begin();
    f.controller.pressLedger_.append(PressRow::LINK_HIT, 0);
    f.controller.pressLedger_.append(PressRow::LINK_HIT, 1);
    f.controller.retireNodeContext(f.context(), NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(f.controller.linkHits().size() == 1);
    PressRow before;
    LOKA_VERIFY(f.controller.selectedPress(point(13), before) == later->getContext());
    LOKA_VERIFY(before.kind == PressRow::LINK_HIT && before.index == 0);
    ToolboxLinkHit laterHit;
    AttributedString::LinkRun laterRun;
    ToolboxAttributedTextContext *laterContext = static_cast<ToolboxAttributedTextContext *>(later->getContext());
    LOKA_VERIFY(laterContext->prepareLinkHit(point(13), laterHit) == TOOLBOX_HIT_LINK);
    LOKA_VERIFY(laterContext->queryEligibleRun(laterHit, laterRun));
    LOKA_VERIFY(f.click(point(13), point(13)));
    f.controller.pressLedger_.commit();
    PressRow selected;
    LOKA_VERIFY(f.controller.selectedPress(point(13), selected) == later->getContext());
    LOKA_VERIFY(selected.kind == PressRow::LINK_HIT && selected.index == 0);
    LOKA_VERIFY(f.click(point(13), point(13)));
    // Restore by the ordinary walk; retirement pin above checks LIVE/staging repair.
    f.render();
    later->props = AttributedText(Styled("plain", TextStyle())).props;
    laterContext->onPropsApplied();
    f.replace(Styled("plain", TextStyle()));
    LOKA_VERIFY(f.controller.linkHits().empty());
    std::puts("[pin] P8 link enrollment, dirty replay, reset and retirement index repair passed");
  }
  void cover()
  {
    {
      Fixture f(Linked("aaaaaaaaaaaaaaaa", TextStyle(), LinkTarget(String("x"))));
      const Point first = f.segment(0);
      ButtonNode *button = new ButtonNode(ButtonProps().onClick(&f.upperAction));
      const Rect rect = {10, 60, 35, 110};
      button->setContext(new press_door::PlacedButton(button, &f.controller, rect));
      f.stack.addChild(button);
      NotifySubtreeNodeAttached(button);
      f.render();
      // Isolate the native face contract from its containing ZStack UNIT;
      // the separate shield pin below tests that whole-unit ownership.
      f.controller.pressLedger_.begin();
      f.controller.pressLedger_.append(PressRow::LINK_HIT, 0);
      f.controller.pressLedger_.append(PressRow::NATIVE_BUTTON, 0);
      f.controller.pressLedger_.commit();
      toolbox_host::testControls = 0;
      LOKA_VERIFY(!f.click(first, point(70)) && f.writes == 0 && f.upperCalls == 0);
      LOKA_VERIFY(toolbox_host::testControls == 1);
      LOKA_VERIFY(!f.click(point(70), point(70)) && f.writes == 0 && f.upperCalls == 1);
      LOKA_VERIFY(toolbox_host::mouse.calls == 0);
    }
    {
      Fixture f(Linked("aaaaaaaaaaaaaaaa", TextStyle(), LinkTarget(String("x"))));
      // A completed UNIT above the release area; no callbacks during tracking.
      const Rect rect = {10, 60, 35, 110};
      const ToolboxUnitLedger::UnitSeat shield = {&f.stack, 99, rect, rect};
      const std::size_t index = f.controller.unitLedger_.mark();
      f.controller.unitLedger_.push(shield);
      f.controller.pressLedger_.begin();
      f.controller.pressLedger_.append(PressRow::LINK_HIT, 0);
      f.controller.pressLedger_.append(PressRow::UNIT, index);
      f.controller.pressLedger_.commit();
      LOKA_VERIFY(!f.click(f.segment(0), point(70)) && f.writes == 0);
    }
    {
      Fixture f;
      TextNode label(TextProps("plain label"));
      label.setContext(new ToolboxTextContext(&label, &f.controller));
      NotifySubtreeNodeAttached(&label);
      LayoutState seat;
      seat.x = 12;
      seat.y = 12;
      seat.width = 120;
      label.getContext()->layout(&f.controller, seat);
      label.getContext()->render(&f.controller);
      // A plain label painted over the link adds no native/UNIT shield.
      LOKA_VERIFY(f.click(f.segment(0), f.segment(1)) && f.writes == 1);
    }
    {
      Fixture f;
      f.add(value(), 150);
      f.render();
      f.controller.pressLedger_.begin();
      f.controller.pressLedger_.append(PressRow::LINK_HIT, 0);
      f.controller.pressLedger_.append(PressRow::LINK_HIT, 1);
      f.controller.pressLedger_.commit();
      LOKA_VERIFY(!f.click(f.segment(0), point(151)) && f.writes == 0);
      LOKA_VERIFY(toolbox_host::mouse.calls == 1);
    }
    std::puts("[pin] P4/P5 native and UNIT cover, plain-label pass-through and context identity passed");
  }
  /** Existing press fixtures install custom hit payloads directly. The Cell
      geometry is fixed; its input body and the link's render are production. */
  struct Ground
  {
    Fixture &f;
    CellNode node;
    ToolboxCellContext *context;
    explicit Ground(Fixture &fixture)
        : f(fixture),
          node(CellProps().text("ground").onClick(&fixture.groundAction)),
          context(0)
    {
      this->context = new ToolboxCellContext(&this->node, &f.controller);
      this->node.setContext(this->context);
      NotifySubtreeNodeAttached(&this->node);
      const Rect rect = {0, 0, 80, 140};
      this->context->updateData(this->node.props.text_);
      this->context->updateRect(rect);
      ToolboxHitLedger::CellHit hit = {rect, &f.groundAction, 0, this->context, this->node.props.text_};
      f.controller.installHit(hit);
    }
    ~Ground()
    {
      LifecycleFactTestAccess::MarkSubtreeRetired(&this->node);
      LifecycleFactTestAccess::DeliverFacts(&this->node);
    }
    void publish(BoxNode *clip = 0)
    {
      this->f.controller.hitLedger_.clearPressRows();
      const Rect rect = {0, 0, 80, 140};
      ToolboxHitLedger::CellHit hit = {rect, &f.groundAction, 0, this->context, this->node.props.text_};
      f.controller.installHit(hit);
      this->f.controller.pressLedger_.begin();
      this->f.controller.pressLedger_.append(PressRow::CELL_HIT, 0);
      if (clip)
        this->f.controller.renderClipBox(clip);
      else
        this->f.context()->render(&this->f.controller);
      this->f.controller.pressLedger_.commit();
    }
  };
  void groundAndClip()
  {
    {
      Fixture f;
      Ground ground(f);
      ground.publish();
      f.focus();
      LOKA_VERIFY(!f.click(point(129), point(129)) && f.writes == 0 && f.groundCalls == 0);
      f.expectFocus(0);
      f.replace(Styled("plain", TextStyle()));
      ground.publish();
      LOKA_VERIFY(f.click(point(13), point(13)) && f.writes == 0 && f.groundCalls == 1);
    }
    {
      Fixture f;
      Ground ground(f);
      const Point first = f.segment(0), second = f.segment(1);
      BoxNode *clip = new BoxNode(ClipBox().size(second.h, 80).props);
      ToolboxClipBoxContext *clipContext = new ToolboxClipBoxContext();
      clip->setContext(clipContext);
      const Rect visible = {0, 0, 80, second.h};
      clipContext->setPlacement(ToolboxClipPlacement(visible));
      LOKA_VERIFY(f.stack.detachChildren() == f.node);
      clip->addChild(f.node);
      f.stack.addChild(clip);
      NotifySubtreeNodeAttached(clip);
      ground.publish(clip);
      LOKA_VERIFY(f.click(first, first) && f.writes == 1);
      LOKA_VERIFY(f.click(second, second) && f.groundCalls == 1 && f.writes == 1);
      LOKA_VERIFY(!f.click(first, second) && f.writes == 1 && f.groundCalls == 1);
    }
    std::puts("[pin] P7/P8/P10 linked-area ownership, unlinked pass-through and projection clip passed");
  }
  class CloseApp : public WindowAdmissionTestApp
  {
  public:
    explicit CloseApp(Window &window)
        : WindowAdmissionTestApp(window)
    {
    }
    using App::isWindowClosePending;
  };
  struct Observer
  {
    Fixture &f;
    unsigned mode;
    bool called;
    Window *closing;
    CloseApp *app;
    Observer(Fixture &fixture, unsigned action)
        : f(fixture),
          mode(action),
          called(false),
          closing(0),
          app(0)
    {
      if (action == 2)
      {
        this->closing = new Window(0, WindowProps());
        this->app = new CloseApp(*this->closing);
      }
      this->f.result.bind(&changed, this, false);
    }
    ~Observer()
    {
      this->f.result.unbind(&changed, this);
      delete this->app;
    }
    static void changed(void *data)
    {
      Observer &o = *static_cast<Observer *>(data);
      o.called = true;
      if (o.mode == 0)
      {
        o.f.node->props = AttributedText(Styled("replacement", TextStyle())).props;
        o.f.context()->onPropsApplied();
      }
      else if (o.mode == 1)
      {
        NotifySubtreeNodeDetached(o.f.node);
      }
      else
      {
        o.app->requestWindowClose(o.closing);
        LOKA_VERIFY(o.app->isWindowClosePending(o.closing));
      }
    }
  };
  void observers()
  {
    for (unsigned mode = 0; mode < 3; ++mode)
    {
      Fixture f;
      Observer observer(f, mode);
      LOKA_VERIFY(f.click(f.segment(0), f.segment(0)) && observer.called && f.writes == 1);
      if (mode == 0)
        LOKA_VERIFY(f.node->props.text_->get().segment(0).link.isNone());
      else if (mode == 1)
        LOKA_VERIFY(f.node->lifecycleFact() != NODE_FACT_ATTACHED);
    }
    std::puts("[pin] P9 synchronous observer applies props/detaches/requests close before dispatch returns passed");
  }
  void run(const char *name)
  {
    if (std::strcmp(name, "LinkPress-P11") == 0)
    {
      parkedAndUnknown();
      return;
    }
    mouseScript();
    basic();
    cover();
    groundAndClip();
    observers();
    parkedAndUnknown();
    rows();
  }
} // namespace link_press
