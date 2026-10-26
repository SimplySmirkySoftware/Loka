#include "context/ToolboxClipBoxContext.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include <new>

namespace
{
  template <typename NodeT>
  class ToolboxClipBoxNodeHandler
      : public loka::app::scene::RetainedNodeHandler<ToolboxClipBoxNodeHandler<NodeT>, NodeT, ToolboxClipBoxContext>
  {
  public:
    static NodeT *cast(loka::app::scene::Node *node)
    {
      return node && node->nodeTypeKey() == loka::app::scene::NodeTypeToken<NodeT>() ? static_cast<NodeT *>(node) : 0;
    }
    static ToolboxClipBoxContext *
    create(NodeT *, loka::app::scene::IPlatformController *, const loka::app::scene::LayoutState &)
    {
      return new (std::nothrow) ToolboxClipBoxContext();
    }
  };
  ToolboxClipBoxNodeHandler<loka::app::BoxNode> gToolboxBoxHandler;
  ToolboxClipBoxNodeHandler<loka::app::CardNode> gToolboxCardHandler;
} // namespace

bool IsToolboxClipBoxType(const void *key)
{
  return key == loka::app::scene::NodeTypeToken<loka::app::BoxNode>()
         || key == loka::app::scene::NodeTypeToken<loka::app::CardNode>();
}

bool RegisterToolboxClipBoxNodeHandlers(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  const bool box = registry.registerHandler(&gToolboxBoxHandler);
  return registry.registerHandler(&gToolboxCardHandler) && box;
}

void ToolboxScenePlatformController::ensureClipBoxContext(loka::app::scene::Node *node,
                                                          const loka::app::scene::LayoutState &state)
{
  loka::app::scene::IPlatformNodeHandler *handler = this->nodeHandlerRegistry_.find(node);
  if (handler)
    handler->ensureContext(node, this, state);
}
