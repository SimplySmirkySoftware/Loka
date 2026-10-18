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
  // Paint the unit's own bounds, as LokaRectSurfaceView does: on a layer-backed
  // window (Tahoe) the dirty rect handed to an opaque view can span the whole
  // root, and filling it would paint the window ground over the ground child.
  (void)rect;
  loka::macos::MacPaintGround(loka::app::SURFACE_GROUND_WINDOW, [self bounds]);
}
@end

// Runtime declarations keep pre-10.10 SDKs independent of the effect type.
@interface NSView (LokaStackVisualEffect)
- (void)setBlendingMode:(NSInteger)mode;
- (void)setMaterial:(NSInteger)material;
- (void)setState:(NSInteger)state;
- (void)setWantsLayer:(BOOL)value;
@end

@interface LokaStackEffectUnitView : LokaStackUnitView
@end
@implementation LokaStackEffectUnitView
- (BOOL)isOpaque
{
  return NO;
}
- (void)drawRect:(NSRect)rect
{
  (void)rect; // The bottom effect subview supplies the ground.
}
@end

namespace
{
  /** Unit admission calls once per missing view; constant work, no row walk.
      Returns a retained view, whose subviews own the native effect. */
  NSView *CreateBlurUnit(NSView *parent, NSRect frame)
  {
    Class effectClass = NSClassFromString(@"NSVisualEffectView");
    if (!effectClass)
      return [[LokaStackUnitView alloc] initWithFrame:frame];
    NSView *view = [[LokaStackEffectUnitView alloc] initWithFrame:frame];
    if (!view)
      return nil;
    NSView *effect = [[effectClass alloc] initWithFrame:[view bounds]];
    if (!effect)
    {
      [view release];
      return nil;
    }
    // 10.13 raises an exception for HUD (13); select by running AppKit.
    if ([effect respondsToSelector:@selector(setMaterial:)])
      [effect setMaterial:NSAppKitVersionNumber >= 1671 ? 13 : 3];
    if ([effect respondsToSelector:@selector(setBlendingMode:)])
      [effect setBlendingMode:1];
    if ([effect respondsToSelector:@selector(setState:)])
      [effect setState:0];
    [effect setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    // Old SDK builds do not implicitly layer-back within-window blending.
    if ([parent respondsToSelector:@selector(setWantsLayer:)])
      [parent setWantsLayer:YES];
    if ([view respondsToSelector:@selector(setWantsLayer:)])
      [view setWantsLayer:YES];
    if ([effect respondsToSelector:@selector(setWantsLayer:)])
      [effect setWantsLayer:YES];
    [view addSubview:effect positioned:NSWindowBelow relativeTo:nil];
    [effect release];
    return view;
  }
} // namespace

MacZStackContext::Unit::Unit(unsigned index)
    : index_(index),
      next(0),
      view_(0)
{
}

bool MacZStackContext::Unit::ensure(
    void *parent, const loka::macos::MacRect &seat, void *below, void *above, loka::app::ZStackUnitGround ground)
{
  if (this->view_)
    return true;
#ifdef TEST_BUILD
  if (loka::testing::declineMacStackUnitView())
    return false;
#endif
  // Only the first unit (second child) may blur; visibility and height never
  // promote a later unit from its ordinary window ground.
  NSView *view = ground == loka::app::ZSTACK_UNIT_GROUND_EXP_BLUR && this->index_ == 1
                     ? CreateBlurUnit((NSView *)parent, seat.r)
                     : [[LokaStackUnitView alloc] initWithFrame:seat.r];
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

MacZStackContext::MacZStackContext(MacScenePlatformController *controller, loka::app::ZStackUnitGround ground)
    : MacRetirableContext(controller),
      ground_(ground),
      units_(0)
{
}

bool MacZStackContext::ensureUnit(Unit &unit, void *parent, const loka::macos::MacRect &seat, void *below, void *above)
{
  return unit.ensure(parent, seat, below, above, this->ground_);
}

void MacZStackContext::onPropsApplied()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  const loka::app::ZStackNode *stack = this->owner() ? this->owner()->asZStackNode() : 0;
  assert(stack && stack->props.unitGround == this->ground_
         && "ZStack unitGround is chosen at first projection; switch grounds through a seat (Show/Match arm or Keyed) "
            "for a new ZStack identity");
#endif
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
  NSArray *subviews = [host subviews];
  const NSUInteger index = [subviews indexOfObject:(NSView *)previousLast];
  if (index == NSNotFound)
  {
    assert(false && "ground-pass snapshot must remain parented until the safe point");
    return;
  }
  const NSUInteger count = [subviews count] - 1 - index;
  NSView *anchor = (NSView *)first->view();
  // Units are inserted at creation and previousLast is at or above unit 0,
  // so no own unit can occur in this suffix. Its fixed size bounds repair.
  for (NSUInteger moved = 0; moved < count; ++moved)
  {
    NSView *view = [[host subviews] lastObject];
    for (Unit *unit = this->units_; unit; unit = unit->next)
      if (unit->view() == view)
      {
        assert(false && "ground repair must not move an own unit");
        return;
      }
    [host addSubview:view positioned:NSWindowBelow relativeTo:anchor];
    anchor = view;
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
