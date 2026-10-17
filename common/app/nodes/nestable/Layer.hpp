#ifndef LOKA_APP2_NODES_NESTABLE_LAYER_HPP
#define LOKA_APP2_NODES_NESTABLE_LAYER_HPP

#include "app/scene/Node.hpp"

namespace loka
{
  namespace app
  {
    struct LayerTypeTag
    {
    };

    class LayerNode;

    struct LayerProps : public scene::NodePropsBase<LayerProps>
    {
      typedef LayerTypeTag TypeTag;
      typedef LayerNode NodeType;
      LayerProps() {}
      bool operator<(const scene::PropsBase &rhs) const
      {
        if (rhs.propsTypeId() != propsTypeId())
          return false;
        return false;
      }
    };

    /** A Layer is the unit of stacking inside a ZStack: it sits at its
        composition position (a later ZStack child draws on top) and fills its
        whole frame with the window ground (GroundForKind). Place a Layer only
        over ground that does not respond to input, such as the gap a
        ScrollView's content insets leave or a modal dim, never over live
        controls. Rails that have not adopted Layer treat it exactly like a
        ZStack: a transparent container offering its seat to its children. */
    class LayerNode : public scene::NestableNode
    {
    public:
      typedef LayerTypeTag TypeTag;
      LayerProps props;
      LayerNode(const LayerProps &p)
          : scene::NestableNode(),
            props(p)
      {
      }
      virtual scene::NodeKind kind() const
      {
        return scene::NODE_KIND_LAYER;
      }
      virtual const void *nodeTypeKey() const
      {
        return scene::NodeTypeToken<LayerNode>();
      }
      virtual LayerNode *asLayerNode()
      {
        return this;
      }
    };

    struct LayerDefinition : public scene::NestableNodeDefinition<LayerProps, LayerNode, LayerDefinition>,
                             public scene::TestIdDslMixin<LayerDefinition>
    {
      typedef scene::NestableNodeDefinition<LayerProps, LayerNode, LayerDefinition> BaseType;
      using BaseType::operator<<;
      LayerDefinition()
          : BaseType()
      {
      }
      LayerDefinition(const LayerProps &p)
          : BaseType(p)
      {
      }
      LayerDefinition(const LayerDefinition &other)
          : BaseType(other)
      {
      }
    };

    typedef LayerDefinition Layer;
  } // namespace app
} // namespace loka

#endif // LOKA_APP2_NODES_NESTABLE_LAYER_HPP
