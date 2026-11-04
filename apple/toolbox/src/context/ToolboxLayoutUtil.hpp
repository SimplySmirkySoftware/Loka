#ifndef LOKA_TOOLBOX_LAYOUT_UTIL_HPP
#define LOKA_TOOLBOX_LAYOUT_UTIL_HPP

#include "core/String.hpp"
#include "app/style/Style.hpp"
#include "ToolboxBusy.hpp"
#include <Quickdraw.h>
#include <Fonts.h>

class ToolboxScenePlatformController;

/** Text style projected onto the owning port, or the role's family and size.
    Unset fields inherit the port; explicit face fields replace only their bit. */
class ToolboxTextFontDescriptor
{
public:
  ToolboxTextFontDescriptor() : style_() {}
  explicit ToolboxTextFontDescriptor(const loka::app::TextStyle &style) : style_(style) {}

  short font(short portFont) const
  {
    short size = 0;
    this->resolve(portFont, size);
    return portFont;
  }
  short size(short portSize) const
  {
    short font = 0;
    this->resolve(font, portSize);
    return portSize;
  }

  Style face(Style portFace) const
  {
    loka::app::TextWeight weight = loka::app::TEXT_WEIGHT_NORMAL;
    // Declared normal clears an inherited bold.
    if (this->style_.queryWeight(weight))
      portFace = static_cast<Style>((portFace & ~bold) | (weight == loka::app::TEXT_WEIGHT_BOLD ? bold : 0));
    bool on = false;
    if (this->style_.queryItalic(on))
      portFace = static_cast<Style>((portFace & ~italic) | (on ? italic : 0));
    if (this->style_.queryUnderline(on))
      portFace = static_cast<Style>((portFace & ~underline) | (on ? underline : 0));
    return portFace;
  }

private:
  void resolve(short &font, short &size) const
  {
    int points = 0;
    if (this->style_.size_.queryPoints(points))
    {
      size = static_cast<short>(loka::app::detail::SnapTextSizePoints(points));
      return;
    }
    loka::app::TextSizeRole role;
    if (this->style_.size_.queryRole(role))
    {
      switch (role)
      {
      case loka::app::TEXT_SIZE_ROLE_SMALL_LABEL:
        font = kFontIDGeneva;
        size = 9;
        return;
      case loka::app::TEXT_SIZE_ROLE_COUNT:
        break;
      }
    }
  }

  loka::app::TextStyle style_;
};

/** Original window-port font fields, including unresolved zero defaults. */
class ToolboxTextPortFont
{
public:
  static ToolboxTextPortFont read(GrafPtr port);
  const short font;
  const short size;
  const Style face;

private:
  ToolboxTextPortFont(short font, short size, Style face)
      : font(font),
        size(size),
        face(face)
  {
  }
};

/** Resolved text settings. Font zero stays unmapped for selection equality. */
class ToolboxTextSelection
{
public:
  static ToolboxTextSelection resolve(const ToolboxTextFontDescriptor &descriptor, const ToolboxTextPortFont &port);
  bool operator==(const ToolboxTextSelection &other) const
  {
    return this->font == other.font && this->size == other.size && this->face == other.face;
  }
  short nativeFont() const
  {
    return this->font == 0 ? GetSysFont() : this->font;
  }
  const short font;
  const short size;
  const Style face;

private:
  ToolboxTextSelection(short font, short size, Style face)
      : font(font),
        size(size),
        face(face)
  {
  }
};

/** Window-bound text measurement and painting transaction.

    The controller supplies the window owner. The scope restores both the
    window port's font state and whichever GrafPort the caller had selected.
    Measurement reuse assumes this window's port font/size/face and system
    default font/size remain frozen between input marks. Scoped selection is
    restored; ambient font changes require a new input mark before layout.
    Native transactions at non-default sizes borrow the app cursor through
    selection, measurement, drawing and restoration, regardless of font family.
    Character-width cache hits open no native transaction and borrow no cursor. */
class ToolboxTextMeasureScope
{
public:
  ToolboxTextMeasureScope(
      const ToolboxScenePlatformController &controller,
      const ToolboxTextFontDescriptor &descriptor = ToolboxTextFontDescriptor());
  ToolboxTextMeasureScope(const ToolboxScenePlatformController &controller, const ToolboxTextSelection &selection);
  ~ToolboxTextMeasureScope();
  ToolboxTextMeasureScope(const ToolboxScenePlatformController &controller,
                          const ToolboxTextFontDescriptor *descriptors, std::size_t count);
  short measure(const loka::core::String &value) const;
  /** Refuses absent ports and failed conversion; zero width can be success. */
  bool measure(const loka::core::String &value, short &width) const;
  /** One-line pitch and baseline for the selected font; no layout state is read. */
  bool queryLineMetrics(long &pitch, short &baseline) const;
  bool valid() const { return this->measurePort_ != 0; }
  /** Check the current port and native projection, without retaining another selection. */
  bool applies(const ToolboxTextSelection &selection) const;
  /** Select against the original port, never the preceding run. */
  void select(const ToolboxTextFontDescriptor &descriptor) const;
  /** Compare the actual font settings inherited from this transaction's port. */
  bool sameFont(const ToolboxTextFontDescriptor &a, const ToolboxTextFontDescriptor &b) const;

private:
  ToolboxTextMeasureScope(const ToolboxTextMeasureScope &);
  ToolboxTextMeasureScope &operator=(const ToolboxTextMeasureScope &);

  void open(const ToolboxTextSelection &selection) const;
  GrafPtr previousPort_;
  GrafPtr measurePort_;
  const ToolboxTextPortFont original_;
  BusyScope busy_;
};

#endif // LOKA_TOOLBOX_LAYOUT_UTIL_HPP
