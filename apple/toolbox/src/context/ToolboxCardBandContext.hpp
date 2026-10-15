#ifndef LOKA_TOOLBOX_CARD_BAND_CONTEXT_HPP
#define LOKA_TOOLBOX_CARD_BAND_CONTEXT_HPP

#include "context/ToolboxProjectedNodeContext.hpp"
#include "ToolboxCompositionReplay.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"
#include <Quickdraw.h>

/** A transparent ink strip. Placement is supplied by the common framed layout. */
class ToolboxCardBandContext : public ToolboxProjectedNodeContext
{
public:
  ToolboxCardBandContext(loka::app::CardBandNode *node, ToolboxScenePlatformController *controller);
  void readLifecycleFactOnAttach();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);
  virtual void render(loka::app::scene::IPlatformController *controller);
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;

private:
  loka::app::CardBandNode *node_;
  Rect rect_;
  Rect paintRect_;
  ToolboxCompositionReplay::Registration replay_;
};

bool RegisterToolboxCardBandNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);
#endif
