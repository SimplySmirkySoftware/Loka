#ifndef LOKA_MARKDOWN_EDITOR_MARKDOWN_HPP
#define LOKA_MARKDOWN_EDITOR_MARKDOWN_HPP

#include "core/ObservableList.hpp"
#include "app/style/AttributedString.hpp"

namespace markdowneditor
{
  enum BlockKind
  {
    UNUSED,
    PARAGRAPH,
    HEADING1,
    HEADING2,
    HEADING3,
    BULLET,
    NUMBERED,
    QUOTE,
    CODE,
    RULE
  };
  enum
  {
    kMaxBlocks = 64
  };

  /** Completed text and presentation facts; no runtime node or borrowed input. */
  class Block
  {
  public:
    Block()
        : kind_(UNUSED)
    {
    }
    Block(BlockKind kind, const loka::app::AttributedString &text, const loka::core::String &marker)
        : kind_(kind),
          text_(text),
          marker_(marker)
    {
    }
    BlockKind kind() const
    {
      return this->kind_;
    }
    const loka::app::AttributedString &text() const
    {
      return this->text_;
    }
    const loka::core::String &marker() const
    {
      return this->marker_;
    }

  private:
    BlockKind kind_;
    loka::app::AttributedString text_;
    loka::core::String marker_;
  };

  /** Immutable bounded parse result. Refusal never publishes a successful prefix. */
  class Markdown
  {
  public:
    enum Result
    {
      READY,
      CAPACITY,
      REFUSED
    };
    explicit Markdown(const loka::core::ObservableList<loka::core::String> &lines)
        : size_(0),
          result_(READY)
    {
      using namespace loka::core;
      unsigned short line = 0;
      while (line < lines.size() && this->result_ == READY)
      {
        const StringBuffer bytes = lines.at(line).value.bufferWithEncoding(StringEncodingUtf8);
        if (!bytes.data() && !lines.at(line).value.empty())
        {
          this->result_ = REFUSED;
          break;
        }
        const char *p = static_cast<const char *>(bytes.data());
        const std::size_t n = bytes.length();
        if (!n)
        {
          ++line;
          continue;
        }
        std::size_t start = 0;
        const BlockKind kind = classify(p, n, start);
        String marker;
        if (kind == BULLET)
          marker = String::Literal("-");
        if (kind == NUMBERED)
          marker = String::Utf8(p, start - 1);
        String text = String::Utf8(p + start, n - start);
        ++line;
        if (kind == CODE)
        {
          text = String();
          bool first = true;
          while (line < lines.size())
          {
            const String &next = lines.at(line++).value;
            const StringBuffer b = next.bufferWithEncoding(StringEncodingUtf8);
            if (!b.data() && !next.empty())
            {
              this->result_ = REFUSED;
              break;
            }
            std::size_t skip = 0;
            if (classify(static_cast<const char *>(b.data()), b.length(), skip) == CODE)
              break;
            if (!first)
              text += "\n";
            text += next;
            first = false;
          }
        }
        else if (kind == PARAGRAPH)
        {
          while (line < lines.size())
          {
            const String &next = lines.at(line).value;
            const StringBuffer b = next.bufferWithEncoding(StringEncodingUtf8);
            if (!b.data() && !next.empty())
            {
              this->result_ = REFUSED;
              break;
            }
            std::size_t skip = 0;
            if (!b.length() || classify(static_cast<const char *>(b.data()), b.length(), skip) != PARAGRAPH)
              break;
            text += " ";
            text += next;
            ++line;
          }
        }
        if (this->result_ != READY)
          break;
        if (this->size_ == kMaxBlocks)
        {
          this->result_ = CAPACITY;
          break;
        }
        loka::app::AttributedString styled;
        if (!inlineText(text, kind, styled))
        {
          this->result_ = REFUSED;
          break;
        }
        this->blocks_[this->size_++] = Block(kind, styled, marker);
      }
    }
    Result result() const
    {
      return this->result_;
    }
    unsigned size() const
    {
      return this->size_;
    }
    const Block &at(unsigned index) const
    {
      assert(index < this->size_);
      return this->blocks_[index];
    }

  private:
    static BlockKind classify(const char *p, std::size_t n, std::size_t &start)
    {
      start = 0;
      if (n >= 3 && p[0] == '`' && p[1] == '`' && p[2] == '`')
        return CODE;
      if (n == 3 && p[0] == '-' && p[1] == '-' && p[2] == '-')
        return RULE;
      unsigned hashes = 0;
      while (hashes < n && p[hashes] == '#')
        ++hashes;
      if (hashes >= 1 && hashes <= 3 && hashes < n && p[hashes] == ' ')
      {
        start = hashes + 1;
        return hashes == 1 ? HEADING1 : hashes == 2 ? HEADING2 : HEADING3;
      }
      if (n >= 2 && (p[0] == '-' || p[0] == '*') && p[1] == ' ')
      {
        start = 2;
        return BULLET;
      }
      if (n && p[0] == '>')
      {
        start = n > 1 && p[1] == ' ' ? 2 : 1;
        return QUOTE;
      }
      std::size_t digits = 0;
      while (digits < n && p[digits] >= '0' && p[digits] <= '9')
        ++digits;
      if (digits && digits + 1 < n && p[digits] == '.' && p[digits + 1] == ' ')
      {
        start = digits + 2;
        return NUMBERED;
      }
      return PARAGRAPH;
    }
    static bool inlineText(const loka::core::String &text, BlockKind kind, loka::app::AttributedString &out)
    {
      using namespace loka::app;
      using namespace loka::core;
      TextStyle base;
      if (kind == HEADING1)
        base = FontSize<24>() + Bold;
      if (kind == HEADING2)
        base = FontSize<18>() + Bold;
      if (kind == HEADING3)
        base = FontSize<14>() + Bold;
      if (kind == QUOTE || kind == CODE)
        base = Italic + Secondary;
      if (kind == CODE || kind == RULE)
      {
        out = Styled(kind == RULE ? String() : text, base);
        return out.valid();
      }
      const StringBuffer bytes = text.bufferWithEncoding(StringEncodingUtf8);
      if (!bytes.data() && !text.empty())
        return false;
      const char *p = static_cast<const char *>(bytes.data());
      const std::size_t n = bytes.length();
      AttributedString::Builder builder(8);
      std::size_t pos = 0, plain = 0;
      while (pos < n)
      {
        if (p[pos] != '*' && p[pos] != '`')
        {
          ++pos;
          continue;
        }
        const std::size_t width = p[pos] == '*' && pos + 1 < n && p[pos + 1] == '*' ? 2 : 1;
        std::size_t end = pos + width;
        while (end < n && !(p[end] == p[pos] && (width == 1 || (end + 1 < n && p[end + 1] == '*'))))
          ++end;
        if (end == n || end == pos + width)
        {
          pos += width;
          continue;
        }
        if (pos > plain && !builder.append(String::Utf8(p + plain, pos - plain), base))
          return false;
        const TextStyle style = p[pos] == '`' ? Italic + Secondary : width == 2 ? Bold : Italic;
        if (!builder.append(String::Utf8(p + pos + width, end - pos - width), base + style))
          return false;
        pos = end + width;
        plain = pos;
      }
      if (plain < n && !builder.append(String::Utf8(p + plain, n - plain), base))
        return false;
      out = builder.build();
      return out.valid();
    }
    Block blocks_[kMaxBlocks];
    unsigned size_;
    Result result_;
  };
} // namespace markdowneditor
#endif
