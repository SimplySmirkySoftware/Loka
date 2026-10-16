#ifndef LOKA_APP2_NODES_NESTABLE_BOX_HPP
#define LOKA_APP2_NODES_NESTABLE_BOX_HPP

#include "app/scene/Node.hpp"
#include "core/State.hpp"
#include "app/layout/LayoutGap.hpp"

namespace loka
{
  namespace app
  {
    struct BoxTypeTag
    {
    };

    class BoxNode;

    struct BoxProps : public scene::NodePropsBase<BoxProps>
    {
      typedef BoxTypeTag TypeTag;
      typedef BoxNode NodeType;
      layout::LayoutGap gap;
      int padding;
      short width;
      short height;
      /** Borrowed live width claim; null selects the constant width. */
      loka::core::State<int> *widthState_;
      BoxProps()
          : padding(0),
            width(0),
            height(0),
            widthState_(0)
      {
      }
      int hash() const
      {
        return padding + width + height + this->gap.hash();
      }
      BoxProps &setGap(int value)
      {
        this->gap = layout::LayoutGap::of(value);
        return *this;
      }
      BoxProps &setPadding(int value)
      {
        padding = value;
        return *this;
      }
      /** Declares the fixed outer area owned by this container. */
      BoxProps &setSize(short fixedWidth, short fixedHeight)
      {
        width = fixedWidth;
        this->widthState_ = 0;
        height = fixedHeight;
        return *this;
      }
      /** True when this container owns an explicit outer layout extent. */
      bool hasFixedSize() const
      {
        return this->effectiveWidth() > 0 && height > 0;
      }
      /** Resolves the width used by Box layout and its parent's seat claim. */
      short effectiveWidth() const
      {
        return this->widthState_ ? static_cast<short>(this->widthState_->get()) : this->width;
      }
      bool operator<(const scene::PropsBase &rhs) const
      {
        if (rhs.propsTypeId() != propsTypeId())
          return false;
        const BoxProps &other = static_cast<const BoxProps &>(rhs);
        if (this->gap != other.gap)
          return this->gap < other.gap;
        if (padding != other.padding)
          return padding < other.padding;
        if (this->widthState_ != other.widthState_)
          return this->widthState_ < other.widthState_;
        if (width != other.width)
          return width < other.width;
        return height < other.height;
      }
    };

    class BoxNode : public scene::NestableNode
    {
    public:
      typedef BoxTypeTag TypeTag;
      BoxProps props;
      BoxNode(const BoxProps &p)
          : scene::NestableNode(),
            props(p)
      {
      }
      virtual void declareDirtySources(scene::DirtySourceRegistrar &registrar)
      {
        registrar.markDirtyOnChange(this->props.widthState_, scene::NODE_DIRTY_LAYOUT);
      }
      virtual scene::NodeKind kind() const
      {
        return scene::NODE_KIND_BOX;
      }
      virtual const void *nodeTypeKey() const
      {
        return scene::NodeTypeToken<BoxNode>();
      }
      virtual BoxNode *asBoxNode()
      {
        return this;
      }
    };

    struct BoxDefinition : public scene::NestableNodeDefinition<BoxProps, BoxNode, BoxDefinition>,
                           public scene::TestIdDslMixin<BoxDefinition>
    {
      typedef scene::NestableNodeDefinition<BoxProps, BoxNode, BoxDefinition> BaseType;
      using BaseType::operator<<;

      BoxDefinition()
          : BaseType()
      {
      }
      BoxDefinition(const BoxProps &p)
          : BaseType(p)
      {
      }
      BoxDefinition(const BoxDefinition &other)
          : BaseType(other)
      {
      }
      BoxDefinition &gap(int value)
      {
        this->props.setGap(value);
        return *this;
      }
      BoxDefinition &padding(int value)
      {
        this->props.setPadding(value);
        return *this;
      }
      /** Borrows a live width claim; a nonpositive value leaves it unconstrained.
          A null State selects the constant width supplied by size(). The claim's
          domain is the short of size() and of LayoutState::width on every rail;
          effectiveWidth() narrows the State's int to it like the size() parameter
          would, so keep the value within short range. (State<short> would carry
          the domain in the type but costs a second State instantiation family,
          measured +1792 bytes on LokaSmirkBench68K.) */
      BoxDefinition &width(loka::core::State<int> *value)
      {
        this->props.widthState_ = value;
        return *this;
      }
      /** Declares the fixed outer area owned by this container. */
      BoxDefinition &size(short width, short height)
      {
        this->props.setSize(width, height);
        return *this;
      }
    };

    typedef BoxDefinition Box;

    inline BoxNode *createNode(const BoxProps &props)
    {
      return new BoxNode(props);
    }

  } // namespace app
} // namespace loka

#endif // LOKA_APP2_NODES_NESTABLE_BOX_HPP
