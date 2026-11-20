#ifndef LOKA_MAC_TEXT_DOCUMENT_VIEW_CONTEXT_HPP
#define LOKA_MAC_TEXT_DOCUMENT_VIEW_CONTEXT_HPP
#include "app/text/TextDocumentViewContext.hpp"
#include "app/nodes/controls/TextDocumentEditor.hpp"
@class NSScrollView, NSView;
class MacScenePlatformController;
class MacTextStore;
namespace loka
{
  namespace app
  {
    namespace scene
    {
      class PlatformNodeHandlerRegistry;
    }
  } // namespace app
} // namespace loka
/** Owns the scroll projection. The linked document row owns its text view. */
class MacTextDocumentViewContext : public loka::app::scene::TextDocumentViewContext
{
public:
  MacTextDocumentViewContext(MacScenePlatformController &,
                             NSView *,
                             loka::app::scene::SceneTextDocuments &,
                             loka::app::TextDocumentEditorNode &);
  virtual ~MacTextDocumentViewContext();
  NSScrollView *scroll() const
  {
    return this->scroll_;
  }
  MacTextStore *store() const;
  void readLifecycleFactOnAttach();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact, loka::app::scene::NodeLifecycleFact);
  virtual short layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &);

private:
  void installScroll();
  void retireScroll();
  MacScenePlatformController &controller_;
  NSView *const parent_;
  NSScrollView *scroll_;
};
void RegisterMacTextDocumentViewNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &);
#endif
