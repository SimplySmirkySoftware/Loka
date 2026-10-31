#ifndef LOKA_TEST_TOOLBOX_PAINT_REFUSAL_PINS_HPP
#define LOKA_TEST_TOOLBOX_PAINT_REFUSAL_PINS_HPP
#include "context/ToolboxClipBoxContext.hpp"
#include "ToolboxScrollViewDecisions.hpp"
#include "ToolboxPaintResultText.hpp"
#include "context/ToolboxScrollBarContext.hpp"
#include "app/nodes/controls/EditText.hpp"
namespace
{
  void refusalSite(const ToolboxPaintRefusalRecord &record, const char *suffix)
  {
    std::fprintf(stderr,
                 "paint site expected=%s actual=%s reason=%s\n",
                 suffix,
                 record.site ? record.site : "-",
                 ToolboxPaintRefusalName(record.reason));
    LOKA_VERIFY(record.refused());
    LOKA_VERIFY(record.site);
    const std::size_t actual = std::strlen(record.site), expected = std::strlen(suffix);
    LOKA_VERIFY(actual >= expected && std::strcmp(record.site + actual - expected, suffix) == 0);
  }
  void refusalScopes(const char *mode)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    const Rect rect = {10, 10, 90, 110};
    if (std::strstr(mode, "Clip"))
    {
      BoxNode node(Box().props);
      if (std::strcmp(mode, "RefusalClipMissing") != 0)
      {
        ToolboxClipBoxContext *context = new ToolboxClipBoxContext;
        context->setPlacement(ToolboxClipPlacement(rect));
        node.setContext(context);
      }
      controller.observation_.reset();
      if (std::strcmp(mode, "RefusalClipRegion") == 0)
        toolbox_host::failRegions = 2;
      if (std::strcmp(mode, "RefusalClipGuard") == 0)
        while (controller.projectionParentScopes_.pushClip(controller.projectionParentScopes_.current()))
        {
        }
      controller.renderClipBox(&node);
      toolbox_host::failRegions = 0;
      refusalSite(controller.observation_.outsideRefusal(),
                  std::strcmp(mode, "RefusalClipMissing") == 0 ? "ToolboxClipBoxRender.cpp:32"
                                                               : "ToolboxClipBoxRender.cpp:29");
      while (controller.projectionParentScopes_.activeDepth())
        controller.projectionParentScopes_.pop();
    }
    else
    {
      ScrollViewNode node((ScrollViewProps()));
      if (std::strcmp(mode, "RefusalScrollMissing") != 0)
      {
        controller.ensureViewportScrollBarControl(
            rect, &node, 200, 80, 0, ToolboxScrollViewResolveMetrics(100, 100, 0), false);
        controller.viewportScroll().contentRect = rect;
      }
      controller.observation_.reset();
      RgnHandle saved = controller.scrollViewClipRgn_;
      if (std::strcmp(mode, "RefusalScrollPersistent") == 0)
        controller.scrollViewClipRgn_ = 0;
      if (std::strcmp(mode, "RefusalScrollRegion") == 0)
        toolbox_host::failRegions = 2;
      controller.renderScrollView(&node);
      controller.scrollViewClipRgn_ = saved;
      toolbox_host::failRegions = 0;
      refusalSite(controller.observation_.outsideRefusal(),
                  std::strcmp(mode, "RefusalScrollMissing") == 0      ? "ToolboxScrollViewRender.cpp:19"
                  : std::strcmp(mode, "RefusalScrollPersistent") == 0 ? "ToolboxScrollViewRender.cpp:24"
                                                                      : "ToolboxScrollViewRender.cpp:42");
    }
  }
  void refusalUnits(const char *mode)
  {
    StaticFixture f;
    const bool live = std::strstr(mode, "Live") != 0;
    f.stack.props.unitGround = live ? ZSTACK_UNIT_GROUND_EXP_BLUR : ZSTACK_UNIT_GROUND_EXP_STATIC_BLUR;
    f.window.drawForFlush();
    f.controller.observation_.reset();
    const char *expected = 0;
    if (std::strcmp(mode, "RefusalUnitRegion") == 0)
    {
      toolbox_host::failRegions = 2;
      expected = "ToolboxZStack.cpp:183";
    }
    else if (std::strstr(mode, "Copy"))
    {
      toolbox_host::staticCopySucceeds = false;
      expected = live ? "ToolboxZStack.cpp:155" : "ToolboxZStack.cpp:169";
    }
    else if (std::strstr(mode, "Device"))
    {
      toolbox_host::staticEligible = false;
      expected = live ? "ToolboxZStack.cpp:158" : "ToolboxZStack.cpp:171";
    }
    else if (std::strstr(mode, "Hairline"))
    {
      toolbox_host::refuseHairlines = 1;
      expected = "ToolboxZStack.cpp:142";
    }
    else if (std::strstr(mode, "Frost"))
    {
      f.controller.bandBlur_.clear();
      ToolboxErasedPass::Guard erased(f.controller.erasedPass(), f.window.port.portRect);
      f.controller.renderZStack(&f.stack);
      refusalSite(f.controller.observation_.outsideRefusal(), "ToolboxZStack.cpp:150");
      return;
    }
    else if (std::strstr(mode, "Absent"))
    {
      f.controller.bandBlur_.clear();
      expected = "ToolboxZStack.cpp:152";
    }
    f.controller.renderZStack(&f.stack);
    toolbox_host::failRegions = 0;
    refusalSite(f.controller.observation_.outsideRefusal(), expected);
  }
  void refusalDefaultNode()
  {
    ToolboxWindow window;
    ToolboxScenePlatformController controller(&window);
    ButtonNode node(Button("missing").props);
    node.Node::render(&controller);
    refusalSite(controller.observation_.outsideRefusal(), "ToolboxRender.cpp:6");
    node.Node::render(0);
  }
  void refusalFormatter()
  {
    ToolboxPresentObservation observation;
    ToolboxSceneDebugStats stats;
    {
      ToolboxPresentObservation::Interval interval(observation, false, stats);
    }
    LOKA_VERIFY(ToolboxPaintResultText(stats.lastPresentObservation)
                == "paint_result=refused rule=2 first=none site=- coverage=-");
    {
      ToolboxPresentObservation::Interval interval(observation, false, stats);
      observation.complete(0);
      observation.markPaintFinished();
    }
    LOKA_VERIFY(ToolboxPaintResultText(stats.lastPresentObservation)
                == "paint_result=refused rule=4 first=none site=- coverage=not_window");
  }
  class LoseScratchProbe : public Probe
  {
  public:
    LoseScratchProbe()
        : Probe("lose-scratch", 20)
    {
    }
    virtual void render(IPlatformController *p)
    {
      static_cast<ToolboxScenePlatformController *>(p)->paintSuppressClipRgn_ = 0;
    }
  };
  void refusalNative(const char *mode)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController c(&window);
    const Rect rect = {10, 10, 90, 110};
    MutableState<bool> enabled(true);
    ScrollViewNode node((ScrollViewProps()));
    const bool horizontal = std::strstr(mode, "Horizontal") != 0;
    const bool acquisition = std::strstr(mode, "Acquire") != 0;
    RgnHandle saved = c.paintSuppressClipRgn_;
    const char *expected = 0;
    if (std::strstr(mode, "Viewport"))
    {
      if (horizontal && acquisition)
        c.ensureViewportScrollBarControl(rect, &node, 200, 80, 0, ToolboxScrollViewMetrics(100, 0), false);
      if (horizontal && !acquisition)
        SetRect(&c.projectionClip, 10, 80, 80, 90);
      c.observation_.reset();
      if (acquisition)
        toolbox_host::refuseControls = 1;
      else
        c.paintSuppressClipRgn_ = 0;
      c.ensureViewportScrollBarControl(rect, &node, 200, 80, 0, ToolboxScrollViewMetrics(100, 0), horizontal);
      expected = acquisition ? (horizontal ? "ToolboxViewportScrollBar.cpp:126" : "ToolboxViewportScrollBar.cpp:101")
                             : (horizontal ? "ToolboxViewportScrollBar.cpp:120" : "ToolboxViewportScrollBar.cpp:95");
    }
    else
    {
      const bool button = std::strstr(mode, "Button") != 0;
      ToolboxScenePlatformController::ButtonControlBinding *b = 0;
      ToolboxScenePlatformController::ScrollBarControlBinding *bar = 0;
      if (button)
        LOKA_VERIFY(c.ensureButtonControl(111, rect, String("button"), 0, &enabled, NATIVE_HINT_EAGER_RELEASE, 0, b));
      else
        LOKA_VERIFY(c.ensureScrollBarBinding(111, rect, 0, 100, 1, 10, NATIVE_HINT_EAGER_RELEASE, bar));
      c.observation_.reset();
      if (std::strstr(mode, "Tail"))
      {
        LoseScratchProbe root;
        c.rootNode_ = &root;
        ToolboxInputDoor::render(c, 0);
        c.rootNode_ = 0;
        c.paintSuppressClipRgn_ = saved;
        refusalSite(c.debugStats_.lastPresentObservation.firstRefusal(),
                    button ? "ToolboxRender.cpp:159" : "ToolboxRender.cpp:169");
        return;
      }
      c.paintSuppressClipRgn_ = 0;
      if (std::strstr(mode, "Enabled"))
      {
        if (button)
          c.refreshButtonEnabled(*b);
        else
        {
          bar->enabled = &enabled;
          c.refreshScrollBarEnabled(*bar);
        }
        expected = button ? "ToolboxEnabledControls.cpp:21" : "ToolboxEnabledControls.cpp:46";
      }
      else
      {
        c.hideRetiredControl(button ? b->control : bar->control);
        expected = "ToolboxNativeRetirement.cpp:103";
      }
    }
    c.paintSuppressClipRgn_ = saved;
    toolbox_host::refuseControls = 0;
    refusalSite(c.observation_.outsideRefusal(), expected);
  }
  void refusalDirty(const char *mode)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController c(&window);
    ButtonNode root(Button("dirty-root").props);
    const Rect rect = {10, 10, 30, 60};
    c.rootNode_ = &root;
    if (std::strcmp(mode, "RefusalDirtySurface") == 0)
    {
      RectSurfaceNode surface((RectSurfaceProps()));
      c.rootNode_ = &surface;
      ToolboxInputDoor::renderDirty(c, rect);
      refusalSite(c.observation_.outsideRefusal(), "ToolboxDirtyRectReplay.inc:18");
    }
    else if (std::strcmp(mode, "RefusalDirtyCell") == 0)
    {
      ToolboxScenePlatformController::CellHit hit = {rect, 0, 0, 0, 0};
      c.installHit(hit);
      ToolboxInputDoor::renderDirty(c, rect);
      refusalSite(c.observation_.outsideRefusal(), "ToolboxDirtyPresentation.cpp:75");
    }
    else
    {
      MutableState<String> value(String("text"));
      ToolboxScenePlatformController::TextHit hit = {0, rect, 10, 25, &value, 0, 30, false};
      if (std::strcmp(mode, "RefusalTextHitConversion") == 0)
        hit.text = 0;
      else
        toolbox_host::failRegions = 2;
      c.redrawTextHit(hit);
      toolbox_host::failRegions = 0;
      refusalSite(c.observation_.outsideRefusal(),
                  hit.text ? "ToolboxTextHitPaint.cpp:58" : "ToolboxTextHitPaint.cpp:53");
    }
    c.rootNode_ = 0;
  }
  class ShiftDuringLayoutProbe : public Probe
  {
  public:
    ShiftDuringLayoutProbe()
        : Probe("shift-during-layout", 30)
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      const short width = Probe::layoutProjected(platform, state);
      // Restoring x=10 succeeds; projecting the original x=10 then exceeds short.
      static_cast<ToolboxScenePlatformController *>(platform)->projectionParentScopes_.current().translationX =
          SHRT_MAX;
      return width;
    }
  };
  void refusalUnitLayout(const char *mode)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController c(&window);
    ZStackNode stack((ZStackProps()));
    Probe *ground = new Probe("ground", 20), *child = std::strcmp(mode, "RefusalLayoutProjection") == 0
                                                          ? static_cast<Probe *>(new ShiftDuringLayoutProbe)
                                                          : new Probe("child", 30);
    stack.addChild(ground);
    stack.addChild(child);
    ProjectionParentScope frame(window.window(), 0, 0, Frame(0, 0, 300, 200));
    ProjectionViewportScopeGuard viewport(c.projectionParentScopes_, frame);
    if (std::strcmp(mode, "RefusalLayoutParent") == 0)
      ground->refuse = true;
    if (std::strcmp(mode, "RefusalLayoutChild") == 0)
      child->refuse = true;
    LayoutState state = seat();
    if (std::strcmp(mode, "RefusalLayoutRight") == 0)
      state.x = SHRT_MAX;
    c.layoutZStack(&stack, state, 0);
    refusalSite(c.observation_.outsideRefusal(),
                ground->refuse                                      ? "ToolboxZStack.cpp:32"
                : child->refuse                                     ? "ToolboxZStack.cpp:46"
                : std::strcmp(mode, "RefusalLayoutProjection") == 0 ? "ToolboxZStack.cpp:52"
                                                                    : "ToolboxZStack.cpp:61");
  }
  void refusalControls(const char *mode)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController c(&window);
    const Rect rect = {10, 10, 90, 110};
    RgnHandle scratch = c.paintSuppressClipRgn_;
    const char *expected = 0;
    if (std::strstr(mode, "Button"))
    {
      ButtonNode node(Button("native").props);
      ToolboxButtonContext *context = new ToolboxButtonContext(&node, &c);
      node.setContext(context);
      LayoutState placement = seat();
      context->layout(&c, placement);
      if (std::strstr(mode, "Props"))
        context->draw(&c);
      c.observation_.reset();
      if (std::strstr(mode, "Acquire"))
        toolbox_host::refuseControls = 1;
      else
        c.paintSuppressClipRgn_ = 0;
      if (std::strstr(mode, "Props"))
        c.refreshContextProps(&node, c.buttonControls_[0].resourceId);
      else
        context->draw(&c);
      c.paintSuppressClipRgn_ = scratch;
      toolbox_host::refuseControls = 0;
      expected = std::strstr(mode, "Props")     ? "ToolboxContextProps.cpp:90"
                 : std::strstr(mode, "Acquire") ? "ToolboxButtonContext.cpp:183"
                                                : "ToolboxButtonControl.cpp:101";
      refusalSite(c.observation_.outsideRefusal(), expected);
      return;
    }
    if (std::strstr(mode, "Popup"))
    {
      ToolboxPopupMenuContext context(0, &c);
      context.updateRect(rect, 14);
      context.draw();
      refusalSite(c.observation_.outsideRefusal(), "ToolboxPopupMenuContext.cpp:181");
      return;
    }
    if (std::strstr(mode, "Standalone"))
    {
      ScrollBarNode node((ScrollBarProps()));
      ToolboxScrollBarContext context(std::strstr(mode, "Missing") ? 0 : &node, &c);
      context.updateRect(rect);
      if (std::strstr(mode, "Acquire"))
        toolbox_host::refuseControls = 1;
      if (std::strstr(mode, "Suppress"))
        c.paintSuppressClipRgn_ = 0;
      context.draw(&c);
      c.paintSuppressClipRgn_ = scratch;
      toolbox_host::refuseControls = 0;
      expected = std::strstr(mode, "Missing")   ? "ToolboxScrollBarContext.cpp:98"
                 : std::strstr(mode, "Acquire") ? "ToolboxScrollBarContext.cpp:119"
                                                : "ToolboxScrollBarLedger.cpp:246";
    }
    else
    {
      ScrollViewNode node((ScrollViewProps()));
      const ToolboxViewportRows rows =
          c.ensureViewportScrollBarControl(rect, &node, 200, 80, 0, ToolboxScrollViewMetrics(100, 0), true);
      c.observation_.reset();
      c.paintSuppressClipRgn_ = 0;
      if (std::strstr(mode, "Request"))
      {
        c.applyViewportScrollRequest(node, rows, Frame(1, 1, 100, 80));
        expected = "ToolboxControlInput.cpp:103";
      }
      else
      {
        SetRect(&c.projectionClip, 200, 200, 300, 300);
        c.ensureViewportScrollBarControl(rect, &node, 200, 80, 0, ToolboxScrollViewMetrics(100, 0), false);
        expected = "ToolboxViewportScrollBar.cpp:145";
      }
    }
    c.paintSuppressClipRgn_ = scratch;
    refusalSite(c.observation_.outsideRefusal(), expected);
  }
  void editOmission(bool offscreen)
  {
    MutableState<String> text(String("native edit"));
    NodeState<String> textSeat(&text, 0);
    BoxNode *content = new BoxNode(Box().size(180, 600).props);
    content->addChild(new EditTextNode(EditTextProps(textSeat)));
    toolbox_host::failNew = 1;
    PresentFixture f(false, offscreen ? 160 : 0, content);
    const ToolboxPresentObservation &result = f.last();
    LOKA_VERIFY(result.paintFinished());
    LOKA_VERIFY(result.eligibility() == (offscreen ? TOOLBOX_PAINT_CLEAN : TOOLBOX_PAINT_REFUSED));
    LOKA_VERIFY(result.firstRefusal().reason
                == (offscreen ? TOOLBOX_PAINT_REFUSAL_NONE : TOOLBOX_PAINT_REFUSAL_NATIVE_ACQUISITION));
    LOKA_VERIFY(f.controller.editControls_.empty());
    LOKA_VERIFY(toolbox_host::failNew == (offscreen ? 1 : 0));
    toolbox_host::failNew = 0;
  }
  void unitSeatOmission(bool refused)
  {
    if (refused)
    {
      StaticFixture f;
      f.unit->refuse = true;
      f.window.drawForFlush();
      LOKA_VERIFY(!unitRow(f.controller, &f.stack, 1));
      const ToolboxPresentObservation &result = f.controller.debugStats_.lastPresentObservation;
      LOKA_VERIFY(result.paintFinished());
      LOKA_VERIFY(result.eligibility() == TOOLBOX_PAINT_REFUSED);
      refusalSite(result.firstRefusal(), "ToolboxZStack.cpp:46");
      return;
    }
    ZStackNode *stack = new ZStackNode(ZStackProps());
    stack->props.unitGround = ZSTACK_UNIT_GROUND_EXP_BLUR;
    Probe *omitted = new Probe("offscreen-unit", 20);
    class LaterUnit : public Probe
    {
    public:
      LaterUnit()
          : Probe("later-unit", 200)
      {
      }
      virtual void render(IPlatformController *platform)
      {
        LOKA_VERIFY(!static_cast<ToolboxScenePlatformController *>(platform)->observation_.refusedSoFar());
        Probe::render(platform);
      }
    };
    Probe *later = new LaterUnit;
    ZStackNode *ground = new ZStackNode(ZStackProps());
    ground->addChild(new Probe("ground", 600));
    ground->addChild(omitted);
    stack->addChild(ground);
    stack->addChild(later);
    toolbox_host::bandCaptures = 0;
    toolbox_host::staticEligible = toolbox_host::staticCaptureSucceeds = toolbox_host::staticCopySucceeds = true;
    PresentFixture f(false, 80, stack);
    LOKA_VERIFY(!unitRow(f.controller, ground, 1));
    LOKA_VERIFY(unitRow(f.controller, stack, 1));
    LOKA_VERIFY(omitted->layouts > 0 && omitted->renders == 0);
    LOKA_VERIFY(later->renders == 1);
    LOKA_VERIFY(toolbox_host::bandCaptures == 1);
    const ToolboxPresentObservation &result = f.last();
    LOKA_VERIFY(result.paintFinished());
    LOKA_VERIFY(result.eligibility() == TOOLBOX_PAINT_CLEAN);
    LOKA_VERIFY(!result.firstRefusal().refused());
  }
  void runRefusalPin(const char *mode)
  {
    if (std::strcmp(mode, "RefusalUnitOmitted") == 0 || std::strcmp(mode, "RefusalUnitLayoutRefused") == 0)
      unitSeatOmission(std::strcmp(mode, "RefusalUnitLayoutRefused") == 0);
    else if (std::strcmp(mode, "RefusalEditOffscreen") == 0 || std::strcmp(mode, "RefusalEditVisibleRefused") == 0)
      editOmission(std::strcmp(mode, "RefusalEditOffscreen") == 0);
    else if (std::strstr(mode, "Control"))
      refusalControls(mode);
    else if (std::strstr(mode, "Clip") || std::strstr(mode, "Scroll"))
      refusalScopes(mode);
    else if (std::strcmp(mode, "RefusalDefaultNode") == 0)
      refusalDefaultNode();
    else if (std::strcmp(mode, "RefusalFormatter") == 0)
      refusalFormatter();
    else if (std::strstr(mode, "Viewport") || std::strstr(mode, "Tail") || std::strstr(mode, "Enabled")
             || std::strstr(mode, "Retired"))
      refusalNative(mode);
    else if (std::strstr(mode, "Dirty") || std::strstr(mode, "TextHit"))
      refusalDirty(mode);
    else if (std::strstr(mode, "Layout"))
      refusalUnitLayout(mode);
    else
      refusalUnits(mode);
  }
} // namespace
#endif
