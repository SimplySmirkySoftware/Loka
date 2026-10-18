#include "MacZStackContext.hpp"
#include "../MacGround.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include <cassert>
#include <new>

#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    bool declineMacStackUnitView();
  }
} // namespace loka
#endif

@interface LokaStackUnitView : NSView
@end
@implementation LokaStackUnitView
- (BOOL)isFlipped
{
  return YES;
}
- (BOOL)isOpaque
{
  return YES;
}
- (BOOL)acceptsFirstResponder
{
  return NO;
}
- (void)drawRect:(NSRect)rect
{
  loka::macos::MacPaintGround(loka::app::SURFACE_GROUND_WINDOW, rect);
}
@end

MacZStackContext::Unit::Unit(unsigned index)
    : index_(index),
      next(0),
      view_(0)
{
}

bool MacZStackContext::Unit::ensure(void *parent, const loka::macos::MacRect &seat, void *below, void *above)
{
  if (this->view_)
    return true;
#ifdef TEST_BUILD
  if (loka::testing::declineMacStackUnitView())
    return false;
#endif
  NSView *view = [[LokaStackUnitView alloc] initWithFrame:seat.r];
  if (!view)
    return false;
  this->view_ = view;
  if (below)
    [(NSView *)parent addSubview:view positioned:NSWindowBelow relativeTo:(NSView *)below];
  else
    [(NSView *)parent addSubview:view positioned:NSWindowAbove relativeTo:(NSView *)above];
  return true;
}

void MacZStackContext::Unit::place(const loka::macos::MacRect &seat)
{
  loka::macos::SetMacFrame((NSView *)this->view_, seat);
}

MacZStackContext::MacZStackContext(MacScenePlatformController *controller)
    : MacRetirableContext(controller),
      units_(0)
{
}

MacZStackContext::~MacZStackContext()
{
  while (this->units_)
  {
    Unit *unit = this->units_;
    this->units_ = unit->next;
    assert(!unit->view_ && "terminal fact must queue every unit before context reclaim");
    delete unit;
  }
}

MacZStackContext::Unit *MacZStackContext::unitFor(unsigned index, Unit **&cursor)
{
  if (*cursor && (*cursor)->serves(index))
  {
    Unit *unit = *cursor;
    cursor = &unit->next;
    return unit;
  }
  Unit *unit = new (std::nothrow) Unit(index);
  if (!unit)
    return 0;
  unit->next = *cursor;
  *cursor = unit;
  cursor = &unit->next;
  return unit;
}

void MacZStackContext::lowerNewGroundViews(void *parent, void *previousLast)
{
  Unit *first = this->units_;
  while (first && !first->view())
    first = first->next;
  if (!first)
    return;
  if (!previousLast)
  {
    assert(false && "existing units require a ground-pass subview snapshot");
    return;
  }
  NSView *host = (NSView *)parent;
  // Retirement removal cannot run during this layout pass: the safe point
  // follows layout, so the recorded view must still belong to this parent.
  if ([(NSView *)previousLast superview] != host)
  {
    assert(false && "ground-pass snapshot must remain parented until the safe point");
    return;
  }
  NSView *anchor = (NSView *)first->view();
  // Walk the new suffix backwards, preserving its order below the unit.
  NSView *view = [[host subviews] lastObject];
  while (view && view != previousLast)
  {
    for (Unit *unit = this->units_; unit; unit = unit->next)
      if (unit->view() == view)
        return; // Always-on wall: repair must never move an existing own unit.
    [host addSubview:view positioned:NSWindowBelow relativeTo:anchor];
    anchor = view;
    view = [[host subviews] lastObject];
  }
}

void MacZStackContext::onFactChanged(loka::app::scene::NodeLifecycleFact, loka::app::scene::NodeLifecycleFact next)
{
  for (Unit *unit = this->units_; unit; unit = unit->next)
  {
    NSView *view = (NSView *)unit->view_;
    [view setHidden:next != loka::app::scene::NODE_FACT_ATTACHED];
    if (next == loka::app::scene::NODE_FACT_RETIRED)
    {
      [view removeFromSuperview];
      this->retireNativeObject(unit->view_);
    }
  }
}
