#include "MacClipBoxContext.hpp"
#include "LokaClipHostView.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/controls/Card.hpp"
#include <cassert>

// Deliberate twin of IsToolboxClipBoxType in apple/toolbox/src/context/ToolboxClipBoxContext.cpp.
bool IsMacClipBoxType(const void *key)
{
  return key == loka::app::scene::NodeTypeToken<loka::app::BoxNode>()
         || key == loka::app::scene::NodeTypeToken<loka::app::CardNode>();
}

MacClipBoxContext::MacClipBoxContext(MacScenePlatformController *controller)
    : MacRetirableContext(controller),
      view_(0)
{
}
MacClipBoxContext::~MacClipBoxContext()
{
  assert(!this->view_ && "terminal fact must queue the host before context reclaim");
}
bool MacClipBoxContext::ensureHost(void *parent, const loka::macos::MacRect &seat)
{
  if (this->view_)
    return true;
#ifdef TEST_BUILD
  if (loka::testing::declineMacClipHostView())
    return false;
#endif
  LokaClipHostView *view = [[LokaClipHostView alloc] initWithFrame:seat.r];
  if (!view)
    return false;
  this->view_ = view;
  [(NSView *)parent addSubview:view];
  this->readLifecycleFactOnAttach();
  return true;
}
void MacClipBoxContext::readLifecycleFactOnAttach()
{
  if (this->owner())
    this->onFactChanged(this->owner()->lifecycleFact(), this->owner()->lifecycleFact());
}
void MacClipBoxContext::onFactChanged(loka::app::scene::NodeLifecycleFact, loka::app::scene::NodeLifecycleFact next)
{
  if (!this->view_)
    return;
  NSView *view = (NSView *)this->view_;
  [view setHidden:next != loka::app::scene::NODE_FACT_ATTACHED];
  if (next == loka::app::scene::NODE_FACT_RETIRED)
  {
    [view removeFromSuperview];
    this->retireNativeObject(this->view_);
  }
}
