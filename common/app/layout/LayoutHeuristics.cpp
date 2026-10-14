#include "app/layout/LayoutHeuristics.hpp"

#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "dsl/composition/CompositionList.hpp"
#include <climits>

namespace loka
{
  namespace app
  {
    namespace layout
    {
      int preferredChildWidthForRow(loka::app::scene::Node *child)
      {
        if (!child)
        {
          return -1;
        }
        if (child->propsTypeId() == loka::app::FragmentProps::staticTypeId())
        {
          loka::app::scene::INestable *fragment = child->asNestable();
          if (!fragment || fragment->childrenCount() == 0)
          {
            return 0;
          }
          if (fragment->childrenCount() == 1)
          {
            return preferredChildWidthForRow(fragment->childrenHead());
          }
          return -1;
        }
        if (child->asOpenFileDialogNode())
        {
          // A dialog never paints in the Row: it is a materialized resource
          // (the Show that opens it is a node while the dialog is up), not a
          // seat. Like an empty Fragment it takes neither width nor a gap,
          // so opening it leaves the visual siblings' allocation unchanged
          // (#588).
          return 0;
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
          if (box->props.effectiveWidth() > 0)
          {
            return box->props.effectiveWidth();
          }
        }
        return -1;
      }

      namespace
      {
        /** Pass-local seat classification and spacing shared by allocation and measurement. */
        class RowWidthTally
        {
        public:
          RowWidthTally()
              : fixed_(0),
                live_(0),
                flex_(0)
          {
          }
          void add(int width)
          {
            if (width == 0)
              return;
            ++this->live_;
            if (width < 0)
              ++this->flex_;
            else
              this->fixed_ += width;
          }
          long occupied(int gap) const
          {
            return this->fixed_ + gap * (this->live_ > 0 ? this->live_ - 1 : 0);
          }
          long flexSeats() const
          {
            return this->flex_;
          }

        private:
          long fixed_;
          long live_;
          long flex_;
        };

        bool queryNaturalWidth(scene::Node *child, int gap, const INaturalWidthSource *source, short &width)
        {
          if (RibbonGroupNode *group = child->asRibbonGroupNode())
          {
            short contentWidth = 0;
            if (!group->childrenHead() || !queryNaturalWidth(group->childrenHead(), gap, source, contentWidth))
              return false;
            const long answer = static_cast<long>(contentWidth) + 2 * RibbonGroupNode::kFrameInset;
            if (answer > SHRT_MAX)
              return false;
            width = static_cast<short>(answer);
            return true;
          }
          StackNode *stack = child->asStackNode();
          if (!stack)
            return source && source->queryNaturalWidth(child, width) && width > 0;

          long answer = 0;
          RowWidthTally tally;
          int maximum = 0;
          loka::dsl::CompositionCursor<scene::Node> children(stack->childrenHead(), stack->childrenCount());
          switch (stack->props.effectiveAxis())
          {
          case STACK_AXIS_ROW:
            for (scene::Node *seat = children.next(); seat; seat = children.next())
            {
              const int declared = preferredChildWidthForRow(seat);
              int resolved = declared;
              if (declared < 0)
              {
                short natural = 0;
                if (!queryNaturalWidth(seat, gap, source, natural))
                  return false;
                resolved = natural;
                if (natural > maximum)
                  maximum = natural;
              }
              tally.add(stack->props.rowUndeclaredWidth_ == ROW_UNDECLARED_WIDTH_NATURAL ? resolved : declared);
            }
            answer = tally.occupied(gap) + tally.flexSeats() * maximum;
            break;
          case STACK_AXIS_COLUMN:
            for (scene::Node *seat = children.next(); seat; seat = children.next())
            {
              int resolved = declaredChildWidthForColumn(seat);
              if (resolved < 0)
              {
                short natural = 0;
                if (!queryNaturalWidth(seat, gap, source, natural))
                  return false;
                resolved = natural;
              }
              if (resolved > answer)
                answer = resolved;
            }
            break;
          }
          if (answer <= 0 || answer > SHRT_MAX)
            return false;
          width = static_cast<short>(answer);
          return true;
        }
      } // namespace

      bool queryContentOverflowWidth(scene::Node *parent, int gap, const INaturalWidthSource *source, short &width)
      {
        int maximum = 0;
        scene::INestable *content = parent ? parent->asNestable() : 0;
        loka::dsl::CompositionCursor<scene::Node> children(content ? content->childrenHead() : 0,
                                                           content ? content->childrenCount() : 0);
        for (scene::Node *child = children.next(); child; child = children.next())
        {
          int resolved = declaredChildWidthForColumn(child);
          if (resolved < 0)
          {
            short measured = 0;
            StackNode *stack = child->asStackNode();
            ImageViewNode *image = child->asImageViewNode();
            if (stack && stack->props.effectiveAxis() == STACK_AXIS_COLUMN)
            {
              if (!queryContentOverflowWidth(stack, gap, source, measured))
                return false;
            }
            else if ((stack && stack->props.rowUndeclaredWidth_ == ROW_UNDECLARED_WIDTH_NATURAL)
                     || child->asRibbonGroupNode()
                     || (image && image->props.hasAttr_ && image->props.attr_.hasSizePolicyValue_
                         && image->props.attr_.sizePolicyValue_ == IMAGE_VIEW_SIZE_INTRINSIC))
            {
              if (!queryNaturalWidth(child, gap, source, measured))
                return false;
            }
            resolved = measured;
          }
          if (resolved > SHRT_MAX)
            return false;
          if (resolved > maximum)
            maximum = resolved;
        }
        width = static_cast<short>(maximum);
        return true;
      }

      RowWidthConsultation::RowWidthConsultation(loka::app::scene::Node *childrenHead,
                                                 size_t childCount,
                                                 int availableWidth,
                                                 int gap,
                                                 RowUndeclaredWidth policy,
                                                 const INaturalWidthSource *source)
          : source_(0),
            gap_(gap),
            baseFlexWidth_(0),
            flexRemainder_(0),
            liveSeatsSeen_(0)
      {
        switch (policy)
        {
        case ROW_UNDECLARED_WIDTH_SHARED:
          break;
        case ROW_UNDECLARED_WIDTH_NATURAL:
          this->source_ = source;
          break;
        }
        RowWidthTally tally;
        loka::dsl::CompositionCursor<loka::app::scene::Node> consult(childrenHead, childCount);
        for (loka::app::scene::Node *child = consult.next(); child; child = consult.next())
          tally.add(this->preferredWidth(child));
        const long flexSeats = tally.flexSeats();
        long remainingWidth = availableWidth - tally.occupied(gap);
        if (remainingWidth < 0)
        {
          remainingWidth = 0;
        }
        if (flexSeats > 0)
        {
          this->baseFlexWidth_ = static_cast<int>(remainingWidth / flexSeats);
          this->flexRemainder_ = static_cast<int>(remainingWidth - this->baseFlexWidth_ * flexSeats);
        }
      }

      int RowWidthConsultation::preferredWidth(loka::app::scene::Node *child) const
      {
        const int declaredWidth = preferredChildWidthForRow(child);
        short naturalWidth = 0;
        if (declaredWidth < 0 && this->source_ && queryNaturalWidth(child, this->gap_, this->source_, naturalWidth)
            && naturalWidth > 0)
          return naturalWidth;
        return declaredWidth;
      }

      RowChildWidth RowWidthConsultation::next(loka::app::scene::Node *child)
      {
        const int preferredWidth = this->preferredWidth(child);
        if (preferredWidth == 0)
        {
          return RowChildWidth(0, false, false);
        }
        int width = preferredWidth;
        if (preferredWidth < 0)
        {
          width = this->baseFlexWidth_;
          if (this->flexRemainder_ > 0)
          {
            ++width;
            --this->flexRemainder_;
          }
        }
        const bool gapBefore = this->liveSeatsSeen_ > 0;
        ++this->liveSeatsSeen_;
        return RowChildWidth(width, true, gapBefore);
      }
    } // namespace layout
  } // namespace app
} // namespace loka
