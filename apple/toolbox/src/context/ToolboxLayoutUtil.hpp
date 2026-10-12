#ifndef LOKA_TOOLBOX_LAYOUT_UTIL_HPP
#define LOKA_TOOLBOX_LAYOUT_UTIL_HPP

#include "core/String.hpp"
#include "app/style/Style.hpp"
#include "ToolboxApp.hpp"
#include <Quickdraw.h>

class ToolboxScenePlatformController;

/** Resolved Text style projected onto the owning port's font family.
    Unset fields inherit the port; explicit face fields replace only their bit. */
class ToolboxTextFontDescriptor
{
public:
  ToolboxTextFontDescriptor() : style_() {}
  explicit ToolboxTextFontDescriptor(const loka::app::TextStyle &style) : style_(style) {}

  short font(short portFont) const
  {
    return portFont;
  }
  short size(short portSize) const
  {
    int points = 0;
    if (!this->style_.size_.queryPoints(points))
      return portSize;
    loka::app::SizeOf(points).size_.queryPoints(points);
    return static_cast<short>(points);
  }
  Style face(Style portFace) const
  {
    if (this->style_.hasWeight_)
      portFace = static_cast<Style>((portFace & ~bold)
          | (this->style_.weight_ == loka::app::TEXT_WEIGHT_BOLD ? bold : 0));
    if (this->style_.hasItalic_)
      portFace = static_cast<Style>((portFace & ~italic) | (this->style_.italic_ ? italic : 0));
    return portFace;
  }

private:
  loka::app::TextStyle style_;
};

/** Window-bound text measurement and painting transaction.

    The controller supplies the window owner. The scope restores both the
    window port's font state and whichever GrafPort the caller had selected.
    Measurement reuse assumes this window's port font/size/face and system
    default font/size remain frozen between input marks. Scoped selection is
    restored; ambient font changes require a new input mark before layout.
    Non-default sizes borrow the app cursor through selection, measurement,
    drawing and restoration, on every transaction regardless of font family. */
class ToolboxTextMeasureScope
{
public:
  ToolboxTextMeasureScope(
      const ToolboxScenePlatformController &controller,
      const ToolboxTextFontDescriptor &descriptor = ToolboxTextFontDescriptor());
  ~ToolboxTextMeasureScope();
  ToolboxTextMeasureScope(const ToolboxScenePlatformController &controller,
                          const ToolboxTextFontDescriptor *descriptors, std::size_t count);
  short measure(const loka::core::String &value) const;
  /** Refuses absent ports and failed conversion; zero width can be success. */
  bool measure(const loka::core::String &value, short &width) const;
  bool valid() const { return this->measurePort_ != 0; }
  /** Select against the original port, never the preceding run. */
  void select(const ToolboxTextFontDescriptor &descriptor) const;
  /** Compare the actual font settings inherited from this transaction's port. */
  bool sameFont(const ToolboxTextFontDescriptor &a, const ToolboxTextFontDescriptor &b) const;

private:
  ToolboxTextMeasureScope(const ToolboxTextMeasureScope &);
  ToolboxTextMeasureScope &operator=(const ToolboxTextMeasureScope &);

  GrafPtr previousPort_;
  GrafPtr measurePort_;
  short previousFont_;
  short previousSize_;
  Style previousFace_;
  BusyScope busy_;
};

#endif // LOKA_TOOLBOX_LAYOUT_UTIL_HPP
