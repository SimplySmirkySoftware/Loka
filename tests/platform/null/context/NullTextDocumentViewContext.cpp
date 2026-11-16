#include "platform/null/context/NullTextDocumentViewContext.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
using namespace loka::app;
namespace
{
  class Handler : public scene::RetainedNodeHandler<Handler, TextDocumentEditorNode, NullTextDocumentViewContext>
  {
  public:
    static TextDocumentEditorNode *cast(scene::Node *node)
    {
      return node && node->nodeTypeKey() == scene::NodeTypeToken<TextDocumentEditorNode>()
                 ? static_cast<TextDocumentEditorNode *>(node)
                 : 0;
    }
    static NullTextDocumentViewContext *
    create(TextDocumentEditorNode *node, scene::IPlatformController *controller, const scene::LayoutState &)
    {
      return static_cast<NullScenePlatformController *>(controller)->createTextViewContext(*node);
    }
  };
  Handler handler;
} // namespace
void RegisterNullTextDocumentViewNodeHandler(NullScenePlatformController &controller)
{
  controller.registerNodeHandler(&handler);
}
