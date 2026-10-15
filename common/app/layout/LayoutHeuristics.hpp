#ifndef LOKA_APP_LAYOUT_HEURISTICS_HPP
#define LOKA_APP_LAYOUT_HEURISTICS_HPP

#include "app/RectSurface.hpp"
#include "app/layout/NaturalWidthSource.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/scene/Node.hpp"

namespace loka
{
  namespace app
  {
    namespace layout
    {
      /** Converts an intermediate layout calculation to the target's
          declared coordinate width at the assignment boundary. */
      template <typename LayoutStateT>
      inline typename LayoutStateT::Coordinate layoutCoordinate(int value)
      {
        return static_cast<typename LayoutStateT::Coordinate>(value);
      }

      inline int clampToAvailable(int value, int available)
      {
        if (value < 0)
        {
          return 0;
        }
        if (available >= 0 && value > available)
        {
          return available;
        }
        return value;
      }

      /** Unclamped Column width claim, or a negative value when undeclared. */
      inline int declaredChildWidthForColumn(loka::app::scene::Node *child)
      {
        if (!child)
        {
          return -1;
        }
        if (loka::app::ImageViewNode *image = child->asImageViewNode())
        {
          if (image->props.width_ > 0)
          {
            return image->props.width_;
          }
        }
        if (loka::app::RectSurfaceNode *surface = child->asRectSurfaceNode())
        {
          if (surface->props.width_ > 0)
          {
            return surface->props.width_;
          }
        }
        if (loka::app::BoxNode *box = child->asBoxNode())
        {
          if (box->props.hasFixedSize())
          {
            return box->props.effectiveWidth();
          }
        }
        return -1;
      }

      inline int preferredChildWidthForColumn(loka::app::scene::Node *child, int availableWidth)
      {
        const int declared = declaredChildWidthForColumn(child);
        return clampToAvailable(declared < 0 ? availableWidth : declared, availableWidth);
      }

      /** Nestable without its own container or projected layout. */
      bool isLayoutPassThrough(loka::app::scene::Node *node);

      /** Whether a child occupies a container seat, independently of its size. */
      bool childTakesLayoutSeat(loka::app::scene::Node *child);

      /** Whether a seat is a container, recursively through pass-through nodes. */
      bool isContainerSeat(loka::app::scene::Node *child);

      /** Gap before child: none without two seats, group beside a container,
          related between leaves. Walks only pass-through descendants of the
          two borrowed neighbours during the caller's layout pass. */
      int tieredGap(scene::Node *previousSeat, scene::Node *child, int related, int group);

      /** Returns a Row child's fixed-width claim, a negative value for a flex
          seat, or zero for an empty Fragment that consumes no seat. A
          single-child Fragment forwards the child's answer so branch seats
          preserve the active arm's claim. A Box claims its positive declared
          width independently of height; the Row owns the child's height. The
          declared claim is never clamped to the row: the child lays out at its
          declared width, so shrinking only the seat would overlap the next
          sibling. */
      int preferredChildWidthForRow(loka::app::scene::Node *child);

      /** Immutable allocation for one child in a consulted Row pass. */
      struct RowChildWidth
      {
        RowChildWidth(int widthValue, bool liveSeatValue, bool gapBeforeValue)
            : width_(widthValue),
              liveSeat_(liveSeatValue),
              gapBefore_(gapBeforeValue)
        {
        }

        int width() const { return this->width_; }
        bool isLiveSeat() const { return this->liveSeat_; }
        bool hasGapBefore() const { return this->gapBefore_; }

      private:
        int width_;
        bool liveSeat_;
        bool gapBefore_;
      };

      /** Owns one Row pass's flex remainder and live-seat progression.
          NATURAL rows query each eligible child at most twice: once while
          totalling, once in next(). SHARED rows never query the source.
          Stacks compose the smallest width that seats each child at its own width
          under their layout rules; only leaves reach the source. Each stack ask
          walks its own subtree without caching. Source answers and child claims
          must stay stable for the whole pass. */
      class RowWidthConsultation
      {
      public:
        RowWidthConsultation(loka::app::scene::Node *childrenHead, size_t childCount, int availableWidth, int gap,
                             RowUndeclaredWidth policy, const INaturalWidthSource *source);

        RowChildWidth next(loka::app::scene::Node *child);

      private:
        int preferredWidth(loka::app::scene::Node *child) const;
        const INaturalWidthSource *source_;
        int gap_;
        int baseFlexWidth_;
        int flexRemainder_;
        int liveSeatsSeen_;
      };

      inline int remainingChildHeightForColumn(int parentHeight, int parentStartY, int currentY)
      {
        if (parentHeight <= 0)
        {
          return parentHeight;
        }
        const int usedHeight = currentY - parentStartY;
        int remainingHeight = parentHeight - usedHeight;
        if (remainingHeight < 0)
        {
          remainingHeight = 0;
        }
        return remainingHeight;
      }

      inline int preferredChildHeightForRow(loka::app::scene::Node *child,
                                            int fallbackHeight,
                                            int buttonHeight,
                                            int editTextHeight,
                                            int popupMenuHeight,
                                            int textHeight,
                                            int imageFallbackHeight)
      {
        if (!child)
        {
          return fallbackHeight;
        }
        if (child->asOpenFileDialogNode())
        {
          // Seats nothing (see preferredChildWidthForRow): it must not raise
          // an aligned Row's height either.
          return 0;
        }
        if (child->asButtonNode())
        {
          return buttonHeight;
        }
        if (child->asEditTextNode())
        {
          return editTextHeight;
        }
        if (child->asPopupMenuNode())
        {
          return popupMenuHeight;
        }
        if (child->asTextNode())
        {
          return textHeight;
        }
        if (loka::app::ImageViewNode *image = child->asImageViewNode())
        {
          if (image->props.height_ > 0)
          {
            return image->props.height_;
          }
          if (fallbackHeight > 0)
          {
            return fallbackHeight;
          }
          return imageFallbackHeight;
        }
        if (loka::app::RectSurfaceNode *surface = child->asRectSurfaceNode())
        {
          if (surface->props.height_ > 0)
          {
            return surface->props.height_;
          }
          return fallbackHeight;
        }
        return fallbackHeight;
      }
    } // namespace layout
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_HEURISTICS_HPP
