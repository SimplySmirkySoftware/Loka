#include "MacRetirableContext.hpp"

#include <cassert>
#include "../MacScenePlatformController.hpp"

MacRetirableContext::MacRetirableContext(MacScenePlatformController *controller)
    : loka::app::scene::NativeNodeContext(),
      controller_(controller)
{
}

MacRetirableContext::~MacRetirableContext() {}

void MacRetirableContext::retireNativeObjects(void *&primary, void *&auxiliary)
{
  if (!primary && !auxiliary)
  {
    return;
  }
  assert(this->controller_ && "live native objects must have a controller retirement owner");
  if (!this->controller_)
  {
    return;
  }
  // Twin of MacTextDocumentViewContext::retireScroll.
  this->controller_->queueNativeRetirement(primary, auxiliary);
  primary = 0;
  auxiliary = 0;
}

void MacRetirableContext::retireNativeObject(void *&primary)
{
  void *none = 0;
  this->retireNativeObjects(primary, none);
}
