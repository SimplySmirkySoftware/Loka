#include "ToolboxScrollViewVisit.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"
#ifndef LOKA_TEST_TOOLBOX_BOX_LAYOUT_HOST_HPP
#define LOKA_TEST_TOOLBOX_BOX_LAYOUT_HOST_HPP

// Compile production dispatch with inert non-Box platform neighbors.
#define LOKA_TOOLBOX_SCENE_PLATFORM_CONTROLLER_HPP
#ifndef LOKA_HOST_RECT_SURFACE
#define LOKA_TOOLBOX_RECT_SURFACE_CONTEXT_HPP
#endif
#include "app/scene/projection/PlatformController.hpp"
#include "app/scene/projection/PlatformLayoutHandler.hpp"
#include "app/scene/projection/ProjectionParentScope.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "support/TestVerify.hpp"
#include <climits>
#include "Quickdraw.h"

class ToolboxScenePlatformController : public loka::app::scene::IPlatformController
{
public:
  // Deterministic answer exercises the production traversal through the registered layout handlers.
  bool queryNaturalWidth(loka::app::scene::Node *, short &width) const { width = 37; return true; }
  /** One registry attempt per flagged Box/Card placement, before children. */
  void ensureClipBoxContext(loka::app::scene::Node *node, const loka::app::scene::LayoutState &state);
  loka::app::scene::PlatformNodeHandlerRegistry nodeHandlerRegistry_;
  loka::app::scene::PlatformLayoutHandlerRegistry registry;
  loka::app::scene::PlatformLayoutHandlerRegistry *layoutHandlerRegistry() { return &this->registry; }
  loka::app::scene::ProjectionParentScopeStack projectionParentScopes_;
  // Mirror ToolboxScenePlatformController::refuseNarrowingInScrollScope;
  // dispatch and Box layout below are the unmodified production bodies.
  bool refuseNarrowingInScrollScope(int resultY)
  {
    if (this->projectionParentScopes_.activeDepth() == 0) return false;
    loka::app::scene::ProjectionParentScope &scope = this->projectionParentScopes_.current();
    if (scope.hasShortRangeRefusal()) return true;
    if (resultY >= SHRT_MIN && resultY <= SHRT_MAX) return false;
    scope.markShortRangeRefused();
    return true;
  }
  loka::app::scene::BoundaryNode *activeLayoutBoundary() { return 0; }
  void setActiveLayoutBoundary(loka::app::scene::BoundaryNode *) {}
  bool restoreProjectedLayoutState(loka::app::scene::LayoutState &) { return true; }
  bool projectLayoutState(loka::app::scene::LayoutState &) { return true; }
#ifdef LOKA_HOST_RECT_SURFACE
  unsigned extentRecords;
  ToolboxScenePlatformController() : extentRecords(0) {}
  void recordRectSurfaceExtent(loka::app::RectSurfaceNode *, const loka::core::Frame &) { ++this->extentRecords; }
  void cancelRectSurfaceExtent(loka::app::RectSurfaceNode *) {}
  void requestStructurePresent() {}
  // RectSurface owns no controller-ledger handles; Node deletes its context.
  void retireNodeContext(loka::app::scene::NodeContext *, loka::app::scene::NativeLifetimeHint) {}

#else
  void recordRectSurfaceExtent(loka::app::RectSurfaceNode *, const loka::core::Frame &) { LOKA_VERIFY(false); }
#endif
  bool intersectWithProjectionClip(const Rect &rect, Rect &out) const
  {
    out = rect;
    if (this->projectionParentScopes_.activeDepth())
    {
      const loka::core::Frame &clip = this->projectionParentScopes_.current().clipRect;
      const Rect bounds = {static_cast<short>(clip.y),
                           static_cast<short>(clip.x),
                           static_cast<short>(clip.y + clip.height),
                           static_cast<short>(clip.x + clip.width)};
      out.left = rect.left > bounds.left ? rect.left : bounds.left;
      out.top = rect.top > bounds.top ? rect.top : bounds.top;
      out.right = rect.right < bounds.right ? rect.right : bounds.right;
      out.bottom = rect.bottom < bounds.bottom ? rect.bottom : bounds.bottom;
    }
    return out.left < out.right && out.top < out.bottom;
  }
  short layoutZStack(loka::app::ZStackNode *, loka::app::scene::LayoutState &, loka::app::scene::BoundaryNode *)
  {
    LOKA_VERIFY(false);
    return 0;
  }
  void renderClipBox(loka::app::scene::Node *)
  {
    LOKA_VERIFY(false);
  }
  void renderZStack(loka::app::ZStackNode *)
  {
    LOKA_VERIFY(false);
  }
  ToolboxScrollViewLayoutResult
  layoutScrollView(loka::app::ScrollViewNode *, loka::app::scene::LayoutState &, loka::app::scene::BoundaryNode *)
  {
    LOKA_VERIFY(false);
    return ToolboxScrollViewLayoutResult();
  }
  bool
  applyScrollViewRequest(loka::app::ScrollViewNode &, const ToolboxScrollViewLayoutResult &, const loka::core::Frame &)
  {
    LOKA_VERIFY(false);
    return false;
  }
  void renderScrollView(loka::app::ScrollViewNode *) { LOKA_VERIFY(false); }
  virtual void onChange(loka::app::scene::Node *, loka::app::scene::NodeDirtyFlags, bool) {}
  virtual void synchronize() {}
  virtual bool hasPendingSync() const { return false; }
  virtual void destroy() {}
};
#ifndef LOKA_HOST_RECT_SURFACE
class ToolboxRectSurfaceContext : public loka::app::scene::NodeContext
{
public:
  void setBoundary(loka::app::scene::BoundaryNode *) { LOKA_VERIFY(false); }
};
inline void EnsureToolboxRectSurfaceContext(loka::app::RectSurfaceNode *, ToolboxScenePlatformController *)
{ LOKA_VERIFY(false); }
#endif

#endif
