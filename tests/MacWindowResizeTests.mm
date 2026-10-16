#include "MacWindowResizeTests.hpp"
#include "support/TestVerify.hpp"

#include <AppKit/AppKit.h>

#include "MacApp.hpp"
#include "MacWindow.hpp"
#include "MacScenePlatformController.hpp"
#include "platform/MacProjection.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "testing/MacWindowTestAccess.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/controls/Button.hpp"
#include "core/State.hpp"
#include "core/util/StateTrackerGuard.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  typedef loka::dsl::testing::MacWindowTestAccess NativeAccess;

  /** Cocoa resources outlive every native fixture object. */
  struct CocoaHost
  {
    NSAutoreleasePool *pool;
    CocoaHost()
        : pool([[NSAutoreleasePool alloc] init])
    {
      [NSApplication sharedApplication];
    }
    ~CocoaHost()
    {
      [this->pool drain];
    }
  };

  /** Borrowed-window App, as in the input-door fixture, so the Scene mounts. */
  class ResizeTestApp : public MacApp
  {
  public:
    explicit ResizeTestApp(Window &window)
        : MacApp(0)
    {
      this->group_ = new AppComponentGroup(std::vector<AppComponent *>(1, &window));
    }
    virtual ~ResizeTestApp()
    {
      this->group_->build();
    }
    virtual void quit() {}
    void flush()
    {
      this->flushWindowInvalidations();
    }
  };

  void countNotification(void *userData)
  {
    ++(*static_cast<int *>(userData));
  }

  struct WidthIsNarrow : public DerivedState<bool>::EvalFn
  {
    const State<Frame> *frame;
    explicit WidthIsNarrow(const State<Frame> *value)
        : frame(value)
    {
    }
    virtual bool operator()()
    {
      return this->frame && this->frame->get().hasSize() && this->frame->get().width < 320;
    }
  };

  loka::macos::MacProjection projectionOf(MacWindow &window)
  {
    return loka::macos::MacProjection(NativeAccess::contentView(window), loka::macos::DefaultRailMetrics());
  }

  /** A user-style resize: the native window changes its content size with the
      top-left corner fixed, and AppKit delivers windowDidResize itself. */
  void resizeNativeContent(NSWindow *window, NSSize size)
  {
    const NSRect content = [window contentRectForFrameRect:[window frame]];
    const NSRect target = NSMakeRect(content.origin.x, NSMaxY(content) - size.height, size.width, size.height);
    [window setFrame:[window frameRectForContentRect:target] display:NO];
  }

  /** App-side observer of the native fact: it clamps the intent's width, which
      re-enters the rail through FrameChangedThunk -> setFrame: -> windowDidResize. */
  struct ClampFacts
  {
    PushStateTracker tracker;
    MutableState<Frame> extent;
    NodeState<Frame> extentFact;
    MacWindow *window;
    int clampWidth;
    unsigned clamps;
    ClampFacts()
        : extent(Frame()),
          extentFact(&extent, &tracker),
          window(0),
          clampWidth(320),
          clamps(0)
    {
      this->tracker.addState(&this->extent);
    }
    static void nativeChanged(void *userData)
    {
      ClampFacts &facts = *static_cast<ClampFacts *>(userData);
      const Frame native = facts.window->nativeFrame().get();
      if (!native.hasSize() || native.width <= facts.clampWidth)
        return;
      ++facts.clamps;
      StateTrackerGuard guard(facts.window->getTracker());
      facts.window->frameState().set(Frame(native.x, native.y, facts.clampWidth, native.height));
    }
  };

  template <class T> struct ClampProps : NodePropsBase<ClampProps<T> >
  {
    typedef T NodeType;
    struct TypeTag
    {
    };
    ClampFacts *facts;
    explicit ClampProps(ClampFacts *value = 0)
        : facts(value)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      return this->facts < static_cast<const ClampProps &>(rhs).facts;
    }
  };

  class ClampRoot : public StdCompositionBoundaryNodeBase<ClampProps<ClampRoot> >
  {
    typedef StdCompositionBoundaryNodeBase<ClampProps<ClampRoot> > Base;

  public:
    explicit ClampRoot(const ClampProps<ClampRoot> &props)
        : Base(props)
    {
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Column() << RectSurface().laidOutExtent(this->props.facts->extentFact));
    }
  };
} // namespace

void testMacWindowUserResizeStoresNativeFrameWithoutEchoingIntent()
{
  CocoaHost host;
  NullPlatformContext context;
  WindowProps props;
  props.frame(60, 60, 360, 260).visible(true);
  MacWindow window(&context, props);
  NSWindow *native = (NSWindow *)NativeAccess::nativeWindow(window);
  LOKA_VERIFY(native != nil);

  const Frame declared = window.frameState().get();
  const Frame before = window.nativeFrame().get();
  LOKA_VERIFY(before.width == 360 && before.height == 260);
  // An unclamped placement reads back as declared: the fact's origin is the
  // exact inverse of the intent's, so writing it back does not move the window.
  LOKA_VERIFY(before.x == declared.x && before.y == declared.y);

  State<Frame> &nativeFrame = window.nativeFrame();
  DerivedState<bool> narrow(&nativeFrame, new WidthIsNarrow(&nativeFrame));
  PushStateTracker *tracker = window.getTracker()->asPushTracker();
  LOKA_VERIFY(tracker != 0);
  tracker->addState(&narrow);
  int notifications = 0;
  narrow.bind(&countNotification, &notifications, false);

  resizeNativeContent(native, projectionOf(window).projectClientSize(300, 200).r.size);
  const Frame resized = window.nativeFrame().get();
  LOKA_VERIFY(resized.width == 300 && resized.height == 200);
  LOKA_VERIFY(resized.x == before.x && resized.y == before.y);
  // The rail writes only the native fact; the declared intent is not echoed.
  LOKA_VERIFY(window.frameState().get() == declared);
  // Derived state settles inside the native resize, once.
  LOKA_VERIFY(narrow.get());
  LOKA_VERIFY(notifications == 1);

  narrow.unbind(&countNotification, &notifications);
  tracker->removeState(&narrow);
}

void testMacWindowNativeFrameClampReentersAndRelayouts()
{
  CocoaHost host;
  ClampFacts facts;
  NullPlatformContext platform;
  MacWindow window(&platform,
                   WindowProps()
                       .frame(60, 60, 360, 260)
                       .visible(true)
                       .scene(new Scene(Boundary<ClampRoot>(ClampProps<ClampRoot>(&facts)))));
  ResizeTestApp app(window);
  app.flush();
  window.setApp(&app);
  facts.window = &window;
  NSWindow *native = (NSWindow *)NativeAccess::nativeWindow(window);
  LOKA_VERIFY(native != nil);

  const Frame before = window.nativeFrame().get();
  LOKA_VERIFY(before.width == 360 && before.height == 260);
  // The surface fills the content width less the content inset; the inset is
  // measured here so the final check follows the settled width, not a constant.
  const int inset = before.width - facts.extent.get().width;
  LOKA_VERIFY(facts.extent.get().width > 0 && inset >= 0);

  window.nativeFrame().bind(&ClampFacts::nativeChanged, &facts, false);
  resizeNativeContent(native, projectionOf(window).projectClientSize(400, 260).r.size);
  app.flush();

  // One correction: the clamp re-entered the rail and the second native
  // resize (320) did not ask again.
  LOKA_VERIFY(facts.clamps == 1);
  const NSRect content = [native contentRectForFrameRect:[native frame]];
  LOKA_VERIFY(projectionOf(window).clientCapacityToLu(content.size.width) == 320);
  const Frame settled = window.nativeFrame().get();
  LOKA_VERIFY(settled.width == 320 && settled.height == 260);
  LOKA_VERIFY(settled.x == before.x && settled.y == before.y);
  LOKA_VERIFY(window.frameState().get().width == 320);
  // The scene relaid out from the settled bounds, not from the 400 detour.
  LOKA_VERIFY(facts.extent.get().width == 320 - inset);

  window.nativeFrame().unbind(&ClampFacts::nativeChanged, &facts);
  // The borrowed stack Window outlives its App; revoke its Scene first.
  loka::dsl::testing::SceneTestAccess::unmount(*window.scene());
  window.setApp(0);
}

namespace
{
  void verifyRootMargin(NSView *root, int margin, int expectedWidth, int expectedHeight)
  {
    LOKA_VERIFY(root != nil);
    {
      MacScenePlatformController controller(root, loka::macos::DefaultRailMetrics());
      ScrollViewNode scroll((ScrollViewProps()));
      scroll.addChild(new ButtonNode(ButtonProps()));
      controller.onChange(&scroll, NODE_DIRTY_NONE, false);
      controller.relayout(300, 140);
      LOKA_VERIFY([[root subviews] count] == 1);
      NSScrollView *view = (NSScrollView *)[[root subviews] objectAtIndex:0];
      LOKA_VERIFY([view isKindOfClass:[NSScrollView class]]);
      // The real MacWindow content view is flipped; frames use its top-left origin.
      const NSRect expected = controller.projection()
                                  .projectFrame(Frame(margin, margin, expectedWidth, expectedHeight)).r;
      const NSRect actual = [view frame];
      LOKA_VERIFY(NSEqualRects(actual, expected));
      NSView *document = [view documentView];
      LOKA_VERIFY([document isFlipped]);
      LOKA_VERIFY([[document subviews] count] == 1);
      // A zero root height is still "no height given": content is laid out.
      LOKA_VERIFY([[[document subviews] objectAtIndex:0] frame].size.height > 0);
      controller.onChange(0, NODE_DIRTY_NONE, false);
    }
    LOKA_VERIFY([[root subviews] count] == 0);
  }

  void verifyWindowMargin(WindowProps props, int margin, int width, int height)
  {
    CocoaHost host;
    NullPlatformContext platform;
    props.frame(60, 60, 300, 140).visible(true);
    MacWindow window(&platform, props);
    NSView *root = (NSView *)NativeAccess::contentView(window);
    LOKA_VERIFY(root != nil);
    LOKA_VERIFY([root isFlipped]);
    LOKA_VERIFY(MacWindow::fromRootView(root) == &window);
    verifyRootMargin(root, margin, width, height);
  }
} // namespace

void testMacWindowContentMarginUnspecified()
{
  verifyWindowMargin(WindowProps(), 16, 268, 108);
}

void testMacWindowContentMarginZero()
{
  verifyWindowMargin(WindowProps().contentMargin(0), 0, 300, 140);
}

void testMacWindowContentMarginExplicit()
{
  verifyWindowMargin(WindowProps().contentMargin(20), 20, 260, 100);
}

void testMacWindowContentMarginStandard()
{
  verifyWindowMargin(WindowProps().contentMargin(StandardWindowMargin()), 16, 268, 108);
}

void testMacWindowContentMarginOversized()
{
  verifyWindowMargin(WindowProps().contentMargin(200), 200, 0, 0);
}

void testMacWindowContentMarginWithoutWindow()
{
  CocoaHost host;
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 140)];
  LOKA_VERIFY(root != nil);
  LOKA_VERIFY(MacWindow::fromRootView(root) == 0);
  verifyRootMargin(root, 16, 268, 108);
  [root release];
}
