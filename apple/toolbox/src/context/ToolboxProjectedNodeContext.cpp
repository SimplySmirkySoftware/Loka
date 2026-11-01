#include "context/ToolboxProjectedNodeContext.hpp"

#include "ToolboxScenePlatformController.hpp"

void ToolboxProjectedNodeContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                                loka::app::scene::NodeLifecycleFact next)
{
  (void)previous;
  if (next != loka::app::scene::NODE_FACT_ATTACHED)
    this->invalidatePlacement();
  if (next != loka::app::scene::NODE_FACT_RETIRED || !this->controller_)
  {
    return;
  }
  this->retireNativeProjection();
  this->controller_->retireNodeContext(this, this->lifetimeHint());
  this->controller_ = 0;
  this->boundary_ = 0;
}

void ToolboxProjectedNodeContext::comparePlacement(const ToolboxLeafGeometry &previous, const ToolboxLeafGeometry &next)
{
  if (!this->controller_ || !this->controller_->activeViewportContent_)
    return;
  ToolboxViewportContentObservation &content = *this->controller_->activeViewportContent_;
  const ToolboxExpectedMotion &motion = content.motion();
  if (!(this->validity_.present() && motion.hasPrevious && previous.translated(motion.pixelDelta).equals(next)))
    content.noteNotTranslated();
}
