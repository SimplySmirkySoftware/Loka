#ifndef LOKA_APP_SHOW_HPP
#define LOKA_APP_SHOW_HPP

#include "app/nodes/nestable/Fragment.hpp"
#include "app/scene/node/Conditional.hpp"

namespace loka
{
  namespace app
  {
    class ShowDefinition : public scene::NodeDefinitionBase,
                           public scene::IBranchSeatDefinition
    {
    public:
      explicit ShowDefinition(loka::core::State<bool> *condition)
          : scene::NodeDefinitionBase(),
            trueBranch_(),
            falseBranch_(),
            truePolicies_(),
            props_(condition, &this->trueBranch_, &this->falseBranch_)
      {
      }
      ShowDefinition(const ShowDefinition &other)
          : scene::NodeDefinitionBase(other),
            scene::IBranchSeatDefinition(other),
            trueBranch_(other.trueBranch_),
            falseBranch_(other.falseBranch_),
            truePolicies_(other.truePolicies_),
            props_(other.props_.condition, &this->trueBranch_, &this->falseBranch_)
      {
      }
      ShowDefinition &operator=(const ShowDefinition &other)
      {
        if (this != &other)
        {
          scene::NodeDefinitionBase::operator=(other);
          trueBranch_ = other.trueBranch_;
          falseBranch_ = other.falseBranch_;
          this->truePolicies_ = other.truePolicies_;
          props_.condition = other.props_.condition;
          props_.trueDef = &this->trueBranch_;
          props_.falseDef = &this->falseBranch_;
        }
        return *this;
      }

      /** Destroy the true arm on hide; reshow constructs fresh descendants. */
      ShowDefinition &destroyOnDetach()
      {
        this->truePolicies_.destroyOnDetach = true;
        return *this;
      }
      virtual scene::BranchPolicies armPolicies(unsigned arm) const
      {
        return arm == 1 ? this->truePolicies_ : scene::BranchPolicies();
      }

      virtual scene::Node *create() const
      {
        assert(false && "Show seats materialize only through Boundary plan application");
        return 0;
      }
      virtual scene::Node *createInPlace(void *) const
      {
        assert(false && "Show seats have no runtime node");
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
      typedef ShowDefinition CloneType;
      virtual scene::NodeDefinitionBase *clone() const
      {
        return new ShowDefinition(*this);
      }
      virtual scene::NodeKind nodeKind() const
      {
        return scene::NODE_KIND_UNKNOWN;
      }
      virtual const scene::PropsBase *propsBase() const
      {
        return &this->props_;
      }
      virtual bool applyPropsToNode(scene::Node *node) const
      {
        (void)node;
        return false;
      }
      virtual bool isCompatibleWithNode(const scene::Node *node) const
      {
        (void)node;
        return false;
      }
      virtual scene::IBranchSeatDefinition *asBranchSeatDefinition()
      {
        return this;
      }
      virtual bool requiresUniqueSiblingTag() const
      {
        return true;
      }
      virtual loka::core::StateBase *branchCondition() const
      {
        return this->props_.condition;
      }
      virtual bool selectArm(unsigned &armOut) const
      {
        if (!this->props_.condition)
        {
          return false;
        }
        armOut = this->props_.condition->get() ? 1u : 0u;
        return true;
      }
      virtual unsigned armCount() const
      {
        return 2;
      }
      virtual scene::NodeDefinitionBase *armDefinition(unsigned arm) const
      {
        if (arm >= this->armCount())
        {
          return 0;
        }
        FragmentDefinition *branch =
            arm == 1 ? const_cast<FragmentDefinition *>(&this->trueBranch_)
                     : const_cast<FragmentDefinition *>(&this->falseBranch_);
        scene::NodeDefinitionBase *onlyChild =
            branch->childrenCount() == 1 ? branch->childrenHead() : 0;
        return onlyChild && onlyChild->asBranchPolicyScopeDefinition()
                   ? onlyChild
                   : static_cast<scene::NodeDefinitionBase *>(branch);
      }
      virtual const void *branchSeatTypeId() const
      {
        return scene::ConditionalProps::staticTypeId();
      }
      virtual scene::NodeDefinitionBase *retainedDefinitionBranch(unsigned index)
      {
        if (index == 0)
        {
          return &this->trueBranch_;
        }
        if (index == 1)
        {
          return &this->falseBranch_;
        }
        return 0;
      }
      ShowDefinition &operator<<(scene::NodeDefinitionBase &child)
      {
        this->trueBranch_.addChild(&child);
        return *this;
      }
      ShowDefinition &operator<<(const scene::NodeDefinitionBase &child)
      {
        this->trueBranch_.addChild(const_cast<scene::NodeDefinitionBase *>(&child));
        return *this;
      }
      ShowDefinition &operator<<(scene::NodeDefinitionBase *ownedChild)
      {
        this->trueBranch_.addOwnedChild(ownedChild);
        return *this;
      }
      ShowDefinition &operator<<(const std::vector<scene::NodeDefinitionBase *> &container)
      {
        for (size_t i = 0; i < container.size(); ++i)
        {
          this->trueBranch_.addChild(container[i]);
        }
        return *this;
      }
      template <class ItemT, class FactoryT, class KeyExprT>
      ShowDefinition &operator<<(
          const ForBuilder<ItemT, FactoryT, KeyExprT> &builder)
      {
        builder.appendTo(this->trueBranch_);
        return *this;
      }

      const scene::ConditionalProps &props() const
      {
        return this->props_;
      }

      size_t childrenCount() const
      {
        return this->trueBranch_.childrenCount();
      }

    private:
      FragmentDefinition trueBranch_;
      FragmentDefinition falseBranch_;
      scene::BranchPolicies truePolicies_;
      scene::ConditionalProps props_;
    };

    namespace detail
    {
      template <typename T> struct ShowCondition
      {
        static char test(const loka::core::State<bool> *);
        static long test(...);
        enum
        {
          value = sizeof(test(static_cast<T *>(0))) == sizeof(char)
        };
      };

      template <bool Accepted> struct ShowResult
      {
      };

      template <> struct ShowResult<true>
      {
        typedef ShowDefinition Type;
      };
    } // namespace detail

    /** Borrow a boolean State lvalue; deduction preserves const and refuses temporaries. */
    template <typename ConditionT>
    inline typename detail::ShowResult<detail::ShowCondition<ConditionT>::value>::Type Show(ConditionT &condition)
    {
      const loka::core::State<bool> *state = &condition;
      return ShowDefinition(const_cast<loka::core::State<bool> *>(state));
    }

    template <typename ConditionT, typename ChildDefinitionT>
    inline typename detail::ShowResult<detail::ShowCondition<ConditionT>::value>::Type
    operator<<(ConditionT &condition, const ChildDefinitionT &child)
    {
      ShowDefinition definition = Show(condition);
      definition << child;
      return definition;
    }
  } // namespace app
} // namespace loka

#endif // LOKA_APP_SHOW_HPP
