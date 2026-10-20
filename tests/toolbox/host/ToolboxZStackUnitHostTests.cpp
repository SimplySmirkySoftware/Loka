#include "ToolboxUpdateEvidence.hpp"
#include "ToolboxHost.hpp"
#include "ToolboxNodeDispatch.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxDirtyReplay.hpp"
#include "ToolboxPlatformLayoutHandlers.hpp"
#include "app/nodes/nestable/Box.hpp"
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
class ToolboxStaticBlurInspection
{
public:
  static ToolboxStaticBlur::Phase phase(const ToolboxStaticBlur &owner)
  {
    return owner.phase();
  }
  static bool hasPixels(const ToolboxStaticBlur &owner)
  {
    return owner.pixels_.image_ != 0;
  }
};
namespace
{
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
      LOKA_VERIFY(ToolboxStaticBlurInspection::phase(this->controller.staticBlur_) == ToolboxStaticBlur::READY);
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
      LOKA_VERIFY(ToolboxStaticBlurInspection::phase(f.controller.staticBlur_) == ToolboxStaticBlur::EMPTY);
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
    LOKA_VERIFY(ToolboxStaticBlurInspection::phase(f.controller.staticBlur_) == ToolboxStaticBlur::EMPTY);
    Rect small = f.rect();
    --small.right;
    ToolboxRenderDirtyInCompositionOrder(f.controller, small);
    LOKA_VERIFY(toolbox_host::staticCaptures == 0);
    LOKA_VERIFY(f.window.pendingInvalidateRects_.empty());
    const unsigned before = toolbox_host::invalidations;
    f.window.flushInvalidate();
    LOKA_VERIFY(toolbox_host::invalidations == before);
    LOKA_VERIFY(!f.window.hasPendingInvalidate());
    LOKA_VERIFY(ToolboxStaticBlurInspection::phase(f.controller.staticBlur_) == ToolboxStaticBlur::EMPTY);
    ToolboxRenderDirtyInCompositionOrder(f.controller, f.rect());
    LOKA_VERIFY(toolbox_host::staticCaptures == 1);
    LOKA_VERIFY(ToolboxStaticBlurInspection::phase(f.controller.staticBlur_) == ToolboxStaticBlur::READY);
    LOKA_VERIFY(!f.window.hasPendingInvalidate());
    f.update(false);
    LOKA_VERIFY(toolbox_host::staticCaptures == 1);
    ToolboxRenderDirtyInCompositionOrder(f.controller, f.rect());
    LOKA_VERIFY(toolbox_host::staticCaptures == 1);
    toolbox_host::staticEligible = false;
    const unsigned copies = toolbox_host::staticCopies;
    f.update(false);
    LOKA_VERIFY(toolbox_host::staticCopies == copies);
    LOKA_VERIFY(ToolboxStaticBlurInspection::phase(f.controller.staticBlur_) == ToolboxStaticBlur::READY);
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
    LOKA_VERIFY(ToolboxStaticBlurInspection::phase(f.controller.staticBlur_) == ToolboxStaticBlur::READY);
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
              painted = f.controller.staticBlur_.paint(f.window.window(), row, pass);
            }
            else
              painted = f.controller.staticBlur_.paint(f.window.window(), row, pass);
            const bool admitted = open && contains == 1 && (o != 2 || covers);
            LOKA_VERIFY(painted == admitted);
            LOKA_VERIFY(toolbox_host::staticCaptures == (admitted ? 1u : 0u));
            LOKA_VERIFY(ToolboxStaticBlurInspection::phase(f.controller.staticBlur_)
                        == (admitted ? ToolboxStaticBlur::READY : ToolboxStaticBlur::EMPTY));
            for (unsigned later = 0; later < 3; ++later)
            {
              ToolboxErasedPass::Guard guard(pass, rect);
              const bool copied = f.controller.staticBlur_.paint(f.window.window(), row, pass);
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
    LOKA_VERIFY(ToolboxStaticBlurInspection::hasPixels(f.controller.staticBlur_));
    LOKA_VERIFY(ToolboxStaticBlurInspection::phase(f.controller.staticBlur_) == ToolboxStaticBlur::READY);
    toolbox_host::staticCopySucceeds = true;
    toolbox_host::erasedRects.clear();
    f.update();
    LOKA_VERIFY(toolbox_host::staticCopies == copies + 2);
    LOKA_VERIFY(toolbox_host::staticCaptures == 1 && toolbox_host::staticReleases == 0);
    LOKA_VERIFY(toolbox_host::erasedRects.empty());
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
    f.controller.staticBlur_.reconcile(ledger.firstStaticBlur());
    ToolboxErasedPass::Guard pass(f.controller.erasedPass(), rect);
    const bool laterPainted = f.controller.staticBlur_.paint(f.window.window(), second, f.controller.erasedPass());
    LOKA_VERIFY(!laterPainted);
    LOKA_VERIFY(toolbox_host::staticCaptures == 0);
    const bool firstPainted = f.controller.staticBlur_.paint(f.window.window(), first, f.controller.erasedPass());
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
    LOKA_VERIFY(ToolboxStaticBlurInspection::phase(f.controller.staticBlur_) == ToolboxStaticBlur::READY);
  }
  void staticIneligible()
  {
    StaticFixture f;
    toolbox_host::staticEligible = false;
    f.update();
    f.window.drawForFlush();
    LOKA_VERIFY(ToolboxStaticBlurInspection::phase(f.controller.staticBlur_) == ToolboxStaticBlur::EMPTY);
    const unsigned before = toolbox_host::invalidations;
    toolbox_host::staticEligible = true;
    f.window.flushInvalidate();
    f.window.flushInvalidate();
    LOKA_VERIFY(toolbox_host::invalidations == before);
    LOKA_VERIFY(toolbox_host::staticCaptures == 0);
    LOKA_VERIFY(!f.window.hasPendingInvalidate());
    f.update();
    LOKA_VERIFY(toolbox_host::staticCaptures == 1);
    LOKA_VERIFY(ToolboxStaticBlurInspection::phase(f.controller.staticBlur_) == ToolboxStaticBlur::READY);
  }
  void staticFailure()
  {
    StaticFixture f;
    toolbox_host::staticCaptureSucceeds = false;
    f.window.requestInvalidate();
    f.window.flushInvalidate();
    LOKA_VERIFY(!f.window.hasPendingInvalidate());
    LOKA_VERIFY(ToolboxStaticBlurInspection::phase(f.controller.staticBlur_) == ToolboxStaticBlur::DISABLED);
    LOKA_VERIFY(toolbox_host::staticCaptures == 1);
    toolbox_host::staticCaptureSucceeds = true;
    const unsigned before = toolbox_host::invalidations;
    f.update(false);
    LOKA_VERIFY(ToolboxStaticBlurInspection::phase(f.controller.staticBlur_) == ToolboxStaticBlur::DISABLED);
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
    LOKA_VERIFY(ToolboxStaticBlurInspection::hasPixels(f.controller.staticBlur_) == changedRect);
    LOKA_VERIFY(ToolboxStaticBlurInspection::phase(f.controller.staticBlur_)
                == (changedRect ? ToolboxStaticBlur::READY : ToolboxStaticBlur::EMPTY));
    LOKA_VERIFY(toolbox_host::staticCaptures == (changedRect ? 2u : 1u));
    f.window.drawForFlush();
    LOKA_VERIFY(toolbox_host::staticCaptures == (changedRect ? 2u : 1u));
    LOKA_VERIFY(ToolboxStaticBlurInspection::hasPixels(f.controller.staticBlur_) == changedRect);
    if (changedRect)
    {
      LOKA_VERIFY(ToolboxStaticBlurInspection::phase(f.controller.staticBlur_) == ToolboxStaticBlur::READY);
      f.unit->height = 72;
      f.window.drawForFlush();
      LOKA_VERIFY(toolbox_host::staticReleases == 2);
      LOKA_VERIFY(toolbox_host::staticCaptures == 3);
      LOKA_VERIFY(ToolboxStaticBlurInspection::phase(f.controller.staticBlur_) == ToolboxStaticBlur::READY);
      return;
    }
    f.unit->height = 30;
    f.stack.props.unitGround = ZSTACK_UNIT_GROUND_EXP_STATIC_BLUR;
    f.controller.rootNode_ = &f.stack;
    f.ready();
    LOKA_VERIFY(toolbox_host::staticCaptures == 2);
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
      LOKA_VERIFY(controller.frostCache_.paints == 3);
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
    const Rect rect = controller.buttonControls_[0].rect;
    LOKA_VERIFY(rect.left == 15 && rect.right == 90 && rect.top == 25 && rect.bottom <= 55);
    const toolbox_host::ControlCall &draw = controller.buttonControls_[0].control->calls.back();
    LOKA_VERIFY(draw.operation == "draw" && draw.painted);
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
    equal(controller.pendingDirtyRects_[0], rect);
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
    ObservableList<String> lines;
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
    equal(controller.pendingDirtyRects_[0], bar.rect);
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
    ToolboxScenePlatformController::EditTextControlBinding binding;
    binding.ownerContext = 0;
    binding.editor = 0;
    binding.text = 0;
    binding.rect = Rect();
    binding.rect.left = 5;
    binding.rect.top = 25;
    binding.rect.right = 90;
    binding.rect.bottom = 75;
    binding.te = TENew(&binding.rect, &binding.rect);
    binding.usedThisFrame = true;
    binding.lifetimeHint = NATIVE_HINT_EAGER_RELEASE;
    controller.editControls_.add(binding);
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
    controller.editControls_[0].rect = insideAOverlappingB;
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY(toolbox_host::invalidatedRects.size() == 1);
    const Rect overlapB = {25, 15, 50, 90};
    equal(toolbox_host::invalidatedRects[0], overlapB);
    toolbox_host::invalidatedRects.clear();
    const Rect contained = {25, 15, 45, 90};
    controller.editControls_[0].rect = contained;
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY(toolbox_host::invalidatedRects.empty());
    const Rect touching = {60, 10, 80, 90};
    controller.editControls_[0].rect = touching;
    ToolboxInputDoor::idleTextEdits(controller);
    LOKA_VERIFY(toolbox_host::invalidatedRects.empty());
    controller.rootNode_ = &stack;
    const unsigned before = controller.debugStats_.totalRenderCalls;
    ToolboxInputDoor::renderDirty(controller, touching);
    LOKA_VERIFY(controller.debugStats_.totalRenderCalls == before);
    controller.rootNode_ = 0;
    toolbox_host::afterIdle = 0;
    controller.clearControls();
    // A vanished window cannot supply a safe deactivation port. Its active
    // record must retire eagerly instead of entering the EditText churn pool.
    binding.te = TENew(&binding.rect, &binding.rect);
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
    const unsigned before = controller.debugStats_.totalRenderCalls;
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
      const unsigned before = controller.debugStats_.totalRenderCalls;
      ToolboxInputDoor::renderDirty(controller, expected);
      LOKA_VERIFY(controller.debugStats_.totalRenderCalls == before + 1);
      LOKA_VERIFY(upper->renders >= 2);
      controller.rootNode_ = 0;
    }
  }
} // namespace
int main(int argc, char **argv)
{
  const char *names[] = {"StaticFirstUnit",
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
  if (std::strcmp(argv[1], "StaticNoRequests") == 0)
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
  else if (std::strcmp(argv[1], "FrostErasedPassAdmission") == 0)
    erasedFrostAdmission();
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
