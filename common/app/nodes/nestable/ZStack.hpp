#ifndef LOKA_APP2_NODES_NESTABLE_ZSTACK_HPP
#define LOKA_APP2_NODES_NESTABLE_ZSTACK_HPP

#include "app/scene/Node.hpp"

namespace loka
{
  namespace app
  {
    struct ZStackTypeTag
    {
    };

    class ZStackNode;

    /** Ground requested for the units (later children) of a ZStack. */
    enum ZStackUnitGround
    {
      ZSTACK_UNIT_GROUND_WINDOW,
      /** The first unit (second child) shows a blur of what lies beneath it
          where the rail supports it; later units and other rails keep the
          window ground. */
      ZSTACK_UNIT_GROUND_EXP_BLUR
    };

    struct ZStackProps : public scene::NodePropsBase<ZStackProps>
    {
      typedef ZStackTypeTag TypeTag;
      typedef ZStackNode NodeType;
      ZStackUnitGround unitGround;
      ZStackProps()
          : unitGround(ZSTACK_UNIT_GROUND_WINDOW)
      {
      }
      bool operator<(const scene::PropsBase &rhs) const
      {
        if (rhs.propsTypeId() != propsTypeId())
          return false;
        return this->unitGround < static_cast<const ZStackProps &>(rhs).unitGround;
      }
    };

    class ZStackNode : public scene::NestableNode
    {
    public:
      typedef ZStackTypeTag TypeTag;
      ZStackProps props;
      ZStackNode(const ZStackProps &p)
          : scene::NestableNode(),
            props(p)
      {
      }
      virtual scene::NodeKind kind() const
      {
        return scene::NODE_KIND_ZSTACK;
      }
      virtual const void *nodeTypeKey() const
      {
        return scene::NodeTypeToken<ZStackNode>();
      }
      virtual ZStackNode *asZStackNode()
      {
        return this;
      }
    };

    struct ZStackDefinition : public scene::NestableNodeDefinition<ZStackProps, ZStackNode, ZStackDefinition>,
                              public scene::TestIdDslMixin<ZStackDefinition>
    {
      typedef scene::NestableNodeDefinition<ZStackProps, ZStackNode, ZStackDefinition> BaseType;
      using BaseType::operator<<;
      ZStackDefinition()
          : BaseType()
      {
      }
      ZStackDefinition(const ZStackProps &p)
          : BaseType(p)
      {
      }
      /** Experimental: the first unit (second child) shows a blur of what lies
          beneath it where the rail supports it. Later units and other rails
          keep the window ground. Eligibility follows composition index for
          this ZStack identity; a hidden or zero-height first unit does not
          promote a later one. */
      ZStackDefinition &expBlurUnit()
      {
        this->props.unitGround = ZSTACK_UNIT_GROUND_EXP_BLUR;
        return *this;
      }
      ZStackDefinition(const ZStackDefinition &other)
          : BaseType(other)
      {
      }
    };

    typedef ZStackDefinition ZStack;
  } // namespace app
} // namespace loka

#endif // LOKA_APP2_NODES_NESTABLE_ZSTACK_HPP
