#ifndef LOKA_APP2_NODES_NESTABLE_GRID_HPP
#define LOKA_APP2_NODES_NESTABLE_GRID_HPP

#include "app/scene/Node.hpp"

namespace loka
{
  namespace app
  {
    struct GridTypeTag
    {
    };

    class GridNode;

    struct GridProps : public scene::NodePropsBase<GridProps>
    {
      typedef GridTypeTag TypeTag;
      typedef GridNode NodeType;
      short rows;
      short cols;
      GridProps()
          : rows(1),
            cols(1)
      {
      }
      GridProps(short r, short c)
          : rows(r),
            cols(c)
      {
      }
      bool operator<(const scene::PropsBase &rhs) const
      {
        if (rhs.propsTypeId() != propsTypeId())
          return false;
        const GridProps &other = static_cast<const GridProps &>(rhs);
        if (rows != other.rows)
        {
          return rows < other.rows;
        }
        return cols < other.cols;
      }
    };

    class GridNode : public scene::NestableNode
    {
    public:
      typedef GridTypeTag TypeTag;
      GridProps props;
      GridNode(const GridProps &p)
          : scene::NestableNode(),
            props(p)
      {
      }
      virtual scene::NodeKind kind() const
      {
        return scene::NODE_KIND_GRID;
      }
      virtual const void *nodeTypeKey() const
      {
        return scene::NodeTypeToken<GridNode>();
      }
      virtual GridNode *asGridNode()
      {
        return this;
      }
    };

    struct GridDefinition : public scene::NestableNodeDefinition<GridProps, GridNode, GridDefinition>,
                            public scene::TestIdDslMixin<GridDefinition>
    {
      typedef scene::NestableNodeDefinition<GridProps, GridNode, GridDefinition> BaseType;
      explicit GridDefinition(const scene::CloneShell<GridDefinition> &shell)
          : BaseType(shell)
      {
      }
      using BaseType::operator<<;
      GridDefinition()
          : BaseType()
      {
      }
      GridDefinition(const GridProps &p)
          : BaseType(p)
      {
      }
      GridDefinition(const GridDefinition &other)
          : BaseType(other)
      {
      }
      GridDefinition &rows(short value)
      {
        this->props.rows = value;
        return *this;
      }
      GridDefinition &cols(short value)
      {
        this->props.cols = value;
        return *this;
      }
    };

    typedef GridDefinition Grid;
  } // namespace app
} // namespace loka

#endif // LOKA_APP2_NODES_NESTABLE_GRID_HPP
