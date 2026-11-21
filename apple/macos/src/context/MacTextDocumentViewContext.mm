#include "MacTextDocumentViewContext.hpp"
#include "../MacScenePlatformController.hpp"
#include "../MacObjCCompat.hpp"
#include "../platform/MacNativeGeometry.hpp"
#include "../text/MacTextStore.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
using namespace loka::app;
namespace
{
  class Handler : public scene::RetainedNodeHandler<Handler, TextDocumentEditorNode, MacTextDocumentViewContext>
  {
  public:
    static TextDocumentEditorNode *cast(scene::Node *node)
    {
      return node && node->nodeTypeKey() == scene::NodeTypeToken<TextDocumentEditorNode>()
                 ? static_cast<TextDocumentEditorNode *>(node)
                 : 0;
    }
    static MacTextDocumentViewContext *
    create(TextDocumentEditorNode *node, scene::IPlatformController *controller, const scene::LayoutState &)
    {
      MacScenePlatformController *mac = static_cast<MacScenePlatformController *>(controller);
      return mac->createTextViewContext(*node, mac->projectionParentView());
    }
  };
  Handler handler;
} // namespace
void RegisterMacTextDocumentViewNodeHandler(scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&handler);
}
MacTextDocumentViewContext::MacTextDocumentViewContext(MacScenePlatformController &controller,
                                                       NSView *parent,
                                                       scene::SceneTextDocuments &documents,
                                                       TextDocumentEditorNode &node)
    : TextDocumentViewContext(controller, documents, node),
      controller_(controller),
      parent_(parent),
      scroll_([[NSScrollView alloc] initWithFrame:NSZeroRect])
{
  [this->scroll_ setHasVerticalScroller:YES];
}
MacTextStore *MacTextDocumentViewContext::store() const
{
  return static_cast<MacTextStore *>(this->attachedStore());
}
void MacTextDocumentViewContext::installScroll()
{
  if (![this->scroll_ superview])
    [this->parent_ addSubview:this->scroll_];
  [this->scroll_ setHidden:NO];
}
void MacTextDocumentViewContext::readLifecycleFactOnAttach()
{
  if (this->owner()->lifecycleFact() == scene::NODE_FACT_ATTACHED)
    this->installScroll();
  TextDocumentViewContext::readLifecycleFactOnAttach();
}
void MacTextDocumentViewContext::onFactChanged(scene::NodeLifecycleFact previous, scene::NodeLifecycleFact next)
{
  if (next == scene::NODE_FACT_ATTACHED)
    this->installScroll();
  TextDocumentViewContext::onFactChanged(previous, next);
  if (next == scene::NODE_FACT_RETIRED)
    this->retireScroll();
  else if (next != scene::NODE_FACT_ATTACHED)
    [this->scroll_ setHidden:YES];
}
void MacTextDocumentViewContext::retireScroll()
{
  if (!this->scroll_)
    return;
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(!this->attachedStore());
#endif
  [this->scroll_ removeFromSuperview];
  // Twin of MacRetirableContext::retireNativeObjects for the document context base.
  this->controller_.queueNativeRetirement(this->scroll_, 0);
  this->scroll_ = nil;
}
MacTextDocumentViewContext::~MacTextDocumentViewContext()
{
  this->retireView();
  this->retireScroll();
}
short MacTextDocumentViewContext::layout(scene::IPlatformController *, scene::LayoutState &state)
{
  if (state.height <= 0)
    state.height = 80; // Same unsized multiline projection as MacTextEditorContext.
  loka::macos::SetMacFrame(
      this->scroll_,
      this->controller_.projection().projectFrame(loka::core::Frame(state.x, state.y, state.width, state.height)));
  if (this->store())
    [this->store()->view() setMinSize:[this->scroll_ contentSize]];
  return static_cast<short>(state.y + state.height);
}
