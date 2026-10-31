#include "platform/ToolboxPascalText.hpp"
#include "context/ToolboxLayoutUtil.hpp"

#include "ToolboxScenePlatformController.hpp"
#include "ToolboxWindow.hpp"
#include "platform/StringUTF8.hpp"
#include <Fonts.h>
#include <cstring>
#include <string>

namespace
{
  short ResolvedFontSize(short size)
  {
    return size == 0 ? GetDefFontSize() : size;
  }
}

ToolboxTextMeasureScope::ToolboxTextMeasureScope(
    const ToolboxScenePlatformController &controller,
    const ToolboxTextFontDescriptor &descriptor)
    : previousPort_(0),
      measurePort_(controller.window_ ? reinterpret_cast<GrafPtr>(controller.window_->window()) : 0),
      previousFont_(this->measurePort_ ? this->measurePort_->txFont : 0),
      previousSize_(this->measurePort_ ? this->measurePort_->txSize : 0),
      previousFace_(this->measurePort_ ? this->measurePort_->txFace : 0),
      busy_(this->measurePort_
                && ResolvedFontSize(descriptor.size(this->previousSize_)) != ResolvedFontSize(this->previousSize_)
            ? controller.cursorOwner() : 0)
{
  GetPort(&this->previousPort_);
  if (!this->measurePort_)
  {
    return;
  }

  SetPort(this->measurePort_);
  this->select(descriptor);
}

ToolboxTextMeasureScope::~ToolboxTextMeasureScope()
{
  if (this->measurePort_)
  {
    SetPort(this->measurePort_);
    TextFont(this->previousFont_);
    TextSize(this->previousSize_);
    TextFace(this->previousFace_);
  }
  SetPort(this->previousPort_);
}

short ToolboxTextMeasureScope::measure(const loka::core::String &value) const
{
  short width = 0;
  this->measure(value, width);
  return width;
}

bool ToolboxTextMeasureScope::measure(const loka::core::String &value, short &width) const
{
  if (!this->valid())
    return false;
  Str255 text;
  if (!ToolboxEncodePascal(value, text))
    return false;
  width = StringWidth(text);
  return true;
}

void ToolboxTextMeasureScope::select(const ToolboxTextFontDescriptor &descriptor) const
{
  if (!this->measurePort_)
    return;
  const short font = descriptor.font(this->previousFont_);
  TextFont(font == 0 ? GetSysFont() : font);
  TextSize(ResolvedFontSize(descriptor.size(this->previousSize_)));
  TextFace(descriptor.face(this->previousFace_));
}

bool ToolboxTextMeasureScope::sameFont(const ToolboxTextFontDescriptor &a,
                                       const ToolboxTextFontDescriptor &b) const
{
  return a.font(this->previousFont_) == b.font(this->previousFont_)
         && ResolvedFontSize(a.size(this->previousSize_)) == ResolvedFontSize(b.size(this->previousSize_))
         && a.face(this->previousFace_) == b.face(this->previousFace_);
}

namespace
{
  bool NeedsBusy(GrafPtr port, const ToolboxTextFontDescriptor *descriptors, std::size_t count)
  {
    if (!port)
      return false;
    short largest = count ? ResolvedFontSize(descriptors[0].size(port->txSize)) : ResolvedFontSize(port->txSize);
    for (std::size_t i = 1; i < count; ++i)
    {
      const short size = ResolvedFontSize(descriptors[i].size(port->txSize));
      if (size > largest)
        largest = size;
    }
    return largest != ResolvedFontSize(port->txSize);
  }
}

ToolboxTextMeasureScope::ToolboxTextMeasureScope(
    const ToolboxScenePlatformController &controller,
    const ToolboxTextFontDescriptor *descriptors, std::size_t count)
    : previousPort_(0),
      measurePort_(controller.window_ ? reinterpret_cast<GrafPtr>(controller.window_->window()) : 0),
      previousFont_(this->measurePort_ ? this->measurePort_->txFont : 0),
      previousSize_(this->measurePort_ ? this->measurePort_->txSize : 0),
      previousFace_(this->measurePort_ ? this->measurePort_->txFace : 0),
      busy_(NeedsBusy(this->measurePort_, descriptors, count) ? controller.cursorOwner() : 0)
{
  GetPort(&this->previousPort_);
  if (this->measurePort_)
  {
    SetPort(this->measurePort_);
  }
}

bool ToolboxTextMeasureScope::queryLineMetrics(long &pitch, short &baseline) const
{
  if (!this->valid())
    return false;
  FontInfo info;
  GetFontInfo(&info);
  pitch = static_cast<long>(info.ascent) + info.descent + info.leading;
  baseline = info.ascent;
  return true;
}
