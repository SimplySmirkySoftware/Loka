#ifndef LOKA_CORE2_SCENE_NODE_CONDITIONAL_HPP
#define LOKA_CORE2_SCENE_NODE_CONDITIONAL_HPP

#include "core/State.hpp"
#include "../Node.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      struct ConditionalTypeTag
      {
      };

      /** Compose-invariant description of one conditional seat. Both branch
          definitions stay owned by the definition generation. */
      struct ConditionalProps : public PropsBase
      {
        typedef ConditionalTypeTag TypeTag;
        loka::core::State<bool> *condition;
        NodeDefinitionBase *trueDef;
        NodeDefinitionBase *falseDef;
        ConditionalProps(loka::core::State<bool> *cond,
                         NodeDefinitionBase *tDef,
                         NodeDefinitionBase *fDef);
        ConditionalProps(const loka::core::State<bool> *cond,
                         NodeDefinitionBase *tDef,
                         NodeDefinitionBase *fDef);
        static Node *rejectRuntimeFactory(const PropsBase &)
        {
          assert(false && "conditional props have no runtime node factory");
          return 0;
        }
        virtual NodeFactoryFunc nodeFactory() const
        {
          return &ConditionalProps::rejectRuntimeFactory;
        }
        static const void *staticTypeId()
        {
          static char id;
          return &id;
        }
        virtual const void *propsTypeId() const
        {
          return staticTypeId();
        }
        bool operator<(const PropsBase &rhs) const
        {
          if (rhs.propsTypeId() != this->propsTypeId())
          {
            return false;
          }
          const ConditionalProps &other = static_cast<const ConditionalProps &>(rhs);
          return this->condition < other.condition;
        }
      };

      /** Definition-only conditional seat consumed by Boundary plan
          application. It cannot materialize a runtime node. */
      struct ConditionalDefinition : public NodeDefinitionBase,
                                     public IBranchSeatDefinition
      {
        ConditionalProps props;
        NodeDefinitionBase *ownedTrueDef;
        NodeDefinitionBase *ownedFalseDef;
        ConditionalDefinition(const ConditionalProps &p);
        ConditionalDefinition(const ConditionalDefinition &other);
        ConditionalDefinition &operator=(const ConditionalDefinition &other);
        ~ConditionalDefinition();
        Node *create() const;
        Node *createInPlace(void *mem) const;
        size_t nodeSize() const;
        size_t nodeAlign() const;
        typedef ConditionalDefinition CloneType;
        virtual NodeDefinitionBase *clone() const;
        virtual NodeKind nodeKind() const
        {
          return NODE_KIND_UNKNOWN;
        }
        virtual const PropsBase *propsBase() const
        {
          return &this->props;
        }
        virtual bool applyPropsToNode(Node *) const
        {
          return false;
        }
        virtual bool isCompatibleWithNode(const Node *) const
        {
          return false;
        }
        virtual IBranchSeatDefinition *asBranchSeatDefinition()
        {
          return this;
        }
        virtual bool requiresUniqueSiblingTag() const
        {
          return true;
        }
        virtual loka::core::StateBase *branchCondition() const
        {
          return this->props.condition;
        }
        virtual bool selectArm(unsigned &armOut) const
        {
          if (!this->props.condition)
          {
            return false;
          }
          armOut = this->props.condition->get() ? 1u : 0u;
          return true;
        }
        virtual unsigned armCount() const
        {
          return 2;
        }
        virtual NodeDefinitionBase *armDefinition(unsigned arm) const
        {
          if (arm == 0)
          {
            return this->ownedFalseDef;
          }
          if (arm == 1)
          {
            return this->ownedTrueDef;
          }
          return 0;
        }
        virtual const void *branchSeatTypeId() const
        {
          return ConditionalProps::staticTypeId();
        }
        virtual NodeDefinitionBase *retainedDefinitionBranch(unsigned index)
        {
          if (index == 0)
          {
            return this->ownedTrueDef;
          }
          if (index == 1)
          {
            return this->ownedFalseDef;
          }
          return 0;
        }

      private:
        bool assignFrom(const ConditionalProps &nextProps,
                        const NodeDefinitionBase *trueDef,
                        const NodeDefinitionBase *falseDef,
                        const NodeDefinitionBase *nextBase);
      };

    } // namespace scene
  } // namespace app
} // namespace loka

#endif // LOKA_CORE2_SCENE_NODE_CONDITIONAL_HPP
