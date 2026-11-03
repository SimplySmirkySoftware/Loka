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

  void apply(const ToolboxTextSelection &selection)
  {
    TextFont(selection.font == 0 ? GetSysFont() : selection.font);
    TextSize(selection.size);
    TextFace(selection.face);
  }
} // namespace

ToolboxTextPortFont ToolboxTextPortFont::read(GrafPtr port)
{
  return ToolboxTextPortFont(port ? port->txFont : 0, port ? port->txSize : 0, port ? port->txFace : 0);
}

ToolboxTextSelection ToolboxTextSelection::resolve(const ToolboxTextFontDescriptor &descriptor,
                                                   const ToolboxTextPortFont &port)
{
  return ToolboxTextSelection(
      descriptor.font(port.font), ResolvedFontSize(descriptor.size(port.size)), descriptor.face(port.face));
}

ToolboxTextMeasureScope::ToolboxTextMeasureScope(const ToolboxScenePlatformController &controller,
                                                 const ToolboxTextFontDescriptor &descriptor)
    : previousPort_(0),
      measurePort_(controller.window_ ? reinterpret_cast<GrafPtr>(controller.window_->window()) : 0),
      original_(ToolboxTextPortFont::read(this->measurePort_)),
      busy_(this->measurePort_
                    && ToolboxTextSelection::resolve(descriptor, this->original_).size
                           != ToolboxTextSelection::resolve(ToolboxTextFontDescriptor(), this->original_).size
                ? controller.cursorOwner()
                : 0)
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
    TextFont(this->original_.font);
    TextSize(this->original_.size);
    TextFace(this->original_.face);
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
  apply(ToolboxTextSelection::resolve(descriptor, this->original_));
}

bool ToolboxTextMeasureScope::sameFont(const ToolboxTextFontDescriptor &a,
                                       const ToolboxTextFontDescriptor &b) const
{
  return ToolboxTextSelection::resolve(a, this->original_) == ToolboxTextSelection::resolve(b, this->original_);
}

namespace
{
  bool NeedsBusy(const ToolboxTextPortFont &original, const ToolboxTextFontDescriptor *descriptors, std::size_t count)
  {
    const short baseline = ToolboxTextSelection::resolve(ToolboxTextFontDescriptor(), original).size;
    short largest = count ? ToolboxTextSelection::resolve(descriptors[0], original).size : baseline;
    for (std::size_t i = 1; i < count; ++i)
    {
      const short size = ToolboxTextSelection::resolve(descriptors[i], original).size;
      if (size > largest)
        largest = size;
    }
    return largest != baseline;
  }
}

ToolboxTextMeasureScope::ToolboxTextMeasureScope(const ToolboxScenePlatformController &controller,
                                                 const ToolboxTextFontDescriptor *descriptors,
                                                 std::size_t count)
    : previousPort_(0),
      measurePort_(controller.window_ ? reinterpret_cast<GrafPtr>(controller.window_->window()) : 0),
      original_(ToolboxTextPortFont::read(this->measurePort_)),
      busy_(this->measurePort_ && NeedsBusy(this->original_, descriptors, count) ? controller.cursorOwner() : 0)
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
