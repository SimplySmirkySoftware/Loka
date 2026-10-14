#ifndef LOKA_TOOLBOX_RIBBON_GROUP_EDGE_CONTEXT_HPP
#define LOKA_TOOLBOX_RIBBON_GROUP_EDGE_CONTEXT_HPP

#include "context/ToolboxProjectedNodeContext.hpp"
#include "ToolboxCompositionReplay.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"
#include <Quickdraw.h>

/** An opaque, stateless strip. Placement is supplied by the common group layout. */
class ToolboxRibbonGroupEdgeContext : public ToolboxProjectedNodeContext
{
public:
  ToolboxRibbonGroupEdgeContext(loka::app::RibbonGroupEdgeNode *node, ToolboxScenePlatformController *controller);
  void readLifecycleFactOnAttach();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);
  virtual void render(loka::app::scene::IPlatformController *controller);
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;

private:
  loka::app::RibbonGroupEdgeNode *node_;
  Rect rect_;
  Rect paintRect_;
  ToolboxCompositionReplay::Registration replay_;
};

bool RegisterToolboxRibbonGroupEdgeNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);
#endif
