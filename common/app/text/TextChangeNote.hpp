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
        void noteEdit(const TextChangedRange *range)
        {
          TextChangedRange previous;
          if (!range || (this->reading_.changed() && !this->reading_.queryRange(previous)))
          {
            this->reading_ = TextStoreReading(true, 0, 0, 0, this->reading_.marked());
            return;
          }
          if (!this->reading_.changed())
          {
            this->reading_ = TextStoreReading(true, range, 0, 0, this->reading_.marked());
            return;
          }
          // The second edit's coordinates follow the first edit. Expand the old and
          // new spans together, preserving the net size change without signed offsets.
          const unsigned long start = std::min(previous.start(), range->start());
          const unsigned long oldEnd = previous.start() + previous.length();
          const unsigned long newEnd = previous.start() + previous.insertedLength();
          const unsigned long editEnd = range->start() + range->length();
          const unsigned long tail = editEnd > newEnd ? editEnd - newEnd : 0;
          const unsigned long oldLength = oldEnd + tail - start;
          const unsigned long newLength = std::max(newEnd, editEnd) - start - range->length() + range->insertedLength();
          const TextChangedRange combined(start, oldLength, newLength);
          this->reading_ = TextStoreReading(true, &combined, 0, 0, this->reading_.marked());
        }
        void note(const TextStoreReading &reading)
        {
          TextChangedRange range;
          if (reading.changed())
            this->noteEdit(reading.queryRange(range) ? &range : 0);
          this->reading_ = TextStoreReading(this->pending(),
                                            this->reading_.queryRange(range) ? &range : 0,
                                            reading.caret(),
                                            reading.anchor(),
                                            reading.marked());
        }
        bool pending() const
        {
          return this->reading_.changed();
        }
        TextStoreReading take(unsigned long caret = 0, unsigned long anchor = 0)
        {
          TextChangedRange range;
          const TextStoreReading result(
              this->pending(), this->reading_.queryRange(range) ? &range : 0, caret, anchor, this->reading_.marked());
          this->reading_ = TextStoreReading(false, 0, 0, 0, this->reading_.marked());
          return result;
        }

      private:
        TextStoreReading reading_;
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
