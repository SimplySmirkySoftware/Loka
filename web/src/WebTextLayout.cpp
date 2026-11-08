#include "WebTextLayout.hpp"
#include "WebUtf8Bytes.hpp"
#include "WebBoundary.hpp"
#include "WebTestHooks.hpp"
#include <cstdlib>
#include "core/Utf8Decode.hpp"
#include <climits>
#include <cstring>
#include <limits>
#include <new>
using namespace loka::app;
namespace
{
  const loka::core::LokaAllocationSite Storage("WebTextSnapshot", "Buffer");
  /** Cost: build allocation once per table; checks its own size before allocating. */
  template <class T> T *Allocate(std::size_t count)
  {
    if (count > (std::numeric_limits<std::size_t>::max)() / sizeof(T))
      return 0;
    return static_cast<T *>(loka::core::LokaAllocRaw((count ? count : 1) * sizeof(T), Storage));
  }
  /** WHATWG error consumption around the shared strict decoder. Cost: normalizer
      once per invalid scalar, consumes only the borrowed prefix (at least one byte). */
  void ConsumeError(const char *bytes, std::size_t length, std::size_t &index)
  {
    const unsigned char lead = static_cast<unsigned char>(bytes[index++]);
    unsigned need = lead >= 0xc2 && lead <= 0xdf   ? 1
                    : lead >= 0xe0 && lead <= 0xef ? 2
                    : lead >= 0xf0 && lead <= 0xf4 ? 3
                                                   : 0;
    for (unsigned n = 0; n < need && index < length; ++n)
    {
      const unsigned char c = static_cast<unsigned char>(bytes[index]);
      const unsigned low = n == 0 && lead == 0xe0 ? 0xa0 : n == 0 && lead == 0xf0 ? 0x90 : 0x80;
      const unsigned high = n == 0 && lead == 0xed ? 0x9f : n == 0 && lead == 0xf4 ? 0x8f : 0xbf;
      if (c < low || c > high)
        break;
      ++index;
    }
  }
  /** Cost: build or equality once; one scan of borrowed bytes. Build writes its
      own prepared buffer; equality uses the identical normalizer without storage. */
  bool Normalize(const WebUtf8Bytes &source,
                 bool button,
                 char *destination,
                 const char *expected,
                 std::size_t expectedLength,
                 std::size_t &length)
  {
    length = 0;
    for (std::size_t i = 0; i < source.length();)
    {
      const std::size_t start = i;
      unsigned int value = 0;
      const bool valid = loka::core::detail::DecodeUtf8Codepoint(source.bytes(), source.length(), i, value);
      if (!valid)
      {
        ConsumeError(source.bytes(), source.length(), i);
        value = 0xfffd;
      }
      const char *bytes = source.bytes() + start;
      std::size_t count = i - start;
      char single = 0;
      if (!valid || value == 0)
      {
        bytes = "\xef\xbf\xbd";
        count = 3;
      }
      else if (value == '\r' || value == '\n' || value == '\t' || value == '\f')
      {
        if (value == '\r' && i < source.length() && source.bytes()[i] == '\n')
          ++i;
        single = value == '\t' || value == '\f' || button ? ' ' : '\n';
        bytes = &single;
        count = 1;
      }
      if (destination)
        std::memcpy(destination + length, bytes, count);
      else if (length > expectedLength || count > expectedLength - length
               || std::memcmp(expected + length, bytes, count))
        return false;
      length += count;
    }
    return destination || length == expectedLength;
  }
  /** Cost: each service result once; protocol diagnostics disappear in release,
      but the status and refusal remain intact. No rows or scheduling. */
  int CheckedStatus(int status)
  {
    if (status == WEB_MEASURE_PROTOCOL)
      assert(false && "Web measurement protocol refusal");
    return status;
  }
  bool Ellipsis(const BlockStyle &block)
  {
    return block.hasTruncation_ && block.truncation_ == TEXT_TRUNCATION_ELLIPSIS;
  }
} // namespace
int WebFontKey(const TextStyle &style)
{
  int points = 12;
  TextSizeRole role = TEXT_SIZE_ROLE_SMALL_LABEL;
  if (!style.size_.queryPoints(points))
    points = style.size_.queryRole(role) ? 10 : 12;
  unsigned index = 0;
  for (int i = 1; i < detail::StyleVocabularySizeCount; ++i)
    if (std::abs(points - detail::StyleVocabularySizes[i]) < std::abs(points - detail::StyleVocabularySizes[index]))
      index = i;
  TextWeight weight = TEXT_WEIGHT_NORMAL;
  bool italic = false;
  style.queryWeight(weight);
  style.queryItalic(italic);
  return static_cast<int>(index) * 8 + (weight == TEXT_WEIGHT_BOLD ? 4 : 0) + (italic ? 2 : 0)
         + (style.family() == TEXT_FAMILY_MONOSPACE ? 1 : 0);
}
WebTextDescriptor WebMetricDescriptor(int key, const TextStyle &paint)
{
  TextStyle style(TextSizeValue::points(detail::StyleVocabularySizes[key / 8]));
  style.weight(key & 4 ? TEXT_WEIGHT_BOLD : TEXT_WEIGHT_NORMAL)
      .italic((key & 2) != 0)
      .family(key & 1 ? TEXT_FAMILY_MONOSPACE : TEXT_FAMILY_SYSTEM);
  bool underline = false;
  if (paint.queryUnderline(underline))
    style.underline(underline);
  style.color_ = paint.color_;
  return WebTextDescriptor(style);
}
WebTextSnapshot::WebTextSnapshot(const WebTextConstraint &constraint)
    : constraint_(constraint),
      prepared_(0),
      joined_(0),
      characters_(0),
      lines_(0),
      preparedLength_(0),
      joinedLength_(0),
      characterCount_(0),
      lineCount_(0),
      metrics_(),
      extent_()
{
}
WebTextSnapshot::~WebTextSnapshot()
{
  loka::core::LokaFreeRaw(this->prepared_, Storage);
  loka::core::LokaFreeRaw(this->joined_, Storage);
  loka::core::LokaFreeRaw(this->characters_, Storage);
  loka::core::LokaFreeRaw(this->lines_, Storage);
}
int WebTextSnapshot::matches(const loka::core::String &value, bool button, bool &out) const
{
#ifdef TEST_BUILD
  loka::web::testing::textCompared();
#endif
  const WebUtf8Bytes source(value);
  if (!source.valid())
    return WEB_MEASURE_REFUSED;
  std::size_t length = 0;
  out = Normalize(source, button, 0, this->prepared_, this->preparedLength_, length);
  return WEB_MEASURE_OK;
}
/** Per-build source and open-addressed memo, owned entirely by the build stack.
    Cost: breaker range probes walk only this source's memo; each miss measures the
    standalone prepared range. Growth rehashes only this build's rows. */
class WebTextMeasure : public TextWidthSource
{
public:
  explicit WebTextMeasure(const WebTextSnapshot &snapshot)
      : snapshot_(snapshot),
        descriptor_(WebMetricDescriptor(snapshot.constraint_.fontKey)),
        rows_(0),
        capacity_(0),
        used_(0),
        status_(WEB_MEASURE_OK)
  {
    this->span_.segment = this->span_.start = 0;
    this->span_.end = snapshot.characterCount_;
    if (Ellipsis(snapshot.constraint_.block))
      for (std::size_t i = 0; i < snapshot.characterCount_; ++i)
        if (snapshot.characters_[i].value == '\n')
        {
          this->span_.end = i;
          break;
        }
    if (!this->grow())
      this->status_ = WEB_MEASURE_REFUSED;
  }
  ~WebTextMeasure()
  {
    loka::core::LokaFreeRaw(this->rows_, Storage);
  }
  int status() const
  {
    return this->status_;
  }
  virtual bool valid() const
  {
    return this->status_ == WEB_MEASURE_OK;
  }
  virtual std::size_t length() const
  {
    return this->span_.end;
  }
  virtual const TextBreakCharacter &character(std::size_t i) const
  {
    return this->snapshot_.characters_[i];
  }
  virtual std::size_t spanCount() const
  {
    return 1;
  }
  virtual const TextStyleSpan &span(std::size_t) const
  {
    return this->span_;
  }
  virtual TextLineMetrics metrics(const TextStyle &) const
  {
    return this->snapshot_.metrics_;
  }
  virtual bool width(std::size_t start, std::size_t end, std::size_t, int &out) const
  {
    if (!this->valid())
      return false;
    if (this->used_ >= this->capacity_ / 2 && !this->grow())
    {
      this->status_ = WEB_MEASURE_REFUSED;
      return false;
    }
    std::size_t index = this->find(start, end, this->rows_, this->capacity_);
    if (this->rows_[index].occupied)
    {
      out = this->rows_[index].width;
      return true;
    }
    int measured = 0;
    this->status_ = CheckedStatus(web_measure(
        this->descriptor_.data(), this->snapshot_.prepared_ + start, static_cast<unsigned>(end - start), &measured));
    if (!this->valid())
      return false;
    Row &row = this->rows_[index];
    row.start = start;
    row.end = end;
    row.width = measured;
    row.occupied = true;
    ++this->used_;
    out = measured;
    return true;
  }

private:
  struct Row
  {
    std::size_t start, end;
    int width;
    bool occupied;
  };
  const WebTextSnapshot &snapshot_;
  WebTextDescriptor descriptor_;
  TextStyleSpan span_;
  mutable Row *rows_;
  mutable std::size_t capacity_, used_;
  mutable int status_;
  std::size_t find(std::size_t start, std::size_t end, Row *rows, std::size_t capacity) const
  {
    std::size_t index = (start * 33 + end) & (capacity - 1);
    while (rows[index].occupied && (rows[index].start != start || rows[index].end != end))
      index = (index + 1) & (capacity - 1);
    return index;
  }
  bool grow() const
  {
    if (this->capacity_ > (std::numeric_limits<std::size_t>::max)() / 2)
      return false;
    const std::size_t capacity = this->capacity_ ? this->capacity_ * 2 : 64;
    Row *rows = Allocate<Row>(capacity);
    if (!rows)
      return false;
    for (std::size_t i = 0; i < capacity; ++i)
      rows[i].occupied = false;
    for (std::size_t i = 0; i < this->capacity_; ++i)
      if (this->rows_[i].occupied)
        rows[this->find(this->rows_[i].start, this->rows_[i].end, rows, capacity)] = this->rows_[i];
    loka::core::LokaFreeRaw(this->rows_, Storage);
    this->rows_ = rows;
    this->capacity_ = capacity;
    return true;
  }
  WebTextMeasure(const WebTextMeasure &);
  WebTextMeasure &operator=(const WebTextMeasure &);
};
int WebTextLayout::build(const loka::core::String &value,
                         const WebTextConstraint &constraint,
                         bool button,
                         WebTextSnapshot *&out)
{
#ifdef TEST_BUILD
  loka::web::testing::textBuilt();
#endif
  const WebUtf8Bytes bytes(value);
  if (!bytes.valid() || bytes.length() > UINT_MAX / 3)
    return WEB_MEASURE_REFUSED;
  WebTextSlot temporary;
  WebTextSnapshot *s = new (std::nothrow) WebTextSnapshot(constraint);
  if (!s)
    return WEB_MEASURE_REFUSED;
  temporary.replace(s);
  s->prepared_ = Allocate<char>(bytes.length() * 3);
  if (!s->prepared_)
    return WEB_MEASURE_REFUSED;
  std::size_t length = 0;
  Normalize(bytes, button, s->prepared_, 0, 0, length);
  s->preparedLength_ = static_cast<unsigned>(length);
  s->characters_ = Allocate<TextBreakCharacter>(length);
  if (!s->characters_)
    return WEB_MEASURE_REFUSED;
  for (std::size_t i = 0; i < length;)
  {
    TextBreakCharacter &c = s->characters_[s->characterCount_++];
    c.offset = i;
    c.span = 0;
    const bool decoded = loka::core::detail::DecodeUtf8Codepoint(s->prepared_, length, i, c.value);
    assert(decoded);
    (void)decoded;
    c.end = i;
  }
  const WebTextDescriptor descriptor = WebMetricDescriptor(constraint.fontKey);
  int status = CheckedStatus(web_metrics(descriptor.data(), &s->metrics_.ascent, &s->metrics_.descent));
  if (status != WEB_MEASURE_OK)
    return status;
  WebTextMeasure source(*s);
  BlockStyle block = constraint.block;
  if (Ellipsis(block) || button)
    block.wrap(TEXT_WRAP_NONE);
  const TextLineBreaker result(source, block, constraint.availableWidth);
  if (!result.valid())
    return source.status() == WEB_MEASURE_OK ? WEB_MEASURE_REFUSED : source.status();
  s->lineCount_ = result.lineCount();
  s->lines_ = Allocate<WebTextSnapshot::Line>(s->lineCount_);
  if (!s->lines_)
    return WEB_MEASURE_REFUSED;
  std::size_t joinedLength = s->lineCount_ - 1;
  std::size_t nextByte = 0;
  for (std::size_t i = 0; i < s->lineCount_; ++i)
  {
    const TextLineRecord &line = result.line(i);
    WebTextSnapshot::Line &target = s->lines_[i];
    target.start = target.end = nextByte;
    target.width = line.width;
    if (line.fragmentCount)
    {
      target.start = result.fragment(line.firstFragment).start;
      target.end = result.fragment(line.firstFragment + line.fragmentCount - 1).end;
    }
    nextByte = target.end;
    if (nextByte < s->preparedLength_ && s->prepared_[nextByte] == '\n')
      ++nextByte;
    const std::size_t count = target.end - target.start;
    if (joinedLength > UINT_MAX || count > UINT_MAX - joinedLength)
      return WEB_MEASURE_REFUSED;
    joinedLength += count;
  }
  s->joined_ = Allocate<char>(joinedLength);
  if (!s->joined_)
    return WEB_MEASURE_REFUSED;
  int maxWidth = 0;
  for (std::size_t i = 0; i < s->lineCount_; ++i)
  {
    if (i)
      s->joined_[s->joinedLength_++] = '\n';
    const WebTextSnapshot::Line &line = s->lines_[i];
    if (line.width > maxWidth)
      maxWidth = line.width;
    const unsigned count = static_cast<unsigned>(line.end - line.start);
    std::memcpy(s->joined_ + s->joinedLength_, s->prepared_ + line.start, count);
    s->joinedLength_ += count;
  }
  s->extent_ = loka::core::Frame(0, 0, maxWidth, result.height());
  // Transfer the completed value; the temporary no longer owns it.
  out = s;
  temporary.release();
  return WEB_MEASURE_OK;
}
bool WebTextLayout::natural(
    const loka::core::String &value, const WebTextConstraint &constraint, bool button, bool height, short &out)
{
  if (!button && height && constraint.block.hasWrap_ && constraint.block.wrap_ != TEXT_WRAP_NONE)
    return false;
  const WebUtf8Bytes source(value);
  if (!source.valid() || source.length() > UINT_MAX / 3)
    return false;
  char *prepared = Allocate<char>(source.length() * 3);
  if (!prepared)
    return false;
  std::size_t length = 0;
  Normalize(source, button, prepared, 0, 0, length);
  const bool declined = !button && ((!height && !length) || std::memchr(prepared, '\n', length));
  int status = WEB_MEASURE_REFUSED, measured = 0;
  const WebTextDescriptor descriptor = WebMetricDescriptor(constraint.fontKey);
  if (!declined)
  {
    if (height)
    {
      int ascent = 0, descent = 0;
      status = CheckedStatus(web_metrics(descriptor.data(), &ascent, &descent));
      if (status == WEB_MEASURE_OK)
        measured = ascent + descent;
    }
    else
      status = CheckedStatus(web_measure(descriptor.data(), prepared, static_cast<unsigned>(length), &measured));
  }
  loka::core::LokaFreeRaw(prepared, Storage);
  const int inset = button ? 2 * ((height ? kWebButtonInsetY : kWebButtonInsetX) + kWebButtonBorder) : 0;
  if (status != WEB_MEASURE_OK || measured < 0 || measured > SHRT_MAX - inset || measured + inset < 1)
    return false;
  out = static_cast<short>(measured + inset);
  return true;
}
