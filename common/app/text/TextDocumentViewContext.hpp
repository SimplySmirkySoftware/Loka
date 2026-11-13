#ifndef LOKA_APP_TEXT_DOCUMENT_VIEW_CONTEXT_HPP
#define LOKA_APP_TEXT_DOCUMENT_VIEW_CONTEXT_HPP
#include "app/scene/projection/NativeNodeContext.hpp"
#include "app/text/TextViewClaim.hpp"
namespace loka
{
  namespace app
  {
    class TextDocumentEditorNode;
    namespace scene
    {
      class SceneTextDocuments;
      /** View place and Scene membership. The store owns the body; terminal cleanup never publishes. */
      class TextDocumentViewContext : public NativeNodeContext, private TextViewClaim
      {
      public:
        virtual ~TextDocumentViewContext();
        /** Retained handler entry after adoption; attempts attachment only for an attached owner. */
        void readLifecycleFactOnAttach();
        /** Existing lifecycle delivery; terminal delivery removes membership before context deletion. */
        virtual void onFactChanged(NodeLifecycleFact previous, NodeLifecycleFact next);
        /** Compatible props refresh changes presentation only, never retries a refused attachment. */
        virtual void onPropsApplied();

      protected:
        TextDocumentViewContext(IPlatformController &, SceneTextDocuments &, TextDocumentEditorNode &);

      private:
        IPlatformController &controller_;
        void attachView();
        void retireView();
        friend class ::loka::app::detail::TextDocumentViewAccess;
        friend class ::loka::app::detail::TextDocumentPublisher;
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
