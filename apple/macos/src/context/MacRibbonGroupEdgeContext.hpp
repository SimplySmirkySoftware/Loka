#ifndef LOKA_MAC_RIBBON_GROUP_EDGE_CONTEXT_HPP
#define LOKA_MAC_RIBBON_GROUP_EDGE_CONTEXT_HPP

#include "MacRetirableContext.hpp"

namespace loka
{
  namespace app
  {
    class RibbonGroupEdgeNode;
    namespace scene
    {
      class PlatformNodeHandlerRegistry;
    }
  } // namespace app
} // namespace loka

/** Owns one edge view through the same fact and retirement doors as Cell. */
class MacRibbonGroupEdgeContext : public MacRetirableContext
{
public:
  MacRibbonGroupEdgeContext(MacScenePlatformController *controller,
                            void *parentView,
                            int x,
                            int y,
                            int width,
                            int height,
                            loka::app::RibbonGroupEdgeNode *node);
  virtual ~MacRibbonGroupEdgeContext();
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);
  void readLifecycleFactOnAttach();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);
  void relayout(int x, int y, int width, int height);

private:
  loka::app::RibbonGroupEdgeNode *node_;
  void *view_;
};

void RegisterMacRibbonGroupEdgeNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif
