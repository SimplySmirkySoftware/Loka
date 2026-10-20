#include "app/nodes/nestable/ZStack.hpp"
#include "app/layout/ZStackLayout.hpp"
#include "ToolboxPaintGround.hpp"
#include "context/ToolboxPaintSupport.hpp"

/** Shared by the rail and host. Deliberate Win32/macOS twin: full seats,
    maximum child bottom, and no ZStack or viewport inside a unit. Band extent
    uses common/app/layout/ZStackLayout.hpp, like the native-frame twins. */
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
      const int bandBottom =
          loka::app::layout::zStackUnitBottom(stack->props.unitGround, index, state.y, childState.y, maxY);
      const int bottom = static_cast<int>(projected.y) + bandBottom - state.y;
      if (this->refuseNarrowingInScrollScope(right) || this->refuseNarrowingInScrollScope(bottom))
        continue;
      Rect rect = {projected.y, projected.x, static_cast<short>(bottom), static_cast<short>(right)};
      Rect clipped;
      if (this->intersectWithProjectionClip(rect, clipped))
      {
        // Unlike Win32's retained rectangle, a refused Toolbox unit disappears
        // for this pass. No row means no render (and no control re-registration).
        // Layout appends once per visible unit; vector OOM follows the hit ledgers.
        const ToolboxUnitLedger::UnitSeat row = {stack, index, clipped, rect};
        this->unitLedger_.push(row);
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
  // One forward scan per stack; each unit erases and intersects its clip.
  ToolboxUnitLedger::Mark cursor = 0;
  loka::dsl::CompositionCursor<loka::app::scene::Node> children(stack->childrenHead(), stack->childrenCount());
  for (loka::app::scene::Node *child = children.next(); child; child = children.next(), ++index)
  {
    if (!index)
    {
      RenderNode(child, this);
      continue;
    }
    const ToolboxUnitLedger::UnitSeat *seat = this->unitLedger_.find(stack, index, cursor);
    if (!seat)
      continue;
    const ToolboxUnitLedger::UnitSeat &row = *seat;
    const loka::app::scene::ProjectionParentScope &parent = this->projectionParentScopes_.current();
    loka::app::scene::ProjectionParentScope frame;
    if (!parent.deriveScrolled(parent.nativeParent, 0, 0, parent.clipRect, frame))
      continue;
    loka::app::scene::ProjectionUnitScopeGuard guard(this->projectionParentScopes_, frame);
    if (!guard.isActive())
      continue;
    // Milk requires an erased walk. Static blur keeps ground until capture;
    // a READY snapshot can paint during any walk without reading the backdrop.
    bool painted = false;
    if (index == 1)
    {
      switch (stack->props.unitGround)
      {
      case loka::app::ZSTACK_UNIT_GROUND_WINDOW:
        break;
      case loka::app::ZSTACK_UNIT_GROUND_EXP_BLUR:
        painted = this->erasedPass_.isOpen() && this->frostCache_.paint(this->window_->window(), row.rect, row.unit);
        break;
      case loka::app::ZSTACK_UNIT_GROUND_EXP_STATIC_BLUR:
        painted = this->staticBlur_.paint(this->window_->window(), row, this->erasedPass_);
        break;
      }
    }
    if (!painted)
      loka::toolbox::ToolboxPaintGround(loka::app::SURFACE_GROUND_WINDOW, row.rect);
    this->pressLedger_.append(PressRow::UNIT, cursor);
    ToolboxPaintClip clip(row.rect, ToolboxPaintClip::INTERSECT);
    if (clip.isActive())
      RenderNode(child, this);
  }
}

/** O(units) per native paint; repair each partial overlap. Full containment
    skips only that row, never partial overlaps with other rows. It deliberately
    has no repair for the containing row: own controls must fit inside their unit, and
    fully covered ground/earlier-unit controls are outside the v1 contract.
    Their selection highlight, TEActivate/TEDeactivate paint, and TrackControl
    hilite stay on screen until an unrelated repaint presents the unit area.
    The press ledger shields covered ground independently of this paint repair. */
void ToolboxScenePlatformController::invalidateUnitOverlap(const Rect &rect)
{
  if (!this->window_)
    return;
  ToolboxUnitLedger::Mark cursor = 0;
  Rect overlap;
  while (this->unitLedger_.nextOverlap(rect, cursor, overlap))
  {
    // An intersection equal to the control rect means this row contains it.
    if (EqualRect(&rect, &overlap))
      continue;
    this->window_->requestInvalidateRect(overlap);
  }
}

/** Window flush completion, once even for an empty flush; O(1), no row walk. */
void ToolboxScenePlatformController::completedInvalidateFlush()
{
  const Rect *request = this->staticBlur_.completedFlush();
  if (request && this->window_)
    this->window_->requestInvalidateRect(*request);
}
