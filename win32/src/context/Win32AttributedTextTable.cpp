#include "app/layout/AlignedLineOffset.hpp"
#include "Win32AttributedTextTable.hpp"
#include "../Win32PaintTextInk.hpp"
#include "../Win32ScenePlatformController.hpp"
#include <climits>

using namespace loka::app;
namespace
{
  loka::core::LokaAllocationSite BreakSite()
  {
    return loka::core::LokaAllocationSite("Win32AttributedText", "Break");
  }
  /** Restores the borrowed DC even on a failed partial measure. */
  class FontSelection
  {
  public:
    explicit FontSelection(HDC dc)
        : dc_(dc),
          original_(GetCurrentObject(dc, OBJ_FONT))
    {
    }
    ~FontSelection()
    {
      if (this->original_)
        SelectObject(this->dc_, this->original_);
    }
    bool select(HFONT font)
    {
      HGDIOBJ previous = font ? SelectObject(this->dc_, font) : 0;
      return previous && previous != HGDI_ERROR;
    }

  private:
    HDC dc_;
    HGDIOBJ original_;
    FontSelection(const FontSelection &);
    FontSelection &operator=(const FontSelection &);
  };
} // namespace

class Win32AttributedTextTable::WidthSource : public TextWidthSource
{
public:
  explicit WidthSource(const Win32AttributedTextTable &table)
      : table_(table)
  {
  }
  virtual bool valid() const
  {
    return this->table_.units_.valid();
  }
  virtual std::size_t length() const
  {
    return this->table_.characters_.size();
  }
  virtual const TextBreakCharacter &character(std::size_t i) const
  {
    return this->table_.characters_[i];
  }
  virtual std::size_t spanCount() const
  {
    return this->table_.spans_.size();
  }
  virtual const TextStyleSpan &span(std::size_t i) const
  {
    return this->table_.spans_[i];
  }
  virtual bool width(std::size_t start, std::size_t end, std::size_t span, int &out) const
  {
    if (start > end || end > this->table_.units_.size() || span >= this->spanCount())
      return false;
    out = this->table_.advances_[end] - this->table_.advances_[start];
    return out >= 0;
  }
  virtual TextLineMetrics metrics(const TextStyle &style) const
  {
    for (std::size_t i = 0; i < this->spanCount(); ++i)
      if (this->span(i).style == style)
        return this->table_.metrics_[i];
    return this->table_.metrics_[0]; // Empty input's default descriptor.
  }

private:
  const Win32AttributedTextTable &table_;
};

Win32AttributedTextTable::Win32AttributedTextTable()
    : lines_(0)
{
}
Win32AttributedTextTable::~Win32AttributedTextTable()
{
  this->clear();
}
void Win32AttributedTextTable::clear()
{
  this->measurement_.invalidate();
  loka::core::LokaDelete(this->lines_, BreakSite());
  this->lines_ = 0;
  this->snapshot_ = AttributedString();
  this->units_.clear();
  this->advances_.clear();
  this->characters_.clear();
  this->spans_.clear();
  this->fonts_.clear();
  this->metrics_.clear();
}
bool Win32AttributedTextTable::build(const AttributedString &value,
                                     const BlockStyle &block,
                                     int width,
                                     HDC dc,
                                     const Win32ScenePlatformController &controller)
{
  this->clear();
  if (!dc || !this->buildRows(value, block, width, dc, controller))
  {
    this->clear();
    return false;
  }
  this->snapshot_ = value;
  const loka::win32::Win32DisplayScale &scale = controller.displayScale();
  int height = 0, intrinsicWidth = 0;
  for (std::size_t i = 0; i < this->lines_->lineCount(); ++i)
  {
    const TextLineRecord &line = this->lines_->line(i);
    const int lineWidth = scale.measurementToLu(line.width);
    if (lineWidth > intrinsicWidth)
      intrinsicWidth = lineWidth;
    height += scale.measurementToLu(line.metrics.ascent + line.metrics.descent + line.metrics.leading);
    if (height > SHRT_MAX)
    {
      height = SHRT_MAX;
      break;
    }
  }
  this->measurement_.commit(width, Extent(intrinsicWidth, height));
  return true;
}
bool Win32AttributedTextTable::buildRows(const AttributedString &value,
                                         const BlockStyle &block,
                                         int width,
                                         HDC dc,
                                         const Win32ScenePlatformController &controller)
{
  // Deliberate sibling of Toolbox's decoder/coalescer, with native UTF-16
  // offsets. A character row always covers the complete surrogate pair.
  const SyntheticTextWidthSource decoded(value);
  if (!decoded.valid() || decoded.length() > static_cast<std::size_t>(INT_MAX / 2))
    return false;
  std::size_t count = 0;
  for (std::size_t i = 0; i < decoded.length(); ++i)
    count += decoded.character(i).value > 0xffff ? 2 : 1;
  const std::size_t descriptors = decoded.spanCount() ? decoded.spanCount() : 1;
  detail::TextMeasureTable<int> positions;
  if (!this->units_.allocate(count) || !this->advances_.allocate(count + 1) || !positions.allocate(count)
      || !this->characters_.allocate(decoded.length()) || !this->spans_.allocate(decoded.spanCount())
      || !this->fonts_.allocate(descriptors) || !this->metrics_.allocate(descriptors))
    return false;
  std::size_t offset = 0;
  for (std::size_t i = 0; i < decoded.length(); ++i)
  {
    TextBreakCharacter &row = this->characters_[i];
    row = decoded.character(i);
    row.offset = offset;
    if (row.value > 0xffff)
    {
      const unsigned int pair = row.value - 0x10000;
      this->units_[offset++] = static_cast<WCHAR>(0xd800 + (pair >> 10));
      this->units_[offset++] = static_cast<WCHAR>(0xdc00 + (pair & 0x3ff));
    }
    else
      this->units_[offset++] = static_cast<WCHAR>(row.value);
    row.end = offset;
  }
  FontSelection selection(dc);
  this->advances_[0] = 0;
  for (std::size_t i = 0; i < descriptors; ++i)
  {
    const TextStyle style = decoded.spanCount() ? decoded.spanStyle(i) : TextStyle();
    if (decoded.spanCount())
      this->spans_[i] = decoded.span(i);
    this->fonts_[i] = controller.fallbackFont(style);
    if (!selection.select(this->fonts_[i]))
      return false;
    std::size_t previous = 0;
    while (previous < i && this->fonts_[previous] != this->fonts_[i])
      ++previous;
    if (previous < i)
      this->metrics_[i] = this->metrics_[previous];
    else
    {
      TEXTMETRICW metrics;
      if (!GetTextMetricsW(dc, &metrics))
        return false;
      this->metrics_[i] = TextLineMetrics(metrics.tmAscent, metrics.tmDescent, metrics.tmExternalLeading);
    }
    if (!decoded.spanCount())
      continue;
    const std::size_t start = this->characters_[this->spans_[i].start].offset;
    const std::size_t end = this->characters_[this->spans_[i].end - 1].end;
    SIZE size;
    // One GDI call per coalesced span; every breaker probe is subtraction.
    if (!GetTextExtentExPointW(
            dc, &this->units_[start], static_cast<int>(end - start), INT_MAX, NULL, &positions[0], &size))
      return false;
    const int base = this->advances_[start];
    for (std::size_t unit = 0; unit < end - start; ++unit)
    {
      if (positions[unit] < 0 || positions[unit] > INT_MAX - base)
        return false;
      this->advances_[start + unit + 1] = base + positions[unit];
    }
  }
  const WidthSource source(*this);
  // The shared breaker uses short layout widths; saturate device capacity.
  const short capacity = static_cast<short>(width > SHRT_MAX ? SHRT_MAX : width < 0 ? 0 : width);
  this->lines_ = loka::core::LokaNew<TextLineBreaker>(BreakSite(), source, block, capacity);
  return this->lines_ && this->lines_->valid();
}

bool Win32AttributedTextTable::joinLines(std::wstring &out) const
{
  if (!this->valid())
    return false;
  std::wstring joined;
  for (std::size_t i = 0; i < this->lines_->lineCount(); ++i)
  {
    if (i)
    {
      if (joined.size() == joined.max_size())
        return false;
      joined.push_back(L'\n');
    }
    const TextLineRecord &line = this->lines_->line(i);
    for (std::size_t j = 0; j < line.fragmentCount; ++j)
    {
      const TextFragment &fragment = this->lines_->fragment(line.firstFragment + j);
      const std::size_t count = fragment.end - fragment.start;
      if (count > joined.max_size() - joined.size())
        return false;
      if (count)
        joined.append(&this->units_[fragment.start], count);
    }
  }
  out.swap(joined);
  return true;
}

bool Win32AttributedTextTable::draw(HDC dc, const RECT &clip, const BlockStyle &block) const
{
  if (!this->valid())
    return false;
  FontSelection selection(dc);
  const COLORREF previousText = GetTextColor(dc);
  const UINT alignment = GetTextAlign(dc);
  const int background = SetBkMode(dc, TRANSPARENT);
  SetTextAlign(dc, TA_BASELINE | TA_LEFT);
  bool painted = true;
  const WidthSource source(*this);
  int y = clip.top;
  for (std::size_t i = 0; i < this->lines_->lineCount() && painted; ++i)
  {
    const TextLineRecord &line = this->lines_->line(i);
    const int available = clip.right - clip.left;
    const bool ellipsis = (!block.hasWrap_ || block.wrap_ == TEXT_WRAP_NONE) && block.hasTruncation_
                          && block.truncation_ == TEXT_TRUNCATION_ELLIPSIS && available > 0 && line.width > available
                          && line.fragmentCount;
    const std::size_t lastSpan =
        line.fragmentCount ? this->lines_->fragment(line.firstFragment + line.fragmentCount - 1).span : 0;
    int budget = available;
    int dotsWidth = 0;
    if (ellipsis)
    {
      SIZE dots = {0, 0};
      painted =
          selection.select(this->fonts_[lastSpan]) && GetTextExtentExPointW(dc, L"...", 3, INT_MAX, NULL, NULL, &dots);
      if (!painted)
        break;
      dotsWidth = dots.cx;
      budget = budget > dotsWidth ? budget - dotsWidth : 0;
    }
    std::size_t paintedEnd = line.fragmentCount
        ? this->lines_->fragment(line.firstFragment + line.fragmentCount - 1).end : 0;
    int paintedWidth = line.width;
    if (ellipsis)
    {
      paintedWidth = dotsWidth;
      for (std::size_t f = 0; f < line.fragmentCount; ++f)
      {
        const TextFragment &fragment = this->lines_->fragment(line.firstFragment + f);
        std::size_t end = fragment.end;
        if (fragment.width > budget)
        {
          end = fragment.start;
          for (std::size_t c = 0; c < this->characters_.size(); ++c)
          {
            const TextBreakCharacter &character = this->characters_[c];
            if (character.offset < fragment.start)
              continue;
            if (character.end > fragment.end)
              break;
            int width = 0;
            if (!source.width(fragment.start, character.end, fragment.span, width) || width > budget)
              break;
            end = character.end;
          }
        }
        int advance = 0;
        if (!source.width(fragment.start, end, fragment.span, advance))
        {
          painted = false;
          break;
        }
        paintedWidth += advance;
        budget -= advance;
        paintedEnd = end;
        if (end != fragment.end)
          break;
      }
    }
    // Twin of ToolboxAttributedTextTable::draw: fit once from cached widths
    // before placing the line; no extra GDI measurement for alignment.
    int x = clip.left + AlignedLineOffset(available, paintedWidth,
        block.hasAlign_ ? block.align_ : TEXT_ALIGN_LEFT);
    for (std::size_t f = 0; f < line.fragmentCount && painted; ++f)
    {
      const TextFragment &fragment = this->lines_->fragment(line.firstFragment + f);
      const std::size_t end = fragment.end < paintedEnd ? fragment.end : paintedEnd;
      SetTextColor(dc, loka::win32::Win32PaintTextInk(this->spans_[fragment.span].style.color_, SURFACE_GROUND_WINDOW));
      painted = selection.select(this->fonts_[fragment.span]);
      std::size_t start = fragment.start;
      while (painted && start < end)
      {
        // ExtTextOutW accepts at most 8192 UTF-16 units per call.
        std::size_t next = end - start > 8192 ? start + 8192 : end;
        if (next < end && this->units_[next] >= 0xdc00 && this->units_[next] <= 0xdfff)
          --next;
        int batch = 0;
        painted = source.width(start, next, fragment.span, batch)
                  && ExtTextOutW(dc,
                                 x,
                                 y + line.metrics.ascent,
                                 ETO_CLIPPED,
                                 &clip,
                                 &this->units_[start],
                                 static_cast<UINT>(next - start),
                                 NULL);
        x += batch;
        start = next;
      }
      if (end != fragment.end)
        break;
    }
    if (painted && ellipsis)
    {
      SetTextColor(dc, loka::win32::Win32PaintTextInk(this->spans_[lastSpan].style.color_, SURFACE_GROUND_WINDOW));
      painted = selection.select(this->fonts_[lastSpan])
                && ExtTextOutW(dc, x, y + line.metrics.ascent, ETO_CLIPPED, &clip, L"...", 3, NULL);
    }
    y += line.metrics.ascent + line.metrics.descent + line.metrics.leading;
  }
  SetTextColor(dc, previousText);
  SetTextAlign(dc, alignment);
  SetBkMode(dc, background);
  return painted;
}
