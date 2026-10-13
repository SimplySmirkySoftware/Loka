#ifndef LOKA_APP_TEXT_LINE_BREAKER_HPP
#define LOKA_APP_TEXT_LINE_BREAKER_HPP

#include "app/style/AttributedString.hpp"
#include <limits>
#include <cassert>

namespace loka
{
  namespace app
  {

    namespace detail
    {
      /** Construction-only storage. Small measures stay on the stack; larger
          tables refuse through the same allocation gate as AttributedString. */
      template <class T> class TextMeasureTable
      {
      public:
        TextMeasureTable()
            : rows_(0),
              count_(0)
        {
        }
        ~TextMeasureTable()
        {
          this->clear();
        }
        bool allocate(std::size_t count)
        {
          assert(!this->rows_);
          if (count <= INLINE_CAPACITY)
            this->rows_ = this->inline_;
          else
          {
            if (count > (std::numeric_limits<std::size_t>::max)() / sizeof(T))
              return false;
            this->rows_ = static_cast<T *>(
                core::LokaAllocRaw(count * sizeof(T), core::LokaAllocationSite("TextLineBreaker", "Table")));
            if (!this->rows_)
              return false;
            for (std::size_t i = 0; i < count; ++i)
              new (this->rows_ + i) T();
          }
          this->count_ = count;
          return true;
        }
        void clear()
        {
          if (this->rows_ && this->rows_ != this->inline_)
          {
            for (std::size_t i = 0; i < this->count_; ++i)
              this->rows_[i].~T();
            core::LokaFreeRaw(this->rows_, core::LokaAllocationSite("TextLineBreaker", "Table"));
          }
          this->rows_ = 0;
          this->count_ = 0;
        }
        bool valid() const
        {
          return this->rows_ != 0;
        }
        std::size_t size() const
        {
          return this->count_;
        }
        T &operator[](std::size_t i)
        {
          assert(i < this->count_);
          return this->rows_[i];
        }
        const T &operator[](std::size_t i) const
        {
          assert(i < this->count_);
          return this->rows_[i];
        }

      private:
        enum
        {
          INLINE_CAPACITY = 32
        };
        T inline_[INLINE_CAPACITY];
        T *rows_;
        std::size_t count_;
        TextMeasureTable(const TextMeasureTable &);
        TextMeasureTable &operator=(const TextMeasureTable &);
      };
    } // namespace detail

    /** Resolved descriptor metrics. Leading is explicit, outside ascent + descent. */
    struct TextLineMetrics
    {
      int ascent, descent, leading;
      TextLineMetrics(int a = 12, int d = 0, int l = 0)
          : ascent(a),
            descent(d),
            leading(l)
      {
      }
    };

    /** One coalesced descriptor span. segment is the first contributing input
        segment; start/end delimit a half-open range of character indices. Empty
        input segments contribute neither a span nor a descriptor boundary. */
    struct TextStyleSpan
    {
      std::size_t segment, start, end;
      TextStyle style;
    };

    /** One code point in a joined native buffer. offset/end delimit encoded units;
        span indexes the source's coalesced descriptor table. */
    struct TextBreakCharacter
    {
      unsigned int value;
      std::size_t offset, end, span;
    };

    /** Borrowed width provider for one synchronous break. Ranges never split a code
        point, may cross adjacent equal-style segments, and use the source's native
        units. Sources own ordered, contiguous character spans, coalescing adjacent
        equal styles before breaking. Each character names its containing span.
        A refusal invalidates the complete result. Widths must be nonnegative.
        Successful probes must be repeatable during construction: the breaker
        counts and fills using the same probe sequence.
        Keep the source alive when resolving span IDs from its completed result. */
    class TextWidthSource
    {
    public:
      virtual ~TextWidthSource() {}
      virtual bool valid() const = 0;
      virtual std::size_t length() const = 0;
      virtual const TextBreakCharacter &character(std::size_t index) const = 0;
      virtual std::size_t spanCount() const = 0;
      virtual const TextStyleSpan &span(std::size_t index) const = 0;
      const TextStyle &spanStyle(std::size_t index) const
      {
        return this->span(index).style;
      }
      virtual bool width(std::size_t start, std::size_t end, std::size_t span, int &out) const = 0;
      virtual TextLineMetrics metrics(const TextStyle &style) const = 0;
    };

    /** Headless font projection: raw points, SmallLabel at 9, absent size at 12. */
    int SyntheticTextSize(const TextSizeValue &size);

    /** Synthetic joined UTF-32 table, owned by the measure. No state is retained. */
    class SyntheticTextWidthSource : public TextWidthSource
    {
    public:
      explicit SyntheticTextWidthSource(const AttributedString &value);
      SyntheticTextWidthSource(const core::String &value, const TextStyle &style);
      virtual ~SyntheticTextWidthSource();
      virtual bool valid() const;
      virtual std::size_t length() const;
      virtual const TextBreakCharacter &character(std::size_t index) const;
      virtual std::size_t spanCount() const;
      virtual const TextStyleSpan &span(std::size_t index) const;
      virtual bool width(std::size_t start, std::size_t end, std::size_t span, int &out) const;
      virtual TextLineMetrics metrics(const TextStyle &style) const;

    private:
      detail::TextMeasureTable<TextBreakCharacter> characters_;
      detail::TextMeasureTable<TextStyleSpan> spans_;
      void append(const core::StringBuffer &, const TextStyle &, std::size_t segment);
      std::size_t length_;
      bool valid_;
      SyntheticTextWidthSource(const SyntheticTextWidthSource &);
      SyntheticTextWidthSource &operator=(const SyntheticTextWidthSource &);
    };

    /** Fragment in the joined source's native units. Resolve its descriptor and
        first input segment through that source's span table, including after wrap. */
    struct TextFragment
    {
      std::size_t span, start, end;
      int width;
    };

    /** Completed line; fragments are a contiguous slice of the result's table. */
    struct TextLineRecord
    {
      TextLineMetrics metrics;
      int width;
      std::size_t firstFragment, fragmentCount;
    };

    /** Immutable completed break with owned line/fragment tables. The source is
        borrowed only during construction; resolving fragment span IDs still needs
        the matching source. Invalid results expose no partial rows.
        WORD spans segments; CHAR splits code points, not graphemes. Line height is
        ascent + descent; leading is recorded separately. */
    class TextLineBreaker
    {
    public:
      TextLineBreaker(const TextWidthSource &source,
                      const BlockStyle &block,
                      short availableWidth,
                      const TextStyle &emptyStyle = TextStyle());
      ~TextLineBreaker();
      bool valid() const
      {
        return this->lines_.valid();
      }
      std::size_t lineCount() const
      {
        return this->lineCount_;
      }
      const TextLineRecord &line(std::size_t index) const;
      const TextFragment &fragment(std::size_t index) const;
      short width() const;
      short height() const;

    private:
      detail::TextMeasureTable<TextLineRecord> lines_;
      detail::TextMeasureTable<TextFragment> fragments_;
      std::size_t lineCount_, fragmentCount_;
      bool build(const TextWidthSource &, const BlockStyle &, short, const TextStyle &);
      void clear();
      TextLineBreaker(const TextLineBreaker &);
      TextLineBreaker &operator=(const TextLineBreaker &);
    };
    /** Fitted synthetic width of one completed line, after clip/ellipsis. */
    int SyntheticTextLineWidth(const TextLineRecord &line, const BlockStyle &block, short availableWidth);

    /** Synthetic projection of a valid completed break, including Null's
        truncation rule. Refusal leaves out unchanged. Extent origin is (0,0);
        this is not native rail geometry. */
    bool SyntheticTextExtent(const TextLineBreaker &result, const BlockStyle &block, short availableWidth, core::Frame &out);
  } // namespace app
} // namespace loka
#endif
