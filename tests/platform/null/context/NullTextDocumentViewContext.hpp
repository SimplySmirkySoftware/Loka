#ifndef LOKA_TESTS_NULL_TEXT_DOCUMENT_VIEW_CONTEXT_HPP
#define LOKA_TESTS_NULL_TEXT_DOCUMENT_VIEW_CONTEXT_HPP
#include "app/text/TextDocumentViewContext.hpp"
#include "app/nodes/controls/TextDocumentEditor.hpp"
class NullScenePlatformController;
/** Null place has no native body; its claim still follows the common lifecycle. */
class NullTextDocumentViewContext : public loka::app::scene::TextDocumentViewContext
{
public:
  NullTextDocumentViewContext(loka::app::scene::IPlatformController &controller,
                              loka::app::scene::SceneTextDocuments &documents,
                              loka::app::TextDocumentEditorNode &node)
      : TextDocumentViewContext(controller, documents, node)
  {
  }
  virtual short layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
  {
    state.y = static_cast<short>(state.y + 40);
    return state.y;
  }
};
void RegisterNullTextDocumentViewNodeHandler(NullScenePlatformController &);
#endif
