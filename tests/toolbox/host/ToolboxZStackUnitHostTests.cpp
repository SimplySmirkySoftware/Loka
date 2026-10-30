#include "support/TextBufferFixture.hpp"
#include "ToolboxUpdateEvidence.hpp"
#include "ToolboxUnitHairline.hpp"
#include "ToolboxHost.hpp"
#include "ToolboxNodeDispatch.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxDirtyReplay.hpp"
#include "ToolboxPlatformLayoutHandlers.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include <cstring>
#include <cstdio>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "support/TextEditorStateOwner.hpp"
#include "ToolboxBuiltInSupport.hpp"
#include "toolbox/ToolboxTextEditorAccess.hpp"

using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::core;
class ToolboxBlurSnapshotInspection
{
public:
  template <ToolboxBlurKind K> static typename ToolboxBlurSnapshot<K>::Phase phase(const ToolboxBlurSnapshot<K> &owner)
  {
    return owner.phase();
  }
  template <ToolboxBlurKind K> static bool hasPixels(const ToolboxBlurSnapshot<K> &owner)
  {
    return owner.pixels_.image_ != 0;
  }
};
namespace
{
  void nativePlacement()
  {
    const Rect full = {1, 2, 31, 42};
    const Rect visible = {5, 6, 21, 32};
    const ToolboxNativePlacement placement(full, visible);
    LOKA_VERIFY(EqualRect(&placement.full(), &full));
    Rect out;
    LOKA_VERIFY(placement.queryVisible(out));
    LOKA_VERIFY(EqualRect(&out, &visible));
    const ToolboxNativePlacement emptyVisible(full, Rect());
    LOKA_VERIFY(EqualRect(&emptyVisible.full(), &full));
    LOKA_VERIFY(!emptyVisible.queryVisible(out));
    const Rect zeroWidth = {1, 2, 31, 2};
    LOKA_VERIFY(!ToolboxNativePlacement(full, zeroWidth).queryVisible(out));
    const Rect zeroHeight = {1, 2, 1, 42};
    LOKA_VERIFY(!ToolboxNativePlacement(full, zeroHeight).queryVisible(out));
    const ToolboxNativePlacement empty;
    LOKA_VERIFY(EmptyRect(&empty.full()));
    LOKA_VERIFY(!empty.queryVisible(out));
  }

  /** Pins record operations and bounding-box clips, never pixel/region verdicts. */
  class Probe : public RectSurfaceNode, public IProjectedLayoutNode
  {
  public:
    const char *name;
    short height;
    unsigned layouts, renders;
    Rect clip;
    bool refuse;
    Probe(const char *value, short extent)
        : RectSurfaceNode(RectSurfaceProps()),
          name(value),
          height(extent),
          layouts(0),
          renders(0),
          clip(),
          refuse(false)
    {
    }
    virtual IProjectedLayoutNode *asProjectedLayoutNode()
    {
      return this;
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      ToolboxScenePlatformController &controller = *static_cast<ToolboxScenePlatformController *>(platform);
      ++layouts;
      if (!controller.projectLayoutState(state))
        return 0;
      if (refuse)
        controller.refuseScrollViewShortRange();
      state.y = static_cast<short>(state.y + height);
      return state.width;
    }
    virtual void render(IPlatformController *)
    {
      ++renders;
      clip = toolbox_host::currentClip();
      toolbox_host::paintOperations.push_back(name);
    }
  };
  LayoutState seat()
  {
    LayoutState s;
    s.x = 10;
    s.y = 20;
    s.width = 100;
    s.height = 80;
    s.spacing = 0;
    return s;
  }
  void clipFrameProjection()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ScrollViewNode scroll((ScrollViewProps()));
    Probe *child = new Probe("clip-scroll-child", 30);
    scroll.addChild(child);
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    ProjectionParentScope clip(window.window(), 123, 456, Frame(0, 0, 300, 200));
    {
      ProjectionClipScopeGuard guard(controller.projectionParentScopes_, clip);
      LOKA_VERIFY(guard.isActive());
      LOKA_VERIFY(!controller.refuseNarrowingInScrollScope(SHRT_MAX + 1));
      controller.refuseScrollViewShortRange();
      LOKA_VERIFY(!controller.projectionParentScopes_.current().hasShortRangeRefusal());
      LayoutState state = seat();
      LOKA_VERIFY(controller.projectLayoutState(state));
      LOKA_VERIFY(state.x == 10 && state.y == 20);
      LOKA_VERIFY(controller.restoreProjectedLayoutState(state));
      LOKA_VERIFY(state.x == 10 && state.y == 20);
      controller.layoutScrollView(&scroll, state, 0);
      LOKA_VERIFY(child->layouts == 1);
      controller.renderScrollView(&scroll);
      LOKA_VERIFY(child->renders == 1);
      ProjectionParentScope viewport(window.window(), -7, -11, Frame(0, 0, 100, 80));
      ProjectionViewportScopeGuard viewportGuard(controller.projectionParentScopes_, viewport);
      LOKA_VERIFY(viewportGuard.isActive());
      state = seat();
      LOKA_VERIFY(controller.projectLayoutState(state));
      LOKA_VERIFY(state.x == 3 && state.y == 9);
      LOKA_VERIFY(controller.restoreProjectedLayoutState(state));
      LOKA_VERIFY(state.x == 10 && state.y == 20);
      LOKA_VERIFY(!controller.refuseNarrowingInScrollScope(20));
      LOKA_VERIFY(controller.refuseNarrowingInScrollScope(SHRT_MAX + 1));
      LOKA_VERIFY(controller.projectionParentScopes_.current().hasShortRangeRefusal());
      LOKA_VERIFY(controller.refuseNarrowingInScrollScope(20));
    }
    LOKA_VERIFY(controller.projectionParentScopes_.activeDepth() == 0);
    controller.destroy();
  }
  void equal(const Rect &a, const Rect &b)
  {
    LOKA_VERIFY(EqualRect(&a, &b));
  }
  const ToolboxUnitLedger::UnitSeat *
  unitRow(const ToolboxScenePlatformController &controller, ZStackNode *stack, unsigned index)
  {
    ToolboxUnitLedger::Mark cursor = 0;
    return controller.unitLedger_.find(stack, index, cursor);
  }
  class ShowingNode;
  typedef BoundaryPropsFor<ShowingNode> ShowingProps;
  class ShowingNode : public StdCompositionBoundaryNodeBase<ShowingProps>
  {
  public:
    typedef ShowingProps::TypeTag TypeTag;
    NodeState<bool> visible;
    explicit ShowingNode(const ShowingProps &props)
        : StdCompositionBoundaryNodeBase<ShowingProps>(props)
    {
      this->state(this->visible, true);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(ZStack() << Box().size(100, 70) << (Show(*this->visible.state()) << Box().size(100, 40)));
    }
  };
  /** Native storage is doubled, but phase/layout/erase/flush routing is production. */
  class StaticFixture
  {
  public:
    ToolboxWindow window;
    HostPortScope port;
    ToolboxScenePlatformController controller;
    ZStackNode stack;
    Probe *unit;
    StaticFixture()
        : port(window.window()),
          controller(&window),
          stack(ZStackProps()),
          unit(new Probe("unit", 30))
    {
      this->stack.props.unitGround = ZSTACK_UNIT_GROUND_EXP_STATIC_BLUR;
      this->stack.addChild(new Probe("ground", 70));
      this->stack.addChild(this->unit);
      this->controller.rootNode_ = &this->stack;
      this->window.scenePlatformController_ = &this->controller;
      toolbox_host::staticCaptures = toolbox_host::staticCopies = toolbox_host::staticReleases = 0;
      toolbox_host::bandCaptures = toolbox_host::bandCopies = toolbox_host::bandReleases = toolbox_host::bandHairlines =
          0;
      toolbox_host::staticEligible = toolbox_host::staticCaptureSucceeds = true;
      toolbox_host::staticCopySucceeds = true;
    }
    ~StaticFixture()
    {
      this->controller.rootNode_ = 0;
      this->window.scenePlatformController_ = 0;
    }
    void update(bool covered = true)
    {
      Region region = {sizeof(Region), this->window.port.portRect};
      Region *pointer = &region;
      this->window.port.updateRgn = covered ? &pointer : 0;
      this->window.drawForUpdate();
      this->window.port.updateRgn = 0;
    }
    Rect rect()
    {
      const ToolboxUnitLedger::UnitSeat *row = unitRow(this->controller, &this->stack, 1);
      LOKA_VERIFY(row != 0);
      return row->rect;
    }
    void ready()
    {
      this->window.requestInvalidate();
      this->window.flushInvalidate();
      LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(this->controller.staticBlur_)
                  == ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::READY);
    }
  };
  void flushFirstCapture();
  void staticNoRequests()
  {
    StaticFixture f;
    const unsigned before = toolbox_host::invalidations;
    f.update(false);
    LOKA_VERIFY(toolbox_host::invalidations == before);
    LOKA_VERIFY(!f.window.hasPendingInvalidate());
    for (unsigned i = 0; i < 3; ++i)
    {
      f.window.flushInvalidate();
      LOKA_VERIFY(toolbox_host::invalidations == before);
      LOKA_VERIFY(!f.window.hasPendingInvalidate());
      toolbox_host::refuseRegionAttempt = toolbox_host::regions + 1;
      f.window.flushInvalidate();
      toolbox_host::refuseRegionAttempt = 0;
      std::printf("no-request iteration=%u before=%u after=%u captures=%u pending=%lu\n",
                  i,
                  before,
                  toolbox_host::invalidations,
                  toolbox_host::staticCaptures,
                  static_cast<unsigned long>(f.window.pendingInvalidateRects_.size()));
      std::fflush(stdout);
      LOKA_VERIFY(toolbox_host::invalidations == before);
      LOKA_VERIFY(!f.window.hasPendingInvalidate());
    }
  }
  void staticRefusedReplay()
  {
    StaticFixture f;
    ToolboxInputDoor::render(f.controller);
    for (unsigned i = 0; i < 3; ++i)
    {
      f.window.requestInvalidateRect(f.rect());
      const unsigned before = toolbox_host::invalidations;
      toolbox_host::refuseRegionAttempt = toolbox_host::regions + 1;
      f.window.flushInvalidate();
      toolbox_host::refuseRegionAttempt = 0;
      LOKA_VERIFY(toolbox_host::invalidations == before);
      LOKA_VERIFY(!f.window.hasPendingInvalidate());
      LOKA_VERIFY(toolbox_host::staticCaptures == 0);
      LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                  == ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::EMPTY);
    }
    ToolboxRenderDirtyInCompositionOrder(f.controller, f.rect());
    LOKA_VERIFY(toolbox_host::staticCaptures == 1);
  }
  void staticAdmission()
  {
    flushFirstCapture();
    StaticFixture f;
    f.update(false);
    LOKA_VERIFY(toolbox_host::staticCaptures == 0);
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                == ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::EMPTY);
    Rect small = f.rect();
    --small.right;
    ToolboxRenderDirtyInCompositionOrder(f.controller, small);
    LOKA_VERIFY(toolbox_host::staticCaptures == 0);
    LOKA_VERIFY(f.window.pendingInvalidateRects_.empty());
    const unsigned before = toolbox_host::invalidations;
    f.window.flushInvalidate();
    LOKA_VERIFY(toolbox_host::invalidations == before);
    LOKA_VERIFY(!f.window.hasPendingInvalidate());
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                == ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::EMPTY);
    ToolboxRenderDirtyInCompositionOrder(f.controller, f.rect());
    LOKA_VERIFY(toolbox_host::staticCaptures == 1);
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                == ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::READY);
    LOKA_VERIFY(!f.window.hasPendingInvalidate());
    f.update(false);
    LOKA_VERIFY(toolbox_host::staticCaptures == 1);
    ToolboxRenderDirtyInCompositionOrder(f.controller, f.rect());
    LOKA_VERIFY(toolbox_host::staticCaptures == 1);
    toolbox_host::staticEligible = false;
    const unsigned copies = toolbox_host::staticCopies;
    f.update(false);
    LOKA_VERIFY(toolbox_host::staticCopies == copies);
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                == ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::READY);
    toolbox_host::staticEligible = true;
    f.update(false);
    LOKA_VERIFY(toolbox_host::staticCopies == copies + 1);
    LOKA_VERIFY(toolbox_host::invalidations == before);
  }
  void flushFirstCapture()
  {
    StaticFixture f;
    f.window.requestInvalidate();
    f.window.flushInvalidate();
    LOKA_VERIFY(toolbox_host::staticCaptures == 1);
    LOKA_VERIFY(f.unit->renders == 1);
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                == ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::READY);
    LOKA_VERIFY(!f.window.hasPendingInvalidate());
    f.window.flushInvalidate();
    LOKA_VERIFY(toolbox_host::staticCaptures == 1 && f.unit->renders == 1);
  }
  /** EMPTY admission matrix, followed by READY on every later opportunity. */
  void staticOpportunities()
  {
    for (unsigned o = 0; o < 3; ++o)
      for (unsigned open = 0; open < 2; ++open)
        for (unsigned contains = 0; contains < 3; ++contains)
          for (unsigned covers = 0; covers < 2; ++covers)
          {
            StaticFixture f;
            ToolboxInputDoor::render(f.controller); // Layout with no erase evidence.
            const Rect rect = f.rect();
            Rect area = rect;
            if (!contains)
              --area.right;
            if (contains == 2)
              area = Rect();
            if (o == 2 && !covers)
              area = Rect();
            const unsigned before = toolbox_host::invalidations;
            const ToolboxUnitLedger::UnitSeat row = {&f.stack, 1, rect, rect};
            ToolboxErasedPass pass;
            bool painted;
            if (open)
            {
              ToolboxErasedPass::Guard guard(pass, area);
              painted = f.controller.staticBlur_.paint(f.window.window(), row, pass) == BLUR_PICTURE;
            }
            else
              painted = f.controller.staticBlur_.paint(f.window.window(), row, pass) == BLUR_PICTURE;
            const bool admitted = open && contains == 1 && (o != 2 || covers);
            LOKA_VERIFY(painted == admitted);
            LOKA_VERIFY(toolbox_host::staticCaptures == (admitted ? 1u : 0u));
            LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                        == (admitted ? ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::READY
                                     : ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::EMPTY));
            for (unsigned later = 0; later < 3; ++later)
            {
              ToolboxErasedPass::Guard guard(pass, rect);
              const bool copied = f.controller.staticBlur_.paint(f.window.window(), row, pass) == BLUR_PICTURE;
              LOKA_VERIFY(copied);
              LOKA_VERIFY(toolbox_host::staticCaptures == 1);
            }
            LOKA_VERIFY(!f.window.hasPendingInvalidate());
            LOKA_VERIFY(toolbox_host::invalidations == before);
          }
  }
  void erasedAdmissionMatrix()
  {
    ToolboxErasedPass pass;
    ToolboxWindow unrelated;
    HostPortScope unrelatedPort(unrelated.window());
    SetRect(&unrelated.port.portBits.bounds, 1000, 2000, 3000, 4000);
    const Rect unit = {10, 10, 30, 30};
    const Rect empty = {0, 0, 0, 0};
    const Rect areas[] = {{0, 0, 40, 40}, {10, 10, 20, 20}, {0, 0, 0, 0}, {50, 50, 60, 60}};
    for (unsigned a = 0; a < 4; ++a)
      for (unsigned e = 0; e < 4; ++e)
      {
        LOKA_VERIFY(!pass.admitsCapture(unit));
        for (unsigned o = 0; o < 3; ++o)
        {
          const Rect evidence = o == 0 ? ToolboxCaptureEvidence(areas[a], areas[e]) : areas[a];
          ToolboxErasedPass::Guard guard(pass, evidence);
          LOKA_VERIFY(pass.isOpen());
          LOKA_VERIFY(!pass.admitsCapture(empty));
          LOKA_VERIFY(pass.admitsCapture(unit) == (a == 0 && (o != 0 || e == 0)));
        }
        LOKA_VERIFY(!pass.isOpen());
      }
    equal(ToolboxCaptureEvidence(areas[0], areas[3]), empty);
    for (short offset = -50; offset <= 50; offset += 100)
    {
      const Rect bounds = {offset, offset, 200, 200};
      Region region = {sizeof(Region), unit};
      Region *pointer = &region;
      Rect expected = unit;
      OffsetRect(&expected, offset, offset);
      equal(ToolboxUpdateEvidence(&pointer, bounds), expected);
      region.rgnSize += 2;
      equal(ToolboxUpdateEvidence(&pointer, bounds), empty);
      pointer = 0;
      equal(ToolboxUpdateEvidence(&pointer, bounds), empty);
      equal(ToolboxUpdateEvidence(0, bounds), empty);
    }
    for (unsigned e = 0; e < 4; ++e)
      for (unsigned route = 0; route < 3; ++route)
      {
        StaticFixture f;
        ToolboxInputDoor::render(f.controller);
        Region region = {sizeof(Region), f.rect()};
        if (e == 1)
          --region.rgnBBox.right;
        if (e == 2)
          region.rgnBBox = empty;
        if (e == 3)
          region.rgnSize += 2;
        Region *pointer = &region;
        f.window.port.updateRgn = &pointer;
        const unsigned before = toolbox_host::invalidations;
        if (route == 0)
          f.window.drawForUpdate();
        if (route == 1)
          f.window.drawForFlush();
        if (route == 2)
          ToolboxRenderDirtyInCompositionOrder(f.controller, f.rect());
        LOKA_VERIFY(toolbox_host::staticCaptures == ((route != 0 || e == 0) ? 1u : 0u));
        LOKA_VERIFY(toolbox_host::invalidations == before);
        LOKA_VERIFY(!f.window.hasPendingInvalidate());
        if (route == 0)
          equal(region.rgnBBox, empty); // BeginUpdate consumed the source.
      }
    {
      ToolboxWindow window;
      Region region = {sizeof(Region), window.port.portRect};
      Region *pointer = &region;
      window.port.updateRgn = &pointer;
      const unsigned before = toolbox_host::invalidations;
      window.drawForUpdate(); // A controller is not required to consume an OS update.
      equal(region.rgnBBox, empty);
      LOKA_VERIFY(toolbox_host::invalidations == before);
    }
#if !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
    const pid_t child = fork();
    LOKA_VERIFY(child >= 0);
    if (!child)
    {
      StaticFixture nested;
      ToolboxErasedPass::Guard guard(nested.controller.erasedPass(), empty);
      nested.window.drawForUpdate();
      _exit(0);
    }
    int status = 0;
    LOKA_VERIFY(waitpid(child, &status, 0) == child);
    LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#else
    std::printf("[skip] nested-update assertion requires a debug build without ASan.\n");
#endif
    StaticFixture f;
    Region region = {sizeof(Region), f.window.port.portRect};
    Region *pointer = &region;
    f.window.port.updateRgn = &pointer;
    const unsigned before = toolbox_host::invalidations;
    toolbox_host::refuseRegionAttempt = toolbox_host::regions + 1;
    f.window.drawForUpdate();
    toolbox_host::refuseRegionAttempt = 0;
    LOKA_VERIFY(toolbox_host::staticCaptures == 0);
    LOKA_VERIFY(toolbox_host::invalidations == before);
  }
  void staticCopyFailure()
  {
    StaticFixture f;
    f.ready();
    toolbox_host::staticCopySucceeds = false;
    toolbox_host::erasedRects.clear();
    const unsigned copies = toolbox_host::staticCopies;
    f.update();
    LOKA_VERIFY(toolbox_host::staticCopies == copies + 1);
    LOKA_VERIFY(!toolbox_host::erasedRects.empty());
    equal(toolbox_host::erasedRects.back(), f.rect());
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::hasPixels(f.controller.staticBlur_));
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                == ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::READY);
    toolbox_host::staticCopySucceeds = true;
    toolbox_host::erasedRects.clear();
    f.update();
    LOKA_VERIFY(toolbox_host::staticCopies == copies + 2);
    LOKA_VERIFY(toolbox_host::staticCaptures == 1 && toolbox_host::staticReleases == 0);
    // The full draw erases the port; a successful copy adds no unit fallback erase.
    LOKA_VERIFY(toolbox_host::erasedRects.size() == 1);
    equal(toolbox_host::erasedRects.front(), f.window.port.portRect);
  }
  void staticBandGeometry()
  {
    const ZStackUnitGround grounds[] = {
        ZSTACK_UNIT_GROUND_WINDOW, ZSTACK_UNIT_GROUND_EXP_BLUR, ZSTACK_UNIT_GROUND_EXP_STATIC_BLUR};
    const short heights[] = {0, 30, 90};
    for (unsigned g = 0; g < 3; ++g)
      for (unsigned h = 0; h < 3; ++h)
      {
        ToolboxWindow window;
        HostPortScope port(window.window());
        ToolboxScenePlatformController controller(&window);
        ZStackProps props;
        props.unitGround = grounds[g];
        ZStackNode stack(props);
        Probe *unit = new Probe("unit", heights[h]);
        stack.addChild(new Probe("ground", 70));
        stack.addChild(unit);
        stack.addChild(new Probe("later", 10));
        LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
        LayoutState state = seat();
        LayoutNode(&stack, state, &controller, 0);
        LOKA_VERIFY(state.y == (heights[h] > 70 ? 110 : 90));
        ToolboxUnitLedger::Mark cursor = 0;
        const ToolboxUnitLedger::UnitSeat *row = controller.unitLedger_.find(&stack, 1, cursor);
        if (!heights[h])
          LOKA_VERIFY(!row);
        else
        {
          const short height = g == 2 && heights[h] < 70 ? 70 : heights[h];
          const Rect expected = {20, 10, static_cast<short>(20 + height), 110};
          LOKA_VERIFY(row);
          equal(row->unit, expected);
          equal(row->rect, expected);
          LOKA_VERIFY(unit->height == heights[h] && unit->layouts == 1);
        }
        row = controller.unitLedger_.find(&stack, 2, cursor);
        LOKA_VERIFY(row && row->unit.bottom == 30);
      }
  }
  void staticFirstUnit()
  {
    staticBandGeometry();
    StaticFixture f;
    f.update(false);
    ZStackNode other(f.stack.props);
    const Rect rect = f.rect();
    const ToolboxUnitLedger::UnitSeat first = {&f.stack, 1, rect, rect};
    const ToolboxUnitLedger::UnitSeat second = {&other, 1, rect, rect};
    ToolboxUnitLedger ledger;
    ledger.push(first);
    ledger.push(second);
    f.controller.staticBlur_.reconcile(ledger.firstBlurUnits());
    ToolboxErasedPass::Guard pass(f.controller.erasedPass(), rect);
    const bool laterPainted =
        f.controller.staticBlur_.paint(f.window.window(), second, f.controller.erasedPass()) == BLUR_PICTURE;
    LOKA_VERIFY(!laterPainted);
    LOKA_VERIFY(toolbox_host::staticCaptures == 0);
    const bool firstPainted =
        f.controller.staticBlur_.paint(f.window.window(), first, f.controller.erasedPass()) == BLUR_PICTURE;
    LOKA_VERIFY(firstPainted);
    LOKA_VERIFY(toolbox_host::staticCaptures == 1);
  }
  void staticExternalDraw()
  {
    StaticFixture f;
    ToolboxInputDoor::render(f.controller);
    f.window.flushInvalidate();
    LOKA_VERIFY(!f.window.hasPendingInvalidate());
    f.window.requestInvalidate();
    const unsigned before = toolbox_host::invalidations;
    f.window.flushInvalidate();
    LOKA_VERIFY(toolbox_host::invalidations == before);
    LOKA_VERIFY(!f.window.hasPendingInvalidate());
    LOKA_VERIFY(toolbox_host::staticCaptures == 1);
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                == ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::READY);
  }
  void staticIneligible()
  {
    StaticFixture f;
    toolbox_host::staticEligible = false;
    f.update();
    f.window.drawForFlush();
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                == ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::EMPTY);
    const unsigned before = toolbox_host::invalidations;
    toolbox_host::staticEligible = true;
    f.window.flushInvalidate();
    f.window.flushInvalidate();
    LOKA_VERIFY(toolbox_host::invalidations == before);
    LOKA_VERIFY(toolbox_host::staticCaptures == 0);
    LOKA_VERIFY(!f.window.hasPendingInvalidate());
    f.update();
    LOKA_VERIFY(toolbox_host::staticCaptures == 1);
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                == ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::READY);
  }
  void staticFailure()
  {
    StaticFixture f;
    toolbox_host::staticCaptureSucceeds = false;
    f.window.requestInvalidate();
    f.window.flushInvalidate();
    LOKA_VERIFY(!f.window.hasPendingInvalidate());
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                == ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::DISABLED);
    LOKA_VERIFY(toolbox_host::staticCaptures == 1);
    toolbox_host::staticCaptureSucceeds = true;
    const unsigned before = toolbox_host::invalidations;
    f.update(false);
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                == ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::DISABLED);
    LOKA_VERIFY(toolbox_host::invalidations == before);
    f.update();
    f.window.drawForFlush();
    ToolboxRenderDirtyInCompositionOrder(f.controller, f.rect());
    f.window.flushInvalidate();
    LOKA_VERIFY(toolbox_host::staticCaptures == 1);
    LOKA_VERIFY(!f.window.hasPendingInvalidate());
  }
  void staticReconcile(const char *reason)
  {
    StaticFixture f;
    f.ready();
    f.update();
    LOKA_VERIFY(toolbox_host::staticReleases == 0);
    if (std::strcmp(reason, "StaticMissingRow") == 0)
      f.unit->height = 0;
    if (std::strcmp(reason, "StaticChangedRect") == 0)
    {
      ++f.unit->height;
      f.update();
      // A short child's growth leaves the ground-filled band unchanged.
      LOKA_VERIFY(toolbox_host::staticReleases == 0);
      f.unit->height = 71; // Now the band itself grows beyond the 70-high ground.
    }
    if (std::strcmp(reason, "StaticChangedGround") == 0)
      f.stack.props.unitGround = ZSTACK_UNIT_GROUND_WINDOW;
    if (std::strcmp(reason, "StaticAbortedLayout") == 0)
      f.controller.rootNode_ = 0;
    f.update();
    LOKA_VERIFY(toolbox_host::staticReleases == 1);
    const bool changedRect = std::strcmp(reason, "StaticChangedRect") == 0;
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::hasPixels(f.controller.staticBlur_) == changedRect);
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                == (changedRect ? ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::READY
                                : ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::EMPTY));
    LOKA_VERIFY(toolbox_host::staticCaptures == (changedRect ? 2u : 1u));
    f.window.drawForFlush();
    LOKA_VERIFY(toolbox_host::staticCaptures == (changedRect ? 2u : 1u));
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::hasPixels(f.controller.staticBlur_) == changedRect);
    if (changedRect)
    {
      LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                  == ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::READY);
      f.unit->height = 72;
      f.window.drawForFlush();
      LOKA_VERIFY(toolbox_host::staticReleases == 2);
      LOKA_VERIFY(toolbox_host::staticCaptures == 3);
      LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                  == ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::READY);
      return;
    }
    f.unit->height = 30;
    f.stack.props.unitGround = ZSTACK_UNIT_GROUND_EXP_STATIC_BLUR;
    f.controller.rootNode_ = &f.stack;
    f.ready();
    LOKA_VERIFY(toolbox_host::staticCaptures == 2);
  }
  void bandGeometry()
  {
    const Rect key = {10, 10, 30, 30};
    const Rect cases[] = {{0, 0, 0, 0},
                          {10, 0, 30, 10},
                          {10, 30, 30, 40},
                          {0, 10, 10, 30},
                          {30, 10, 40, 30},
                          {40, 40, 50, 50},
                          {0, 0, 40, 40},
                          {15, 15, 20, 20},
                          {15, 5, 20, 20},
                          {5, 15, 20, 20},
                          {15, 20, 20, 35},
                          {20, 15, 35, 20},
                          {-32768, -32768, 20, 20},
                          {20, 20, 32767, 32767}};
    const Rect expected[] = {{0, 0, 0, 0},
                             {10, 0, 30, 10},
                             {10, 30, 30, 40},
                             {0, 10, 10, 30},
                             {30, 10, 40, 30},
                             {40, 40, 50, 50},
                             {0, 0, 40, 40},
                             {10, 10, 30, 30},
                             {10, 5, 30, 30},
                             {5, 10, 30, 30},
                             {10, 10, 30, 35},
                             {10, 10, 35, 30},
                             {-32768, -32768, 30, 30},
                             {10, 10, 32767, 32767}};
    const Rect empty = {15, 15, 15, 20};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
      equal(ToolboxBlurDirtyExtent(cases[i], key), expected[i]);
      equal(ToolboxBlurDirtyExtent(cases[i], empty), cases[i]);
    }
    const Rect stale = {0, 0, 30, 200}, dirty = {10, 90, 20, 100}, widened = {0, 0, 30, 200};
    equal(ToolboxBlurDirtyExtent(dirty, stale), widened);
    const Rect visible[] = {key, {10, 10, 29, 30}, {20, 15, 30, 25}, {29, 10, 30, 30}, empty};
    const bool draws[] = {true, false, true, true, false};
    for (unsigned i = 0; i < 5; ++i)
    {
      Rect line;
      const bool drawn = ToolboxUnitHairlineRect(visible[i], key, line);
      LOKA_VERIFY(drawn == draws[i]);
      if (drawn)
      {
        const Rect expectedLine = {29, visible[i].left, 30, visible[i].right};
        equal(line, expectedLine);
      }
    }
  }
  void bandLifetime()
  {
    StaticFixture f;
    f.stack.props.unitGround = ZSTACK_UNIT_GROUND_EXP_BLUR;
    ToolboxInputDoor::render(f.controller);
    LOKA_VERIFY(toolbox_host::bandCaptures == 0 && f.controller.frostCache_.paints == 0);
    f.update(false);
    LOKA_VERIFY(f.controller.frostCache_.paints == 1 && toolbox_host::bandCaptures == 0);
    f.window.drawForFlush();
    LOKA_VERIFY(toolbox_host::bandCaptures == 1 && toolbox_host::bandHairlines == 1);
    f.window.drawForFlush();
    LOKA_VERIFY(toolbox_host::bandCaptures == 2);
    f.update();
    LOKA_VERIFY(toolbox_host::bandCaptures == 3);
    const Rect row = f.rect();
    Rect partial = row;
    --partial.bottom;
    ToolboxRenderDirtyInCompositionOrder(f.controller, partial);
    LOKA_VERIFY(toolbox_host::bandCaptures == 3 && toolbox_host::bandCopies == 4);
    Region update = {sizeof(Region), partial};
    Region *pointer = &update;
    f.window.port.updateRgn = &pointer;
    f.window.drawForUpdate();
    f.window.port.updateRgn = 0;
    f.update(false);
    LOKA_VERIFY(toolbox_host::bandCaptures == 3 && toolbox_host::bandCopies == 6);
    ToolboxRenderDirtyInCompositionOrder(f.controller, row);
    LOKA_VERIFY(toolbox_host::bandCaptures == 4);
    const unsigned requests = toolbox_host::invalidations;
    for (unsigned i = 0; i < 5; ++i)
      ToolboxInputDoor::render(f.controller);
    LOKA_VERIFY(toolbox_host::bandCaptures == 4 && toolbox_host::bandCopies == 12);
    LOKA_VERIFY(toolbox_host::invalidations == requests);
    const unsigned lines = toolbox_host::bandHairlines;
    const unsigned milk = f.controller.frostCache_.paints;
    toolbox_host::staticCopySucceeds = false;
    f.update(false);
    LOKA_VERIFY(f.controller.frostCache_.paints == milk && toolbox_host::bandHairlines == lines);
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::hasPixels(f.controller.bandBlur_));
    toolbox_host::staticCopySucceeds = true;
    toolbox_host::staticEligible = false;
    f.window.drawForFlush();
    LOKA_VERIFY(toolbox_host::bandCaptures == 4 && toolbox_host::bandHairlines == lines);
    LOKA_VERIFY(f.controller.frostCache_.paints == milk);
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.bandBlur_)
                == ToolboxBlurSnapshot<TOOLBOX_BLUR_LIVE>::READY);
    toolbox_host::staticEligible = true;
    toolbox_host::staticCaptureSucceeds = false;
    f.window.drawForFlush();
    LOKA_VERIFY(toolbox_host::bandCaptures == 5 && f.controller.frostCache_.paints == milk + 1);
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.bandBlur_)
                == ToolboxBlurSnapshot<TOOLBOX_BLUR_LIVE>::DISABLED);
    toolbox_host::staticCaptureSucceeds = true;
    f.window.drawForFlush();
    f.update();
    LOKA_VERIFY(toolbox_host::bandCaptures == 5 && f.controller.frostCache_.paints == milk + 3);
    ToolboxInputDoor::render(f.controller);
    LOKA_VERIFY(f.controller.frostCache_.paints == milk + 3);
    f.unit->height = 31;
    f.window.drawForFlush();
    LOKA_VERIFY(toolbox_host::bandCaptures == 6);
    f.controller.destroy();
    Rect absent;
    LOKA_VERIFY(!f.controller.bandBlur_.queryKeyRect(absent));
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.bandBlur_)
                == ToolboxBlurSnapshot<TOOLBOX_BLUR_LIVE>::EMPTY);
    equal(f.controller.dirtyExtent(partial), partial);
    f.controller.rootNode_ = &f.stack;
    f.window.drawForFlush();
    LOKA_VERIFY(toolbox_host::bandCaptures == 7);
  }
  void bandDelivery()
  {
    StaticFixture f;
    const Rect dirty = {5, 5, 10, 10};
    equal(f.controller.dirtyExtent(dirty), dirty);
    f.stack.props.unitGround = ZSTACK_UNIT_GROUND_EXP_BLUR;
    f.window.drawForFlush();
    const Rect band = f.rect();
    Rect partial = band;
    --partial.right;
    --partial.bottom;
    f.window.requestInvalidateRect(partial);
    f.window.flushInvalidate();
    LOKA_VERIFY(toolbox_host::bandCaptures == 2);
    equal(f.controller.dirtyExtent(partial), band);
    LOKA_VERIFY(!f.window.hasPendingInvalidate());
  }
  /** Host controller has no prepareProjectedLayout door; retain the real
      editor context but project its layout explicitly, as editorIdleRepair does. */
  class ReplayEditor : public TextEditorNode
  {
  public:
    explicit ReplayEditor(const TextEditorProps &props)
        : TextEditorNode(props)
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      ToolboxScenePlatformController *controller = static_cast<ToolboxScenePlatformController *>(platform);
      if (!controller->projectLayoutState(state))
        return 0;
      return this->Node::layout(platform, state);
    }
  };
  void bandRepairSettles()
  {
    loka::app::testing::TextEditorStateOwner owner;
    loka::testing::TextBufferFixture lines;
    LOKA_VERIFY(lines.attach(&owner.tracker, 16) == ATTACH_OK);
    LOKA_VERIFY(lines.insert(0, String("a partly covered editor")) == EDIT_OK);
    StaticFixture f;
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
    ZStackNode stack((ZStackProps()));
    stack.props.unitGround = ZSTACK_UNIT_GROUND_EXP_BLUR;
    TextEditorNode *editor = new ReplayEditor(TextEditorProps(lines, owner.cursor));
    stack.addChild(editor);
    stack.addChild(new Probe("band", 30));
    f.controller.rootNode_ = &stack;
    IPlatformNodeHandler *handler = f.controller.nodeHandlerRegistry_.find(editor);
    LOKA_VERIFY(handler);
    NodeContext *context = handler->ensureContext(editor, &f.controller, seat());
    LOKA_VERIFY(context);
    f.window.drawForFlush();
    LOKA_VERIFY(f.controller.editControls_.size() == 1);
    const Rect band = unitRow(f.controller, &stack, 1)->rect;
    const Rect editorRect = f.controller.editControls_[0].placement.visible();
    LOKA_VERIFY(editorRect.bottom > band.bottom && editorRect.top < band.bottom);
    Rect dirty = band;
    --dirty.right;
    --dirty.bottom;
    const unsigned captures = toolbox_host::bandCaptures;
    f.window.requestInvalidateRect(dirty);
    for (unsigned i = 0; i < 4 && f.window.hasPendingInvalidate(); ++i)
      f.window.flushInvalidate();
    LOKA_VERIFY(toolbox_host::bandCaptures > captures);
    LOKA_VERIFY(!f.window.hasPendingInvalidate());
    const unsigned settled = toolbox_host::invalidations;
    f.window.flushInvalidate();
    LOKA_VERIFY(toolbox_host::invalidations == settled);
    // Retained node owns the context; detach through its regular fact door.
    context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    f.controller.rootNode_ = 0;
    f.controller.flushTE();
  }
  void bandSecondUnit()
  {
    StaticFixture f;
    StackNode root((StackProps(STACK_AXIS_COLUMN)));
    ZStackNode *first = new ZStackNode(ZStackProps());
    ZStackNode *second = new ZStackNode(ZStackProps());
    first->props.unitGround = second->props.unitGround = ZSTACK_UNIT_GROUND_EXP_BLUR;
    first->addChild(new Probe("ground", 70));
    first->addChild(new Probe("unit", 30));
    second->addChild(new Probe("ground2", 70));
    second->addChild(new Probe("unit2", 30));
    root.addChild(first);
    root.addChild(second);
    f.controller.rootNode_ = &root;
    f.window.drawForFlush();
    LOKA_VERIFY(toolbox_host::bandCaptures == 1 && f.controller.frostCache_.paints == 1);
    LOKA_VERIFY(toolbox_host::bandHairlines == 1);
    f.controller.rootNode_ = 0;
  }
  void bandSelection()
  {
    StaticFixture f;
    ZStackNode band((ZStackProps())), later((ZStackProps()));
    band.props.unitGround = later.props.unitGround = ZSTACK_UNIT_GROUND_EXP_BLUR;
    const Rect frozenRect = {80, 80, 100, 100}, bandRect = {10, 10, 30, 30}, laterRect = {40, 40, 60, 60};
    ToolboxUnitLedger ledger;
    ToolboxUnitLedger::UnitSeat frozen = {&f.stack, 1, frozenRect, frozenRect};
    ToolboxUnitLedger::UnitSeat first = {&band, 1, bandRect, bandRect};
    ToolboxUnitLedger::UnitSeat last = {&later, 1, laterRect, laterRect};
    ledger.push(frozen);
    ledger.push(first);
    ledger.push(last);
    const ToolboxUnitLedger::FirstBlurUnits selection = ledger.firstBlurUnits();
    f.controller.staticBlur_.reconcile(selection);
    f.controller.bandBlur_.reconcile(selection);
    Rect actual;
    LOKA_VERIFY(f.controller.staticBlur_.queryKeyRect(actual));
    equal(actual, frozenRect);
    LOKA_VERIFY(f.controller.bandBlur_.queryKeyRect(actual));
    equal(actual, bandRect);
    const Rect dirty = {15, 15, 20, 20};
    equal(f.controller.dirtyExtent(dirty), bandRect);
    equal(f.controller.dirtyExtent(frozenRect), frozenRect);
    {
      ToolboxErasedPass::Guard pass(f.controller.erasedPass(), f.window.port.portRect);
      const ToolboxBlurPaint a = f.controller.staticBlur_.paint(f.window.window(), frozen, f.controller.erasedPass());
      const ToolboxBlurPaint b = f.controller.bandBlur_.paint(f.window.window(), first, f.controller.erasedPass());
      const ToolboxBlurPaint c = f.controller.bandBlur_.paint(f.window.window(), last, f.controller.erasedPass());
      LOKA_VERIFY(a == BLUR_PICTURE && b == BLUR_PICTURE && c == BLUR_ABSENT);
    }
    const unsigned requests = toolbox_host::invalidations;
    for (unsigned i = 0; i < 5; ++i)
    {
      const ToolboxBlurPaint a = f.controller.staticBlur_.paint(f.window.window(), frozen, f.controller.erasedPass());
      const ToolboxBlurPaint b = f.controller.bandBlur_.paint(f.window.window(), first, f.controller.erasedPass());
      LOKA_VERIFY(a == BLUR_PICTURE && b == BLUR_PICTURE);
    }
    LOKA_VERIFY(toolbox_host::invalidations == requests);
    LOKA_VERIFY(toolbox_host::bandCaptures == 1 && toolbox_host::staticCaptures == 1);
    f.controller.destroy();
    LOKA_VERIFY(!f.controller.staticBlur_.queryKeyRect(actual) && !f.controller.bandBlur_.queryKeyRect(actual));
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.staticBlur_)
                == ToolboxBlurSnapshot<TOOLBOX_BLUR_FROZEN>::EMPTY);
    LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(f.controller.bandBlur_)
                == ToolboxBlurSnapshot<TOOLBOX_BLUR_LIVE>::EMPTY);
    equal(f.controller.dirtyExtent(dirty), dirty);
  }
  void erasedFrostAdmission()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ZStackProps props;
    props.unitGround = ZSTACK_UNIT_GROUND_EXP_BLUR;
    ZStackNode stack(props);
    stack.addChild(new Probe("ground", 70));
    stack.addChild(new Probe("first", 30));
    stack.addChild(new Probe("later", 20));
    controller.rootNode_ = &stack;
    ToolboxInputDoor::render(controller);
    LOKA_VERIFY(controller.frostCache_.paints == 0);
    {
      window.scenePlatformController_ = &controller;
      const unsigned before = toolbox_host::invalidations;
      window.drawForUpdate(); // Empty evidence must still paint frost L.
      LOKA_VERIFY(toolbox_host::invalidations == before);
      LOKA_VERIFY(controller.frostCache_.paints == 1);
    }
    LOKA_VERIFY(!controller.erasedPass().isOpen());
    ToolboxInputDoor::render(controller);
    LOKA_VERIFY(controller.frostCache_.paints == 1);
    const Rect dirty = {20, 10, 25, 20};
    ToolboxRenderDirtyInCompositionOrder(controller, dirty);
    LOKA_VERIFY(controller.frostCache_.paints == 2);
    LOKA_VERIFY(!controller.erasedPass().isOpen());
    toolbox_host::refuseRegionAttempt = toolbox_host::regions + 1;
    ToolboxRenderDirtyInCompositionOrder(controller, dirty);
    toolbox_host::refuseRegionAttempt = 0;
    LOKA_VERIFY(controller.frostCache_.paints == 2);
    LOKA_VERIFY(!controller.erasedPass().isOpen());
    stack.props.unitGround = ZSTACK_UNIT_GROUND_WINDOW;
    ToolboxRenderDirtyInCompositionOrder(controller, dirty);
    LOKA_VERIFY(controller.frostCache_.paints == 2);
    stack.props.unitGround = ZSTACK_UNIT_GROUND_EXP_BLUR;
    {
      ToolboxErasedPass::Guard erased(controller.erasedPass(), window.window()->portRect);
      ToolboxInputDoor::render(controller);
      LOKA_VERIFY(controller.frostCache_.paints == 2);
      LOKA_VERIFY(ToolboxBlurSnapshotInspection::phase(controller.bandBlur_)
                  == ToolboxBlurSnapshot<TOOLBOX_BLUR_LIVE>::READY);
    }
    controller.rootNode_ = 0;
  }
  void rebuilt()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    LOKA_VERIFY(RegisterToolboxPlatformLayoutHandlers(controller.gapLayoutRegistry));
    Scene scene((Boundary<ShowingNode>(ShowingProps())));
    scene.mount(&controller);
    typedef loka::dsl::testing::SceneTestAccess Access;
    Access::updateAttached(scene, true);
    ShowingNode *root = static_cast<ShowingNode *>(Access::rootBoundary(scene));
    controller.rootNode_ = root;
    ToolboxInputDoor::render(controller);
    LOKA_VERIFY(controller.unitLedger_.mark() == 1);
    BoundaryLocalApplyInfo info;
    info.paintKind = LOCAL_APPLY_PAINT_COMPOSITED;
    PlatformApplyPlan plan;
    plan.paintKind = PlatformApplyPlan::PAINT_LOCAL;
    plan.setPrimaryRoot(root);
    const unsigned before = toolbox_host::invalidations;
    controller.onBoundaryApply(root, root, info, plan);
    LOKA_VERIFY(toolbox_host::invalidations > before);
    {
      StateTrackerGuard guard(root->tracker());
      root->visible.set(false);
    }
    Access::flushInvalidation(scene);
    ToolboxInputDoor::render(controller);
    LOKA_VERIFY(controller.unitLedger_.mark() == 0);
    controller.rootNode_ = 0;
    Access::unmount(scene);
  }
  class PlacedButton : public ButtonNode
  {
  public:
    PlacedButton()
        : ButtonNode(ButtonProps().text("Inside"))
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      ToolboxScenePlatformController *controller = static_cast<ToolboxScenePlatformController *>(platform);
      if (!controller->projectLayoutState(state))
        return 0;
      return this->getContext()->layout(platform, state);
    }
  };
  void inheritedButton()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ZStackNode stack((ZStackProps()));
    stack.addChild(new Probe("ground", 70));
    ButtonNode *button = new PlacedButton();
    button->setContext(new ToolboxButtonContext(button, &controller));
    stack.addChild(button);
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    const Rect outer = {25, 15, 55, 90};
    ClipRect(&outer);
    ProjectionParentScope viewport(window.window(), -7, -9, Frame(15, 25, 75, 30));
    ProjectionViewportScopeGuard guard(controller.projectionParentScopes_, viewport);
    LayoutState state = seat();
    state.height = 0;
    state.lineHeight = 40;
    LayoutNode(&stack, state, &controller, 0);
    LOKA_VERIFY(controller.unitLedger_.mark() == 1);
    RenderNode(&stack, &controller);
    LOKA_VERIFY(controller.buttonControls_.size() == 1);
    const Rect rect = controller.buttonControls_[0].placement.full();
    LOKA_VERIFY(rect.left == 3 && rect.right == 103 && rect.top == 11 && rect.bottom == 55);
    Rect visible;
    LOKA_VERIFY(controller.buttonControls_[0].placement.queryVisible(visible));
    LOKA_VERIFY(visible.left == 15 && visible.right == 90 && visible.top == 25 && visible.bottom == 55);
    const toolbox_host::ControlCall &draw = controller.buttonControls_[0].control->calls.back();
    LOKA_VERIFY(draw.operation == "show" && draw.painted);
    LOKA_VERIFY(draw.ownerClip.left >= outer.left && draw.ownerClip.right <= outer.right);
    State<bool> enabled(false);
    controller.buttonControls_[0].enabled = &enabled;
    ControlRef native = controller.buttonControls_[0].control;
    native->calls.clear();
    const unsigned beforeInvalidation = toolbox_host::invalidations;
    {
      ToolboxWindow other;
      HostPortScope otherPort(other.window());
      controller.refreshButtonEnabled(controller.buttonControls_[0]);
      GrafPtr current;
      GetPort(&current);
      LOKA_VERIFY(current == other.window());
    }
    LOKA_VERIFY(toolbox_host::invalidations == beforeInvalidation + 1);
    LOKA_VERIFY(native->hilite == 255);
    controller.pendingDirtyRects_.clear();
    controller.inBatchUpdate_ = true;
    controller.refreshButtonEnabled(controller.buttonControls_[0]);
    LOKA_VERIFY(toolbox_host::invalidations == beforeInvalidation + 1);
    LOKA_VERIFY(controller.pendingDirtyRects_.size() == 1);
    equal(controller.pendingDirtyRects_[0], visible);
    controller.inBatchUpdate_ = false;
    for (size_t i = 0; i < native->calls.size(); ++i)
      LOKA_VERIFY(!native->calls[i].painted);
    controller.clearControls();
  }
  void refusedControlCleanup()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ZStackNode stack((ZStackProps()));
    stack.addChild(new Probe("ground", 70));
    PlacedButton *button = new PlacedButton();
    button->setContext(new ToolboxButtonContext(button, &controller));
    stack.addChild(button);
    stack.addChild(new Probe("later-unit", 40));
    controller.rootNode_ = &stack;
    ToolboxInputDoor::render(controller);
    LOKA_VERIFY(controller.buttonControls_.size() == 1);
    ControlRef control = controller.buttonControls_[0].control;
    LOKA_VERIFY(control->visible);
    control->calls.clear();
    toolbox_host::refuseRegionAttempt = toolbox_host::regions + 1;
    ToolboxInputDoor::render(controller);
    toolbox_host::refuseRegionAttempt = 0;
    LOKA_VERIFY(!control->visible);
    for (size_t i = 0; i < control->calls.size(); ++i)
      LOKA_VERIFY(!control->calls[i].painted);
    controller.clearControls();
    controller.rootNode_ = 0;
  }
  void viewportRow()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    MutableState<Frame> origin(Frame(0, 9, 0, 0));
    NodeState<Frame> viewportState(&origin, 0);
    ScrollViewNode viewport((ScrollViewProps(viewportState)));
    ZStackNode *stack = new ZStackNode(ZStackProps());
    stack->addChild(new Probe("ground", 200));
    Probe *upper = new Probe("upper", 40);
    stack->addChild(upper);
    viewport.addChild(stack);
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    LayoutState state = seat();
    LayoutNode(&viewport, state, &controller, 0);
    LOKA_VERIFY(controller.unitLedger_.mark() == 1);
    const Rect row = unitRow(controller, stack, 1)->rect;
    const Rect clip = controller.viewportScroll().contentRect;
    LOKA_VERIFY(row.top == clip.top && row.bottom == 51);
    LOKA_VERIFY(row.left >= clip.left && row.right <= clip.right);
    LOKA_VERIFY(row.bottom != 60); // An unprojected seat would end here.
    // The row also keeps the projected unit before the cut: scrolled 9 up.
    const Rect unit = unitRow(controller, stack, 1)->unit;
    LOKA_VERIFY(unit.bottom == row.bottom && unit.top == row.top - 9);
    Probe *refused = new Probe("refused-after-unit", 40);
    refused->refuse = true;
    stack->addChild(refused);
    controller.unitLedger_.clear();
    state = seat();
    LayoutNode(&viewport, state, &controller, 0);
    LOKA_VERIFY(controller.unitLedger_.mark() == 0);
    controller.clearControls();
  }
  /** Real dispatch/layout/render with an app-origin state and optional foreground unit. */
  /** The host base omits projected preparation; mirror the production door so
      Text really owns a context and consumes its pre-layout inputs. */
  class PresentController : public ToolboxScenePlatformController
  {
  public:
    explicit PresentController(ToolboxWindow *window)
        : ToolboxScenePlatformController(window)
    {
    }
    virtual bool prepareProjectedLayout(Node *node, LayoutState &state)
    {
      if (!node || !this->projectLayoutState(state))
        return false;
      IPlatformNodeHandler *handler = this->nodeHandlerRegistry_.find(node);
      LOKA_VERIFY(handler);
      NodeContext *context = handler->ensureContext(node, this, state);
      if (!context)
        return false;
      if (IBoundaryTaggedContext *tagged = context->asBoundaryTagged())
        tagged->setBoundary(this->activeLayoutBoundary());
      return true;
    }
  };
  struct PresentFixture
  {
    ToolboxWindow window;
    HostPortScope port;
    PresentController controller;
    MutableState<Frame> origin;
    NodeState<Frame> viewportState;
    ZStackNode stack;
    ScrollViewNode &viewport;
    PresentFixture(bool foreground = false, int initialOrigin = 0, Node *content = 0)
        : port(window.window()),
          controller(&window),
          origin(Frame(0, initialOrigin, 0, 0)),
          viewportState(&origin, 0),
          stack(ZStackProps()),
          viewport(*new ScrollViewNode(ScrollViewProps(viewportState)))
    {
      LOKA_VERIFY(RegisterToolboxBuiltInSupport(this->controller));
      LOKA_VERIFY(RegisterToolboxPlatformLayoutHandlers(this->controller.gapLayoutRegistry));
      if (!content)
      {
        BoxNode *box = new BoxNode(Box().size(180, 600).props);
        box->addChild(new TextNode(TextProps("content")));
        content = box;
      }
      this->viewport.addChild(content);
      this->stack.addChild(&this->viewport);
      if (foreground)
        this->stack.addChild(new Probe("observation-foreground", 80));
      this->controller.rootNode_ = foreground ? static_cast<Node *>(&this->stack) : &this->viewport;
      this->window.scenePlatformController_ = &this->controller;
      this->window.requestInvalidate();
      this->window.flushInvalidate();
      LOKA_VERIFY(this->last().viewports() == 0);
    }
    ~PresentFixture()
    {
      this->controller.rootNode_ = 0;
      this->window.scenePlatformController_ = 0;
      this->controller.clearControls();
    }
    const ToolboxPresentObservation &last() const
    {
      return this->controller.debugStatsForTesting().lastPresentObservation;
    }
    const ToolboxPresentObservation &lastMoved() const
    {
      return this->controller.debugStatsForTesting().lastMovedPresentObservation;
    }
    void scroll(int dy)
    {
      this->origin.set(Frame(0, dy, 0, 0));
      this->window.requestInvalidateWithReason(TOOLBOX_FULL_DRAW_LAYOUT_DIRTY);
      this->window.flushInvalidate();
    }
  };
  class UnknownPresentLeaf : public Probe
  {
  public:
    UnknownPresentLeaf()
        : Probe("unknown", 600)
    {
    }
    explicit UnknownPresentLeaf(const RectSurfaceProps &)
        : Probe("unknown", 600)
    {
    }
    virtual NodeKind kind() const
    {
      return NODE_KIND_UNKNOWN;
    }
  };
  class TransparentPresentNode : public NestableNode
  {
  };
  void presentUnsupportedKind()
  {
    PresentFixture leaf(false, 0, new UnknownPresentLeaf());
    leaf.scroll(17);
    LOKA_VERIFY(leaf.last().unsupportedKinds() >= 1);
    LOKA_VERIFY(leaf.last().refusal() == TOOLBOX_PRESENT_UNSUPPORTED_KIND);
    TransparentPresentNode *component = new TransparentPresentNode();
    BoxNode *box = new BoxNode(Box().size(180, 600).props);
    box->addChild(new TextNode(TextProps("transparent content")));
    component->addChild(box);
    PresentFixture transparent(false, 0, component);
    transparent.scroll(17);
    LOKA_VERIFY(transparent.last().unsupportedKinds() == 0);
    LOKA_VERIFY(transparent.last().refusal() == TOOLBOX_PRESENT_NO_SUPPRESSION_CAPABILITY);
  }
  void presentRefusedVisit()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    PresentController controller(&window);
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    ProjectionParentScope viewport(window.window(), 0, -1000, Frame(0, 0, 300, 200));
    ProjectionViewportScopeGuard viewportScope(controller.projectionParentScopes_, viewport);
    ToolboxViewportContentObservation content;
    {
      ToolboxViewportContentObservation::Scope scope(controller.activeViewportContent_, content);
      UnknownPresentLeaf leaf;
      // Native Y fits, but restoring the content Y crosses SHRT_MAX.
      leaf.requeueLayoutInputs(NODE_DIRTY_PROPS);
      LayoutState state = seat();
      state.y = SHRT_MAX - 100;
      LayoutNode(&leaf, state, &controller, 0);
      LOKA_VERIFY(content.contentInputs() == 0 && content.unsupportedKinds() == 0);
      LOKA_VERIFY(Node::LayoutInputsCheckpoint(leaf).inputs() == NODE_DIRTY_PROPS);
    }
    LOKA_VERIFY(controller.activeViewportContent_ == 0);
  }
  void presentClipBoxRefused()
  {
    BoxNode *box = new BoxNode(Box().size(180, 600).clipChildren().props);
    box->addChild(new TextNode(TextProps("clipped")));
    PresentFixture f(false, 0, box);
    f.scroll(17);
    LOKA_VERIFY(f.last().unsupportedKinds() == 1);
    LOKA_VERIFY(f.last().refusal() == TOOLBOX_PRESENT_UNSUPPORTED_KIND);
  }
  void presentCardBandPhase()
  {
    CardNode *card = new CardNode(CardProps());
    card->addChild(new CardBandNode(CardBandProps(CARD_BAND_TOP)));
    card->addChild(new BoxNode(Box().size(180, 600).props));
    PresentFixture f(false, 0, card);
    f.scroll(17);
    LOKA_VERIFY(f.last().phaseBound() >= 1);
    LOKA_VERIFY(f.last().unsupportedKinds() == 0);
    LOKA_VERIFY(f.last().refusal() == TOOLBOX_PRESENT_DITHER_PHASE);
    f.scroll(33);
    LOKA_VERIFY(f.last().dy() == 16);
    LOKA_VERIFY(f.last().phaseBound() >= 1);
    LOKA_VERIFY(f.last().refusal() == TOOLBOX_PRESENT_NO_SUPPRESSION_CAPABILITY);
  }
  template <int Mode> class PresentLiveNode;
  template <int Mode>
  class PresentLiveNode : public StdCompositionBoundaryNodeBase<BoundaryPropsFor<PresentLiveNode<Mode> > >
  {
  public:
    typedef BoundaryPropsFor<PresentLiveNode<Mode> > PresentLiveProps;
    typedef typename PresentLiveProps::TypeTag TypeTag;
    NodeState<Frame> viewport;
    NodeState<String> text;
    NodeState<TextStyle> style;
    NodeState<int> width;
    NodeState<Frame> stationaryViewport;
    Request<ScrollTarget> request;
    explicit PresentLiveNode(const PresentLiveProps &props)
        : StdCompositionBoundaryNodeBase<PresentLiveProps>(props)
    {
      this->state(this->viewport, Frame());
      this->state(this->text, String("before"));
      this->state(this->style, TextStyle());
      this->state(this->width, 180);
      this->state(this->stationaryViewport, Frame());
      this->state(this->request, ScrollTarget::None());
    }
    virtual bool flushViewDirtyImmediately(NodeDirtyFlags) const
    {
      return false;
    }
    virtual void composeNode(NodeComposition &c)
    {
      if (Mode == 1)
        c.declare(
            Column().gap(0) << (Box().size(180, 100)
                                << (ScrollView(this->stationaryViewport)
                                    << (Box().size(180, 600)
                                        << Text(this->text.state())
                                        << NodeDefinition<RectSurfaceProps, UnknownPresentLeaf>(RectSurfaceProps()))))
                            << (Box().size(180, 100)
                                << (ScrollView(this->viewport) << (Box().size(180, 600) << Text("clean")))));
      else
        c.declare(
            ScrollView(this->viewport).scrollTo(this->request)
            << (Box().width(this->width.state()).height(600) << (Text(this->text.state()) + this->style.state())));
    }
    virtual void declareBindings(BindingToken &token)
    {
      if (Mode == 2)
        token.watch(*this->viewport.state(), this, &PresentLiveNode::published);
    }
    void published()
    {
      if (this->viewport.get().y)
      {
        StateTrackerGuard transaction(this->tracker());
        this->style.set(TextStyle().color(TEXT_COLOR_RED));
      }
    }
  };
  template <int Mode> struct PresentLiveFixture
  {
    ToolboxWindow window;
    HostPortScope port;
    PresentController controller;
    Scene scene;
    PresentLiveNode<Mode> *root;
    PresentLiveFixture()
        : port(window.window()),
          controller(&window),
          scene((Boundary<PresentLiveNode<Mode> >()))
    {
      LOKA_VERIFY(RegisterToolboxBuiltInSupport(this->controller));
      LOKA_VERIFY(RegisterToolboxPlatformLayoutHandlers(this->controller.gapLayoutRegistry));
      LOKA_VERIFY(this->scene.mount(&this->controller));
      loka::dsl::testing::SceneTestAccess::updateAttached(this->scene, true);
      this->root = static_cast<PresentLiveNode<Mode> *>(loka::dsl::testing::SceneTestAccess::rootBoundary(this->scene));
      this->controller.rootNode_ = this->root;
      this->window.scenePlatformController_ = &this->controller;
      this->draw();
    }
    ~PresentLiveFixture()
    {
      this->controller.rootNode_ = 0;
      this->window.scenePlatformController_ = 0;
      loka::dsl::testing::SceneTestAccess::unmount(this->scene);
    }
    void draw()
    {
      loka::dsl::testing::SceneTestAccess::flushInvalidation(this->scene);
      this->window.requestInvalidateWithReason(TOOLBOX_FULL_DRAW_LAYOUT_DIRTY);
      this->window.flushInvalidate();
    }
    const ToolboxPresentObservation &last() const
    {
      return this->controller.debugStatsForTesting().lastMovedPresentObservation;
    }
  };
  void viewportInput(const char *which)
  {
    PresentLiveFixture<0> f;
    f.controller.resetDebugStats();
    const ToolboxSceneDebugStats before = f.controller.debugStatsForTesting();
    ControlRef bar = f.controller.scrollControl(0).control;
    bar->tracking = toolbox_host::TrackScript(21, 21, 0);
    bar->tracking.callbacks.assign(1, 21);
    const Point point = {static_cast<short>(bar->rect.top + 4), static_cast<short>(bar->rect.left + 3)};
    LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, point));
    LOKA_VERIFY(f.controller.debugStats_.windowRectRequestCount == before.windowRectRequestCount);
    const bool fullPending = f.window.needsInvalidate_;
    const char *firstRequest = f.controller.debugStatsForTesting().windowFullRequestSource;
    const bool followUp = std::strcmp(which, "ViewportInputFollowUp") == 0;
    if (followUp)
    {
      // The old dirty seat skipped the follow-up. Its replacement reaches the
      // production no-op door once; the count precedes its NONE/false return.
      LOKA_VERIFY(f.controller.debugStatsForTesting().requestInvalidateCallCount == 1);
      LOKA_VERIFY(f.controller.debugStatsForTesting().requestInvalidateFlags == NODE_DIRTY_NONE);
      LOKA_VERIFY(!f.controller.debugStatsForTesting().requestInvalidateFullRebuild);
      LOKA_VERIFY(f.controller.debugStatsForTesting().batchAccumOnChangeCount == 0);
    }
    std::printf("door: calls=%d rects=%d batch-flags=%d batch-full=%d\n",
                f.controller.debugStats_.requestInvalidateCallCount,
                f.controller.debugStats_.windowRectRequestCount,
                f.controller.pendingInvalidateFlags_,
                f.controller.pendingFullInvalidate_);
    loka::dsl::testing::SceneTestAccess::flushInvalidation(f.scene);
    f.window.flushInvalidate();
    const ToolboxSceneDebugStats &after = f.controller.debugStatsForTesting();
    std::printf("viewport input: calls %lu -> %lu, rects %lu -> %lu, full %lu -> %lu, dirty %lu -> %lu, reasons=%s "
                "refusal=%s\n",
                (unsigned long)before.requestInvalidateCallCount,
                (unsigned long)after.requestInvalidateCallCount,
                (unsigned long)before.windowRectRequestCount,
                (unsigned long)after.windowRectRequestCount,
                (unsigned long)before.windowFlushFullCount,
                (unsigned long)after.windowFlushFullCount,
                (unsigned long)before.windowFlushDirtyCount,
                (unsigned long)after.windowFlushDirtyCount,
                f.last().reasonNames().c_str(),
                ToolboxPresentRefusalName(f.last().refusal()));
    const ToolboxPresentObservation &observation = after.lastPresentObservation;
    LOKA_VERIFY(observation.reasonNames() == "layout_dirty+viewport_input");
    LOKA_VERIFY(observation.reasons()
                == ((1U << TOOLBOX_FULL_DRAW_LAYOUT_DIRTY) | (1U << TOOLBOX_FULL_DRAW_VIEWPORT_INPUT)));
    LOKA_VERIFY(observation.refusal() == TOOLBOX_PRESENT_NO_SUPPRESSION_CAPABILITY);
    LOKA_VERIFY(fullPending);
    LOKA_VERIFY(firstRequest && std::strcmp(firstRequest, "viewport_input") == 0);
    LOKA_VERIFY(after.windowFlushFullCount == before.windowFlushFullCount + 1);
    LOKA_VERIFY(after.windowFlushDirtyCount == before.windowFlushDirtyCount);
    // Scene's later global layout retains its one rectangle fallback request.
    LOKA_VERIFY(after.windowRectRequestCount == 1);
    if (followUp)
      LOKA_VERIFY(after.requestInvalidateCallCount == 1);
  }
  void presentSiblingViewport()
  {
    PresentLiveFixture<1> f;
    {
      StateTrackerGuard transaction(f.root->tracker());
      f.root->text.set(String("stationary dirty text"));
      f.root->viewport.set(Frame(0, 17, 0, 0));
    }
    f.draw();
    LOKA_VERIFY(f.last().viewports() == 1 && f.last().dy() == 17);
    LOKA_VERIFY(f.last().unsupportedKinds() == 0 && f.last().contentInputs() == 0);
  }
  void presentLatePublication()
  {
    PresentLiveFixture<2> f;
    {
      StateTrackerGuard transaction(f.root->tracker());
      f.root->request.set(ScrollTarget::Origin(0, 17));
    }
    f.draw();
    const ToolboxPresentObservation moved = f.controller.debugStatsForTesting().lastPresentObservation;
    f.draw();
    const ToolboxPresentObservation next = f.controller.debugStatsForTesting().lastPresentObservation;
    std::printf("late publication: moved inputs=%u walk=%u; next inputs=%u walk=%u\n",
                moved.contentInputs(),
                moved.walkReasons(),
                next.contentInputs(),
                next.walkReasons());
    // Publication writes the style after child layout. Boundary apply is deferred:
    // the request settles first, then the next full pass sees the Text input and move.
    LOKA_VERIFY(moved.contentInputs() == 0 && moved.walkReasons() == 0);
    LOKA_VERIFY(next.contentInputs() == 1 && next.walkReasons() == 0);
    LOKA_VERIFY(next.dy() == 17 && next.refusal() == TOOLBOX_PRESENT_CONTENT_INPUTS);
  }

  void presentContentInputs()
  {
    for (unsigned variant = 0; variant < 2; ++variant)
    {
      PresentLiveFixture<0> f;
      {
        StateTrackerGuard transaction(f.root->tracker());
        if (variant)
          f.root->text.set(String("after"));
        else
          f.root->style.set(TextStyle().color(TEXT_COLOR_RED));
        f.root->viewport.set(Frame(0, 17, 0, 0));
      }
      f.draw();
      LOKA_VERIFY(f.last().contentInputs() == 1);
      LOKA_VERIFY(f.last().refusal() == TOOLBOX_PRESENT_CONTENT_INPUTS);
      {
        StateTrackerGuard transaction(f.root->tracker());
        f.root->viewport.set(Frame(0, 34, 0, 0));
      }
      f.draw();
      LOKA_VERIFY(f.last().contentInputs() == 0);
      LOKA_VERIFY(f.last().refusal() == TOOLBOX_PRESENT_NO_SUPPRESSION_CAPABILITY);
    }
  }
  void presentContainerInputNotCounted()
  {
    // Known gap: Box never takes its mark despite moved descendant geometry.
    // PR 2's per-leaf translation check (T1 placement facts) closes this gap.
    PresentLiveFixture<0> f;
    {
      StateTrackerGuard transaction(f.root->tracker());
      f.root->width.set(170);
      f.root->viewport.set(Frame(0, 17, 0, 0));
    }
    f.draw();
    LOKA_VERIFY(f.last().contentInputs() == 0);
    LOKA_VERIFY(f.last().unsupportedKinds() == 0);
  }
  void presentReasons()
  {
    PresentFixture f;
    f.window.requestInvalidateWithReason(TOOLBOX_FULL_DRAW_LAYOUT_DIRTY);
    f.window.requestInvalidateWithReason(TOOLBOX_FULL_DRAW_TEXT_RELAYOUT);
    f.window.flushInvalidate();
    LOKA_VERIFY(f.last().reasons()
                == ((1U << TOOLBOX_FULL_DRAW_LAYOUT_DIRTY) | (1U << TOOLBOX_FULL_DRAW_TEXT_RELAYOUT)));
    LOKA_VERIFY(f.last().reasonNames() == "layout_dirty+text_relayout");
  }
  void presentMove()
  {
    PresentFixture f;
    f.scroll(17);
    // One vertical layout move, only layout reason, empty entry damage, no units.
    // Native-control paint suppression is the remaining unproved capability.
    LOKA_VERIFY(f.last().dy() == 17);
    LOKA_VERIFY(f.last().viewports() == 1);
    LOKA_VERIFY(f.last().verdict() == TOOLBOX_PRESENT_ERASE_ALL);
    LOKA_VERIFY(f.last().refusal() == TOOLBOX_PRESENT_NO_SUPPRESSION_CAPABILITY);
    // #1513 republishes the viewport and can present a second, unmoved full layout.
    f.window.requestInvalidateWithReason(TOOLBOX_FULL_DRAW_LAYOUT_DIRTY);
    f.window.flushInvalidate();
    LOKA_VERIFY(f.last().viewports() == 0);
    LOKA_VERIFY(f.last().refusal() == TOOLBOX_PRESENT_NO_VIEWPORT_MOVED);
    LOKA_VERIFY(f.lastMoved().dy() == 17);
    LOKA_VERIFY(f.lastMoved().viewports() == 1);
    f.scroll(29);
    LOKA_VERIFY(f.last().dy() == 12);
    // Native input has already changed appliedValue; placedOrigin must still see the move.
    f.controller.scrollControl(0).appliedValue = 45;
    f.scroll(45);
    LOKA_VERIFY(f.last().dy() == 16);
    PresentFixture initiallyScrolled(false, 9);
    initiallyScrolled.scroll(26);
    LOKA_VERIFY(initiallyScrolled.last().dy() == 17);
  }
  void presentSiblingMove()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    MutableState<Frame> firstOrigin((Frame()));
    MutableState<Frame> secondOrigin((Frame()));
    NodeState<Frame> firstState(&firstOrigin, 0), secondState(&secondOrigin, 0);
    StackNode column(Column().gap(0).props);
    for (unsigned i = 0; i < 2; ++i)
    {
      BoxNode *box = new BoxNode(Box().size(180, 100).props);
      ScrollViewNode *scroll = new ScrollViewNode(ScrollViewProps(i ? secondState : firstState));
      BoxNode *content = new BoxNode(Box().size(180, 600).props);
      content->addChild(new TextNode(TextProps("sibling content")));
      scroll->addChild(content);
      box->addChild(scroll);
      column.addChild(box);
    }
    controller.rootNode_ = &column;
    window.scenePlatformController_ = &controller;
    window.requestInvalidate();
    window.flushInvalidate();
    const Rect first = controller.viewportScroll().contentRect;
    secondOrigin.set(Frame(0, 17, 0, 0));
    window.requestInvalidateWithReason(TOOLBOX_FULL_DRAW_LAYOUT_DIRTY);
    window.flushInvalidate();
    const ToolboxPresentObservation &second = controller.debugStatsForTesting().lastPresentObservation;
    LOKA_VERIFY(second.viewports() == 1 && second.dy() == 17);
    LOKA_VERIFY(second.viewport().top >= first.bottom);
    firstOrigin.set(Frame(0, 17, 0, 0));
    secondOrigin.set(Frame(0, 34, 0, 0));
    window.requestInvalidateWithReason(TOOLBOX_FULL_DRAW_LAYOUT_DIRTY);
    window.flushInvalidate();
    const ToolboxPresentObservation &both = controller.debugStatsForTesting().lastPresentObservation;
    LOKA_VERIFY(both.viewports() == 2 && both.dy() == 17);
    LOKA_VERIFY(both.refusal() == TOOLBOX_PRESENT_SEVERAL_VIEWPORTS_MOVED);
    controller.rootNode_ = 0;
    window.scenePlatformController_ = 0;
    controller.clearControls();
  }
  void presentForeground()
  {
    PresentFixture f(true);
    f.scroll(17);
    LOKA_VERIFY(f.last().dy() == 17 && f.last().viewports() == 1);
    LOKA_VERIFY(f.last().foregroundUnits() >= 1);
    LOKA_VERIFY(f.last().refusal() == TOOLBOX_PRESENT_FOREGROUND_UNITS);
  }
  void presentDamage(bool flush)
  {
    PresentFixture f;
    const Rect local = {25, 31, 70, 93};
    Rect global = local;
    f.window.port.portBits.bounds.left = -11;
    f.window.port.portBits.bounds.top = -19;
    OffsetRect(&global, 11, 19);
    // A nonrectangular OS region still has an observation bbox, although it
    // cannot provide the rectangular erased-pass capture evidence.
    Region region = {sizeof(Region) + 2, global};
    Region *pointer = &region;
    f.window.port.updateRgn = &pointer;
    f.window.port.native.update.push_back(local);
    if (flush)
      f.window.drawForFlush();
    else
      f.window.drawForUpdate();
    const Rect damage = f.last().damage();
    LOKA_VERIFY(damage.top == local.top && damage.left == local.left && damage.bottom == local.bottom
                && damage.right == local.right);
    const Rect emptyBounds[] = {{0, 0, 0, 0}, {20, 10, 40, 10}, {40, 10, 20, 30}};
    for (unsigned i = 0; i < sizeof(emptyBounds) / sizeof(emptyBounds[0]); ++i)
    {
      region.rgnBBox = emptyBounds[i];
      if (flush)
        f.window.drawForFlush();
      else
        f.window.drawForUpdate();
      const Rect emptyDamage = f.last().damage();
      LOKA_VERIFY(emptyDamage.top == 0 && emptyDamage.left == 0 && emptyDamage.bottom == 0 && emptyDamage.right == 0);
    }
    f.window.port.updateRgn = 0;
  }
  void presentReset()
  {
    PresentFixture f;
    f.scroll(17);
    f.window.drawForFlush();
    LOKA_VERIFY(f.last().reasons() == 0);
    LOKA_VERIFY(f.last().viewports() == 0);
    LOKA_VERIFY(f.last().refusal() == TOOLBOX_PRESENT_NO_VIEWPORT_MOVED);
    f.window.requestInvalidateWithReason(TOOLBOX_FULL_DRAW_CHILD_DIRTY);
    f.controller.resetDebugStats();
    LOKA_VERIFY(f.lastMoved().viewports() == 0);
    f.window.flushInvalidate();
    LOKA_VERIFY(f.last().reasons() == 0);
    // The no-root early exit must consume its interval too.
    f.window.requestInvalidateWithReason(TOOLBOX_FULL_DRAW_CHILD_DIRTY);
    f.controller.rootNode_ = 0;
    f.window.flushInvalidate();
    f.controller.rootNode_ = &f.viewport;
    f.window.drawForFlush();
    LOKA_VERIFY(f.last().reasons() == 0);
  }
  void presentOrder()
  {
    const Rect viewport = {10, 10, 100, 100};
    const Rect outside = {0, 0, 200, 200};
    ToolboxPresentObservation observation;
    observation.noteReason(TOOLBOX_FULL_DRAW_UNKNOWN);
    observation.noteDamage(outside);
    observation.complete(1);
    LOKA_VERIFY(observation.refusal() == TOOLBOX_PRESENT_NO_VIEWPORT_MOVED);
    observation.reset();
    observation.noteViewportMove(viewport, 2, 3, ToolboxViewportContentObservation());
    observation.noteViewportMove(viewport, 0, 4, ToolboxViewportContentObservation());
    observation.complete(1);
    LOKA_VERIFY(observation.refusal() == TOOLBOX_PRESENT_SEVERAL_VIEWPORTS_MOVED);
    observation.reset();
    observation.noteViewportMove(viewport, 2, 3, ToolboxViewportContentObservation());
    observation.complete(1);
    LOKA_VERIFY(observation.viewports() == 1);
    LOKA_VERIFY(observation.refusal() == TOOLBOX_PRESENT_HORIZONTAL_MOVE);
    observation.reset();
    observation.noteViewportMove(viewport, 0, 3, ToolboxViewportContentObservation());
    observation.complete(1);
    LOKA_VERIFY(observation.refusal() == TOOLBOX_PRESENT_REASONS_BEYOND_LAYOUT);
    observation.reset();
    observation.noteViewportMove(viewport, 0, 3, ToolboxViewportContentObservation());
    observation.noteReason(TOOLBOX_FULL_DRAW_LAYOUT_DIRTY);
    observation.noteDamage(outside);
    observation.complete(1);
    LOKA_VERIFY(observation.refusal() == TOOLBOX_PRESENT_DAMAGE_OUTSIDE_VIEWPORT);
    observation.reset();
    observation.noteReason(TOOLBOX_FULL_DRAW_LAYOUT_DIRTY);
    observation.noteReason(TOOLBOX_FULL_DRAW_VIEWPORT_INPUT);
    observation.noteViewportMove(viewport, 0, 3, ToolboxViewportContentObservation());
    observation.complete(0);
    LOKA_VERIFY(observation.refusal() == TOOLBOX_PRESENT_NO_SUPPRESSION_CAPABILITY);
    observation.reset();
    observation.noteReason(TOOLBOX_FULL_DRAW_VIEWPORT_INPUT);
    observation.complete(0);
    // No movement precedes the reason gate, even for an admitted input reason.
    LOKA_VERIFY(observation.refusal() == TOOLBOX_PRESENT_NO_VIEWPORT_MOVED);
    observation.noteViewportMove(viewport, 0, 3, ToolboxViewportContentObservation());
    observation.complete(0);
    LOKA_VERIFY(observation.refusal() == TOOLBOX_PRESENT_NO_SUPPRESSION_CAPABILITY);
    observation.reset();
    observation.noteReason(TOOLBOX_FULL_DRAW_LAYOUT_DIRTY);
    observation.noteReason(TOOLBOX_FULL_DRAW_VIEWPORT_INPUT);
    observation.noteReason(TOOLBOX_FULL_DRAW_TEXT_RELAYOUT);
    observation.noteViewportMove(viewport, 0, 3, ToolboxViewportContentObservation());
    observation.complete(0);
    LOKA_VERIFY(observation.refusal() == TOOLBOX_PRESENT_REASONS_BEYOND_LAYOUT);
    // Each later refusal remains present while the earlier one wins.
    for (unsigned gate = 0; gate < 4; ++gate)
    {
      observation.reset();
      observation.noteReason(TOOLBOX_FULL_DRAW_LAYOUT_DIRTY);
      ToolboxPresentObservation::Interval interval(observation);
      ToolboxViewportContentObservation content;
      if (gate == 0)
        content.noteContentInputs();
      if (gate < 2)
        observation.noteReason(TOOLBOX_FULL_DRAW_CHILD_DIRTY);
      if (gate < 3)
        content.noteUnsupportedKind();
      content.notePhaseBound();
      observation.noteViewportMove(viewport, 0, 3, content);
      observation.noteDamage(outside);
      observation.complete(1);
      LOKA_VERIFY(observation.refusal()
                  == (gate == 0   ? TOOLBOX_PRESENT_CONTENT_INPUTS
                      : gate == 1 ? TOOLBOX_PRESENT_REQUESTS_DURING_WALK
                      : gate == 2 ? TOOLBOX_PRESENT_UNSUPPORTED_KIND
                                  : TOOLBOX_PRESENT_DITHER_PHASE));
      LOKA_VERIFY(observation.reasons() == (1U << TOOLBOX_FULL_DRAW_LAYOUT_DIRTY));
      LOKA_VERIFY(observation.walkReasonNames() == (gate < 2 ? "child_dirty" : "none"));
    }
    LOKA_VERIFY(observation.contentInputs() == 0 && observation.unsupportedKinds() == 0);
    LOKA_VERIFY(observation.walkReasons() == 0);
    LOKA_VERIFY(observation.phaseBound() == 0);
    observation.reset();
    {
      ToolboxPresentObservation::Interval interval(observation);
      observation.noteReason(TOOLBOX_FULL_DRAW_LAYOUT_DIRTY);
      observation.complete(0);
      observation.noteReason(TOOLBOX_FULL_DRAW_CHILD_DIRTY);
    }
    observation.complete(0);
    LOKA_VERIFY(observation.reasons() == (1U << TOOLBOX_FULL_DRAW_CHILD_DIRTY));
  }
  void clippedUnitExtent()
  {
    ToolboxWindow window;
    ToolboxScenePlatformController controller(&window);
    ZStackNode stack((ZStackProps()));
    stack.addChild(new Probe("ground", 10));
    stack.addChild(new Probe("offscreen-unit", 40));
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 300)));
    ProjectionParentScope viewport(window.window(), 0, 0, Frame(0, 100, 100, 100));
    ProjectionViewportScopeGuard guard(controller.projectionParentScopes_, viewport);
    LayoutState state = seat();
    LayoutNode(&stack, state, &controller, 0);
    LOKA_VERIFY(controller.unitLedger_.mark() == 0);
    LOKA_VERIFY(state.y == 60);
  }
  void rootOverflowRepro()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    LOKA_VERIFY(RegisterToolboxPlatformLayoutHandlers(controller.gapLayoutRegistry));
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    ZStackNode stack((ZStackProps()));
    stack.addChild(new Probe("ground", 70));
    StackNode *column = new StackNode(Column().gap(0).props);
    column->addChild(new Probe("first", 10));
    column->addChild(new BoxNode(Box().size(100, SHRT_MAX).props));
    column->addChild(new Probe("after-refusal", 40));
    stack.addChild(column);
    LayoutState state = seat();
    LayoutNode(&stack, state, &controller, 0);
    LOKA_VERIFY(controller.unitLedger_.mark() == 0);
  }
  void toggleCaret(TEHandle te)
  {
    (**te).caretState = (**te).caretState ? 0 : -1;
  }
  bool requested(const Rect &rect)
  {
    for (std::size_t i = 0; i < toolbox_host::invalidatedRects.size(); ++i)
      if (EqualRect(&rect, &toolbox_host::invalidatedRects[i]))
        return true;
    return false;
  }
  void editorIdleRepair()
  {
    loka::app::testing::TextEditorStateOwner owner;
    loka::testing::TextBufferFixture lines;
    LOKA_VERIFY(lines.attach(&owner.tracker, 16) == ATTACH_OK);
    LOKA_VERIFY(lines.insert(0, String("abcd")) == EDIT_OK);
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
    ZStackNode stack((ZStackProps()));
    stack.addChild(new Probe("ground", 70));
    stack.addChild(new Probe("unit", 40));
    stack.addChild(new Probe("second-unit", 30));
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    LayoutState state = seat();
    LayoutNode(&stack, state, &controller, 0);
    TextEditorNode node(TextEditorProps(lines, owner.cursor));
    state.x = 5;
    state.y = 25;
    state.width = 85;
    state.height = 50;
    IPlatformNodeHandler *handler = controller.nodeHandlerRegistry_.find(&node);
    LOKA_VERIFY(handler);
    ToolboxTextEditorContext *context =
        static_cast<ToolboxTextEditorContext *>(handler->ensureContext(&node, &controller, state));
    LOKA_VERIFY(context);
    context->layout(&controller, state);
    context->render(&controller);
    LOKA_VERIFY(controller.editControls_.size() == 1);
    State<bool> barEnabled(false);
    ToolboxScrollBarLedger::ScrollBarControlBinding &bar = controller.scrollControl(0);
    bar.enabled = &barEnabled;
    const unsigned beforeBar = toolbox_host::invalidations;
    controller.pendingDirtyRects_.clear();
    controller.inBatchUpdate_ = true;
    controller.refreshScrollBarEnabled(bar);
    LOKA_VERIFY(toolbox_host::invalidations == beforeBar);
    LOKA_VERIFY(controller.pendingDirtyRects_.size() == 1);
    Rect visible;
    LOKA_VERIFY(bar.placement.queryVisible(visible));
    equal(controller.pendingDirtyRects_[0], visible);
    controller.inBatchUpdate_ = false;
    controller.refreshScrollBarEnabled(bar);
    LOKA_VERIFY(toolbox_host::invalidations == beforeBar + 1);
    controller.editControls_.focus(0);
    TEHandle te = loka::testing::ToolboxTextEditorAccess::te(*context);
    LOKA_VERIFY(te);
    const Point point = {(**te).viewRect.top, (**te).viewRect.left};
    LOKA_VERIFY(context->click(point) == EDITOR_OK);
    toolbox_host::invalidatedRects.clear();
    ToolboxInputDoor::idleTextEdits(controller);
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY(toolbox_host::invalidatedRects.empty());
    toolbox_host::afterIdle = toggleCaret;
    ToolboxInputDoor::idleTextEdits(controller);
    const Rect first = {25, 10, 60, 90};
    const Rect second = {25, 10, 50, 90};
    LOKA_VERIFY(toolbox_host::invalidatedRects.size() == 2);
    equal(toolbox_host::invalidatedRects[0], first);
    equal(toolbox_host::invalidatedRects[1], second);
    toolbox_host::afterIdle = 0;
    toolbox_host::invalidatedRects.clear();
    LOKA_VERIFY(context->key('x') == EDITOR_OK);
    LOKA_VERIFY(requested(first) && requested(second));
    toolbox_host::invalidatedRects.clear();
    LOKA_VERIFY(context->click(point) == EDITOR_OK);
    LOKA_VERIFY(requested(first) && requested(second));
    state = seat();
    state.x = 15;
    state.y = 25;
    state.width = 75;
    state.height = 20;
    context->layout(&controller, state);
    context->render(&controller);
    toolbox_host::invalidatedRects.clear();
    toolbox_host::afterIdle = toggleCaret;
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY(toolbox_host::invalidatedRects.empty());
    toolbox_host::afterIdle = 0;
    context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    controller.flushTE();
  }
  void idleRepair()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ZStackNode stack((ZStackProps()));
    stack.addChild(new Probe("ground", 70));
    stack.addChild(new Probe("unit", 40));
    stack.addChild(new Probe("second-unit", 30));
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    LayoutState state = seat();
    LayoutNode(&stack, state, &controller, 0);
    ToolboxScenePlatformController::EditTextControlBinding binding(0, 0, 0);
    binding.ownerContext = 0;
    binding.editor = 0;
    binding.text = 0;
    const Rect bindingRect = {25, 5, 75, 90};
    binding.placement = ToolboxNativePlacement(bindingRect, bindingRect);
    binding.te = TENew(&binding.placement.full(), &binding.placement.full());
    binding.usedThisFrame = true;
    binding.lifetimeHint = NATIVE_HINT_EAGER_RELEASE;
    LOKA_VERIFY(controller.editControls_.prepareAppend());
    controller.editControls_.commitAppend(binding);
    controller.editControls_.focus(0);
    toolbox_host::invalidatedRects.clear();
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY(toolbox_host::invalidatedRects.empty());
    toolbox_host::afterIdle = toggleCaret;
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY(toolbox_host::invalidatedRects.size() == 2);
    const Rect first = {25, 10, 60, 90};
    const Rect second = {25, 10, 50, 90};
    equal(toolbox_host::invalidatedRects[0], first);
    equal(toolbox_host::invalidatedRects[1], second);
    toolbox_host::invalidatedRects.clear();
    // Unit A contains the editor, but the shorter later unit B crosses it.
    // A caret toggle must repair B without requesting anything for A.
    const Rect insideAOverlappingB = {25, 15, 55, 90};
    controller.editControls_[0].placement = ToolboxNativePlacement(insideAOverlappingB, insideAOverlappingB);
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY(toolbox_host::invalidatedRects.size() == 1);
    const Rect overlapB = {25, 15, 50, 90};
    equal(toolbox_host::invalidatedRects[0], overlapB);
    toolbox_host::invalidatedRects.clear();
    const Rect contained = {25, 15, 45, 90};
    controller.editControls_[0].placement = ToolboxNativePlacement(contained, contained);
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY(toolbox_host::invalidatedRects.empty());
    const Rect touching = {60, 10, 80, 90};
    controller.editControls_[0].placement = ToolboxNativePlacement(touching, touching);
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY(toolbox_host::invalidatedRects.empty());
    controller.rootNode_ = &stack;
    const int before = controller.debugStats_.totalRenderCalls;
    ToolboxInputDoor::renderDirty(controller, touching);
    LOKA_VERIFY(controller.debugStats_.totalRenderCalls == before);
    controller.rootNode_ = 0;
    toolbox_host::afterIdle = 0;
    controller.clearControls();
    // A vanished window cannot supply a safe deactivation port. Its active
    // record must retire eagerly instead of entering the EditText churn pool.
    binding.te = TENew(&binding.placement.full(), &binding.placement.full());
    TEActivate(binding.te);
    controller.window_ = 0;
    controller.retireEditTextBinding(binding, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(!binding.te);
    LOKA_VERIFY(controller.retiredTextEdits_.back().lifetimeHint == NATIVE_HINT_EAGER_RELEASE);
    controller.window_ = &window;
  }
  void textDirtyClip()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ZStackNode stack((ZStackProps()));
    stack.addChild(new Probe("ground", 70));
    stack.addChild(new Probe("unit", 40));
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    LayoutState state = seat();
    LayoutNode(&stack, state, &controller, 0);
    State<String> text(String("Ground text partly below unit"));
    ToolboxScenePlatformController::TextHit hit;
    hit.context = 0;
    hit.rect = Rect();
    hit.rect.left = 10;
    hit.rect.top = 50;
    hit.rect.right = 110;
    hit.rect.bottom = 80;
    hit.x = 10;
    hit.y = 65;
    hit.text = &text;
    hit.boundary = 0;
    hit.lastMeasuredWidth = 100;
    hit.needsRelayoutOnChange = false;
    controller.recordTextHit(hit.rect, hit.x, hit.y, hit.text, hit.boundary, false, hit.lastMeasuredWidth, 0);
    controller.rootNode_ = &stack;
    const Rect dirty = {65, 10, 80, 110};
    ClipRect(&dirty); // drawDirty's outer clip, supplied by the native window.
    toolbox_host::draws.clear();
    const int before = controller.debugStats_.totalRenderCalls;
    ToolboxInputDoor::renderDirty(controller, dirty);
    LOKA_VERIFY(controller.debugStats_.totalRenderCalls == before);
    LOKA_VERIFY(toolbox_host::draws.size() == 1);
    equal(toolbox_host::draws[0].clip, dirty);
    toolbox_host::draws.clear();
    toolbox_host::refuseRegionAttempt = toolbox_host::regions + 1;
    ToolboxInputDoor::renderDirty(controller, dirty);
    toolbox_host::refuseRegionAttempt = 0;
    LOKA_VERIFY(toolbox_host::draws.empty());
    State<String> shorter(String("x"));
    hit.text = &shorter;
    hit.lastMeasuredWidth = 100;
    toolbox_host::refuseRegionAttempt = toolbox_host::regions + 1;
    controller.redrawTextHit(hit);
    toolbox_host::refuseRegionAttempt = 0;
    LOKA_VERIFY(hit.lastMeasuredWidth == 100);
    toolbox_host::erasedRects.clear();
    controller.redrawTextHit(hit);
    LOKA_VERIFY(toolbox_host::erasedRects.size() == 1);
    LOKA_VERIFY(toolbox_host::erasedRects[0].right == 110);
    LOKA_VERIFY(hit.lastMeasuredWidth < 100);
    controller.rootNode_ = 0;
  }
  class WidthOfferProbe : public Probe
  {
  public:
    WidthOfferProbe()
        : Probe("width-offer", 7)
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      // Reuse the fixture's rectangle for the offered frame before projection.
      this->clip.left = state.x;
      this->clip.top = state.y;
      this->clip.right = static_cast<short>(state.x + state.width);
      this->clip.bottom = static_cast<short>(state.y + state.height);
      return Probe::layoutProjected(platform, state);
    }
  };
  class WidthLayoutBoundary : public BoundaryNode
  {
  public:
    virtual void composeWithContext(ComponentContext &, ComposeEvent) {}
  };
  void widthOnlyDispatch(bool registered)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    if (registered)
      LOKA_VERIFY(RegisterToolboxPlatformLayoutHandlers(controller.gapLayoutRegistry));
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 500, 200)));
    MutableState<int> liveWidth(300);
    WidthLayoutBoundary outer;
    WidthLayoutBoundary *inner = new WidthLayoutBoundary();
    outer.addChild(inner);
    BoxNode *box = new BoxNode(Box().padding(5).width(&liveWidth).props);
    WidthOfferProbe *probe = new WidthOfferProbe();
    box->addChild(probe);
    inner->addChild(box);
    for (int i = 0; i < 2; ++i)
    {
      if (i == 1)
        liveWidth.set(180);
      LayoutState state = seat();
      const short width = LayoutNode(&outer, state, &controller, 0);
      LOKA_VERIFY(width == (i == 0 ? 300 : 180));
      LOKA_VERIFY(state.y == 37);
      LOKA_VERIFY(probe->clip.left == 15);
      LOKA_VERIFY(probe->clip.top == 25);
      LOKA_VERIFY(probe->clip.right - probe->clip.left == (i == 0 ? 290 : 170));
      LOKA_VERIFY(probe->clip.bottom - probe->clip.top == 70);
    }
    ZStackNode stack((ZStackProps()));
    stack.addChild(new Probe("ground", 70));
    BoxNode *upper = new BoxNode(Box().padding(5).width(300).props);
    WidthOfferProbe *upperProbe = new WidthOfferProbe();
    upper->addChild(upperProbe);
    stack.addChild(upper);
    LayoutState state = seat();
    LayoutNode(&stack, state, &controller, 0);
    LOKA_VERIFY(controller.unitLedger_.mark() == 1);
    const Rect row = unitRow(controller, &stack, 1)->rect;
    // The upper unit retains its parent seat even though Box offers wider content.
    LOKA_VERIFY(row.left == 10);
    LOKA_VERIFY(row.top == 20);
    LOKA_VERIFY(row.right - row.left == 100);
    LOKA_VERIFY(row.bottom - row.top == 17);
    LOKA_VERIFY(upperProbe->clip.left == 15);
    LOKA_VERIFY(upperProbe->clip.top == 25);
    LOKA_VERIFY(upperProbe->clip.right - upperProbe->clip.left == 290);
    LOKA_VERIFY(upperProbe->clip.bottom - upperProbe->clip.top == 70);
    LOKA_VERIFY(state.y == 90);
  }
  void heightOnlyUnit(bool registered)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    if (registered)
      LOKA_VERIFY(RegisterToolboxPlatformLayoutHandlers(controller.gapLayoutRegistry));
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    ZStackNode stack((ZStackProps()));
    stack.addChild(new Probe("ground", 70));
    BoxNode *upper = new BoxNode(Box().height(40).props);
    stack.addChild(upper);
    for (int i = 0; i < 2; ++i)
    {
      if (i == 1)
        upper->props.setHeight(120);
      controller.unitLedger_.clear();
      LayoutState state = seat();
      LayoutNode(&stack, state, &controller, 0);
      LOKA_VERIFY(controller.unitLedger_.mark() == 1);
      const Rect row = unitRow(controller, &stack, 1)->rect;
      LOKA_VERIFY(row.left == 10);
      LOKA_VERIFY(row.top == 20);
      LOKA_VERIFY(row.right - row.left == 100);
      LOKA_VERIFY(row.bottom - row.top == (i == 0 ? 40 : 120));
      LOKA_VERIFY(state.y == (i == 0 ? 90 : 140));
    }
  }
  /** A ground button retiring beneath a later unit hides without painting:
      the Control Manager's hidden-control erase would otherwise punch through
      the unit. The native update damage still queues, so composition replay
      presents the area in order. */
  void retiredGroundHideSuppressed()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    ZStackNode stack((ZStackProps()));
    stack.addChild(new Probe("ground", 70));
    PlacedButton *button = new PlacedButton();
    button->setContext(new ToolboxButtonContext(button, &controller));
    stack.addChild(button);
    stack.addChild(new Probe("later-unit", 40));
    controller.rootNode_ = &stack;
    ToolboxInputDoor::render(controller);
    LOKA_VERIFY(controller.buttonControls_.size() == 1);
    ControlRef control = controller.buttonControls_[0].control;
    const short id = controller.buttonControls_[0].resourceId;
    const Rect rect = control->rect;
    LOKA_VERIFY(control->visible);
    control->calls.clear();
    window.port.native.update.clear();
    // Retire from an unrelated current port, as a detach pass may run.
    ToolboxWindow other;
    HostPortScope otherPort(other.window());
    controller.destroyButtonControl(id, NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(controller.buttonControls_.empty() && !control->visible);
    bool hidden = false;
    for (size_t i = 0; i < control->calls.size(); ++i)
      if (control->calls[i].operation == "hide")
      {
        hidden = true;
        LOKA_VERIFY(!control->calls[i].painted);
        LOKA_VERIFY(control->calls[i].currentPort == window.window());
      }
    LOKA_VERIFY(hidden);
    LOKA_VERIFY(window.port.native.update.size() == 1 && EqualRect(&window.port.native.update[0], &rect));
    GrafPtr current;
    GetPort(&current);
    LOKA_VERIFY(current == other.window());
    controller.clearControls();
    std::puts("[pin] retired ground button hides under an empty clip beneath a later unit");
  }
  void run(const char *mode)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    LOKA_VERIFY(RegisterToolboxPlatformLayoutHandlers(controller.gapLayoutRegistry));
    LOKA_VERIFY(controller.projectionParentScopes_.resetRoot(window.window(), Frame(0, 0, 300, 200)));
    ZStackNode stack((ZStackProps()));
    Probe *ground = new Probe("ground", 70);
    Probe *upper = new Probe("upper", 40);
    stack.addChild(ground);
    const bool nested = std::strcmp(mode, "NestedZStackInUnitLaysOutNothing") == 0;
    const bool scroll = std::strcmp(mode, "ScrollViewRefusedInsideUnit") == 0;
    if (nested)
    {
      ZStackNode *inner = new ZStackNode(ZStackProps());
      inner->addChild(upper);
      StackNode *column = new StackNode(Column().gap(0).props);
      column->addChild(new Probe("unit-content", 10));
      column->addChild(inner);
      stack.addChild(column);
    }
    else if (scroll)
    {
      ScrollViewNode *inner = new ScrollViewNode(ScrollViewProps());
      inner->addChild(upper);
      stack.addChild(inner);
    }
    else
      stack.addChild(upper);
    LayoutState state = seat();
    const Rect outer = {25, 5, 75, 90};
    ClipRect(&outer);
    if (std::strcmp(mode, "RefusedChildHasNoRowAndNoRender") == 0)
    {
      upper->refuse = true;
      LayoutNode(&stack, state, &controller, 0);
      LOKA_VERIFY(controller.unitLedger_.mark() == 0);
      RenderNode(&stack, &controller);
      LOKA_VERIFY(upper->renders == 0);
      return;
    }
    LayoutNode(&stack, state, &controller, 0);
    toolbox_host::paintOperations.clear();
    toolbox_host::erasedRects.clear();
    RenderNode(&stack, &controller);
    if (nested || scroll)
    {
      LOKA_VERIFY(upper->layouts == 0 && upper->renders == 0);
      LOKA_VERIFY(controller.unitLedger_.mark() == (nested ? 1u : 0u));
      return;
    }
    LOKA_VERIFY(controller.unitLedger_.mark() == 1);
    const Rect expected = {20, 10, 60, 110};
    equal(unitRow(controller, &stack, 1)->rect, expected);
    LOKA_VERIFY(unitRow(controller, &stack, 1)->stack == &stack && unitRow(controller, &stack, 1)->index == 1);
    LOKA_VERIFY(state.y == 90 && upper->layouts == 1 && ground->layouts == 1);
    LOKA_VERIFY(toolbox_host::paintOperations.size() == 3);
    LOKA_VERIFY(toolbox_host::paintOperations[0] == "ground");
    LOKA_VERIFY(toolbox_host::paintOperations[1] == "erase");
    LOKA_VERIFY(toolbox_host::paintOperations[2] == "upper");
    equal(toolbox_host::erasedRects[0], expected);
    const Rect clipped = {25, 10, 60, 90};
    equal(upper->clip, clipped);
    {
      toolbox_host::refuseRegionAttempt = toolbox_host::regions + 1;
      const unsigned before = upper->renders;
      RenderNode(&stack, &controller);
      toolbox_host::refuseRegionAttempt = 0;
      LOKA_VERIFY(upper->renders == before);
    }
    if (std::strcmp(mode, "DirtyTouchingUnitTakesCompositionReplay") == 0)
    {
      controller.rootNode_ = &stack;
      const int before = controller.debugStats_.totalRenderCalls;
      ToolboxInputDoor::renderDirty(controller, expected);
      LOKA_VERIFY(controller.debugStats_.totalRenderCalls == before + 1);
      LOKA_VERIFY(upper->renders >= 2);
      controller.rootNode_ = 0;
    }
  }
} // namespace
int main(int argc, char **argv)
{
  const char *names[] = {"NativePlacement",
                         "PresentReasons",
                         "PresentMove",
                         "PresentForeground",
                         "PresentUpdateDamage",
                         "PresentFlushDamage",
                         "PresentReset",
                         "PresentOrder",
                         "ViewportInputReason",
                         "ViewportInputFollowUp",
                         "PresentRefusedVisit",
                         "PresentSiblingViewport",
                         "PresentLatePublication",

                         "PresentContentInputs",
                         "PresentUnsupportedKind",
                         "PresentContainerInputNotCounted",
                         "PresentCardBandPhase",
                         "PresentClipBoxRefused",

                         "ClipFrameProjection",
                         "BandGeometry",
                         "BandLifetime",
                         "BandDelivery",
                         "BandSelection",
                         "StaticFirstUnit",
                         "StaticNoRequests",
                         "StaticCopyFailure",
                         "StaticRefusedReplay",
                         "FlushFirstCapture",
                         "StaticOpportunities",
                         "ErasedAdmissionMatrix",
                         "StaticAdmission",
                         "StaticExternalDraw",
                         "StaticFailure",
                         "StaticIneligible",
                         "StaticMissingRow",
                         "StaticChangedRect",
                         "StaticChangedGround",
                         "StaticAbortedLayout",
                         "FrostErasedPassAdmission",
                         "HeightOnlyBoxUnit",
                         "WidthOnlyBoxDispatch",
                         "TextDirtyClipStaysBelowUnit",
                         "IdleRepairOnlyToggledOverlaps",
                         "LedgerRebuiltEachPass",
                         "UnitFrameInheritsParentClip",
                         "UnitInsideViewportRowIsProjected",
                         "LaterChildGetsUnitRow",
                         "UnitErasesGroundBeforeChild",
                         "UnitClipsChild",
                         "RefusedChildHasNoRowAndNoRender",
                         "NestedZStackInUnitLaysOutNothing",
                         "ScrollViewRefusedInsideUnit",
                         "DirtyTouchingUnitTakesCompositionReplay",
                         "RetiredGroundHideSuppressed"};
  bool known = false;
  if (argc == 2)
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
      known = known || std::strcmp(argv[1], names[i]) == 0;
  if (!known)
  {
    std::fprintf(stderr, "Unknown Toolbox ZStack unit test: %s\n", argc == 2 ? argv[1] : "<missing name>");
    return 1;
  }
  if (std::strcmp(argv[1], "NativePlacement") == 0)
    nativePlacement();
  else if (std::strcmp(argv[1], "ClipFrameProjection") == 0)
    clipFrameProjection();
  else if (std::strcmp(argv[1], "PresentReasons") == 0)
    presentReasons();
  else if (std::strcmp(argv[1], "PresentMove") == 0)
  {
    presentMove();
    presentSiblingMove();
  }
  else if (std::strcmp(argv[1], "PresentForeground") == 0)
    presentForeground();
  else if (std::strcmp(argv[1], "PresentUpdateDamage") == 0)
    presentDamage(false);
  else if (std::strcmp(argv[1], "PresentFlushDamage") == 0)
    presentDamage(true);
  else if (std::strcmp(argv[1], "PresentReset") == 0)
    presentReset();
  else if (std::strcmp(argv[1], "PresentContentInputs") == 0)
    presentContentInputs();
  else if (std::strcmp(argv[1], "PresentUnsupportedKind") == 0)
    presentUnsupportedKind();
  else if (std::strcmp(argv[1], "PresentContainerInputNotCounted") == 0)
    presentContainerInputNotCounted();
  else if (std::strcmp(argv[1], "PresentCardBandPhase") == 0)
    presentCardBandPhase();
  else if (std::strcmp(argv[1], "PresentClipBoxRefused") == 0)
    presentClipBoxRefused();
  else if (std::strcmp(argv[1], "PresentSiblingViewport") == 0)
    presentSiblingViewport();
  else if (std::strcmp(argv[1], "PresentLatePublication") == 0)
    presentLatePublication();
  else if (std::strcmp(argv[1], "PresentRefusedVisit") == 0)
    presentRefusedVisit();
  else if (std::strncmp(argv[1], "ViewportInput", 13) == 0)
    viewportInput(argv[1]);
  else if (std::strcmp(argv[1], "PresentOrder") == 0)
    presentOrder();
  else if (std::strcmp(argv[1], "StaticNoRequests") == 0)
    staticNoRequests();
  else if (std::strcmp(argv[1], "StaticRefusedReplay") == 0)
    staticRefusedReplay();
  else if (std::strcmp(argv[1], "StaticCopyFailure") == 0)
    staticCopyFailure();
  else if (std::strcmp(argv[1], "StaticFirstUnit") == 0)
    staticFirstUnit();
  else if (std::strcmp(argv[1], "FlushFirstCapture") == 0)
    flushFirstCapture();
  else if (std::strcmp(argv[1], "StaticOpportunities") == 0)
    staticOpportunities();
  else if (std::strcmp(argv[1], "ErasedAdmissionMatrix") == 0)
    erasedAdmissionMatrix();
  else if (std::strcmp(argv[1], "StaticAdmission") == 0)
    staticAdmission();
  else if (std::strcmp(argv[1], "StaticExternalDraw") == 0)
    staticExternalDraw();
  else if (std::strcmp(argv[1], "StaticFailure") == 0)
    staticFailure();
  else if (std::strcmp(argv[1], "StaticIneligible") == 0)
    staticIneligible();
  else if (std::strncmp(argv[1], "Static", 6) == 0)
    staticReconcile(argv[1]);
  else if (std::strcmp(argv[1], "BandGeometry") == 0)
    bandGeometry();
  else if (std::strcmp(argv[1], "BandLifetime") == 0)
    bandLifetime();
  else if (std::strcmp(argv[1], "BandDelivery") == 0)
  {
    bandDelivery();
    bandRepairSettles();
  }
  else if (std::strcmp(argv[1], "BandSelection") == 0)
    bandSelection();
  else if (std::strcmp(argv[1], "FrostErasedPassAdmission") == 0)
  {
    erasedFrostAdmission();
    bandSecondUnit();
  }
  else if (std::strcmp(argv[1], "HeightOnlyBoxUnit") == 0)
  {
    heightOnlyUnit(false);
    heightOnlyUnit(true);
  }
  else if (std::strcmp(argv[1], "WidthOnlyBoxDispatch") == 0)
  {
    widthOnlyDispatch(false);
    widthOnlyDispatch(true);
  }
  else if (std::strcmp(argv[1], "TextDirtyClipStaysBelowUnit") == 0)
    textDirtyClip();
  else if (std::strcmp(argv[1], "IdleRepairOnlyToggledOverlaps") == 0)
  {
    idleRepair();
    editorIdleRepair();
  }
  else if (std::strcmp(argv[1], "LedgerRebuiltEachPass") == 0)
    rebuilt();
  else if (std::strcmp(argv[1], "UnitFrameInheritsParentClip") == 0)
    inheritedButton();
  else if (std::strcmp(argv[1], "UnitInsideViewportRowIsProjected") == 0)
    viewportRow();
  else if (std::strcmp(argv[1], "RetiredGroundHideSuppressed") == 0)
    retiredGroundHideSuppressed();
  else
  {
    run(argv[1]);
    if (std::strcmp(argv[1], "LaterChildGetsUnitRow") == 0)
    {
      clippedUnitExtent();
    }
    if (std::strcmp(argv[1], "RefusedChildHasNoRowAndNoRender") == 0)
    {
      refusedControlCleanup();
      rootOverflowRepro();
    }
  }
  return 0;
}
