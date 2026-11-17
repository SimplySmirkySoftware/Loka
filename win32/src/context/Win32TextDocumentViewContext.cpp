#include "Win32TextDocumentViewContext.hpp"
#include "Win32FocusParticipant.hpp"
#include "../Win32ScenePlatformController.hpp"
#include "../text/Win32TextStore.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
using namespace loka::app;
namespace
{
  class Handler : public scene::RetainedNodeHandler<Handler, TextDocumentEditorNode, Win32TextDocumentViewContext>
  {
  public:
    static TextDocumentEditorNode *cast(scene::Node *node)
    {
      return node && node->nodeTypeKey() == scene::NodeTypeToken<TextDocumentEditorNode>()
                 ? static_cast<TextDocumentEditorNode *>(node)
                 : 0;
    }
    static Win32TextDocumentViewContext *
    create(TextDocumentEditorNode *node, scene::IPlatformController *controller, const scene::LayoutState &)
    {
      Win32ScenePlatformController *win32 = static_cast<Win32ScenePlatformController *>(controller);
      return win32->createTextViewContext(*node, win32->projectionParentHwnd());
    }
    static void refresh(Win32TextDocumentViewContext *context, const scene::LayoutState &state)
    {
      context->relayout(state);
    }
  };
  Handler handler;
} // namespace
Win32TextDocumentViewContext::Win32TextDocumentViewContext(Win32ScenePlatformController &controller,
                                                           scene::SceneTextDocuments &documents,
                                                           TextDocumentEditorNode &node,
                                                           HWND parent)
    : TextDocumentViewContext(controller, documents, node),
      viewWindow_(parent)
{
}
HWND Win32TextDocumentViewContext::hwnd() const
{
  scene::TextStoreRow *store = this->attachedStore();
  return store ? static_cast<Win32TextStore *>(store)->edit() : 0;
}
Win32TextDocumentViewContext *Win32TextDocumentViewContext::fromWindow(HWND hwnd)
{
  scene::NodeContext *context = Win32FocusParticipant::read(hwnd);
  scene::Node *node = context ? context->owner() : 0;
  return node && node->nodeTypeKey() == scene::NodeTypeToken<TextDocumentEditorNode>()
             ? static_cast<Win32TextDocumentViewContext *>(context)
             : 0;
}
short Win32TextDocumentViewContext::layout(scene::IPlatformController *, scene::LayoutState &state)
{
  this->relayout(state);
  return static_cast<short>(state.y + state.height);
}
void Win32TextDocumentViewContext::relayout(const scene::LayoutState &state)
{
  scene::TextStoreRow *store = this->attachedStore();
  if (!store)
    return;
  Win32ScenePlatformController &controller = static_cast<Win32ScenePlatformController &>(store->controller());
  const HWND edit = static_cast<Win32TextStore *>(store)->edit();
  const loka::win32::NativeRect frame =
      controller.displayScale().projectFrame(loka::core::Frame(state.x, state.y, state.width, state.height));
  if (!(GetWindowLongW(edit, GWL_STYLE) & WS_VISIBLE))
    controller.showNativeWindowAt(edit, frame);
  else
    controller.positionNativeWindow(edit, frame);
}
scene::PaintAnswer Win32TextDocumentViewContext::queryPaintDamage(const scene::PaintQuery &query) const
{
  if (!this->attachedStore())
    return scene::PaintAnswer::refused(scene::PAINT_REFUSED_NO_CONTEXT);
  const scene::PaintDamage empty = {query.scope, 0, 0, 0, 0, scene::PAINT_COVERAGE_PAINT_ONLY};
  return scene::PaintAnswer::exact(empty);
}
void RegisterWin32TextDocumentViewNodeHandler(scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&handler);
}
