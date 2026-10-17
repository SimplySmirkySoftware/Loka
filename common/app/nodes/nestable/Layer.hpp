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

    /** A Layer is the unit of stacking inside a ZStack: it sits at its composition
        position (a later ZStack child is on top) and paints its own ground over
        its whole frame. In this PR only the node exists; until rails adopt it,
        they treat it exactly like a ZStack: a transparent container offering
        its seat to its children, with an optional own drawing surface deferred.
        ground: the window ground (GroundForKind); z: composition order inside
        a ZStack; rails adopt in later PRs. */
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

    inline LayerNode *createNode(const LayerProps &props)
    {
      return new LayerNode(props);
    }
  } // namespace app
} // namespace loka

#endif // LOKA_APP2_NODES_NESTABLE_LAYER_HPP
