#ifndef LOKA_TESTING_SCENE_PROBE_ARM_SEAT_DEFINITION_HPP
#define LOKA_TESTING_SCENE_PROBE_ARM_SEAT_DEFINITION_HPP

#ifdef TEST_BUILD

#include <cassert>
#include "app/scene/Node.hpp"
#include "core/util/OwnedDef.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      namespace testing
      {
        struct ProbeArmSeatTypeTag
        {
        };

        struct ProbeArmSeatProps : public PropsBase
        {
          typedef ProbeArmSeatTypeTag TypeTag;

          ProbeArmSeatProps(loka::core::State<unsigned> *selectionValue = 0,
                            unsigned armCountValue = 0)
              : selection(selectionValue),
                armCount(armCountValue)
          {
          }

          static Node *rejectRuntimeFactory(const PropsBase &)
          {
            assert(false && "probe arm seats have no runtime node factory");
            return 0;
          }
          virtual NodeFactoryFunc nodeFactory() const
          {
            return &ProbeArmSeatProps::rejectRuntimeFactory;
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
            const ProbeArmSeatProps &other =
                static_cast<const ProbeArmSeatProps &>(rhs);
            return this->selection < other.selection ||
                   (this->selection == other.selection &&
                    this->armCount < other.armCount);
          }

          loka::core::State<unsigned> *selection;
          unsigned armCount;
        };

        /** Fixed-capacity test definition that exercises the production
            branch-seat interface without adding an app-facing N-arm DSL. */
        class ProbeArmSeatDefinition : public NodeDefinitionBase,
                                       public IBranchSeatDefinition
        {
        public:
          enum
          {
            MAX_ARMS = 4
          };

          ProbeArmSeatDefinition(loka::core::State<unsigned> *selection,
                                 NodeDefinitionBase *const *arms,
                                 unsigned armCount,
                                 int *selectCallCount = 0)
              : NodeDefinitionBase(),
                IBranchSeatDefinition(),
                props_(selection, armCount),
                selectCallCount_(selectCallCount)
          {
            this->cloneArms(arms, armCount);
          }

          ProbeArmSeatDefinition(const ProbeArmSeatDefinition &other)
              : NodeDefinitionBase(other),
                IBranchSeatDefinition(other),
                props_(other.props_),
                selectCallCount_(other.selectCallCount_)
          {
            NodeDefinitionBase *sources[MAX_ARMS] = {0, 0, 0, 0};
            for (unsigned arm = 0; arm < other.props_.armCount; ++arm)
            {
              sources[arm] = other.arms_[arm].get();
            }
            this->cloneArms(sources, other.props_.armCount);
          }

          virtual Node *create() const
          {
            assert(false && "probe arm seats materialize through a Boundary plan");
            return 0;
          }
          virtual Node *createInPlace(void *) const
          {
            assert(false && "probe arm seats have no runtime node");
            return 0;
          }
          virtual size_t nodeSize() const
          {
            return 0;
          }
          virtual size_t nodeAlign() const
          {
            return 1;
          }
          typedef ProbeArmSeatDefinition CloneType;
          virtual NodeDefinitionBase *clone() const
          {
            return new ProbeArmSeatDefinition(*this);
          }
          virtual NodeKind nodeKind() const
          {
            return NODE_KIND_UNKNOWN;
          }
          virtual const PropsBase *propsBase() const
          {
            return &this->props_;
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
            return this->props_.selection;
          }
          virtual bool selectArm(unsigned &armOut) const
          {
            if (this->selectCallCount_)
            {
              ++*this->selectCallCount_;
            }
            if (!this->props_.selection)
            {
              return false;
            }
            const unsigned arm = this->props_.selection->get();
            if (arm >= this->props_.armCount)
            {
              return false;
            }
            armOut = arm;
            return true;
          }
          virtual unsigned armCount() const
          {
            return this->props_.armCount;
          }
          virtual NodeDefinitionBase *armDefinition(unsigned arm) const
          {
            return arm < this->props_.armCount ? this->arms_[arm].get() : 0;
          }
          virtual const void *branchSeatTypeId() const
          {
            return ProbeArmSeatProps::staticTypeId();
          }
          virtual NodeDefinitionBase *retainedDefinitionBranch(unsigned arm)
          {
            return this->armDefinition(arm);
          }

        private:
          void cloneArms(NodeDefinitionBase *const *arms, unsigned armCount)
          {
            assert(armCount <= MAX_ARMS && "probe arm seat capacity exceeded");
            this->props_.armCount = armCount <= MAX_ARMS ? armCount : 0;
            for (unsigned arm = 0; arm < this->props_.armCount; ++arm)
            {
              this->arms_[arm].reset(arms && arms[arm] ? arms[arm]->clone() : 0);
            }
          }

          ProbeArmSeatProps props_;
          loka::core::OwnedDef<NodeDefinitionBase> arms_[MAX_ARMS];
          int *selectCallCount_;
        };
      } // namespace testing
    } // namespace scene
  } // namespace app
} // namespace loka

#endif // TEST_BUILD

#endif // LOKA_TESTING_SCENE_PROBE_ARM_SEAT_DEFINITION_HPP
