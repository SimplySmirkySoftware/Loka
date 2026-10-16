#ifndef LOKA_APP_LAYOUT_FALLBACK_CONTROL_METRICS_HPP
#define LOKA_APP_LAYOUT_FALLBACK_CONTROL_METRICS_HPP

#include "app/layout/RowLayout.hpp"

namespace loka
{
  namespace app
  {
    namespace layout
    {
      /** Cross-platform fallback sizes used when native layout has no measured value. */
      struct FallbackControlMetrics
      {
        enum
        {
          kButtonHeight = 32,
          kEditTextHeight = 24,
          kPopupMenuHeight = 26,
          kTextHeight = 20,
          kCellHeight = 20,
          kVerticalSpacing = 12,
          kHorizontalSpacing = 12,
          kImageFallbackHeight = 160
        };

        static RowLayoutMetrics rowLayout()
        {
          RowLayoutMetrics metrics;
          metrics.fallbackHeight = kTextHeight;
          metrics.buttonHeight = kButtonHeight;
          metrics.editTextHeight = kEditTextHeight;
          metrics.popupMenuHeight = kPopupMenuHeight;
          metrics.textHeight = kTextHeight;
          metrics.imageFallbackHeight = kImageFallbackHeight;
          return metrics;
        }
      };
    } // namespace layout
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_FALLBACK_CONTROL_METRICS_HPP
