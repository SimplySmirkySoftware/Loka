#ifndef LOKA_APP_NODES_CONTROLS_RIBBON_HPP
#define LOKA_APP_NODES_CONTROLS_RIBBON_HPP

#include "app/nodes/controls/Button.hpp"
#include "app/nodes/Text.hpp"
#include "app/style/StyleVocab.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include <cassert>

namespace loka
{
  namespace app
  {
    struct RibbonControlDefinition;
    struct RibbonGroupDefinition;

    struct RibbonGroupTypeTag
    {
    };

    struct RibbonGroupProps : public scene::NodePropsBase<RibbonGroupProps>
    {
      typedef RibbonGroupTypeTag TypeTag;
      typedef RibbonGroupNode NodeType;
      int hash() const
      {
        return 0;
      }
      bool operator<(const scene::PropsBase &) const
      {
        return false;
      }
    };

    /** Closed ribbon container; its single child is the items-and-caption column. */
    class RibbonGroupNode : public scene::NestableNode
    {
    public:
      typedef RibbonGroupTypeTag TypeTag;
      /** The inset is the band each rail draws the group frame into (left edge,
          right edge, bottom line); the content never overlaps it. */
      static const int kFrameInset = 4;
      RibbonGroupProps props;
      explicit RibbonGroupNode(const RibbonGroupProps &p)
          : scene::NestableNode(),
            props(p)
      {
      }
      virtual scene::NodeKind kind() const
      {
        return scene::NODE_KIND_RIBBON_GROUP;
      }
      virtual const void *nodeTypeKey() const
      {
        return scene::NodeTypeToken<RibbonGroupNode>();
      }
      virtual RibbonGroupNode *asRibbonGroupNode()
      {
        return this;
      }
    };

    /** One native push button in a left-to-right RibbonControl, using its rail's
        natural width unless width(n) declares a fixed seat. The emitter is borrowed;
        null does nothing.
        Items form the closed ribbon vocabulary for future native projections;
        enabled, icons and identifier commands are later work. */
    struct RibbonItemDefinition
    {
      explicit RibbonItemDefinition(const char *text)
          : title_(loka::core::String::Literal(text)),
            onClick_(0),
            width_()
      {
      }
      RibbonItemDefinition &onClick(loka::core::EmitterState *emitter)
      {
        this->onClick_ = emitter;
        return *this;
      }
      /** Overrides the seat width; it must be positive. */
      RibbonItemDefinition &width(short n)
      {
        assert(n > 0);
        this->width_ = DeclaredWidth(n);
        return *this;
      }

    private:
      // Read only by RibbonControlDefinition when it expands the item.
      friend struct RibbonControlDefinition;
      loka::core::String title_;
      loka::core::EmitterState *onClick_;
      /** Optional fixed seat; the numeric payload is read only in FIXED mode. */
      class DeclaredWidth
      {
      public:
        DeclaredWidth() : kind_(NATURAL), value_(0) {}
        explicit DeclaredWidth(short value) : kind_(FIXED), value_(value) {}
        bool query(short &value) const
        {
          if (this->kind_ == NATURAL)
            return false;
          value = this->value_;
          return true;
        }
      private:
        enum Kind { NATURAL, FIXED };
        Kind kind_;
        short value_;
      };
      DeclaredWidth width_;
    };
    typedef RibbonItemDefinition RibbonItem;

    /** A left-to-right row of native push buttons using rail-measured natural
        widths or the item's explicit width(n). A declining rail shares the remaining
        width among undeclared seats. Only RibbonItems and RibbonGroups may
        be appended, preserving a closed declaration for future native projections.
        Enabled, icons and identifier commands are later work. */
    struct RibbonControlDefinition
        : public scene::NestableNodeDefinition<StackProps, StackNode, RibbonControlDefinition>,
          public scene::TestIdDslMixin<RibbonControlDefinition>
    {
      typedef scene::NestableNodeDefinition<StackProps, StackNode, RibbonControlDefinition> BaseType;
      RibbonControlDefinition()
          : BaseType(StackProps(STACK_AXIS_ROW))
      {
        this->props.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL;
      }
      RibbonControlDefinition(const RibbonControlDefinition &other)
          : BaseType(other)
      {
      }

      // Declaring this operator hides the generic node operator<< of the base,
      // so a RibbonControl accepts only the closed ribbon vocabulary.
      RibbonControlDefinition &operator<<(const RibbonItemDefinition &item)
      {
        short fixedWidth = 0;
        if (item.width_.query(fixedWidth))
          BaseType::operator<<(Box().size(fixedWidth, 0)
                               << Button(ButtonProps().text(item.title_).onClick(item.onClick_)));
        else
          BaseType::operator<<(Button(ButtonProps().text(item.title_).onClick(item.onClick_)));
        return *this;
      }
      RibbonControlDefinition &operator<<(const RibbonGroupDefinition &group);

    private:
      /** Only RibbonControl expands a group into this runtime container. */
      struct GroupFrameDefinition
          : public scene::NestableNodeDefinition<RibbonGroupProps, RibbonGroupNode, GroupFrameDefinition>
      {
      };
    };
    typedef RibbonControlDefinition RibbonControl;

    /** A captioned group of RibbonItems. The items sit above a centered small
        secondary label; the wider part plus the frame band determines the group's natural width.
        A rail that declines either part gives the group a shared seat. */
    struct RibbonGroupDefinition
    {
      explicit RibbonGroupDefinition(const char *caption)
          : caption_(loka::core::String::Literal(caption)),
            items_()
      {
      }

      RibbonGroupDefinition &operator<<(const RibbonItemDefinition &item)
      {
        this->items_ << item;
        return *this;
      }

    private:
      friend struct RibbonControlDefinition;
      loka::core::String caption_;
      RibbonControlDefinition items_;
    };
    typedef RibbonGroupDefinition RibbonGroup;

    inline RibbonControlDefinition &RibbonControlDefinition::operator<<(const RibbonGroupDefinition &group)
    {
      BaseType::operator<<(GroupFrameDefinition()
                           << (VStack() << group.items_
                                        << (Text(group.caption_) + SmallLabel + Secondary + block::Centered)));
      return *this;
    }
  } // namespace app
} // namespace loka

#endif // LOKA_APP_NODES_CONTROLS_RIBBON_HPP
