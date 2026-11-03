#ifndef LOKA_APP_TEXT_STORE_HPP
#define LOKA_APP_TEXT_STORE_HPP
#include "app/text/TextDocumentValues.hpp"
namespace loka
{
  namespace app
  {
    namespace scene
    {
      class IPlatformController;
      class TextStoreRow;
      class TextStoreLedger;
      /** Callback-free twin of FocusLink. Either endpoint's destruction severs the borrow. */
      class TextStoreLink
      {
      public:
        explicit TextStoreLink(TextStoreRow *row = 0)
            : peer_(0),
              row_(row)
        {
        }
        ~TextStoreLink()
        {
          this->cut();
        }
        void connect(TextStoreLink &other)
        {
          this->cut();
          other.cut();
          if (&other == this)
            return;
          this->peer_ = &other;
          other.peer_ = this;
        }
        void cut()
        {
          if (this->peer_)
          {
            this->peer_->peer_ = 0;
            this->peer_ = 0;
          }
        }
        TextStoreRow *row() const
        {
          return this->peer_ ? this->peer_->row_ : 0;
        }

      private:
        TextStoreLink(const TextStoreLink &);
        TextStoreLink &operator=(const TextStoreLink &);
        TextStoreLink *peer_;
        TextStoreRow *const row_;
      };
      /** Consumed native observation. No IME field until the commit door exists. */
      class TextStoreReading
      {
      public:
        TextStoreReading(bool changed = false,
                         const TextChangedRange *range = 0,
                         unsigned long caret = 0,
                         unsigned long anchor = 0)
            : changed_(changed),
              range_(range ? *range : TextChangedRange()),
              reported_(range != 0),
              caret_(caret),
              anchor_(anchor)
        {
        }
        bool changed() const
        {
          return this->changed_;
        }
        bool queryRange(TextChangedRange &out) const
        {
          if (!this->reported_)
            return false;
          out = this->range_;
          return true;
        }
        unsigned long caret() const
        {
          return this->caret_;
        }
        unsigned long anchor() const
        {
          return this->anchor_;
        }

      private:
        bool changed_;
        TextChangedRange range_;
        bool reported_;
        unsigned long caret_, anchor_;
      };
      /** Controller-owned resident. Retirement cuts the link and moves lists without allocation. */
      class TextStoreRow
      {
      public:
        virtual ~TextStoreRow();
        void connect(TextStoreLink &link)
        {
          this->link_.connect(link);
        }
        void retire();
        IPlatformController &controller() const
        {
          return this->controller_;
        }

      protected:
        TextStoreRow(TextStoreLedger &owner, IPlatformController &controller);

      private:
        TextStoreRow(const TextStoreRow &);
        TextStoreRow &operator=(const TextStoreRow &);
        TextStoreLedger &owner_;
        IPlatformController &controller_;
        TextStoreLink link_;
        TextStoreRow *previous_, *next_;
        enum Phase
        {
          LIVE,
          RETIRED
        };
        Phase phase_;
        friend class TextStoreLedger;
      };
      /** Owns both lifetime lines; destruction is a callback-free final drain. */
      class TextStoreLedger
      {
      public:
        TextStoreLedger()
            : live_(0),
              retired_(0)
        {
        }
        ~TextStoreLedger()
        {
          this->retireAll();
          this->drain();
        }
        void retireAll()
        {
          while (this->live_)
            this->live_->retire();
        }
        void drain()
        {
          while (this->retired_)
            delete this->retired_;
        }

      private:
        TextStoreLedger(const TextStoreLedger &);
        TextStoreLedger &operator=(const TextStoreLedger &);
        void unlink(TextStoreRow &row)
        {
          TextStoreRow *&head = row.phase_ == TextStoreRow::LIVE ? this->live_ : this->retired_;
          if (row.previous_)
            row.previous_->next_ = row.next_;
          else
            head = row.next_;
          if (row.next_)
            row.next_->previous_ = row.previous_;
          row.previous_ = row.next_ = 0;
        }
        TextStoreRow *live_, *retired_;
        friend class TextStoreRow;
      };
      inline TextStoreRow::TextStoreRow(TextStoreLedger &owner, IPlatformController &controller)
          : owner_(owner),
            controller_(controller),
            link_(this),
            previous_(0),
            next_(owner.live_),
            phase_(LIVE)
      {
        if (this->next_)
          this->next_->previous_ = this;
        owner.live_ = this;
      }
      inline TextStoreRow::~TextStoreRow()
      {
        this->owner_.unlink(*this);
      }
      inline void TextStoreRow::retire()
      {
        if (this->phase_ == RETIRED)
          return;
        this->link_.cut();
        this->owner_.unlink(*this);
        this->phase_ = RETIRED;
        this->next_ = this->owner_.retired_;
        if (this->next_)
          this->next_->previous_ = this;
        this->owner_.retired_ = this;
      }
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
