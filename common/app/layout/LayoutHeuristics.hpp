#ifndef LOKA_APP_LAYOUT_HEURISTICS_HPP
#define LOKA_APP_LAYOUT_HEURISTICS_HPP

#include "app/RectSurface.hpp"
#include "app/layout/NaturalSizeSource.hpp"
#include "app/layout/RailSpacing.hpp"
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
      /** Shape-only classification; independent of rail handler registration. */
      bool isLayoutPassThrough(scene::Node *node);
      /** Resolves child spacing from own props, or carries it through a transparent node.
          Shape-only: no descendant walk. */
      int resolveLayoutGap(scene::Node *node, int received, const RailSpacing &spacing);
      /** Dialogs and pass-through subtrees without a seat consume no layout gap. */
      bool childTakesLayoutSeat(scene::Node *child);

      /** Completed seat shape: no seat, one borrowed terminal, or at least two leaves.
          The pointer is present only for ONE_LEAF; this value neither owns nor retains it. */
      class SeatLeaves
      {
      public:
        enum Kind
        {
          NO_SEAT,
          ONE_LEAF,
          MANY_LEAVES
        };

        explicit SeatLeaves(scene::Node *leaf = 0)
            : kind_(leaf ? ONE_LEAF : NO_SEAT),
              leaf_(leaf)
        {
        }

        static SeatLeaves many()
        {
          return SeatLeaves(MANY_LEAVES);
        }
        Kind kind() const
        {
          return this->kind_;
        }
        scene::Node *leaf() const
        {
          return this->leaf_;
        }

      private:
        explicit SeatLeaves(Kind kind)
            : kind_(kind),
              leaf_(0)
        {
        }
        Kind kind_;
        scene::Node *leaf_;
      };

      /** Walk transparent descendants once, stopping at terminals and the second live seat.
          O(visited nodes) time, O(wrapper depth) stack; reads the child's composition only. */
      SeatLeaves singleLiveLeaf(scene::Node *seat);

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
          const int width = box->props.effectiveWidth();
          if (width > 0)
          {
            return width;
          }
        }
        return -1;
      }

      inline int preferredChildWidthForColumn(loka::app::scene::Node *child, int availableWidth)
      {
        const int declared = declaredChildWidthForColumn(child);
        return clampToAvailable(declared < 0 ? availableWidth : declared, availableWidth);
      }

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

      /** Pass-local front-first division, also used as a natural-width cap.
          A negative cap preserves the natural answer. */
      class RowWidthDivision
      {
      public:
        explicit RowWidthDivision(int cap = -1, int remainder = 0)
            : cap_(cap),
              remainder_(remainder)
        {
        }
        int take(int natural)
        {
          if (this->cap_ < 0 || natural <= this->cap_)
            return natural;
          int width = this->cap_;
          if (this->remainder_ > 0)
          {
            ++width;
            --this->remainder_;
          }
          return width;
        }

      private:
        int cap_;
        int remainder_;
      };

      /** Answers the maximum offer-independent width within the parent's composed child count,
          treating children as vertically stacked ScrollView content and recursing into Columns.
          ScrollView calls once per range pass. LABELS_SHARE Rows classify each live seat once:
          labels count one without width consultation, empty Boxes count known zero, and other
          undeclared seats are measured once. Empty Rows and refused measurements decline.
          NATURAL Rows count fully shrunk seats after checking the unshrunk sum for saturation.
          Frames count content overflow plus insets, at least their minimum width;
          rail spacing is forwarded unchanged; each Row resolves its own gap, and a null parent or
         offer-bound children answer zero. Unavailable measurements or results above SHRT_MAX refuse without changing
         width. */
      bool queryContentOverflowWidth(scene::Node *parent,
                                     const RailSpacing &spacing,
                                     const INaturalSizeSource *source,
                                     short &width);

      /** Owns one Row pass's flex remainder and live-seat progression: NATURAL rows consult stable
          leaf answers (including intrinsic images) at most twice when the Row fits. A short Row
          adds up to k constructor scans, k being the number of shrinkable seats; each scan consults
          every single-line ELLIPSIS Text seat once, including one whose answer is declined, because
          nothing is remembered between scans. SHARED rows never consult them or classify seat shapes.
          NATURAL classifies only LABEL after a positive undeclared natural answer; nested natural
          measurement needs no shape. Water-filling retains its existing pre-measurement eligibility scan.
          Row layout constructs one consultation per pass. LABELS_SHARE classifies each live
          child once in construction and once in next; labels and empty Boxes are never measured,
          other undeclared seats at most twice. Labels reserve one before sharing the remainder.
          The resolved gap allocates this Row; rail spacing resolves named values in nested Rows.
          Private recursive natural measurement walks each Stack's subtree without caching to find
          the smallest seating width, independently of ScrollView's offer-bound overflow rule. */
      class RowWidthConsultation
      {
      public:
        RowWidthConsultation(loka::app::scene::Node *childrenHead,
                             size_t childCount,
                             int availableWidth,
                             int gap,
                             const RailSpacing &spacing,
                             RowUndeclaredWidth policy,
                             const INaturalSizeSource *source);

        RowChildWidth next(loka::app::scene::Node *child);

      private:
        const RowUndeclaredWidth policy_;
        const INaturalSizeSource *source_;
        const RailSpacing spacing_;
        RowWidthDivision flex_;
        RowWidthDivision shrink_;
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

      /** Aligned Row height table; transparent wrappers expose exactly one live leaf. */
      inline int preferredChildHeightForRow(loka::app::scene::Node *child,
                                            int fallbackHeight,
                                            int buttonHeight,
                                            int editTextHeight,
                                            int popupMenuHeight,
                                            int textHeight,
                                            int imageFallbackHeight,
                                            const INaturalSizeSource *source)
      {
        if (!child)
        {
          return fallbackHeight;
        }
        if (isLayoutPassThrough(child))
        {
          const SeatLeaves leaves = singleLiveLeaf(child);
          if (leaves.kind() == SeatLeaves::NO_SEAT)
            return 0;
          if (leaves.kind() == SeatLeaves::MANY_LEAVES)
            return fallbackHeight;
          child = leaves.leaf();
        }
        if (!childTakesLayoutSeat(child))
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
          short height = 0;
          if (source && source->queryNaturalHeight(child, height))
            return height;
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
        if (loka::app::BoxNode *box = child->asBoxNode())
        {
          if (box->props.height > 0)
          {
            return box->props.height;
          }
          return fallbackHeight;
        }
        return fallbackHeight;
      }
    } // namespace layout
  } // namespace app
} // namespace loka

#endif // LOKA_APP_LAYOUT_HEURISTICS_HPP
