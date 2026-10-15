#ifndef LOKA_APP_LAYOUT_FRAME_GEOMETRY_HPP
#define LOKA_APP_LAYOUT_FRAME_GEOMETRY_HPP

namespace loka
{
  namespace app
  {
    namespace layout
    {
      /** Closed vocabulary for a framed container's decorative leaves. */
      enum FramePlacement
      {
        FRAME_PLACEMENT_RIBBON_GROUP_EDGES,
        FRAME_PLACEMENT_CARD_BANDS
      };

      /** Completed per-side content allowances; Box supplies the symmetric form. */
      struct FrameInsets
      {
        const int left, top, right, bottom;
        explicit FrameInsets(int symmetric)
            : left(symmetric),
              top(symmetric),
              right(symmetric),
              bottom(symmetric)
        {
        }
        FrameInsets(int l, int t, int r, int b)
            : left(l),
              top(t),
              right(r),
              bottom(b)
        {
        }
      };

      /** Completed frame policy and geometry; owns no runtime state. */
      struct FrameGeometry
      {
        const FramePlacement placement;
        const FrameInsets insets;
        const int minimumWidth;
        const int minimumHeight;
        FrameGeometry(FramePlacement value, int contentInset)
            : placement(value),
              insets(contentInset),
              minimumWidth(0),
              minimumHeight(0)
        {
        }
        FrameGeometry(FramePlacement value, const FrameInsets &contentInsets, int width, int height)
            : placement(value),
              insets(contentInsets),
              minimumWidth(width),
              minimumHeight(height)
        {
        }
      };
    } // namespace layout
  } // namespace app
} // namespace loka

#endif
