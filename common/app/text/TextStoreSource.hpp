#ifndef LOKA_APP_TEXT_STORE_SOURCE_HPP
#define LOKA_APP_TEXT_STORE_SOURCE_HPP
#include "app/text/TextReplacement.hpp"
#include <cassert>
namespace loka
{
  namespace app
  {
    /** Kernel cursor borrowed by a rail during store construction. Owns an inert input copy.
        Consume to the end before querying applied placement; no second body scan is needed. */
    class TextStoreSource
    {
    public:
      explicit TextStoreSource(const TextReplacement &replacement)
          : replacement_(replacement),
            rawOffset_(0),
            logicalOffset_(0),
            applied_(replacement.placement())
      {
        assert(replacement.stage() != TextReplacement::NONE);
        if (this->applied_.kind() == TextPlacement::AT_UTF8_OFFSET
            && this->applied_.utf8Offset() > replacement.logicalLength())
          this->applied_ = TextPlacement::AtUtf8Offset(replacement.logicalLength());
      }
      /** Yields a scalar, with separators reported as LF and separator=true. */
      bool next(unsigned int &scalar, bool &separator)
      {
        const core::resource::Blob &blob = this->replacement_.bytes();
        if (this->rawOffset_ == blob.size())
          return false;
        const char *bytes = reinterpret_cast<const char *>(blob.data());
        const std::size_t start = this->rawOffset_;
        const bool valid = core::detail::DecodeUtf8Codepoint(bytes, blob.size(), this->rawOffset_, scalar);
        assert(valid);
        if (!valid)
          return false;
        separator = TextSeparators::Fold(scalar, bytes, blob.size(), this->rawOffset_);
        const unsigned long count = separator ? 1 : static_cast<unsigned long>(this->rawOffset_ - start);
        if (this->applied_.kind() == TextPlacement::AT_UTF8_OFFSET && this->applied_.utf8Offset() > this->logicalOffset_
            && this->applied_.utf8Offset() - this->logicalOffset_ < count)
          this->applied_ = TextPlacement::AtUtf8Offset(this->logicalOffset_);
        this->logicalOffset_ += count;
        if (separator)
          scalar = '\n';
        return true;
      }
      /** Read before traversal to track the native caret while consuming scalars. */
      TextPlacement requestedPlacement() const
      {
        return this->replacement_.placement();
      }
      unsigned long logicalOffset() const
      {
        return this->logicalOffset_;
      }
      TextPlacement appliedPlacement() const
      {
        assert(this->rawOffset_ == this->replacement_.bytes().size());
        return this->applied_;
      }
      bool clamped() const
      {
        return this->appliedPlacement() != this->replacement_.placement();
      }

    private:
      TextReplacement replacement_;
      std::size_t rawOffset_;
      unsigned long logicalOffset_;
      TextPlacement applied_;
    };
  } // namespace app
} // namespace loka
#endif
