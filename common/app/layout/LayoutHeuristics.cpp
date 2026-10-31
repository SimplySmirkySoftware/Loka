#include "app/layout/LayoutHeuristics.hpp"

#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/Text.hpp"
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

      int resolveLayoutGap(scene::Node *node, int received, const RailSpacing &spacing)
      {
        if (!node)
          return spacing.verticalGap();
        // One virtual kind() call: every dispatch visit resolves (#950 PR 6, 68k timing).
        switch (node->kind())
        {
        case scene::NODE_KIND_STACK:
        {
          StackNode *stack = static_cast<StackNode *>(node);
          return stack->props.gap_.resolve(stack->props.effectiveAxis() == STACK_AXIS_ROW ? spacing.horizontalGap()
                                                                                          : spacing.verticalGap(),
                                           spacing.standardWindowMargin());
        }
        case scene::NODE_KIND_BOX:
          return static_cast<BoxNode *>(node)->props.gap.resolve(spacing.verticalGap(), spacing.standardWindowMargin());
        case scene::NODE_KIND_UNKNOWN:
          return unknownKindPassesThrough(node) ? received : spacing.verticalGap();
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
        return spacing.verticalGap();
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

      SeatLeaves singleLiveLeaf(scene::Node *seat)
      {
        if (!isLayoutPassThrough(seat))
          return SeatLeaves(childTakesLayoutSeat(seat) ? seat : 0);
        scene::INestable *group = seat->asNestable();
        loka::dsl::CompositionCursor<scene::Node> children(group->childrenHead(), group->childrenCount());
        SeatLeaves result;
        for (scene::Node *child = children.next(); child; child = children.next())
        {
          const SeatLeaves descendant = singleLiveLeaf(child);
          if (descendant.kind() == SeatLeaves::NO_SEAT)
            continue;
          if (descendant.kind() == SeatLeaves::MANY_LEAVES || result.kind() == SeatLeaves::ONE_LEAF)
            return SeatLeaves::many();
          result = descendant;
        }
        return result;
      }

      namespace
      {
        /** Immutable facts about one live seat; no measurements or allocation decisions. */
        class RowSeatFacts
        {
        public:
          enum Shape
          {
            DECLARED_WIDTH,
            LABEL,
            SPACER,
            OTHER
          };
          explicit RowSeatFacts(scene::Node *seat)
              : shape_(OTHER),
                width_(0)
          {
            const int declared = preferredChildWidthForRow(seat);
            if (declared >= 0)
            {
              this->shape_ = DECLARED_WIDTH;
              this->width_ = declared;
              return;
            }
            scene::Node *leaf = singleLiveLeaf(seat).leaf();
            TextNode *text = leaf ? leaf->asTextNode() : 0;
            if (text && text->props.blockStyle_.truncation_ == TEXT_TRUNCATION_ELLIPSIS
                && (!text->props.blockStyle_.hasWrap_ || text->props.blockStyle_.wrap_ == TEXT_WRAP_NONE))
              this->shape_ = LABEL;
            else if (leaf && leaf->asBoxNode() && leaf->asBoxNode()->childrenCount() == 0
                     && preferredChildWidthForRow(leaf) < 0)
              this->shape_ = SPACER;
          }
          Shape shape() const
          {
            return this->shape_;
          }
          bool queryDeclaredWidth(int &width) const
          {
            if (this->shape_ != DECLARED_WIDTH)
              return false;
            width = this->width_;
            return true;
          }

        private:
          Shape shape_;
          int width_;
        };

        /** Shape/style eligibility; callers supply the positive natural-width answer. */
        bool shrinkableTextSeat(scene::Node *seat)
        {
          return RowSeatFacts(seat).shape() == RowSeatFacts::LABEL;
        }

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

        bool queryNaturalWidth(scene::Node *child,
                               const RailSpacing &spacing,
                               const INaturalSizeSource *source,
                               short &width)
        {
          // A pass-through wrapper (a component such as a JS Button or Cell, a
          // Show arm) lays its children out in place and is as wide as the
          // widest of them, as queryContentOverflowWidth already treats it.
          if (isLayoutPassThrough(child))
          {
            scene::INestable *group = child->asNestable();
            loka::dsl::CompositionCursor<scene::Node> inner(group ? group->childrenHead() : 0,
                                                            group ? group->childrenCount() : 0);
            int widest = 0;
            for (scene::Node *seat = inner.next(); seat; seat = inner.next())
            {
              if (!childTakesLayoutSeat(seat))
                continue;
              // A declared width (a sized ImageView or RectSurface) wins before
              // the rail is asked, as in the column and row paths below.
              int resolved = declaredChildWidthForColumn(seat);
              if (resolved < 0)
              {
                short measured = 0;
                if (!queryNaturalWidth(seat, spacing, source, measured))
                  return false;
                resolved = measured;
              }
              if (resolved > widest)
                widest = resolved;
            }
            if (widest <= 0)
              return false;
            width = static_cast<short>(widest);
            return true;
          }
          if (scene::IFramedNode *frame = child->asFramedNode())
          {
            scene::INestable *group = child->asNestable();
            short contentWidth = 0;
            if (!group || !group->childrenHead()
                || !queryNaturalWidth(group->childrenHead(), spacing, source, contentWidth))
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
              const RowSeatFacts facts(seat);
              if (stack->props.rowUndeclaredWidth_ == ROW_UNDECLARED_WIDTH_NATURAL_LABELS_SHARE
                  && (facts.shape() == RowSeatFacts::LABEL || facts.shape() == RowSeatFacts::SPACER))
                return false;
              int declared = -1;
              facts.queryDeclaredWidth(declared);
              int resolved = declared;
              if (declared < 0)
              {
                short natural = 0;
                if (!queryNaturalWidth(seat, spacing, source, natural))
                  return false;
                resolved = natural;
                if (natural > maximum)
                  maximum = natural;
              }
              switch (stack->props.rowUndeclaredWidth_)
              {
              case ROW_UNDECLARED_WIDTH_SHARED:
                tally.add(declared, takesSeat);
                break;
              case ROW_UNDECLARED_WIDTH_NATURAL:
              case ROW_UNDECLARED_WIDTH_NATURAL_LABELS_SHARE:
                tally.add(resolved, takesSeat);
                break;
              }
            }
            {
              LayoutWidthSum sum = tally.occupied(resolveLayoutGap(stack, spacing.horizontalGap(), spacing));
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
                if (!queryNaturalWidth(seat, spacing, source, natural))
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
        int preferredRowWidth(scene::Node *child,
                              const RowSeatFacts &facts,
                              RowUndeclaredWidth policy,
                              const RailSpacing &spacing,
                              const INaturalSizeSource *source)
        {
          int declared = -1;
          if (facts.queryDeclaredWidth(declared))
            return declared;
          switch (policy)
          {
          case ROW_UNDECLARED_WIDTH_SHARED:
            return -1;
          case ROW_UNDECLARED_WIDTH_NATURAL_LABELS_SHARE:
            if (facts.shape() == RowSeatFacts::LABEL || facts.shape() == RowSeatFacts::SPACER)
              return -1;
            break;
          case ROW_UNDECLARED_WIDTH_NATURAL:
            break;
          }
          short natural = 0;
          return source && queryNaturalWidth(child, spacing, source, natural) ? natural : -1;
        }

      } // namespace

      bool queryContentOverflowWidth(scene::Node *parent,
                                     const RailSpacing &spacing,
                                     const INaturalSizeSource *source,
                                     short &width)
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
              if (!queryContentOverflowWidth(child, spacing, source, measured))
                return false;
            }
            else if (scene::IFramedNode *frame = child->asFramedNode())
            {
              if (!queryContentOverflowWidth(child, spacing, source, measured))
                return false;
              const FrameGeometry geometry = frame->frameGeometry();
              long answer = static_cast<long>(measured) + geometry.insets.left + geometry.insets.right;
              if (answer < geometry.minimumWidth)
                answer = geometry.minimumWidth;
              if (answer > SHRT_MAX)
                return false;
              measured = static_cast<short>(answer);
            }
            else if (stack)
            {
              switch (stack->props.rowUndeclaredWidth_)
              {
              case ROW_UNDECLARED_WIDTH_SHARED:
                break;
              case ROW_UNDECLARED_WIDTH_NATURAL_LABELS_SHARE:
              {
                RowWidthTally minimum;
                bool live = false;
                loka::dsl::CompositionCursor<scene::Node> seats(stack->childrenHead(), stack->childrenCount());
                for (scene::Node *seat = seats.next(); seat; seat = seats.next())
                {
                  if (!childTakesLayoutSeat(seat))
                    continue;
                  live = true;
                  const RowSeatFacts facts(seat);
                  int claim = 0;
                  if (facts.shape() == RowSeatFacts::LABEL)
                    claim = 1;
                  else if (facts.shape() != RowSeatFacts::SPACER && !facts.queryDeclaredWidth(claim))
                  {
                    short natural = 0;
                    if (!queryNaturalWidth(seat, spacing, source, natural))
                      return false;
                    claim = natural;
                  }
                  minimum.add(claim, true);
                }
                const long answer = minimum.occupied(resolveLayoutGap(stack, spacing.horizontalGap(), spacing)).value();
                if (!live || answer > SHRT_MAX)
                  return false;
                measured = static_cast<short>(answer);
                break;
              }
              case ROW_UNDECLARED_WIDTH_NATURAL:
              {
                RowWidthTally original;
                RowWidthTally shrunk;
                loka::dsl::CompositionCursor<scene::Node> seats(stack->childrenHead(), stack->childrenCount());
                for (scene::Node *seat = seats.next(); seat; seat = seats.next())
                {
                  if (!childTakesLayoutSeat(seat))
                    continue;
                  const RowSeatFacts facts(seat);
                  int claim = -1;
                  if (!facts.queryDeclaredWidth(claim))
                  {
                    short natural = 0;
                    if (!queryNaturalWidth(seat, spacing, source, natural))
                      return false;
                    claim = natural;
                  }
                  original.add(claim, true);
                  shrunk.add(claim > 0 && facts.shape() == RowSeatFacts::LABEL ? 1 : claim, true);
                }
                const int gap = resolveLayoutGap(stack, spacing.horizontalGap(), spacing);
                if (original.occupied(gap).value() <= 0 || original.occupied(gap).value() > SHRT_MAX)
                  return false;
                measured = static_cast<short>(shrunk.occupied(gap).value());
                break;
              }
              }
            }
            else if (image && image->props.hasAttr_ && image->props.attr_.hasSizePolicyValue_
                     && image->props.attr_.sizePolicyValue_ == IMAGE_VIEW_SIZE_INTRINSIC)
            {
              if (!queryNaturalWidth(child, spacing, source, measured))
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
                                                 const RailSpacing &spacing,
                                                 RowUndeclaredWidth policy,
                                                 const INaturalSizeSource *source)
          : policy_(policy),
            source_(0),
            spacing_(spacing),
            flex_(0),
            shrink_(),
            liveSeatsSeen_(0)
      {
        switch (policy)
        {
        case ROW_UNDECLARED_WIDTH_SHARED:
          break;
        case ROW_UNDECLARED_WIDTH_NATURAL:
        case ROW_UNDECLARED_WIDTH_NATURAL_LABELS_SHARE:
          this->source_ = source;
          break;
        }
        RowWidthTally tally;
        LayoutWidthSum shrinkSum;
        LayoutWidthSum floors;
        long shrinkCount = 0;
        loka::dsl::CompositionCursor<loka::app::scene::Node> consult(childrenHead, childCount);
        for (loka::app::scene::Node *child = consult.next(); child; child = consult.next())
        {
          if (!childTakesLayoutSeat(child))
            continue;
          const RowSeatFacts facts(child);
          const int width = preferredRowWidth(child, facts, this->policy_, this->spacing_, this->source_);
          tally.add(width, true);
          if (this->policy_ == ROW_UNDECLARED_WIDTH_NATURAL_LABELS_SHARE && facts.shape() == RowSeatFacts::LABEL)
            floors.add(1);
          if (this->policy_ == ROW_UNDECLARED_WIDTH_NATURAL && width > 0 && facts.shape() == RowSeatFacts::LABEL)
          {
            shrinkSum.add(width);
            ++shrinkCount;
          }
        }
        const long flexSeats = tally.flexSeats();
        LayoutWidthSum occupiedSum = tally.occupied(gap);
        occupiedSum.add(floors.value());
        const long occupied = occupiedSum.value();
        long remainingWidth = occupied > SHRT_MAX ? 0 : availableWidth - occupied;
        if (remainingWidth < 0)
        {
          remainingWidth = 0;
        }
        if (flexSeats > 0)
        {
          this->flex_ = RowWidthDivision(static_cast<int>(remainingWidth / flexSeats),
                                         static_cast<int>(remainingWidth % flexSeats));
        }
        if (occupied <= SHRT_MAX && occupied > availableWidth && shrinkCount > 0)
        {
          const long sum = shrinkSum.value();
          long budget = sum - (occupied - availableWidth);
          if (budget < shrinkCount)
            budget = shrinkCount;
          if (budget == sum)
            return;
          if (shrinkCount == 1)
          {
            this->shrink_ = RowWidthDivision(static_cast<int>(budget));
            return;
          }
          // Each raised cap freezes at least one more short seat. Thus at most
          // k scans, without a per-child table or measurements of other seats.
          long cap = budget / shrinkCount;
          for (long pass = 0; pass < shrinkCount; ++pass)
          {
            long shortSum = 0;
            long tallCount = 0;
            loka::dsl::CompositionCursor<scene::Node> seats(childrenHead, childCount);
            for (scene::Node *seat = seats.next(); seat; seat = seats.next())
            {
              if (!shrinkableTextSeat(seat))
                continue;
              short natural = 0;
              if (!queryNaturalWidth(seat, this->spacing_, this->source_, natural))
                continue;
              if (natural <= cap)
                shortSum += natural;
              else
                ++tallCount;
            }
            if (tallCount == 0)
              return;
            const long nextCap = (budget - shortSum) / tallCount;
            if (nextCap == cap)
            {
              this->shrink_ =
                  RowWidthDivision(static_cast<int>(cap), static_cast<int>(budget - shortSum - cap * tallCount));
              return;
            }
            cap = nextCap;
          }
        }
      }

      RowChildWidth RowWidthConsultation::next(loka::app::scene::Node *child)
      {
        if (!childTakesLayoutSeat(child))
        {
          return RowChildWidth(0, false, false);
        }
        const RowSeatFacts facts(child);
        const int preferredWidth = preferredRowWidth(child, facts, this->policy_, this->spacing_, this->source_);
        int width = preferredWidth;
        if (preferredWidth < 0)
        {
          width = this->flex_.take(INT_MAX);
          // Restore the label floor reserved before division in the constructor.
          if (this->policy_ == ROW_UNDECLARED_WIDTH_NATURAL_LABELS_SHARE && facts.shape() == RowSeatFacts::LABEL)
            ++width;
        }
        else if (this->policy_ == ROW_UNDECLARED_WIDTH_NATURAL && width > 0 && facts.shape() == RowSeatFacts::LABEL)
          width = this->shrink_.take(width);
        const bool gapBefore = this->liveSeatsSeen_ > 0;
        ++this->liveSeatsSeen_;
        return RowChildWidth(width, true, gapBefore);
      }
    } // namespace layout
  } // namespace app
} // namespace loka
