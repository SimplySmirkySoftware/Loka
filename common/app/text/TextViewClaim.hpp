#ifndef LOKA_APP_TEXT_VIEW_CLAIM_HPP
#define LOKA_APP_TEXT_VIEW_CLAIM_HPP
#include "app/text/TextStore.hpp"
#include "app/text/TextViewValues.hpp"
#include "app/scene/MemberList.hpp"
namespace loka
{
  namespace app
  {
    namespace detail
    {
      class TextDocumentViewAccess;
      class TextDocumentPublisher;
    } // namespace detail
    namespace scene
    {
      class TextDocumentViewContext;
      /** Context-owned paired view identity. ATTACHED is derived exclusively from the link. */
      class TextViewClaim
      {
      public:
        TextViewAttachment attachment() const
        {
          return this->link_.remote() ? TextViewAttachment(TextViewAttachment::VIEW_ATTACHED) : this->reason_;
        }

      protected:
        TextViewClaim()
            : link_(this),
              membership_(*this)
        {
        }
        ~TextViewClaim()
        {
          assert(!this->link_.remote());
          assert(!this->membership_.owner());
        }

      private:
        TextViewClaim(const TextViewClaim &);
        TextViewClaim &operator=(const TextViewClaim &);
        void linkTo(TextStoreRow &row);
        void refuse(TextViewAttachment::Value reason)
        {
          assert(!this->link_.remote());
          this->reason_ = TextViewAttachment(reason);
        }
        void unlink()
        {
          this->link_.cut();
          this->reason_ = TextViewAttachment();
        }
        TwinLink<TextViewClaim, TextStoreRow> link_;
        TextViewAttachment reason_;
        MemberHook<TextViewClaim> membership_;
        friend class TextDocumentViewContext;
        friend class ::loka::app::detail::TextDocumentViewAccess;
        friend class ::loka::app::detail::TextDocumentPublisher;
        friend void TextStoreRow::transferView(TextStoreRow &, TextStoreRow &);
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
