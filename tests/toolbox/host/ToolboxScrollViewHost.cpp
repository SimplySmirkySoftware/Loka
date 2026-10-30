#include "ToolboxHost.hpp"
#include "ToolboxNodeDispatch.hpp"
#include "ToolboxPlatformLayoutHandlers.hpp"
#include "ToolboxScrollViewDecisions.hpp"
#include <algorithm>
#include "app/layout/LayoutHeuristics.hpp"
#include "app/nodes/nestable/Box.hpp"

// The host substitutes dispatch/OS neighbors, but executes the production
// ScrollView body, Column handler, attributed context and viewport ledger.

#ifndef LOKA_HOST_GAP_DISPATCH
namespace
{
  class Traversal : public loka::app::scene::IPlatformLayoutTraversal
  {
    ToolboxScenePlatformController &controller_;
    short y_;
  public:
    explicit Traversal(ToolboxScenePlatformController &controller) : controller_(controller), y_(0) {}
    virtual int layoutChild(loka::app::scene::Node *node, const loka::app::scene::LayoutState &state)
    {
      loka::app::scene::LayoutState child = state;
      const short width = LayoutNode(node, child, &this->controller_, 0);
      this->y_ = child.y;
      return width;
    }
    virtual bool refuseLayoutResultY(int y)
    {
      return this->controller_.refuseNarrowingInScrollScope(y);
    }
    virtual void setLayoutResultY(short y) { this->y_ = y; }
    virtual short layoutResultY() const { return this->y_; }
  };
}
short LayoutNode(loka::app::scene::Node *node, loka::app::scene::LayoutState &state,
                 ToolboxScenePlatformController *controller, loka::app::scene::BoundaryNode *,
                 const loka::app::layout::LazyWindow *range, loka::app::layout::StackSpans *spans)
{
  if (node->asStackNode() || node->asBoxNode())
  {
    if (node->asBoxNode())
      ++controller->leafLayouts;
    loka::app::scene::PlatformLayoutHandlerRegistry registry;
    RegisterToolboxPlatformLayoutHandlers(registry);
    Traversal traversal(*controller);
    short width = 0;
    ApplyToolboxPlatformLayoutHandler(registry, *node, state, traversal, width, range, spans);
    return width;
  }
  ++controller->leafLayouts;
  if (!loka::app::layout::childTakesLayoutSeat(node))
    return 0;
  if (node->kind() == loka::app::scene::NODE_KIND_BUTTON)
  {
    state.y += 20;
    return state.width;
  }
  if (loka::app::ImageViewNode *image = node->asImageViewNode())
  {
    // Image painting is outside this fixture; R0 reads the real props above.
    if (!controller->projectLayoutState(state))
      return 0;
    state.y += image->props.height_;
    if (!controller->restoreProjectedLayoutState(state))
      return 0;
    return state.width;
  }
  if (!controller->projectLayoutState(state))
    return 0;
  loka::app::scene::IPlatformNodeHandler *handler = controller->nodeHandlerRegistry_.find(node);
  assert(handler);
  loka::app::scene::NodeContext *previous = node->getContext();
  if (!handler->ensureContext(node, controller, state))
  {
    controller->refuseScrollViewShortRange();
    return 0;
  }
  if (node->getContext() != previous) controller->requestStructurePresent();
  const short width = node->layout(controller, state);
  if (!controller->restoreProjectedLayoutState(state))
    return 0;
  return width;
}
#endif
#include "ToolboxScrollViewLayout.cpp"
#include "ToolboxControlPresentation.cpp"
#include "ToolboxStructurePresent.cpp"

#include "ToolboxDirtyReplay.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "app/scene/boundary/Boundary.hpp"
#include "app/scene/projection/CollectPaintAnswers.hpp"
namespace
{
  // Painting neighbors are outside these structure-routing pins.
  const ToolboxFullDrawReason kViewportPaintWidenReason = TOOLBOX_FULL_DRAW_PAINT_WIDENED_VIEWPORT_RENDER;
  struct ToolboxPaintAnswerSource
  {
    /** Exact damage may share a conservative, non-erasing destination only when true. */
    enum { kMergesExactDamage = 0 };
    explicit ToolboxPaintAnswerSource(ToolboxScenePlatformController::RenderStats &) {}
    bool queryPaintAnswer(loka::app::scene::Node *, loka::app::scene::NodeContext *,
        const loka::app::scene::PaintQuery &, loka::app::scene::PaintAnswer &) { return false; }
  };
  bool ContainsOnlyRectSurfacePainting(loka::app::scene::Node *, ToolboxScenePlatformController::RenderStats &)
  { return false; }
  bool CollectRectSurfaceDirtyRect(loka::app::scene::Node *, Rect &, ToolboxScenePlatformController::RenderStats &)
  { return false; }
#ifndef LOKA_HOST_ZSTACK_UNITS
  Rect BoundaryToRect(const loka::app::scene::BoundaryNode *, const Rect &fallback) { return fallback; }
#endif
}
#ifdef LOKA_HOST_ZSTACK_UNITS
#include "ToolboxBatchUpdate.cpp"
#endif
#include "ToolboxBoundaryApply.cpp"
