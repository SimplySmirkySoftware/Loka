#include "ToolboxNodeDispatch.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxBuiltInSupport.hpp"
#include "ToolboxPlatformLayoutHandlers.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ScrollViewSettlement.hpp"
#include "support/Headless.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include <cstring>
using namespace loka::app;
using namespace loka::app::scene;
using loka::core::Frame;
namespace
{
  class Storage : private HeadlessStateOwner
  {
  public:
    NodeState<Frame> viewport;
    Request<ScrollTarget> request;
    explicit Storage(bool immediate = true)
    {
      if (!immediate)
        return;
      StateBatchBase::CreateImmediateState(this, this->viewport, Frame());
      StateBatchBase::CreateImmediateState(this, this->request, ScrollTarget::None());
    }
  };
  typedef loka::app::testing::SettleTrace<ScrollTarget, Frame> Trace;

  struct Fixture
  {
    Storage storage;
    ToolboxWindow window;
    HostPortScope port;
    ToolboxScenePlatformController controller;
    ScrollViewNode node;
    BoxNode *content;
    unsigned writes;
    static void count(void *data)
    {
      Fixture &f = *static_cast<Fixture *>(data);
      ++f.writes;
      f.controller.batchEvents += "P";
    }
    Fixture()
        : port(window.window()),
          controller(&window),
          node(ScrollViewProps(storage.viewport).scrollTo(storage.request)),
          content(new BoxNode(Box().width(400).height(500).props)),
          writes(0)
    {
      LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
      LOKA_VERIFY(RegisterToolboxPlatformLayoutHandlers(controller.gapLayoutRegistry));
      node.addChild(content);
      storage.viewport.state()->bind(&count, this, false);
      Trace::instance().clear();
    }
    ~Fixture()
    {
      storage.viewport.state()->unbind(&count, this);
      LifecycleFactTestAccess::MarkSubtreeRetired(&node);
      LifecycleFactTestAccess::DeliverFacts(&node);
      controller.releaseNodeContexts(&node);
    }
    short visit(short width = 100, short height = 80)
    {
      LayoutState state;
      state.width = width;
      state.height = height;
      const short result = LayoutNode(&node, state, &controller, 0);
      LOKA_VERIFY(!controller.borrowPhase().open());
      return result;
    }
    void post(int x, int y)
    {
      storage.request.set(ScrollTarget::Origin(x, y));
    }
    void result(ScrollRequestResult expected)
    {
      LOKA_VERIFY(storage.request.get().isNone());
      LOKA_VERIFY(Trace::instance().size() != 0);
      const loka::app::testing::SettleTraceRow<ScrollTarget, Frame> &row =
          Trace::instance().at(Trace::instance().size() - 1);
      LOKA_VERIFY(row.count == 1 && row.seam[0] == expected);
    }
  };
  void delivery(bool clamp)
  {
    Fixture f;
    const Rect previous = {0, 0, 260, 370};
    ClipRect(&previous);
    // First visit creates both bars: the second push invalidates the first row pointer.
    f.post(clamp ? 30000 : 25, clamp ? 30000 : 40);
    f.visit();
    f.result(SCROLL_OK);
    LOKA_VERIFY(f.controller.scrollControls().size() == 2);
    LOKA_VERIFY(f.storage.viewport.get().x == (clamp ? f.controller.scrollControl(1).maximum : 25));
    LOKA_VERIFY(f.storage.viewport.get().y == (clamp ? f.controller.scrollControl(0).maximum : 40));
    LOKA_VERIFY(GetControlValue(f.controller.scrollControl(1).control) == f.storage.viewport.get().x);
    LOKA_VERIFY(GetControlValue(f.controller.scrollControl(0).control) == f.storage.viewport.get().y);
    LOKA_VERIFY(f.controller.pendingDirtyRects_.empty());
    LOKA_VERIFY(f.controller.batchEvents == "PP");
    for (unsigned i = 0; i != 2; ++i)
    {
      const toolbox_host::ControlCall &call = f.controller.scrollControl(i).control->calls.back();
      LOKA_VERIFY(call.operation == "value" && call.currentPort == f.window.window());
      LOKA_VERIFY(EmptyRect(&call.ownerClip) && !call.painted);
    }
    const Rect restored = toolbox_host::currentClip();
    LOKA_VERIFY(restored.top == previous.top && restored.left == previous.left && restored.bottom == previous.bottom
                && restored.right == previous.right);
    const unsigned writes = f.writes;
    f.visit();
    LOKA_VERIFY(f.writes == writes);
  }
  void lateRefusal()
  {
    Fixture f;
    f.content->props = Box().width(20).height(SHRT_MAX).props;
    f.node.props.contentInsets(ScrollViewContentInsets::of(1, 0));
    f.post(0, 20);
    LOKA_VERIFY(f.visit() == 100);
    f.result(SCROLL_UNAVAILABLE);
    LOKA_VERIFY(f.writes == 0 && f.controller.scrollControls().empty());
  }
  void zeroRange()
  {
    Fixture f;
    f.content->props = Box().width(20).height(10).props;
    f.visit();
    LOKA_VERIFY(f.controller.scrollControls().size() == 1 && f.controller.scrollControl(0).maximum == 0);
    const unsigned writes = f.writes;
    f.post(0, 50);
    f.visit();
    f.result(SCROLL_OK);
    LOKA_VERIFY(f.storage.viewport.get().y == 0 && f.writes == writes);
  }
  void drag(bool cancelled)
  {
    Fixture f;
    f.visit();
    f.post(25, 40);
    f.visit();
    const unsigned writes = f.writes;
    ControlRef bar = f.controller.scrollControl(0).control;
    bar->tracking = toolbox_host::TrackScript(129, 129, cancelled ? 40 : 0);
    f.controller.publishFixturePressOrder();
    Point point = {static_cast<short>(bar->rect.top + 20), static_cast<short>(bar->rect.left + 3)};
    f.controller.batchEvents.clear();
    const unsigned tracks = toolbox_host::tracks;
    ToolboxInputDoor::mouseDown(f.controller, point);
    LOKA_VERIFY(toolbox_host::tracks == tracks + 1);
    LOKA_VERIFY(f.controller.batchEvents == (cancelled ? "" : "BDPE"));
    LOKA_VERIFY(f.writes == writes + (cancelled ? 0 : 1));
    LOKA_VERIFY(f.storage.viewport.get().y == (cancelled ? 40 : 0));
    LOKA_VERIFY(f.storage.viewport.get().x == 25);
    LOKA_VERIFY(f.controller.pendingDirtyRects_.size() == (cancelled ? 0u : 1u));
  }
  void absent(const char *kind)
  {
    Fixture f;
    if (!std::strcmp(kind, "creation"))
    {
      toolbox_host::refuseControls = 1;
      f.post(0, 40);
      f.visit();
      f.result(SCROLL_UNAVAILABLE);
      LOKA_VERIFY(f.storage.viewport.get().x == 0 && f.storage.viewport.get().y == 0);
      LOKA_VERIFY(f.writes == 1); // The layout still publishes dimensions.
    }
    else if (!std::strcmp(kind, "heightless"))
    {
      f.post(20, 0);
      f.visit(100, 0);
      f.result(SCROLL_UNAVAILABLE);
    }
    else if (!std::strcmp(kind, "clipped"))
    {
      ProjectionParentScope scope = f.controller.projectionParentScopes_.current();
      scope.clipRect = Frame(0, 0, 20, 20);
      ProjectionClipScopeGuard guard(f.controller.projectionParentScopes_, scope);
      f.post(0, 40);
      f.visit();
      f.result(SCROLL_UNAVAILABLE);
    }
    else
    {
      if (!std::strcmp(kind, "stale"))
        f.visit();
      f.post(20, 0);
      f.visit(500);
      f.result(SCROLL_UNAVAILABLE);
      LOKA_VERIFY(f.storage.viewport.get().x == 0);
      if (!std::strcmp(kind, "stale"))
        LOKA_VERIFY(!f.controller.scrollControl(1).control->visible);
      f.post(0, 40);
      f.visit(500);
      f.result(SCROLL_OK);
      LOKA_VERIFY(f.storage.viewport.get().y == 40);
    }
  }
  class Root;
  struct RootProps : NodePropsBase<RootProps>
  {
    typedef Root NodeType;
    struct TypeTag
    {
    };
    Storage *storage;
    explicit RootProps(Storage *value = 0)
        : storage(value)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      return this->storage < static_cast<const RootProps &>(rhs).storage;
    }
  };
  class Root : public StdCompositionBoundaryNodeBase<RootProps>
  {
  public:
    explicit Root(const RootProps &props)
        : StdCompositionBoundaryNodeBase<RootProps>(props)
    {
      this->state(this->props.storage->viewport, Frame());
      this->state(this->props.storage->request, ScrollTarget::None());
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(
          ScrollView(ScrollViewProps(this->props.storage->viewport).scrollTo(this->props.storage->request))
          << Box().width(400).height(500));
    }
  };
  /** Runs real dispatch from the Scene apply seam to exercise an active run. */
  class SceneController : public ToolboxScenePlatformController
  {
  public:
    bool projectInApply;
    unsigned visits, focusReads;
    explicit SceneController(ToolboxWindow &window)
        : ToolboxScenePlatformController(&window),
          projectInApply(true),
          visits(0),
          focusReads(0)
    {
      LOKA_VERIFY(RegisterToolboxBuiltInSupport(*this));
      LOKA_VERIFY(RegisterToolboxPlatformLayoutHandlers(this->gapLayoutRegistry));
    }
    virtual void onBoundaryApply(Node *root,
                                 BoundaryNode *boundary,
                                 const BoundaryLocalApplyInfo &info,
                                 const PlatformApplyPlan &plan)
    {
      ToolboxScenePlatformController::onBoundaryApply(root, boundary, info, plan);
      if (this->projectInApply && (info.hasLayoutWork || plan.hasLayoutWork()))
      {
        ++this->visits;
        LayoutState state;
        state.width = 100;
        state.height = 80;
        LayoutNode(root, state, this, boundary);
      }
    }
    virtual bool readNativeFocus(NodeContext *&out)
    {
      ++this->focusReads;
      out = 0;
      return true;
    }
  };
  class LogicalWindow : public Window
  {
    ToolboxWindow &native_;

  public:
    unsigned syncs, admissions, drains;
    LogicalWindow(ToolboxWindow &native, Storage &storage)
        : Window(0, WindowProps().scene(new Scene(Boundary<Root>(RootProps(&storage))))),
          native_(native),
          syncs(0),
          admissions(0),
          drains(0)
    {
    }
    virtual ~LogicalWindow()
    {
      this->unmountSceneForTeardown(*this->scene());
    }
    virtual bool hasLiveScenePlatform() const
    {
      return true;
    }
    virtual ToolboxWindow *asToolboxWindow()
    {
      return &this->native_;
    }
    virtual void applyNativeVisibility()
    {
      ++this->admissions;
    }
    virtual bool hasPendingScenePlatformSync() const
    {
      return true;
    }
    virtual void synchronizeScenePlatform()
    {
      ++this->syncs;
    }
    virtual void drainNativeRetirements()
    {
      ++this->drains;
    }
  };
  struct SceneFixture
  {
    Storage storage;
    ToolboxWindow native;
    HostPortScope port;
    SceneController controller;
    LogicalWindow window;
    WindowAdmissionTestApp app;
    unsigned callbacks;
    bool probe, expectRun;
    SceneFixture()
        : storage(false),
          port(native.window()),
          controller(native),
          window(native, storage),
          app(window),
          callbacks(0),
          probe(false),
          expectRun(false)
    {
      LOKA_VERIFY(window.scene()->mount(&controller));
      app.flush();
      loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
      window.scene()->requestInvalidate(NODE_DIRTY_LAYOUT);
      app.flush();
      while (window.scene()->hasPendingInvalidation())
        LOKA_VERIFY(window.scene()->flushInvalidation());
      storage.viewport.state()->bind(&changed, this, false);
    }
    ~SceneFixture()
    {
      storage.viewport.state()->unbind(&changed, this);
    }
    static void changed(void *data)
    {
      SceneFixture &f = *static_cast<SceneFixture *>(data);
      ++f.callbacks;
      if (!f.probe)
        return;
      LOKA_VERIFY(f.controller.borrowPhase().open());
      LOKA_VERIFY(f.window.scene()->isRunInProgress() == f.expectRun);
      const unsigned syncs = f.window.syncs, drains = f.window.drains;
      const unsigned admissions = f.window.admissions, focus = f.controller.focusReads;
      f.window.scene()->requestInvalidate(NODE_DIRTY_LAYOUT);
      LOKA_VERIFY(!f.window.scene()->flushInvalidation());
      LOKA_VERIFY(!f.window.flushSceneInvalidation());
      f.app.admitAndApplyWindows();
      f.app.reconcileFocus();
      LOKA_VERIFY(f.window.syncs == syncs && f.window.drains == drains);
      LOKA_VERIFY(f.window.admissions == admissions && f.controller.focusReads == focus);
    }
    void ordinaryFlush()
    {
      const unsigned syncs = window.syncs, drains = window.drains;
      const unsigned admissions = window.admissions, focus = controller.focusReads;
      app.flush();
      app.reconcileFocus();
      LOKA_VERIFY(window.syncs > syncs && window.drains > drains);
      LOKA_VERIFY(window.admissions > admissions && controller.focusReads > focus);
      LOKA_VERIFY(!window.scene()->hasPendingInvalidation());
    }
  };
  void sceneBorrow(bool probe)
  {
    SceneFixture f;
    f.expectRun = true;
    f.probe = probe;
    const unsigned visits = f.controller.visits;
    f.storage.request.set(ScrollTarget::Origin(25, 40));
    while (f.window.scene()->hasPendingInvalidation())
      LOKA_VERIFY(f.window.scene()->flushInvalidation());
    LOKA_VERIFY(f.storage.request.get().isNone());
    LOKA_VERIFY(f.storage.viewport.get().x == 25 && f.storage.viewport.get().y == 40);
    LOKA_VERIFY(f.callbacks == 1);
    if (!probe)
      LOKA_VERIFY(f.controller.visits == visits + 2);
    f.probe = false;
    f.ordinaryFlush();
  }
  struct MountProbe
  {
    Scene &scene;
    SceneController &controller;
    unsigned calls;
    MountProbe(Scene &s, SceneController &c)
        : scene(s),
          controller(c),
          calls(0)
    {
    }
    static void changed(void *data)
    {
      MountProbe &p = *static_cast<MountProbe *>(data);
      ++p.calls;
      LOKA_VERIFY(p.controller.borrowPhase().open() && !p.scene.isRunInProgress());
      p.scene.requestInvalidate(NODE_DIRTY_LAYOUT);
      LOKA_VERIFY(!p.scene.flushInvalidation());
    }
  };
  void deferredMount()
  {
    Storage storage(false);
    ToolboxWindow native;
    HostPortScope port(native.window());
    SceneController controller(native);
    LogicalWindow window(native, storage);
    WindowAdmissionTestApp app(window);
    LOKA_VERIFY(window.scene()->mount(&controller));
    loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
    MountProbe probe(*window.scene(), controller);
    storage.viewport.state()->bind(&MountProbe::changed, &probe, false);
    // Initial projection of the mounted tree, before its first ordinary flush.
    LayoutState state;
    state.width = 100;
    state.height = 80;
    LayoutNode(loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()), state, &controller, 0);
    LOKA_VERIFY(probe.calls == 1);
    LOKA_VERIFY(window.scene()->hasPendingInvalidation() && !controller.borrowPhase().open());
    storage.viewport.state()->unbind(&MountProbe::changed, &probe);
    app.flush();
    LOKA_VERIFY(!window.scene()->hasPendingInvalidation());
  }
  class OuterProbe : public NodeContext
  {
    SceneController &controller_;

  public:
    unsigned calls;
    explicit OuterProbe(SceneController &controller)
        : controller_(controller),
          calls(0)
    {
    }
    virtual void render(IPlatformController *)
    {
      ++this->calls;
      LOKA_VERIFY(this->controller_.borrowPhase().open());
    }
  };
  void renderBorrow()
  {
    SceneFixture f;
    f.controller.projectInApply = false;
    f.probe = true;
    f.storage.request.set(ScrollTarget::Origin(25, 40));
    const Rect dirty = {0, 0, 260, 370};
    OuterProbe outer(f.controller);
    f.controller.renderContext = &outer;
    ToolboxInputDoor::renderDirty(f.controller, dirty);
    f.controller.renderContext = 0;
    LOKA_VERIFY(f.storage.request.get().isNone());
    LOKA_VERIFY(f.storage.viewport.get().x == 25 && f.storage.viewport.get().y == 40);
    LOKA_VERIFY(f.callbacks >= 1);
    LOKA_VERIFY(!f.controller.borrowPhase().open());
    LOKA_VERIFY(outer.calls == 1 && !f.controller.borrowPhase().open());
    f.probe = false;
    f.ordinaryFlush();
  }

} // namespace
int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  const char *name = argv[1];
  if (!std::strcmp(name, "delivery"))
    delivery(false);
  else if (!std::strcmp(name, "late-refusal"))
    lateRefusal();
  else if (!std::strcmp(name, "zero-range"))
    zeroRange();
  else if (!std::strcmp(name, "clamp"))
    delivery(true);
  else if (!std::strcmp(name, "cancel"))
    drag(true);
  else if (!std::strcmp(name, "input"))
    drag(false);
  else if (!std::strcmp(name, "borrow"))
    sceneBorrow(true);
  else if (!std::strcmp(name, "two-visits"))
    sceneBorrow(false);
  else if (!std::strcmp(name, "render"))
    renderBorrow();
  else if (!std::strcmp(name, "mount"))
    deferredMount();
  else
    absent(name);
}
