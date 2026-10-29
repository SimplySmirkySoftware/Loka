#include "app/layout/TextLineBreaker.hpp"
#include <cassert>
#include <climits>
#include <limits>
#include "platform/StringUTF8.hpp"

namespace loka
{
  namespace app
  {
    namespace
    {
      int syntheticAdvance(int size)
      {
        return (size + 2) / 3;
      }
      int add(int a, int b)
      {
        return b > SHRT_MAX - a ? SHRT_MAX : a + b;
      }
      bool newline(unsigned int c)
      {
        return c == '\r' || c == '\n';
      }
      bool space(unsigned int c)
      {
        return c == ' ' || c == '\t';
      }
      void merge(TextLineMetrics &a, const TextLineMetrics &b)
      {
        if (b.ascent > a.ascent)
          a.ascent = b.ascent;
        if (b.descent > a.descent)
          a.descent = b.descent;
        if (b.leading > a.leading)
          a.leading = b.leading;
      }
      // Sizing and filling must agree on exactly which segments introduce rows.
      bool startsSpan(const TextStyle *previous, const TextStyle &style)
      {
        return !previous || *previous != style;
      }
      bool decode(const core::String &text, core::StringBuffer &buffer)
      {
        std::string utf8;
        return platform::CollectUtf8(text, utf8) && buffer.assignFromUtf8(utf8);
      }
    } // namespace
    SyntheticTextWidthSource::SyntheticTextWidthSource(const AttributedString &value)
        : characters_(),
          spans_(),
          length_(0),
          valid_(false)
    {
      if (!value.valid())
        return;
      std::size_t count = 0, spanCount = 0;
      const TextStyle *previousStyle = 0;
      for (std::size_t s = 0; s < value.segmentCount(); ++s)
      {
        const core::String &text = value.segment(s).text;
        core::StringBuffer buffer(core::StringEncodingUtf32);
        if (!decode(text, buffer))
          return;
        if (buffer.length() > (std::numeric_limits<std::size_t>::max)() - count)
          return;
        count += buffer.length();
        if (buffer.length() > 0)
        {
          const TextStyle &style = value.segment(s).style;
          if (startsSpan(previousStyle, style))
            ++spanCount;
          previousStyle = &style;
        }
      }
      if (!this->characters_.allocate(count) || !this->spans_.allocate(spanCount))
        return;
      for (std::size_t s = 0; s < value.segmentCount(); ++s)
      {
        const core::String &text = value.segment(s).text;
        core::StringBuffer buffer(core::StringEncodingUtf32);
        if (!decode(text, buffer))
          return;
        this->append(buffer, value.segment(s).style, s);
      }
      this->valid_ = true;
    }
    SyntheticTextWidthSource::SyntheticTextWidthSource(const core::String &value, const TextStyle &style)
        : characters_(),
          spans_(),
          length_(0),
          valid_(false)
    {
      core::StringBuffer buffer(core::StringEncodingUtf32);
      if (!decode(value, buffer))
        return;
      if (!this->characters_.allocate(buffer.length()) || !this->spans_.allocate(buffer.length() > 0 ? 1 : 0))
        return;
      this->append(buffer, style, 0);
      this->valid_ = true;
    }
    void SyntheticTextWidthSource::append(const core::StringBuffer &buffer, const TextStyle &style, std::size_t segment)
    {
      if (buffer.length() == 0)
        return;
      std::size_t span = this->length_ > 0 ? this->characters_[this->length_ - 1].span : 0;
      if (startsSpan(this->length_ > 0 ? &this->spans_[span].style : 0, style))
      {
        if (this->length_ > 0)
          ++span;
        TextStyleSpan &descriptor = this->spans_[span];
        descriptor.segment = segment;
        descriptor.start = this->length_;
        descriptor.style = style;
      }
      for (std::size_t i = 0; i < buffer.length(); ++i)
      {
        TextBreakCharacter &row = this->characters_[this->length_];
        row.value = buffer.characterAt(i);
        row.offset = this->length_;
        row.end = this->length_ + 1;
        row.span = span;
        ++this->length_;
      }
      this->spans_[span].end = this->length_;
    }
    SyntheticTextWidthSource::~SyntheticTextWidthSource() {}
    bool SyntheticTextWidthSource::valid() const
    {
      return this->valid_;
    }
    std::size_t SyntheticTextWidthSource::length() const
    {
      return this->valid_ ? this->length_ : 0;
    }
    std::size_t SyntheticTextWidthSource::spanCount() const
    {
      return this->valid_ ? this->spans_.size() : 0;
    }
    const TextStyleSpan &SyntheticTextWidthSource::span(std::size_t index) const
    {
      assert(this->valid_ && index < this->spanCount());
      return this->spans_[index];
    }
    const TextBreakCharacter &SyntheticTextWidthSource::character(std::size_t i) const
    {
      assert(this->valid_ && i < this->length_);
      return this->characters_[i];
    }
    int SyntheticTextSize(const TextSizeValue &size)
    {
      int points = 12;
      if (size.queryPoints(points))
        return points;
      TextSizeRole role;
      if (size.queryRole(role))
      {
        switch (role)
        {
        case TEXT_SIZE_ROLE_SMALL_LABEL:
          return 9;
        case TEXT_SIZE_ROLE_COUNT:
          break;
        }
      }
      return points;
    }

    TextLineMetrics SyntheticTextWidthSource::metrics(const TextStyle &style) const
    {
      return TextLineMetrics(SyntheticTextSize(style.size_));
    }
    bool SyntheticTextWidthSource::width(std::size_t start, std::size_t end, std::size_t span, int &out) const
    {
      const int advance = syntheticAdvance(this->metrics(this->spanStyle(span)).ascent);
      out = advance > 0 && end - start > static_cast<std::size_t>(INT_MAX / advance)
                ? INT_MAX
                : static_cast<int>(end - start) * advance;
      return true;
    }

    TextLineBreaker::TextLineBreaker(const TextWidthSource &source,
                                     const BlockStyle &block,
                                     short available,
                                     const TextStyle &emptyStyle)
        : lines_(),
          fragments_(),
          lineCount_(0),
          fragmentCount_(0)
    {
      if (!source.valid() || source.length() == (std::numeric_limits<std::size_t>::max)())
        return;
      // The same probe loop counts and fills; no character-count upper bound
      // forces ordinary editor lines out of the inline tables.
      if (!this->build(source, block, available, emptyStyle) || !this->lines_.allocate(this->lineCount_)
          || !this->fragments_.allocate(this->fragmentCount_))
      {
        this->clear();
        return;
      }
      this->lineCount_ = 0;
      this->fragmentCount_ = 0;
      if (!this->build(source, block, available, emptyStyle))
        this->clear();
    }
    void TextLineBreaker::clear()
    {
      this->lines_.clear();
      this->fragments_.clear();
      this->lineCount_ = 0;
      this->fragmentCount_ = 0;
    }
    TextLineBreaker::~TextLineBreaker()
    {
      this->clear();
    }
    const TextLineRecord &TextLineBreaker::line(std::size_t i) const
    {
      assert(i < this->lineCount_);
      return this->lines_[i];
    }
    const TextFragment &TextLineBreaker::fragment(std::size_t i) const
    {
      assert(i < this->fragmentCount_);
      return this->fragments_[i];
    }
    short TextLineBreaker::width() const
    {
      int result = 0;
      for (std::size_t i = 0; i < this->lineCount_; ++i)
        if (this->lines_[i].width > result)
          result = this->lines_[i].width;
      return static_cast<short>(result);
    }
    short TextLineBreaker::height() const
    {
      int result = 0;
      for (std::size_t i = 0; i < this->lineCount_; ++i)
        result = add(result, add(this->lines_[i].metrics.ascent, this->lines_[i].metrics.descent));
      return static_cast<short>(result);
    }

    namespace
    {
      // Probe growing ranges within one descriptor span, crossing equal-style
      // segment boundaries. A linear native width provider may walk quadratic
      // native units; the synthetic provider answers each probe in constant time.
      class RangeWidth
      {
      public:
        explicit RangeWidth(const TextWidthSource &source)
            : source_(source),
              first_(0),
              end_(0),
              prefix_(0),
              width_(0)
        {
        }
        bool append(std::size_t index)
        {
          const TextBreakCharacter &c = this->source_.character(index);
          if (this->first_ == this->end_ || c.span != this->source_.character(this->first_).span)
          {
            this->prefix_ = this->width_;
            this->first_ = index;
          }
          this->end_ = index + 1;
          int measured = 0;
          if (!this->source_.width(this->source_.character(this->first_).offset, c.end, c.span, measured)
              || measured < 0)
            return false;
          // Keep overflow visible until the wrap decision, even at SHRT_MAX.
          this->width_ = measured > INT_MAX - this->prefix_ ? INT_MAX : this->prefix_ + measured;
          return true;
        }
        int width() const
        {
          return this->width_;
        }

      private:
        const TextWidthSource &source_;
        std::size_t first_, end_;
        int prefix_, width_;
      };
    } // namespace
    bool TextLineBreaker::build(const TextWidthSource &source,
                                const BlockStyle &block,
                                short available,
                                const TextStyle &emptyStyle)
    {
      const bool filling = this->lines_.valid();
      const TextWrap wrap = block.hasWrap_ && available > 0 ? block.wrap_ : TEXT_WRAP_NONE;
      TextLineMetrics empty;
      bool hasEmptyMetrics = false;
      std::size_t begin = 0, index = 0, wordEnd = 0;
      while (true)
      {
        std::size_t end = index;
        TextLineMetrics breaks(0, 0, 0);
        bool explicitBreak = false;
        int width = 0;
        RangeWidth accumulated(source);
        while (index < source.length())
        {
          const TextBreakCharacter &c = source.character(index);
          if (newline(c.value))
          {
            end = index++;
            if (filling)
              breaks = source.metrics(source.spanStyle(c.span));
            if (c.value == '\r' && index < source.length() && source.character(index).value == '\n')
            {
              if (filling)
                merge(breaks, source.metrics(source.spanStyle(source.character(index).span)));
              ++index;
            }
            explicitBreak = true;
            break;
          }
          if (index >= wordEnd)
          {
            wordEnd = index + 1;
            if (wrap == TEXT_WRAP_WORD && !space(c.value))
              while (wordEnd < source.length() && !space(source.character(wordEnd).value)
                     && !newline(source.character(wordEnd).value))
                ++wordEnd;
          }
          RangeWidth word(accumulated);
          if (wrap == TEXT_WRAP_WORD && width > 0)
          {
            for (std::size_t probe = index; probe < wordEnd; ++probe)
              if (!word.append(probe))
                return false;
            if (word.width() > available)
            {
              end = index;
              break;
            }
          }
          bool full = false;
          for (; index < wordEnd; ++index)
          {
            if (!accumulated.append(index))
              return false;
            if (wrap != TEXT_WRAP_NONE && width > 0 && accumulated.width() > available)
            {
              full = true;
              break;
            }
            width = accumulated.width();
          }
          end = index;
          if (full)
            break;
        }
        TextLineRecord line;
        line.metrics = breaks;
        line.firstFragment = this->fragmentCount_;
        line.width = width > SHRT_MAX ? SHRT_MAX : width;
        std::size_t cursor = begin;
        while (cursor < end)
        {
          const TextBreakCharacter &first = source.character(cursor);
          std::size_t next = cursor + 1;
          while (next < end && source.character(next).span == first.span)
            ++next;
          TextFragment fragment;
          fragment.span = first.span;
          fragment.start = first.offset;
          fragment.end = source.character(next - 1).end;
          if (!source.width(fragment.start, fragment.end, fragment.span, fragment.width) || fragment.width < 0)
            return false;
          if (filling)
            merge(line.metrics, source.metrics(source.spanStyle(first.span)));
          if (this->fragments_.valid())
          {
            if (this->fragmentCount_ >= this->fragments_.size())
              return false;
            this->fragments_[this->fragmentCount_] = fragment;
          }
          ++this->fragmentCount_;
          cursor = next;
        }
        line.fragmentCount = this->fragmentCount_ - line.firstFragment;
        if (filling && line.metrics.ascent == 0 && line.metrics.descent == 0)
        {
          if (!hasEmptyMetrics)
          {
            empty = source.metrics(emptyStyle);
            hasEmptyMetrics = true;
          }
          line.metrics = empty;
        }
        if (explicitBreak)
        {
          empty = breaks;
          hasEmptyMetrics = true;
        }
        if (this->lines_.valid())
        {
          if (this->lineCount_ >= this->lines_.size())
            return false;
          this->lines_[this->lineCount_] = line;
        }
        ++this->lineCount_;
        if (index == source.length() && !explicitBreak)
          break;
        begin = index;
      }
      return true;
    }
    int SyntheticTextLineWidth(const TextLineRecord &line, const BlockStyle &block, short availableWidth)
    {
      int width = line.width;
      if ((!block.hasWrap_ || block.wrap_ == TEXT_WRAP_NONE) && availableWidth > 0 && width > availableWidth
          && block.hasTruncation_)
      {
        switch (block.truncation_)
        {
        case TEXT_TRUNCATION_NONE:
          break;
        case TEXT_TRUNCATION_CLIP:
          width = availableWidth;
          break;
        case TEXT_TRUNCATION_ELLIPSIS:
        {
          const int size = detail::SnapTextSizePoints(line.metrics.ascent + line.metrics.descent);
          const int advance = syntheticAdvance(size);
          const int capacity = availableWidth / advance;
          // Synthetic ellipsis occupies one advance; below that, paint an empty line.
          width = capacity * advance;
          break;
        }
        }
      }
      return width;
    }

    bool SyntheticTextExtent(const TextLineBreaker &result, const BlockStyle &block, short availableWidth, core::Frame &out)
    {
      if (!result.valid())
        return false;
      int maxWidth = 0;
      for (std::size_t i = 0; i < result.lineCount(); ++i)
      {
        const TextLineRecord &line = result.line(i);
        const int width = SyntheticTextLineWidth(line, block, availableWidth);
        if (width > maxWidth)
          maxWidth = width;
      }
      out = core::Frame(0, 0, maxWidth, result.height());
      return true;
    }
  } // namespace app
} // namespace loka
