#include "app/style/AttributedString.hpp"
#include "app/layout/TextLineBreaker.hpp"
#include "app/style/AttributedTextRead.hpp"
#include "platform/String.hpp"

#include <cassert>
#include <functional>
#include <limits>

namespace loka
{
  namespace app
  {
    typedef char SegmentSizePin[((sizeof(void *) == 8 && sizeof(AttributedString::Segment) == 32)
                                 || (sizeof(void *) == 4 && sizeof(AttributedString::Segment) == 20))
                                    ? 1
                                    : -1];

    bool AttributedString::estimateExtent(short availableWidth, const BlockStyle &block, core::Frame &out) const
    {
      const SyntheticTextWidthSource source(*this);
      const TextLineBreaker result(source, block, availableWidth);
      return SyntheticTextExtent(result, block, availableWidth, out);
    }

    namespace
    {
      const core::LokaAllocationSite kSegmentsSite("AttributedString", "Segments");

      // A cursor compares joined UTF-8 bytes without concatenating Strings or
      // materializing the whole line. Empty segments contribute no style.
      class ContentCursor
      {
      public:
        enum Kind
        {
          END,
          REFUSED,
          BYTE
        };
        explicit ContentCursor(const AttributedString &value)
            : value_(value),
              segment_(0),
              offset_(0),
              buffer_(),
              view_()
        {
        }

        Kind current()
        {
          while (this->segment_ < this->value_.segmentCount())
          {
            const core::String &text = this->value_.segment(this->segment_).text;
            if (this->offset_ == 0)
            {
              if (style_internal::ReadAttributedText(text, this->buffer_, this->view_) == style_internal::TEXT_REFUSED)
                return REFUSED;
            }
            if (this->offset_ < this->view_.length)
              return BYTE;
            ++this->segment_;
            this->offset_ = 0;
          }
          return END;
        }

        unsigned int byte() const
        {
          return static_cast<unsigned char>(this->view_.bytes[this->offset_]);
        }
        const AttributedString::Segment &segment() const
        {
          return this->value_.segment(this->segment_);
        }
        bool atSegmentStart() const
        {
          return this->offset_ == 0;
        }
        void advance()
        {
          ++this->offset_;
        }

      private:
        const AttributedString &value_;
        std::size_t segment_;
        std::size_t offset_;
        core::StringBuffer buffer_;
        platform::Utf8View view_;
      };
    } // namespace

    // The header and trailing placement-constructed array form one allocation.
    // Fundamental alignment suffices for Segment's pointer/int-based members
    // on the C++98 targets; the union keeps the trailing array aligned as well.
    struct AttributedString::Storage
    {
      union Header
      {
        std::size_t count;
        void *pointerAlignment;
        long double scalarAlignment;
      } header;

      explicit Storage(std::size_t count)
      {
        this->header.count = count;
      }
      Segment *segments()
      {
        return reinterpret_cast<Segment *>(this + 1);
      }
      const Segment *segments() const
      {
        return reinterpret_cast<const Segment *>(this + 1);
      }

      static void Release(Storage *storage, void *)
      {
        for (std::size_t i = storage->header.count; i > 0; --i)
          storage->segments()[i - 1].~Segment();
        storage->~Storage();
        core::LokaFreeRaw(storage, kSegmentsSite);
      }
    };

    AttributedString::AttributedString()
        : storage_(),
          valid_(true)
    {
    }
    bool AttributedString::valid() const
    {
      return this->valid_;
    }
    std::size_t AttributedString::segmentCount() const
    {
      return this->storage_.isValid() ? this->storage_->header.count : 0;
    }
    const AttributedString::Segment &AttributedString::segment(std::size_t index) const
    {
      assert(index < this->segmentCount());
      return this->storage_->segments()[index];
    }

    AttributedString AttributedString::Allocate(std::size_t count)
    {
#if __cplusplus >= 201103L
      static_assert(alignof(Storage) >= alignof(Segment), "Segment storage alignment");
#endif
      AttributedString result;
      result.valid_ = false;
      if (count > ((std::numeric_limits<std::size_t>::max)() - sizeof(Storage)) / sizeof(Segment))
        return result;
      void *memory = core::LokaAllocRaw(sizeof(Storage) + count * sizeof(Segment), kSegmentsSite);
      if (!memory)
        return result;
      Storage *storage = new (memory) Storage(count);
      for (std::size_t i = 0; i < count; ++i)
        new (storage->segments() + i) Segment();
      result.storage_ = core::Managed<Storage>::TryWrap(storage, &Storage::Release, 0);
      if (!result.storage_.isValid())
      {
        Storage::Release(storage, 0);
        return result;
      }
      result.valid_ = true;
      return result;
    }

    AttributedString::Builder::Builder(std::size_t capacityHint)
        : storage_(AttributedString::Allocate(capacityHint ? capacityHint : 1).storage_),
          used_(0)
    {
    }

    bool AttributedString::Builder::append(const core::String &text, const TextStyle &style)
    {
      return this->appendLink(text, style, LinkTarget());
    }

    bool
    AttributedString::Builder::appendLink(const core::String &text, const TextStyle &style, const LinkTarget &target)
    {
      if (!this->storage_.isValid())
        return false;
      const std::size_t capacity = this->storage_->header.count;
      if (this->used_ == capacity)
      {
        // Match Allocate's storage-size bound before doubling, which must not wrap.
        const std::size_t maximum = ((std::numeric_limits<std::size_t>::max)() - sizeof(Storage)) / sizeof(Segment);
        if (capacity == maximum)
          return false;
        const std::size_t next = capacity > maximum / 2 ? maximum : capacity * 2;
        AttributedString grown = AttributedString::Allocate(next);
        if (!grown.valid())
          return false;
        for (std::size_t i = 0; i < this->used_; ++i)
          grown.storage_->segments()[i] = this->storage_->segments()[i];
        // Keep the old owner until the replacement is installed.
        const core::Managed<Storage> previous = this->storage_;
        this->storage_ = grown.storage_;
      }
      Segment &segment = this->storage_->segments()[this->used_];
      segment.text = text;
      segment.style = style;
      segment.link = target;
      ++this->used_;
      return true;
    }

    AttributedString AttributedString::Builder::build()
    {
      AttributedString result;
      if (!this->storage_.isValid())
      {
        result.valid_ = false;
        return result;
      }
      // Release only visits header.count: end unused lifetimes before shrinking it.
      for (std::size_t i = this->storage_->header.count; i > this->used_; --i)
        this->storage_->segments()[i - 1].~Segment();
      this->storage_->header.count = this->used_;
      result.storage_ = this->storage_;
      this->storage_.reset();
      return result;
    }

    AttributedString Linked(const core::String &text, const TextStyle &style, const LinkTarget &target)
    {
      AttributedString result = AttributedString::Allocate(1);
      if (result.valid())
      {
        result.storage_->segments()[0].text = text;
        result.storage_->segments()[0].style = style;
        result.storage_->segments()[0].link = target;
      }
      return result;
    }

    AttributedString Linked(const char *text, const TextStyle &style, const LinkTarget &target)
    {
      return Linked(core::String::Literal(text), style, target);
    }

    AttributedString Styled(const core::String &text, const TextStyle &style)
    {
      return Linked(text, style, LinkTarget());
    }

    AttributedString Styled(const char *text, const TextStyle &style)
    {
      return Styled(core::String::Literal(text), style);
    }

    AttributedString operator+(const AttributedString &left, const AttributedString &right)
    {
      if (!left.valid())
        return left;
      if (!right.valid())
        return right;
      const std::size_t leftCount = left.segmentCount();
      const std::size_t rightCount = right.segmentCount();
      if (rightCount > (std::numeric_limits<std::size_t>::max)() - leftCount)
      {
        AttributedString refused;
        refused.valid_ = false;
        return refused;
      }
      AttributedString result = AttributedString::Allocate(leftCount + rightCount);
      if (!result.valid())
        return result;
      for (std::size_t i = 0; i < leftCount + rightCount; ++i)
        result.storage_->segments()[i] = i < leftCount ? left.segment(i) : right.segment(i - leftCount);
      return result;
    }

    AttributedString AttributedString::fold(const TextStyle &defaults, const TextStyle &overrides) const
    {
      if (!this->valid())
        return *this;
      AttributedString result = Allocate(this->segmentCount());
      if (!result.valid())
        return result;
      for (std::size_t i = 0; i < this->segmentCount(); ++i)
      {
        result.storage_->segments()[i] = this->segment(i);
        result.storage_->segments()[i].style = defaults + this->segment(i).style + overrides;
      }
      return result;
    }

    AttributedString operator+(const TextStyle &left, const AttributedString &right)
    {
      return right.fold(left, TextStyle());
    }
    AttributedString operator+(const AttributedString &left, const TextStyle &right)
    {
      return left.fold(TextStyle(), right);
    }

    bool AttributedString::empty() const
    {
      ContentCursor cursor(*this);
      return cursor.current() == ContentCursor::END;
    }

    int AttributedString::compare(const AttributedString &other) const
    {
      if (this->valid_ != other.valid_)
        return this->valid_ ? 1 : -1;
      if (this->storage_ == other.storage_)
        return 0;
      ContentCursor left(*this);
      ContentCursor right(other);
      for (;;)
      {
        const ContentCursor::Kind leftKind = left.current();
        const ContentCursor::Kind rightKind = right.current();
        if (leftKind != rightKind)
          return leftKind < rightKind ? -1 : 1;
        if (leftKind == ContentCursor::END)
          return 0;
        if (leftKind == ContentCursor::REFUSED)
          return std::less<const Segment *>()(&left.segment(), &right.segment()) ? -1 : 1;
        if (left.byte() != right.byte())
          return left.byte() < right.byte() ? -1 : 1;
        if (left.atSegmentStart() || right.atSegmentStart())
        {
          if (left.segment().style != right.segment().style)
            return left.segment().style < right.segment().style ? -1 : 1;
          const int linkOrder = style_internal::CompareLinkTargets(left.segment().link, right.segment().link);
          if (linkOrder)
            return linkOrder;
        }
        left.advance();
        right.advance();
      }
    }

    bool AttributedString::queryLinkRun(std::size_t segmentIndex, LinkRun &out) const
    {
      if (segmentIndex >= this->segmentCount())
        return false;
      core::StringBuffer scratch;
      platform::Utf8View view;
      const Segment &selected = this->segment(segmentIndex);
      if (style_internal::ReadAttributedText(selected.text, scratch, view) == style_internal::TEXT_EMPTY
          || selected.link.isNone())
        return false;
      LinkRun result;
      result.first = segmentIndex;
      result.last = segmentIndex;
      result.target = selected.link;
      // Both directions share the same participant rule; only the cursor moves differently.
      for (int direction = 0; direction < 2; ++direction)
      {
        std::size_t cursor = segmentIndex;
        while (direction == 0 ? cursor > 0 : cursor + 1 < this->segmentCount())
        {
          cursor = direction == 0 ? cursor - 1 : cursor + 1;
          const Segment &candidate = this->segment(cursor);
          if (style_internal::ReadAttributedText(candidate.text, scratch, view) == style_internal::TEXT_EMPTY)
            continue;
          if (candidate.link != result.target)
            break;
          if (direction == 0)
            result.first = cursor;
          else
            result.last = cursor;
        }
      }
      out = result;
      return true;
    }

    bool AttributedString::hasSameSegmentValues(const AttributedString &other) const
    {
      if (!this->valid_ || !other.valid_)
        return false;
      if (this->segmentCount() != other.segmentCount())
        return false;
      if (this->storage_ == other.storage_)
        return true;
      for (std::size_t s = 0; s < this->segmentCount(); ++s)
      {
        const Segment &a = this->segment(s);
        const Segment &b = other.segment(s);
        if (a.style != b.style || !a.text.equals(b.text) || a.link != b.link)
          return false;
      }
      return true;
    }

    bool AttributedString::equals(const AttributedString &other) const
    {
      return this->compare(other) == 0;
    }
    bool AttributedString::operator==(const AttributedString &other) const
    {
      return this->equals(other);
    }
    bool AttributedString::operator!=(const AttributedString &other) const
    {
      return !this->equals(other);
    }
  } // namespace app
} // namespace loka
