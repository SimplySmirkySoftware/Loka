#ifndef LOKA_APP_TEXT_DOCUMENT_REGISTRATION_HPP
#define LOKA_APP_TEXT_DOCUMENT_REGISTRATION_HPP
#include "app/scene/node/ComposableNode.hpp"
#include "app/text/TextStore.hpp"
#include "app/scene/SceneTextDocuments.hpp"
#include "app/scene/MemberList.hpp"
namespace loka
{
  namespace app
  {
    class TextReader;
    namespace scene
    {
      /** Only initial projection or root refusal can finish an awaited declaration. */
      class TextStorePhase
      {
      public:
        enum Value
        {
          STORE_AWAITED,
          STORE_LINKED,
          STORE_REFUSED
        };
        TextStorePhase()
            : value_(STORE_AWAITED)
        {
        }
        Value value() const
        {
          return this->value_;
        }
        void link()
        {
          if (this->value_ == STORE_AWAITED)
            this->value_ = STORE_LINKED;
        }
        void refuse()
        {
          if (this->value_ == STORE_AWAITED)
            this->value_ = STORE_REFUSED;
        }

      private:
        Value value_;
      };
      /** Participant-owned declaration and Scene membership, borrowing the root's document. */
      class TextDocumentRegistration : public ComposableNode::Participant, private MemberList<TextReader>
      {
      public:
        explicit TextDocumentRegistration(TextDocument &document)
            : document_(this),
              membership_(*this)
        {
          this->document_.connect(document.registration_);
        }
        virtual ~TextDocumentRegistration()
        {
          this->withdraw();
        }
        virtual void attach(Node &node, Scene &scene);
        virtual void withdraw();
        virtual void reclaim()
        {
          delete this;
        }
        void connectStore(TextStoreRow &row)
        {
          row.connect(this->store_);
        }
        /** Membership supplies the owning publication phase without a second owner pointer. */
        bool isPublishing() const
        {
          const SceneTextDocuments *owner = static_cast<SceneTextDocuments *>(this->membership_.owner());
          return owner && owner->isPublishing();
        }
        TextStoreRow *store() const
        {
          return this->store_.row();
        }

      private:
        TextDocumentRegistration(const TextDocumentRegistration &);
        TextDocumentRegistration &operator=(const TextDocumentRegistration &);
        TwinLink<TextDocumentRegistration, TextDocument> document_;
        TextStorePhase phase_;
        TextStoreLink store_;
        MemberHook<TextDocumentRegistration> membership_;
        friend class ::loka::app::TextReader;
        friend class SceneTextDocuments;
        friend class ::loka::app::detail::TextDocumentPublisher;
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
