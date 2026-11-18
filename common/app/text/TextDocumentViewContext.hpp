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
        /** Rail-only borrowed read: the claim link is attachment authority, never the native place.
            O(1), no callbacks; the returned row is not retained. */
        TextStoreRow *attachedStore() const
        {
          return this->link_.remote();
        }

        /** Terminal cleanup only. Derived destructors may call this nonvirtual, idempotent door
            while their resources and controller are live. Detaches without kernel reentry and
            leaves Scene publication, independent of destructor-time onFactChanged delivery. */
        void retireView();

      private:
        IPlatformController &controller_;
        void attachView();
        friend class ::loka::app::detail::TextDocumentViewAccess;
        friend class ::loka::app::detail::TextDocumentPublisher;
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
