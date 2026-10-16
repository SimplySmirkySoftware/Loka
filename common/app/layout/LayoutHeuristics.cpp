#include "app/layout/LayoutHeuristics.hpp"

#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "dsl/composition/CompositionList.hpp"
#include <climits>
#include "app/layout/LayoutWidthSum.hpp"

namespace loka
{
  namespace app
  {
    namespace layout
    {
      namespace
      {
        /** The shape half of the pass-through verdict, for a node already known to be NODE_KIND_UNKNOWN. */
        bool unknownKindPassesThrough(scene::Node *node)
        {
          return node->asNestable() && !node->asProjectedLayoutNode();
        }
      } // namespace

      bool isLayoutPassThrough(scene::Node *node)
      {
        // Built-in containers have known kinds; pass-through wrappers do not.
        return node && node->kind() == scene::NODE_KIND_UNKNOWN && unknownKindPassesThrough(node);
      }

      int resolveLayoutGap(scene::Node *node, int received, int verticalDefault, int horizontalDefault)
      {
        if (!node)
          return verticalDefault;
        // One virtual kind() call: every dispatch visit resolves (#950 PR 6, 68k timing).
        switch (node->kind())
        {
        case scene::NODE_KIND_STACK:
        {
          StackNode *stack = static_cast<StackNode *>(node);
          return stack->props.gap_.resolve(stack->props.effectiveAxis() == STACK_AXIS_ROW ? horizontalDefault
                                                                                          : verticalDefault);
        }
        case scene::NODE_KIND_BOX:
          return static_cast<BoxNode *>(node)->props.gap.resolve(verticalDefault);
        case scene::NODE_KIND_UNKNOWN:
          return unknownKindPassesThrough(node) ? received : verticalDefault;
        // Containers without a gap reset to the vertical default; a leaf's value is never read.
        case scene::NODE_KIND_ZSTACK:
        case scene::NODE_KIND_GRID:
        case scene::NODE_KIND_SCROLL_VIEW:
        case scene::NODE_KIND_CANVAS:
        case scene::NODE_KIND_RIBBON_GROUP:
        case scene::NODE_KIND_CARD:
        case scene::NODE_KIND_CELL:
        case scene::NODE_KIND_TEXT:
        case scene::NODE_KIND_BUTTON:
        case scene::NODE_KIND_EDIT_TEXT:
        case scene::NODE_KIND_POPUP_MENU:
        case scene::NODE_KIND_OPEN_FILE_DIALOG:
        case scene::NODE_KIND_IMAGE_VIEW:
        case scene::NODE_KIND_RECT_SURFACE:
        case scene::NODE_KIND_SCROLL_BAR:
        case scene::NODE_KIND_ATTRIBUTED_TEXT:
        case scene::NODE_KIND_TEXT_EDITOR:
        case scene::NODE_KIND_CONFIRM_DIALOG:
        case scene::NODE_KIND_RIBBON_GROUP_EDGE:
        case scene::NODE_KIND_CARD_BAND:
          break;
        }
        return verticalDefault;
      }

      bool childTakesLayoutSeat(scene::Node *child)
      {
        if (!child)
          return false;
        switch (child->kind())
        {
        case scene::NODE_KIND_OPEN_FILE_DIALOG:
        case scene::NODE_KIND_CONFIRM_DIALOG:
          return false;
        case scene::NODE_KIND_UNKNOWN:
          break;
        case scene::NODE_KIND_BOX:
        case scene::NODE_KIND_ZSTACK:
        case scene::NODE_KIND_GRID:
        case scene::NODE_KIND_CELL:
        case scene::NODE_KIND_STACK:
        case scene::NODE_KIND_TEXT:
        case scene::NODE_KIND_BUTTON:
        case scene::NODE_KIND_EDIT_TEXT:
        case scene::NODE_KIND_POPUP_MENU:
        case scene::NODE_KIND_IMAGE_VIEW:
        case scene::NODE_KIND_RECT_SURFACE:
        case scene::NODE_KIND_SCROLL_BAR:
        case scene::NODE_KIND_SCROLL_VIEW:
        case scene::NODE_KIND_CANVAS:
        case scene::NODE_KIND_ATTRIBUTED_TEXT:
        case scene::NODE_KIND_TEXT_EDITOR:
        case scene::NODE_KIND_CARD:
        case scene::NODE_KIND_CARD_BAND:
        case scene::NODE_KIND_RIBBON_GROUP:
        case scene::NODE_KIND_RIBBON_GROUP_EDGE:
          return true;
        }
        scene::INestable *content = child->asNestable();
        if (!content || child->asProjectedLayoutNode())
          return true;
        loka::dsl::CompositionCursor<scene::Node> children(content->childrenHead(), content->childrenCount());
        for (scene::Node *descendant = children.next(); descendant; descendant = children.next())
          if (childTakesLayoutSeat(descendant))
            return true;
        return false;
      }

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
              : live_(0),
                flex_(0)
          {
          }
          void add(int width, bool takesSeat)
          {
            if (!takesSeat)
              return;
            if (this->live_ <= SHRT_MAX + 1L)
              ++this->live_;
            if (width < 0)
            {
              if (this->flex_ <= SHRT_MAX)
                ++this->flex_;
            }
            else
              this->fixed_.add(width);
          }
          LayoutWidthSum occupied(int gap) const
          {
            LayoutWidthSum result = this->fixed_;
            result.addProduct(this->live_ > 0 ? this->live_ - 1 : 0, gap);
            return result;
          }
          long flexSeats() const
          {
            return this->flex_;
          }

        private:
          LayoutWidthSum fixed_;
          long live_;
          long flex_;
        };

        bool queryNaturalWidth(scene::Node *child, int gap, const INaturalWidthSource *source, short &width)
        {
          if (scene::IFramedNode *frame = child->asFramedNode())
          {
            scene::INestable *group = child->asNestable();
            short contentWidth = 0;
            if (!group || !group->childrenHead()
                || !queryNaturalWidth(group->childrenHead(), gap, source, contentWidth))
              return false;
            const FrameGeometry geometry = frame->frameGeometry();
            long answer = static_cast<long>(contentWidth) + geometry.insets.left + geometry.insets.right;
            if (answer < geometry.minimumWidth)
              answer = geometry.minimumWidth;
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
              const bool takesSeat = childTakesLayoutSeat(seat);
              if (!takesSeat)
                continue;
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
              tally.add(stack->props.rowUndeclaredWidth_ == ROW_UNDECLARED_WIDTH_NATURAL ? resolved : declared,
                        takesSeat);
            }
            {
              LayoutWidthSum sum = tally.occupied(resolveLayoutGap(stack, gap, gap, gap));
              sum.addProduct(tally.flexSeats(), maximum);
              answer = sum.value();
            }
            break;
          case STACK_AXIS_COLUMN:
            for (scene::Node *seat = children.next(); seat; seat = children.next())
            {
              if (!childTakesLayoutSeat(seat))
                continue;
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
            // A Column stacks its children under the same offer; a pass-through
            // nestable (Fragment, a Show arm) lays them out in place. Both
            // overflow by their widest child.
            if ((stack && stack->props.effectiveAxis() == STACK_AXIS_COLUMN) || isLayoutPassThrough(child))
            {
              if (!queryContentOverflowWidth(child, gap, source, measured))
                return false;
            }
            else if ((stack && stack->props.rowUndeclaredWidth_ == ROW_UNDECLARED_WIDTH_NATURAL)
                     || child->asFramedNode()
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
                                                 int horizontalDefault,
                                                 RowUndeclaredWidth policy,
                                                 const INaturalWidthSource *source)
          : source_(0),
            horizontalDefault_(horizontalDefault),
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
        {
          const bool takesSeat = childTakesLayoutSeat(child);
          tally.add(takesSeat ? this->preferredWidth(child) : 0, takesSeat);
        }
        const long flexSeats = tally.flexSeats();
        const long occupied = tally.occupied(gap).value();
        long remainingWidth = occupied > SHRT_MAX ? 0 : availableWidth - occupied;
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
        if (declaredWidth < 0 && this->source_
            && queryNaturalWidth(child, this->horizontalDefault_, this->source_, naturalWidth) && naturalWidth > 0)
          return naturalWidth;
        return declaredWidth;
      }

      RowChildWidth RowWidthConsultation::next(loka::app::scene::Node *child)
      {
        if (!childTakesLayoutSeat(child))
        {
          return RowChildWidth(0, false, false);
        }
        const int preferredWidth = this->preferredWidth(child);
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
