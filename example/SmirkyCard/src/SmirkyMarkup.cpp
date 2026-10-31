#include "SmirkyMarkup.hpp"
#include <climits>
#include <cstring>

namespace smirkycard
{
  namespace
  {
    using namespace loka::app;
    const loka::core::LokaAllocationSite kFrames("SmirkyMarkup", "Frame");

    enum Tag
    {
      BOLD,
      ITALIC,
      UNDERLINE,
      CODE,
      SIZE
    };

    class StyleStack
    {
      struct Frame
      {
        Tag tag;
        TextStyle style;
        Frame *next;
        Frame(Tag t, const TextStyle &s, Frame *n)
            : tag(t),
              style(s),
              next(n)
        {
        }
      };
      Frame *head_;
      const TextStyle base_;
      StyleStack(const StyleStack &);
      StyleStack &operator=(const StyleStack &);

      void pop()
      {
        Frame *old = this->head_;
        this->head_ = old->next;
        loka::core::LokaDelete(old, kFrames);
      }

    public:
      explicit StyleStack(const TextStyle &base)
          : head_(0),
            base_(base)
      {
      }
      ~StyleStack()
      {
        while (this->head_)
          this->pop();
      }
      const TextStyle &style() const
      {
        return this->head_ ? this->head_->style : this->base_;
      }
      bool empty() const
      {
        return !this->head_;
      }
      bool push(Tag tag, const TextStyle &overlay)
      {
        Frame *frame = loka::core::LokaNew<Frame>(kFrames, tag, this->style() + overlay, this->head_);
        if (!frame)
          return false;
        this->head_ = frame;
        return true;
      }
      bool close(Tag tag)
      {
        if (!this->head_ || this->head_->tag != tag)
          return false;
        this->pop();
        return true;
      }
    };

    bool append(AttributedString::Builder &builder, const char *bytes, std::size_t length, const TextStyle &style)
    {
      if (!length)
        return true;
      const loka::core::String text = loka::core::String::Utf8(bytes, length);
      return builder.append(text, style);
    }

    bool tagEquals(const char *bytes, std::size_t length, const char *tag)
    {
      return length == std::strlen(tag) && !std::memcmp(bytes, tag, length);
    }
  } // namespace

  bool ParseSmirkyMarkup(const char *bytes,
                         std::size_t length,
                         const loka::app::TextStyle &base,
                         loka::app::AttributedString &out)
  {
    if (!bytes && length)
      return false;
    AttributedString::Builder builder(4);
    StyleStack stack(base);
    std::size_t pos = 0;
    while (pos < length)
    {
      if (bytes[pos] == '\\' && pos + 1 < length && (bytes[pos + 1] == '<' || bytes[pos + 1] == '\\'))
      {
        if (!append(builder, bytes + pos + 1, 1, stack.style()))
          return false;
        pos += 2;
      }
      else if (bytes[pos] == '<')
      {
        const std::size_t start = ++pos;
        while (pos < length && bytes[pos] != '>')
          ++pos;
        if (pos == length)
          return false;
        const bool closing = bytes[start] == '/';
        const char *tag = bytes + start + (closing ? 1 : 0);
        const std::size_t count = pos - start - (closing ? 1 : 0);
        ++pos;
        Tag kind;
        TextStyle overlay;
        if (tagEquals(tag, count, "b"))
        {
          kind = BOLD;
          overlay = Bold;
        }
        else if (tagEquals(tag, count, "i"))
        {
          kind = ITALIC;
          overlay = Italic;
        }
        else if (tagEquals(tag, count, "u"))
        {
          kind = UNDERLINE;
          overlay = Underline;
        }
        else if (tagEquals(tag, count, "code"))
        {
          kind = CODE;
          overlay = Monospace;
        }
        else if (closing && tagEquals(tag, count, "size"))
        {
          kind = SIZE;
        }
        else if (!closing && count > 5 && !std::memcmp(tag, "size=", 5))
        {
          int size = 0;
          for (std::size_t i = 5; i < count; ++i)
          {
            if (tag[i] < '0' || tag[i] > '9' || size > (INT_MAX - (tag[i] - '0')) / 10)
              return false;
            size = size * 10 + (tag[i] - '0');
          }
          kind = SIZE;
          overlay = SizeOf(size);
        }
        else
          return false;
        if (closing ? !stack.close(kind) : !stack.push(kind, overlay))
          return false;
      }
      else
      {
        const std::size_t start = pos++;
        while (pos < length && bytes[pos] != '<' && bytes[pos] != '\\')
          ++pos;
        if (!append(builder, bytes + start, pos - start, stack.style()))
          return false;
      }
    }
    if (!stack.empty())
      return false;
    const AttributedString result = builder.build();
    if (!result.valid())
      return false;
    out = result;
    return true;
  }
} // namespace smirkycard
