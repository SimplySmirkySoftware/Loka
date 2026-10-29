#ifndef LOKA_APP2_NODES_NESTABLE_BOX_HPP
#define LOKA_APP2_NODES_NESTABLE_BOX_HPP

#include "app/layout/ChildClip.hpp"
#include "app/scene/Node.hpp"
#include "core/State.hpp"
#include <climits>
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
      layout::LayoutGap padding;
      short width;
      short height;
      layout::ChildClip childClip_;
      /** Borrowed live width claim; null selects the constant width. */
      loka::core::State<int> *widthState_;
      BoxProps()
          : padding(),
            width(0),
            height(0),
            childClip_(layout::CHILD_CLIP_NONE),
            widthState_(0)
      {
      }
      int hash() const
      {
        return this->padding.hash() + width + height + this->gap.hash() + this->childClip_;
      }
      BoxProps &setGap(int value)
      {
        this->gap = layout::LayoutGap::of(value);
        return *this;
      }
      BoxProps &setPadding(int value)
      {
        this->padding = layout::LayoutGap::of(value);
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
      /** Stores a fixed width and clears the borrowed live width. */
      BoxProps &setWidth(int value)
      {
        this->width = normalizedExtent(value);
        this->widthState_ = 0;
        return *this;
      }
      /** Stores a fixed height without changing either width source. */
      BoxProps &setHeight(int value)
      {
        this->height = normalizedExtent(value);
        return *this;
      }
      /** True when both axes have a positive fixed extent. */
      bool hasFixedSize() const
      {
        return this->effectiveWidth() > 0 && this->height > 0;
      }
      /** Resolves the width used by Box layout and its parent's seat claim. */
      short effectiveWidth() const
      {
        return normalizedExtent(this->widthState_ ? this->widthState_->get() : this->width);
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
        if (height != other.height)
          return height < other.height;
        return this->childClip_ < other.childClip_;
      }

    private:
      /** Normalizes before narrowing so negative inputs cannot become positive extents. */
      static short normalizedExtent(int value)
      {
        return value <= 0 ? 0 : static_cast<short>(value > SHRT_MAX ? SHRT_MAX : value);
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

    namespace scene
    {
      template <> struct NodePropsApplier<BoxNode, BoxProps>
      {
        static bool apply(BoxNode *node, const BoxProps &props)
        {
          return layout::ApplyPropsKeepingChildClip(node, props);
        }
      };
    } // namespace scene

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
      /** Clips children at this box's own rect. With a positive width offer,
          reports no more than that width; a nonpositive offer keeps its natural width.
          Fixed when the box is created; switch with Match / Show arms or Keyed.
          Toolbox, macOS and Win32 clip. */
      BoxDefinition &clipChildren()
      {
        this->props.childClip_ = layout::CHILD_CLIP_OWN_RECT;
        return *this;
      }
      BoxDefinition &gap(int value)
      {
        this->props.setGap(value);
        return *this;
      }
      BoxDefinition &gap(layout::LayoutGap value)
      {
        this->props.gap = value;
        return *this;
      }
      BoxDefinition &padding(layout::LayoutGap value)
      {
        this->props.padding = value;
        return *this;
      }
      BoxDefinition &padding(int value)
      {
        this->props.setPadding(value);
        return *this;
      }
      /** Sets a fixed width, clearing the live width; nonpositive values clear the fixed width.
          Values above SHRT_MAX saturate. Reset the live width with a typed null
          (width(static_cast<State<int>*>(0))); width(0) clears the fixed width. */
      BoxDefinition &width(int value)
      {
        this->props.setWidth(value);
        return *this;
      }
      /** Borrows a live width claim; nonpositive values leave it unconstrained and
          values above SHRT_MAX saturate before narrowing. A typed null State
          selects the constant width supplied by size() or width(int).
          State<short> would carry the domain in the type but costs a second State
          instantiation family (measured +1792 bytes on LokaSmirkBench68K). */
      BoxDefinition &width(loka::core::State<int> *value)
      {
        this->props.widthState_ = value;
        return *this;
      }
      /** Sets only the fixed height; nonpositive values clear it and values above
          SHRT_MAX saturate. Both constant and borrowed live width are preserved. */
      BoxDefinition &height(int value)
      {
        this->props.setHeight(value);
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

    /** Shorthand for Box().clipChildren(): the same Box, with every Box modifier.
        The clip flag is fixed when the box is created, exactly as for clipChildren();
        switch with Match / Show arms or Keyed. See clipChildren() for clipping behavior. */
    inline BoxDefinition ClipBox()
    {
      BoxDefinition box;
      box.clipChildren();
      return box;
    }

    inline BoxNode *createNode(const BoxProps &props)
    {
      return new BoxNode(props);
    }

  } // namespace app
} // namespace loka

#endif // LOKA_APP2_NODES_NESTABLE_BOX_HPP
