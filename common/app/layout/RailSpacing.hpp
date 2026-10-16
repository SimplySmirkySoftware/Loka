#ifndef LOKA_APP_LAYOUT_RAIL_SPACING_HPP
#define LOKA_APP_LAYOUT_RAIL_SPACING_HPP

namespace loka
{
  namespace app
  {
    namespace layout
    {
      /** Immutable layout defaults for one rail, independent of projection ratios. */
      class RailSpacing
      {
      public:
        RailSpacing(int verticalGap, int horizontalGap, int standardWindowMargin)
            : verticalGap_(verticalGap),
              horizontalGap_(horizontalGap),
              standardWindowMargin_(standardWindowMargin)
        {
        }
        int verticalGap() const
        {
          return this->verticalGap_;
        }
        int horizontalGap() const
        {
          return this->horizontalGap_;
        }
        int standardWindowMargin() const
        {
          return this->standardWindowMargin_;
        }

      private:
        const int verticalGap_;
        const int horizontalGap_;
        const int standardWindowMargin_;
      };
    } // namespace layout
  } // namespace app
} // namespace loka
#endif
