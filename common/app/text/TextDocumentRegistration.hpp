#ifndef LOKA_APP_TEXT_DOCUMENT_REGISTRATION_HPP
#define LOKA_APP_TEXT_DOCUMENT_REGISTRATION_HPP
#include "app/scene/node/ComposableNode.hpp"
#include "app/text/TextStore.hpp"
#include "app/scene/SceneTextDocuments.hpp"
namespace loka
{
  namespace app
  {
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
      class TextDocumentRegistration : public ComposableNode::Participant
      {
      public:
        explicit TextDocumentRegistration(TextDocument &document)
            : document_(document),
              owner_(0),
              previous_(0),
              next_(0)
        {
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
        TextStoreRow *store() const
        {
          return this->store_.row();
        }

      private:
        TextDocumentRegistration(const TextDocumentRegistration &);
        TextDocumentRegistration &operator=(const TextDocumentRegistration &);
        void unlink();
        TextDocument &document_;
        TextStorePhase phase_;
        TextStoreLink store_;
        SceneTextDocuments *owner_;
        TextDocumentRegistration *previous_, *next_;
        friend class SceneTextDocuments;
        friend class ::loka::app::detail::TextDocumentPublisher;
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
