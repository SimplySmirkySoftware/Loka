#include "WebAttributedLayout.hpp"
#include "WebAttributedBounds.hpp"
#include "WebTextLayout.hpp"
#include "WebTextScalar.hpp"
#include "WebRangeWidthMemo.hpp"
#include "WebBoundary.hpp"
#include "app/layout/TextSpanBreaks.hpp"
#include "app/style/AttributedTextRead.hpp"
#include <climits>
#include <cstring>
using namespace loka::app;
namespace
{
  // WebFontKey packs three binary font axes below the generated size index.
  const int FontKeyCount = loka::app::detail::kStyleVocabularySizeCount * 8;
  const loka::core::LokaAllocationSite Storage("WebAttributedSnapshot", "Buffer");
  const loka::core::LokaAllocationSite SnapshotStorage("WebAttributedSnapshot", "Snapshot");
  /** Private construction storage; copying/growth never exposes partial rows.
      Cost: build-local appends amortized linear in own rows; destruction walks own objects. */
  template <class T> class Buffer
  {
  public:
    /** Cost: builder per local table; initializes its empty owner, O(1). */
    Buffer()
        : rows_(0),
          size_(0),
          capacity_(0)
    {
    }
    /** Cost: build exit per local table; destroys only its own initialized rows. */
    ~Buffer()
    {
      this->clear();
    }
    /** Cost: builder per reserve; copies only this buffer's initialized rows on growth. */
    bool reserve(std::size_t count)
    {
      if (!web_attributed::allocation(count, sizeof(T)))
        return false;
      if (count <= this->capacity_)
        return true;
      T *rows = static_cast<T *>(loka::core::LokaAllocRaw(count * sizeof(T), Storage));
      if (!rows)
        return false;
      const std::size_t size = this->size_;
      for (std::size_t i = 0; i < size; ++i)
        new (rows + i) T(this->rows_[i]);
      this->clear();
      this->rows_ = rows;
      this->size_ = size;
      this->capacity_ = count;
      return true;
    }
    /** Cost: builder per row; O(1) except geometric growth of own rows. */
    bool append(const T &value)
    {
      if (this->size_ == this->capacity_)
      {
        const std::size_t capacity = this->capacity_ > INT32_MAX / 2 ? INT32_MAX
                                     : this->capacity_               ? 2 * this->capacity_
                                                                     : 32;
        if (this->size_ == INT32_MAX || !this->reserve(capacity))
          return false;
      }
      new (this->rows_ + this->size_) T(value);
      ++this->size_;
      return true;
    }
    /** Cost: build success once; transfers own storage, O(1). */
    T *release()
    {
      T *rows = this->rows_;
      this->rows_ = 0;
      this->size_ = this->capacity_ = 0;
      return rows;
    }
    /** Cost: builder per row access; own initialized row, O(1). */
    T &operator[](std::size_t i)
    {
      assert(i < this->size_);
      return this->rows_[i];
    }
    /** Cost: builder per row access; own initialized row, O(1). */
    const T &operator[](std::size_t i) const
    {
      assert(i < this->size_);
      return this->rows_[i];
    }
    /** Cost: builder/measure per query; own storage, O(1). */
    const T *data() const
    {
      return this->rows_;
    }
    /** Cost: builder per buffer handover; own storage, O(1). */
    T *data()
    {
      return this->rows_;
    }
    /** Cost: builder/measure per query; own count, O(1). */
    std::size_t size() const
    {
      return this->size_;
    }

  private:
    /** Cost: growth/destruction once; destroys only this buffer's initialized rows. */
    void clear()
    {
      for (std::size_t i = 0; i < this->size_; ++i)
        this->rows_[i].~T();
      loka::core::LokaFreeRaw(this->rows_, Storage);
      this->rows_ = 0;
      this->size_ = this->capacity_ = 0;
    }
    T *rows_;
    std::size_t size_, capacity_;
    Buffer(const Buffer &);
    Buffer &operator=(const Buffer &);
  };
  /** Build-local joined byte/character/span owner. Segment read views never escape append.
      Cost: prepare once; reads supplied segments and writes only its own tables. */
  class Prepared
  {
  public:
    Buffer<char> bytes;
    Buffer<TextBreakCharacter> characters;
    Buffer<TextStyleSpan> spans;
    /** Cost: builder once; one read per supplied segment, one scalar scan per read. */
    bool collect(const AttributedString &value)
    {
      loka::core::StringBuffer scratch;
      const AttributedString::Segment *previous = 0;
      for (std::size_t segmentIndex = 0; segmentIndex < value.segmentCount(); ++segmentIndex)
      {
        const AttributedString::Segment &segment = value.segment(segmentIndex);
        loka::platform::Utf8View view;
        const style_internal::TextReadKind kind = style_internal::ReadAttributedText(segment.text, scratch, view);
        if (kind == style_internal::TEXT_REFUSED)
          return false;
        if (kind == style_internal::TEXT_EMPTY)
          continue;
        if (TextSpanBreaks(previous, segment))
        {
          TextStyleSpan span;
          span.start = span.end = this->characters.size();
          span.source = TextSegmentRange(segmentIndex);
          span.style = segment.style;
          if (!this->spans.append(span))
            return false;
        }
        const std::size_t spanIndex = this->spans.size() - 1;
        for (std::size_t i = 0; i < view.length;)
        {
          const std::size_t start = i;
          TextBreakCharacter c;
          const bool valid = WebTextScalar(view.bytes, view.length, i, c.value);
          const char *text = view.bytes + start;
          std::size_t length = i - start;
          if (!valid || !c.value)
          {
            c.value = 0xfffd;
            text = "\xef\xbf\xbd";
            length = 3;
          }
          else if (c.value == '\t' || c.value == '\f')
          {
            c.value = ' ';
            text = " ";
            length = 1;
          }
          c.offset = this->bytes.size();
          c.span = spanIndex;
          std::size_t end = 0;
          if (!web_attributed::add(c.offset, length, end))
            return false;
          for (std::size_t j = 0; j < length; ++j)
            if (!this->bytes.append(text[j]))
              return false;
          c.end = end;
          if (!this->characters.append(c))
            return false;
        }
        this->spans[spanIndex].end = this->characters.size();
        this->spans[spanIndex].source = this->spans[spanIndex].source.through(segmentIndex);
        previous = &segment;
      }
      return true;
    }
  };
  /** Metrics and range adapter own all per-build caches. No service/session ownership.
      Cost: breaker queries own prepared source and local cache; no foreign ledger walk. */
  class WebAttributedMeasure : public TextWidthSource
  {
  public:
    /** Cost: builder once; prefix scan and at most 48 metrics calls before any width probe. */
    WebAttributedMeasure(const Prepared &source, const BlockStyle &block)
        : source_(source),
          length_(source.characters.size()),
          memo_(source.bytes.data(), Storage),
          status_(memo_.status())
    {
      if (block.hasTruncation_ && block.truncation_ == TEXT_TRUNCATION_ELLIPSIS)
        for (std::size_t i = 0; i < this->length_; ++i)
          if (source.characters[i].value == '\r' || source.characters[i].value == '\n')
          {
            this->length_ = i;
            break;
          }
      bool required[FontKeyCount] = {false};
      required[WebFontKey(TextStyle())] = true;
      for (std::size_t i = 0; i < source.spans.size(); ++i)
        if (source.spans[i].start < this->length_)
          required[WebFontKey(source.spans[i].style)] = true;
      for (int key = 0; key < FontKeyCount && this->status_ == WEB_MEASURE_OK; ++key)
        if (required[key])
        {
          const WebTextDescriptor descriptor = WebMetricDescriptor(key);
          TextLineMetrics &metrics = this->metrics_[key];
          metrics.leading = 0;
          this->status_ = web_metrics(descriptor.data(), &metrics.ascent, &metrics.descent);
          if (this->status_ == WEB_MEASURE_PROTOCOL)
            assert(false && "Web measurement protocol refusal");
          if (this->status_ == WEB_MEASURE_OK
              && (metrics.ascent < 0 || metrics.descent < 0 || metrics.ascent > SHRT_MAX
                  || metrics.descent > SHRT_MAX - metrics.ascent || metrics.ascent + metrics.descent == 0))
            this->status_ = WEB_MEASURE_REFUSED;
        }
    }
    /** Cost: builder per outcome; reads local status, O(1). */
    int status() const
    {
      return this->status_ != WEB_MEASURE_OK ? this->status_ : this->memo_.status();
    }
    /** Cost: breaker per query; local status, O(1). */
    virtual bool valid() const
    {
      return this->status() == WEB_MEASURE_OK;
    }
    /** Cost: breaker per query; own prefix, O(1). */
    virtual std::size_t length() const
    {
      return this->length_;
    }
    /** Cost: breaker per scalar; this build's source row, O(1). */
    virtual const TextBreakCharacter &character(std::size_t i) const
    {
      return this->source_.characters[i];
    }
    /** Cost: breaker per query; this build's span count, O(1). */
    virtual std::size_t spanCount() const
    {
      return this->source_.spans.size();
    }
    /** Cost: breaker per span; this build's source row, O(1). */
    virtual const TextStyleSpan &span(std::size_t i) const
    {
      return this->source_.spans[i];
    }
    /** Cost: breaker per fragment/break; reads own pre-fetched key, O(1). */
    virtual TextLineMetrics metrics(const TextStyle &style) const
    {
      return this->metrics_[WebFontKey(style)];
    }
    /** Cost: breaker per range; queries this build's standalone-range memo. */
    virtual bool width(std::size_t start, std::size_t end, std::size_t span, int &out) const
    {
      return this->valid() && this->memo_.width(start, end, WebFontKey(this->spanStyle(span)), out);
    }

  private:
    const Prepared &source_;
    std::size_t length_;
    WebRangeWidthMemo memo_;
    int status_;
    TextLineMetrics metrics_[FontKeyCount];
  };
  /** Cost: builder per extent sum; checked own geometry, O(1). */
  bool Geometry(int a, int b, int &out)
  {
    if (a < 0 || b < 0 || a > SHRT_MAX || b > SHRT_MAX - a)
      return false;
    out = a + b;
    return true;
  }
} // namespace
void *WebAttributedSnapshot::operator new(std::size_t size, const std::nothrow_t &) throw()
{
  return loka::core::LokaAllocRaw(size, SnapshotStorage);
}
void WebAttributedSnapshot::operator delete(void *value) throw()
{
  loka::core::LokaFreeRaw(value, SnapshotStorage);
}
void WebAttributedSnapshot::operator delete(void *value, const std::nothrow_t &) throw()
{
  WebAttributedSnapshot::operator delete(value);
}
WebAttributedSnapshot::WebAttributedSnapshot(const AttributedString &value,
                                             const BlockStyle &block,
                                             short width,
                                             char *bytes,
                                             unsigned byteLength,
                                             Fragment *fragments,
                                             Descriptor *descriptors,
                                             int *words,
                                             unsigned wordCount,
                                             short height)
    : sourceValue_(value),
      block_(block),
      width_(width),
      bytes_(bytes),
      fragments_(fragments),
      descriptors_(descriptors),
      words_(words),
      byteLength_(byteLength),
      wordCount_(wordCount),
      height_(height)
{
}
WebAttributedSnapshot::~WebAttributedSnapshot()
{
  loka::core::LokaFreeRaw(this->bytes_, Storage);
  loka::core::LokaFreeRaw(this->fragments_, Storage);
  loka::core::LokaFreeRaw(this->descriptors_, Storage);
  loka::core::LokaFreeRaw(this->words_, Storage);
}
bool WebAttributedSnapshot::matches(const AttributedString &value, const BlockStyle &block, short width) const
{
  return this->width_ == width && this->block_ == block && this->sourceValue_.hasSameSegmentValues(value);
}
const WebAttributedSnapshot::Fragment &WebAttributedSnapshot::fragment(std::size_t i) const
{
  assert(i < this->fragmentCount());
  return this->fragments_[i];
}
const WebAttributedSnapshot::Descriptor &WebAttributedSnapshot::descriptor(std::size_t i) const
{
  assert(i < this->descriptorCount());
  return this->descriptors_[i];
}
int WebAttributedLayout::build(const AttributedString &value,
                               const BlockStyle &block,
                               short width,
                               WebAttributedSnapshot *&out)
{
  const AttributedString sourceValue(value);
  if (!sourceValue.valid())
    return WEB_MEASURE_REFUSED;
  Prepared prepared;
  if (!prepared.collect(sourceValue))
    return WEB_MEASURE_REFUSED;
  WebAttributedMeasure source(prepared, block);
  if (!source.valid())
    return source.status();
  BlockStyle breaking = block;
  if (block.hasTruncation_ && block.truncation_ == TEXT_TRUNCATION_ELLIPSIS)
    breaking.wrap(TEXT_WRAP_NONE);
  const TextLineBreaker result(source, breaking, width);
  if (!result.valid())
    return source.status() == WEB_MEASURE_OK ? WEB_MEASURE_REFUSED : source.status();
  Buffer<char> display;
  Buffer<WebAttributedSnapshot::Fragment> fragments;
  Buffer<WebAttributedSnapshot::Descriptor> descriptors;
  Buffer<int> words;
  int height = 0;
  for (std::size_t k = 0; k < result.lineCount(); ++k)
  {
    const TextLineRecord &line = result.line(k);
    const TextLineMetrics lineMetrics = line.metrics;
    int lineHeight = 0, bottom = 0, lineWidth = 0;
    if (!Geometry(lineMetrics.ascent, lineMetrics.descent, lineHeight) || !lineHeight
        || !Geometry(height, lineHeight, bottom))
      return WEB_MEASURE_REFUSED;
    for (std::size_t j = 0; j < line.fragmentCount; ++j)
      if (!Geometry(lineWidth, result.fragment(line.firstFragment + j).width, lineWidth))
        return WEB_MEASURE_REFUSED;
    const int spare = width > lineWidth ? width - lineWidth : 0;
    int left = block.hasAlign_ && block.align_ == TEXT_ALIGN_RIGHT    ? spare
               : block.hasAlign_ && block.align_ == TEXT_ALIGN_CENTER ? spare / 2
                                                                      : 0;
    for (std::size_t j = 0; j < line.fragmentCount; ++j)
    {
      const TextFragment &f = result.fragment(line.firstFragment + j);
      const TextStyle &style = source.spanStyle(f.span);
      const TextLineMetrics metrics = source.metrics(style);
      WebAttributedSnapshot::Fragment target;
      target.left = left;
      target.width = f.width;
      int extent = 0;
      if (!Geometry(metrics.ascent, metrics.descent, target.height)
          || !Geometry(height, lineMetrics.ascent - metrics.ascent, target.top) || !Geometry(left, target.width, left)
          || !Geometry(target.top, target.height, extent))
        return WEB_MEASURE_REFUSED;
      const WebTextDescriptor descriptor = WebMetricDescriptor(WebFontKey(style), style);
      std::size_t id = 0;
      while (id < descriptors.size() && std::memcmp(descriptors[id].words, descriptor.data(), 9 * sizeof(int)))
        ++id;
      if (id == descriptors.size())
      {
        WebAttributedSnapshot::Descriptor row;
        std::memcpy(row.words, descriptor.data(), sizeof(row.words));
        if (!descriptors.append(row))
          return WEB_MEASURE_REFUSED;
      }
      target.descriptor = static_cast<int>(id);
      std::size_t end = 0;
      if (!web_attributed::add(display.size(), f.end - f.start, end))
        return WEB_MEASURE_REFUSED;
      target.byteStart = static_cast<int>(display.size());
      target.byteEnd = static_cast<int>(end);
      for (std::size_t i = f.start; i < f.end; ++i)
        if (!display.append(prepared.bytes[i]))
          return WEB_MEASURE_REFUSED;
      if (!fragments.append(target))
        return WEB_MEASURE_REFUSED;
    }
    height = bottom;
  }
  std::size_t wordCount = 0;
  if (!web_attributed::wordCount(fragments.size(), descriptors.size(), wordCount) || !words.reserve(wordCount))
    return WEB_MEASURE_REFUSED;
  if (!words.append(1) || !words.append(static_cast<int>(fragments.size()))
      || !words.append(static_cast<int>(descriptors.size())))
    return WEB_MEASURE_REFUSED;
  for (std::size_t i = 0; i < fragments.size(); ++i)
  {
    const WebAttributedSnapshot::Fragment &f = fragments[i];
    const int row[] = {f.descriptor, f.left, f.top, f.width, f.height, f.byteStart, f.byteEnd};
    for (unsigned j = 0; j < 7; ++j)
      if (!words.append(row[j]))
        return WEB_MEASURE_REFUSED;
  }
  for (std::size_t i = 0; i < descriptors.size(); ++i)
    for (unsigned j = 0; j < 9; ++j)
      if (!words.append(descriptors[i].words[j]))
        return WEB_MEASURE_REFUSED;
  // Construct last, then hand over: WebTextLayout's preserve-old output convention.
  WebAttributedSnapshot *snapshot = new (std::nothrow) WebAttributedSnapshot(sourceValue,
                                                                             block,
                                                                             width,
                                                                             display.data(),
                                                                             static_cast<unsigned>(display.size()),
                                                                             fragments.data(),
                                                                             descriptors.data(),
                                                                             words.data(),
                                                                             static_cast<unsigned>(wordCount),
                                                                             static_cast<short>(height));
  if (!snapshot)
    return WEB_MEASURE_REFUSED;
  display.release();
  fragments.release();
  descriptors.release();
  words.release();
  out = snapshot;
  return WEB_MEASURE_OK;
}
