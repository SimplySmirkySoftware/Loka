#ifndef LOKA_APP2_NODES_NESTABLE_ROWCOLUMN_HPP
#define LOKA_APP2_NODES_NESTABLE_ROWCOLUMN_HPP

#include "app/scene/Node.hpp"
#include "core/State.hpp"
#include "app/layout/LayoutGap.hpp"

namespace loka
{
  namespace app
  {
    enum HorizontalAlignment
    {
      HORIZONTAL_ALIGNMENT_LEADING = 0,
      HORIZONTAL_ALIGNMENT_CENTER = 1,
      HORIZONTAL_ALIGNMENT_TRAILING = 2
    };

    enum VerticalAlignment
    {
      VERTICAL_ALIGNMENT_TOP = 0,
      VERTICAL_ALIGNMENT_CENTER = 1,
      VERTICAL_ALIGNMENT_BOTTOM = 2
    };

    enum StackAxis
    {
      STACK_AXIS_ROW,
      STACK_AXIS_COLUMN
    };

    /** How a Row seats children that declare no width. Columns ignore this policy. */
    enum RowUndeclaredWidth
    {
      ROW_UNDECLARED_WIDTH_SHARED = 0,
      ROW_UNDECLARED_WIDTH_NATURAL
    };

    struct StackTypeTag
    {
    };

    class StackNode;

    struct StackProps : public scene::NodePropsBase<StackProps>
    {
      typedef StackTypeTag TypeTag;
      typedef StackNode NodeType;
      layout::LayoutGap gap_;
      StackAxis axis_;
      RowUndeclaredWidth rowUndeclaredWidth_;
      /** Borrowed live axis; null selects the constant value. */
      loka::core::State<StackAxis> *axisState_;
      bool hasVerticalAlignment_;
      VerticalAlignment verticalAlignment_;
      bool hasHorizontalAlignment_;
      HorizontalAlignment horizontalAlignment_;
      StackProps()
          : axis_(STACK_AXIS_ROW),
            rowUndeclaredWidth_(ROW_UNDECLARED_WIDTH_SHARED),
            axisState_(0),
            hasVerticalAlignment_(false),
            verticalAlignment_(VERTICAL_ALIGNMENT_TOP),
            hasHorizontalAlignment_(false),
            horizontalAlignment_(HORIZONTAL_ALIGNMENT_LEADING)
      {
      }
      explicit StackProps(StackAxis axis)
          : axis_(axis),
            rowUndeclaredWidth_(ROW_UNDECLARED_WIDTH_SHARED),
            axisState_(0),
            hasVerticalAlignment_(false),
            verticalAlignment_(VERTICAL_ALIGNMENT_TOP),
            hasHorizontalAlignment_(false),
            horizontalAlignment_(HORIZONTAL_ALIGNMENT_LEADING)
      {
      }
      explicit StackProps(loka::core::State<StackAxis> *axisState)
          : axis_(STACK_AXIS_ROW),
            rowUndeclaredWidth_(ROW_UNDECLARED_WIDTH_SHARED),
            axisState_(axisState),
            hasVerticalAlignment_(false),
            verticalAlignment_(VERTICAL_ALIGNMENT_TOP),
            hasHorizontalAlignment_(false),
            horizontalAlignment_(HORIZONTAL_ALIGNMENT_LEADING)
      {
      }
      /** Resolves the live input or the props-owned constant. */
      StackAxis effectiveAxis() const
      {
        return this->axisState_ ? this->axisState_->get() : this->axis_;
      }
      StackProps &gap(int value)
      {
        this->gap_ = layout::LayoutGap::of(value);
        return *this;
      }
      StackProps &gap(layout::LayoutGap value)
      {
        this->gap_ = value;
        return *this;
      }
      StackProps &alignVertical(VerticalAlignment value)
      {
        this->hasVerticalAlignment_ = true;
        this->verticalAlignment_ = value;
        return *this;
      }
      StackProps &alignHorizontal(HorizontalAlignment value)
      {
        this->hasHorizontalAlignment_ = true;
        this->horizontalAlignment_ = value;
        return *this;
      }
      bool operator<(const scene::PropsBase &rhs) const
      {
        if (rhs.propsTypeId() != propsTypeId())
          return false;
        const StackProps &other = static_cast<const StackProps &>(rhs);
        if (this->gap_ != other.gap_)
          return this->gap_ < other.gap_;
        if (this->axisState_ != other.axisState_)
          return this->axisState_ < other.axisState_;
        if (this->axis_ != other.axis_)
          return this->axis_ < other.axis_;
        if (this->rowUndeclaredWidth_ != other.rowUndeclaredWidth_)
          return this->rowUndeclaredWidth_ < other.rowUndeclaredWidth_;
        if (this->hasVerticalAlignment_ != other.hasVerticalAlignment_)
          return this->hasVerticalAlignment_ < other.hasVerticalAlignment_;
        if (this->verticalAlignment_ != other.verticalAlignment_)
          return this->verticalAlignment_ < other.verticalAlignment_;
        if (this->hasHorizontalAlignment_ != other.hasHorizontalAlignment_)
          return this->hasHorizontalAlignment_ < other.hasHorizontalAlignment_;
        return this->horizontalAlignment_ < other.horizontalAlignment_;
      }
    };

    class StackNode : public scene::NestableNode
    {
    public:
      typedef StackTypeTag TypeTag;
      StackProps props;
      StackNode(const StackProps &p)
          : props(p)
      {
      }
      virtual void declareDirtySources(scene::DirtySourceRegistrar &registrar)
      {
        registrar.markDirtyOnChange(this->props.axisState_, scene::NODE_DIRTY_LAYOUT);
      }
      virtual scene::NodeKind kind() const
      {
        return scene::NODE_KIND_STACK;
      }
      virtual const void *nodeTypeKey() const
      {
        return scene::NodeTypeToken<StackNode>();
      }
      virtual StackNode *asStackNode()
      {
        return this;
      }
    };

    struct StackDefinition : public scene::NestableNodeDefinition<StackProps, StackNode, StackDefinition>,
                             public scene::TestIdDslMixin<StackDefinition>
    {
      typedef scene::NestableNodeDefinition<StackProps, StackNode, StackDefinition> BaseType;
      using BaseType::operator<<;
      explicit StackDefinition(StackAxis axis)
          : BaseType(StackProps(axis))
      {
      }
      explicit StackDefinition(loka::core::State<StackAxis> *axisState)
          : BaseType(StackProps(axisState))
      {
      }
      StackDefinition(const StackDefinition &other)
          : BaseType(other)
      {
      }
      StackDefinition &gap(int value)
      {
        this->props.gap(value);
        return *this;
      }
      StackDefinition &gap(layout::LayoutGap value)
      {
        this->props.gap(value);
        return *this;
      }
      StackDefinition &alignVertical(VerticalAlignment value)
      {
        this->props.alignVertical(value);
        return *this;
      }
      StackDefinition &alignHorizontal(HorizontalAlignment value)
      {
        this->props.alignHorizontal(value);
        return *this;
      }

    private:
      StackDefinition()
          : BaseType(StackProps(STACK_AXIS_ROW))
      {
      }
      friend struct scene::NestableNodeDefinition<StackProps, StackNode, StackDefinition>;
    };

    struct RowDefinition : public scene::NestableNodeDefinition<StackProps, StackNode, RowDefinition>,
                           public scene::TestIdDslMixin<RowDefinition>
    {
      typedef scene::NestableNodeDefinition<StackProps, StackNode, RowDefinition> BaseType;
      using BaseType::operator<<;
      RowDefinition()
          : BaseType(StackProps(STACK_AXIS_ROW))
      {
      }
      RowDefinition(const RowDefinition &other)
          : BaseType(other)
      {
      }
      /** Seats undeclared-width children at their natural width; the Row then reports
       * a natural width to an enclosing horizontal ScrollView. */
      RowDefinition &naturalWidth()
      {
        this->props.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL;
        return *this;
      }
      RowDefinition &gap(int value)
      {
        this->props.gap(value);
        return *this;
      }
      RowDefinition &gap(layout::LayoutGap value)
      {
        this->props.gap(value);
        return *this;
      }
      RowDefinition &alignVertical(VerticalAlignment value)
      {
        this->props.alignVertical(value);
        return *this;
      }
    };

    struct ColumnDefinition : public scene::NestableNodeDefinition<StackProps, StackNode, ColumnDefinition>,
                              public scene::TestIdDslMixin<ColumnDefinition>
    {
      typedef scene::NestableNodeDefinition<StackProps, StackNode, ColumnDefinition> BaseType;
      using BaseType::operator<<;
      ColumnDefinition()
          : BaseType(StackProps(STACK_AXIS_COLUMN))
      {
      }
      ColumnDefinition(const ColumnDefinition &other)
          : BaseType(other)
      {
      }
      ColumnDefinition &gap(int value)
      {
        this->props.gap(value);
        return *this;
      }
      ColumnDefinition &gap(layout::LayoutGap value)
      {
        this->props.gap(value);
        return *this;
      }
      ColumnDefinition &alignHorizontal(HorizontalAlignment value)
      {
        this->props.alignHorizontal(value);
        return *this;
      }
    };

    typedef StackDefinition Stack;
    typedef ColumnDefinition Column;
    typedef ColumnDefinition VStack;
    typedef RowDefinition Row;
    typedef RowDefinition HStack;
  } // namespace app
} // namespace loka

#endif // LOKA_APP2_NODES_NESTABLE_ROWCOLUMN_HPP
