#include "app/nodes/nestable/ZStack.hpp"
#include "ToolboxPaintGround.hpp"
#include "context/ToolboxPaintSupport.hpp"

/** Shared by the rail and host. Deliberate Win32/macOS twin: full seats,
    maximum child bottom, and no ZStack or viewport inside a unit. */
short ToolboxScenePlatformController::layoutZStack(loka::app::ZStackNode *stack,
                                                   loka::app::scene::LayoutState &state,
                                                   loka::app::scene::BoundaryNode *boundary)
{
  if (this->projectionParentScopes_.insideUnit())
    return 0;
  short maxY = state.y;
  short maxWidth = 0;
  unsigned index = 0;
  loka::dsl::CompositionCursor<loka::app::scene::Node> children(stack->childrenHead(), stack->childrenCount());
  for (loka::app::scene::Node *child = children.next(); child; child = children.next(), ++index)
  {
    loka::app::scene::LayoutState childState = state;
    short width = 0;
    if (!index)
      width = LayoutNode(child, childState, this, boundary);
    else
    {
      const loka::app::scene::ProjectionParentScope &parent = this->projectionParentScopes_.current();
      loka::app::scene::ProjectionParentScope frame;
      if (parent.hasShortRangeRefusal() || !parent.deriveScrolled(parent.nativeParent, 0, 0, parent.clipRect, frame))
        continue;
      loka::app::scene::ProjectionUnitScopeGuard guard(this->projectionParentScopes_, frame);
      if (!guard.isActive())
        continue;
      // Call the arm directly: the generic traversal swallows narrowing refusal.
      width = LayoutNode(child, childState, this, boundary);
      if (this->refuseNarrowingInScrollScope(childState.y)
          || this->projectionParentScopes_.current().hasShortRangeRefusal())
        continue;
      loka::app::scene::LayoutState projected = state;
      if (!this->projectLayoutState(projected))
        continue;
      const int right = static_cast<int>(projected.x) + state.width;
      const int bottom = static_cast<int>(projected.y) + childState.y - state.y;
      if (this->refuseNarrowingInScrollScope(right) || this->refuseNarrowingInScrollScope(bottom))
        continue;
      Rect rect = {projected.y, projected.x, static_cast<short>(bottom), static_cast<short>(right)};
      Rect clipped;
      if (this->intersectWithProjectionClip(rect, clipped))
      {
        // Unlike Win32's retained rectangle, a refused Toolbox unit disappears
        // for this pass. No row means no render (and no control re-registration).
        // Layout appends once per visible unit; vector OOM follows the hit ledgers.
        const UnitSeat row = {stack, index, clipped};
        this->unitLedger_.push_back(row);
      }
      // Clipping removes pixels, not a successfully measured child's extent.
      // An offscreen unit must still contribute to the viewport scroll range.
    }
    if (childState.y > maxY)
      maxY = childState.y;
    if (width > maxWidth)
      maxWidth = width;
  }
  state.y = maxY;
  return maxWidth;
}

void ToolboxScenePlatformController::renderZStack(loka::app::ZStackNode *stack)
{
  if (this->projectionParentScopes_.insideUnit())
    return;
  unsigned index = 0;
  // Scan this controller's rows once per stack, advancing with child order.
  // Each admitted unit costs one erase and one two-region intersecting clip.
  size_t rowIndex = 0;
  loka::dsl::CompositionCursor<loka::app::scene::Node> children(stack->childrenHead(), stack->childrenCount());
  for (loka::app::scene::Node *child = children.next(); child; child = children.next(), ++index)
  {
    if (!index)
    {
      RenderNode(child, this);
      continue;
    }
    while (rowIndex < this->unitLedger_.size()
           && (this->unitLedger_[rowIndex].stack != stack || this->unitLedger_[rowIndex].index < index))
      ++rowIndex;
    if (rowIndex == this->unitLedger_.size() || this->unitLedger_[rowIndex].index != index)
      continue;
    const UnitSeat &row = this->unitLedger_[rowIndex];
    const loka::app::scene::ProjectionParentScope &parent = this->projectionParentScopes_.current();
    loka::app::scene::ProjectionParentScope frame;
    if (!parent.deriveScrolled(parent.nativeParent, 0, 0, parent.clipRect, frame))
      continue;
    loka::app::scene::ProjectionUnitScopeGuard guard(this->projectionParentScopes_, frame);
    if (!guard.isActive())
      continue;
    loka::toolbox::ToolboxPaintGround(loka::app::SURFACE_GROUND_WINDOW, row.rect);
    ToolboxPaintClip clip(row.rect, ToolboxPaintClip::INTERSECT);
    if (clip.isActive())
      RenderNode(child, this);
  }
}

/** Native tracking/caret paints can flash until update. O(units) on this
    controller per native paint; only an overlapping unit requests repair. */
void ToolboxScenePlatformController::invalidateUnitOverlap(const Rect &rect)
{
  for (size_t i = 0; i < this->unitLedger_.size(); ++i)
  {
    const Rect &unit = this->unitLedger_[i].rect;
    if (rect.left < unit.right && rect.right > unit.left && rect.top < unit.bottom && rect.bottom > unit.top)
    {
      this->window_->requestInvalidateRect(rect);
      return;
    }
  }
}
