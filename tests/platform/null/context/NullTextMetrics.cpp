#include "platform/null/context/NullTextMetrics.hpp"
#include <climits>
#include "app/layout/AlignedLineOffset.hpp"

bool MeasureNullText(const loka::app::TextStyle &style,
                     const loka::app::BlockStyle &block,
                     const loka::core::String *value,
                     const loka::app::scene::LayoutState &state,
                     NullTextMeasurement &out)
{
  int points = 12;
  style.size_.queryPoints(points);
  out = NullTextMeasurement(0, static_cast<short>(points), 1);
  if (RefuseNullTextMeasurement())
    return false;
  const loka::app::SyntheticTextWidthSource source(value ? *value : loka::core::String(), style);
  const loka::app::TextLineBreaker result(source, block, state.width, style);
  if (!result.valid())
    return false;
  out = MeasureNullTextLines(result, block, state.width);
  return true;
}

NullTextMeasurement
MeasureNullTextLines(const loka::app::TextLineBreaker &result, const loka::app::BlockStyle &block, short availableWidth)
{
  return NullTextMeasurement(result, block, availableWidth);
}

NullTextMeasurement::NullTextMeasurement(const loka::app::TextLineBreaker &result,
                                         const loka::app::BlockStyle &block,
                                         short availableWidth)
    : result_(),
      lineCount_(static_cast<short>(result.lineCount() > SHRT_MAX ? SHRT_MAX : result.lineCount()))
{
  if (!result.valid())
    return;
  int y = 0;
  int maxWidth = 0;
  if (result.lineCount() > 1)
    this->additionalLines_.reserve(result.lineCount() - 1);
  for (std::size_t i = 0; i < result.lineCount(); ++i)
  {
    const loka::app::TextLineRecord &line = result.line(i);
    const int painted = loka::app::SyntheticTextLineWidth(line, block, availableWidth);
    const int x = loka::app::AlignedLineOffset(
        availableWidth, painted, block.hasAlign_ ? block.align_ : loka::app::TEXT_ALIGN_LEFT);
    const int height = line.metrics.ascent + line.metrics.descent;
    const loka::core::Frame frame(x, y, painted, height);
    if (i == 0)
      this->firstLine_ = frame;
    else
      this->additionalLines_.push_back(frame);
    if (painted > maxWidth)
      maxWidth = painted;
    y += height;
  }
  this->result_.commit(availableWidth, loka::core::Frame(0, 0, maxWidth, result.height()));
}

namespace
{
  int failMeasureCount = 0;
}
void loka::app::testing::NullTextMeasurementAccess::failMeasure(int count)
{
  failMeasureCount = count;
}
bool RefuseNullTextMeasurement()
{
  return failMeasureCount > 0 && --failMeasureCount == 0;
}
