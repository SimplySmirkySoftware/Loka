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
        FRAME_PLACEMENT_RIBBON_GROUP_EDGES
      };

      /** Completed placement and symmetric content inset; owns no runtime state. */
      struct FrameGeometry
      {
        const FramePlacement placement;
        const int inset;
        FrameGeometry(FramePlacement value, int contentInset)
            : placement(value),
              inset(contentInset)
        {
        }
      };
    } // namespace layout
  } // namespace app
} // namespace loka

#endif
