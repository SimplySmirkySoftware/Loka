#ifndef LOKA_APP_TEXT_STORE_HPP
#define LOKA_APP_TEXT_STORE_HPP
#include "app/text/TextDocumentValues.hpp"
#include "app/scene/TwinLink.hpp"
namespace loka
{
  namespace app
  {
    /** Changed span in the rail's native unit: start and replaced length, plus inserted length. */
    class TextChangedRange
    {
    public:
      TextChangedRange(unsigned long start = 0, unsigned long length = 0, unsigned long insertedLength = 0)
          : start_(start),
            length_(length),
            insertedLength_(insertedLength)
      {
      }
      unsigned long start() const
      {
        return this->start_;
      }
      unsigned long length() const
      {
        return this->length_;
      }
      unsigned long insertedLength() const
      {
        return this->insertedLength_;
      }
      bool operator==(const TextChangedRange &other) const
      {
        return this->start_ == other.start_ && this->length_ == other.length_
               && this->insertedLength_ == other.insertedLength_;
      }
      bool operator!=(const TextChangedRange &other) const
      {
        return !(*this == other);
      }

    private:
      unsigned long start_;
      unsigned long length_;
      unsigned long insertedLength_;
    };

    namespace testing
    {
      struct TextStoreTestAccess;
    }
    namespace scene
    {
      enum TextStoreBirth
      {
        TEXT_STORE_BORN,
        TEXT_STORE_REFUSED_ALLOCATION,
        TEXT_STORE_REFUSED_NOT_REPRESENTABLE,
        TEXT_STORE_REFUSED_CAPACITY,
        TEXT_STORE_REFUSED_UNSUPPORTED
      };
      class IPlatformController;
      class TextStoreRow;
      class TextStoreLedger;
      /** Store-specific name for the callback-free paired borrow. */
      class TextStoreLink : public TwinLink<TextStoreRow, TextStoreRow>
      {
      public:
        explicit TextStoreLink(TextStoreRow *row = 0)
            : TwinLink<TextStoreRow, TextStoreRow>(row)
        {
        }
        TextStoreRow *row() const
        {
          return this->remote();
        }
      };
      /** Consumed native observation, including current uncommitted text presence. */
      class TextStoreReading
      {
      public:
        TextStoreReading(bool changed = false,
                         const TextChangedRange *range = 0,
                         unsigned long caret = 0,
                         unsigned long anchor = 0,
                         bool marked = false)
            : changed_(changed),
              range_(range ? *range : TextChangedRange()),
              reported_(range != 0),
              caret_(caret),
              anchor_(anchor),
              marked_(marked)
        {
        }
        bool marked() const
        {
          return this->marked_;
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
        bool marked_;
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
        friend struct ::loka::app::testing::TextStoreTestAccess;
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
        friend struct ::loka::app::testing::TextStoreTestAccess;
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
