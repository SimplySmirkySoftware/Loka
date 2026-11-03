#ifndef LOKA_APP_TEXT_CHANGE_NOTE_HPP
#define LOKA_APP_TEXT_CHANGE_NOTE_HPP
#include "app/text/TextStore.hpp"
#include <algorithm>
namespace loka
{
  namespace app
  {
    namespace scene
    {
      /** Accumulates sequential native edits; unavailable ranges conservatively cover the whole body. */
      class TextChangeNote
      {
      public:
        TextChangeNote()
            : kind_(NONE),
              range_()
        {
        }
        void noteEdit(const TextChangedRange *range)
        {
          if (!range)
          {
            this->kind_ = WHOLE;
            return;
          }
          if (this->kind_ == WHOLE)
            return;
          if (this->kind_ == NONE)
          {
            this->range_ = *range;
            this->kind_ = RANGE;
            return;
          }
          // The second edit's coordinates follow the first edit. Expand the old and
          // new spans together, preserving the net size change without signed offsets.
          const unsigned long start = std::min(this->range_.start(), range->start());
          const unsigned long oldEnd = this->range_.start() + this->range_.length();
          const unsigned long newEnd = this->range_.start() + this->range_.insertedLength();
          const unsigned long editEnd = range->start() + range->length();
          const unsigned long tail = editEnd > newEnd ? editEnd - newEnd : 0;
          const unsigned long oldLength = oldEnd + tail - start;
          const unsigned long newLength = std::max(newEnd, editEnd) - start - range->length() + range->insertedLength();
          this->range_ = TextChangedRange(start, oldLength, newLength);
        }
        void note(const TextStoreReading &reading)
        {
          if (!reading.changed())
            return;
          TextChangedRange range;
          this->noteEdit(reading.queryRange(range) ? &range : 0);
        }
        bool pending() const
        {
          return this->kind_ != NONE;
        }
        TextStoreReading take(unsigned long caret = 0, unsigned long anchor = 0)
        {
          const TextStoreReading result(this->pending(), this->kind_ == RANGE ? &this->range_ : 0, caret, anchor);
          this->kind_ = NONE;
          this->range_ = TextChangedRange();
          return result;
        }

      private:
        enum Kind
        {
          NONE,
          RANGE,
          WHOLE
        };
        Kind kind_;
        TextChangedRange range_;
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
