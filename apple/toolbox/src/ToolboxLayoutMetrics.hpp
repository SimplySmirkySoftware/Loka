#ifndef LOKA_TOOLBOX_LAYOUT_METRICS_HPP
#define LOKA_TOOLBOX_LAYOUT_METRICS_HPP

/** Toolbox-native geometry shared by layout projections and control painters.
    Each member has its out-of-class definition in
    ToolboxPlatformLayoutHandlers.cpp; a new member adds one there too. */
struct ToolboxLayoutMetrics
{
  /** Both axes use the same native container gap. */
  static const short kDefaultGap = 8;
  static const short kDefaultLineHeight = 12;
  static const short kImageFallbackHeight = 80;
  /** LayoutState.y is the top edge of the node's box on every rail;
   * baseline nodes draw their text at y + lineHeight - kControlAscentInset. */
  static const short kControlAscentInset = 2;
  static const short kControlDescent = 6;
  /** Horizontal title inset on each side of a native push button. */
  static const short kPushButtonTitleInset = 8;
  static const short kEditTextDescent = 8;
};

#endif // LOKA_TOOLBOX_LAYOUT_METRICS_HPP
