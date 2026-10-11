#ifndef LOKA_APP_POLICY_SCOPE_HPP
#define LOKA_APP_POLICY_SCOPE_HPP

#include <cassert>
#include "app/nodes/nestable/Fragment.hpp"

namespace loka
{
  namespace app
  {
    /** Deprecated definition-only annotation for one conditional branch root.
        It will be replaced by branch-root policy modifiers such as
        destroyOnDetach() and deliverWhileDetached(). */
    class PolicyScopeDefinition : public scene::NodeDefinitionBase,
                                  public scene::IBranchPolicyScopeDefinition
    {
    public:
      PolicyScopeDefinition()
          : scene::NodeDefinitionBase(),
            content_(),
            policies_()
      {
      }
      PolicyScopeDefinition(const PolicyScopeDefinition &other)
          : scene::NodeDefinitionBase(other),
            scene::IBranchPolicyScopeDefinition(other),
            content_(other.content_),
            policies_(other.policies_)
      {
      }

      PolicyScopeDefinition &destroyOnDetach()
      {
        this->policies_.destroyOnDetach = true;
        return *this;
      }
      PolicyScopeDefinition &deliverWhileDetached()
      {
        this->policies_.deliverWhileDetached = true;
        return *this;
      }

      PolicyScopeDefinition &operator<<(scene::NodeDefinitionBase &child)
      {
        this->content_.addChild(&child);
        return *this;
      }
      PolicyScopeDefinition &operator<<(const scene::NodeDefinitionBase &child)
      {
        this->content_.addChild(const_cast<scene::NodeDefinitionBase *>(&child));
        return *this;
      }
      PolicyScopeDefinition &operator<<(scene::NodeDefinitionBase *ownedChild)
      {
        this->content_.addOwnedChild(ownedChild);
        return *this;
      }
      template <class ItemT, class FactoryT, class KeyExprT>
      PolicyScopeDefinition &operator<<(
          const ForBuilder<ItemT, FactoryT, KeyExprT> &builder)
      {
        builder.appendTo(this->content_);
        return *this;
      }

      virtual scene::Node *create() const
      {
        assert(false && "PolicyScope is legal only as the immediate branch root of a conditional seat");
        return 0;
      }
      virtual scene::Node *createInPlace(void *) const
      {
        assert(false && "PolicyScope never materializes a runtime node");
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
      typedef PolicyScopeDefinition CloneType;
      virtual scene::NodeDefinitionBase *clone() const
      {
        return new PolicyScopeDefinition(*this);
      }
      virtual scene::NodeKind nodeKind() const
      {
        return scene::NODE_KIND_UNKNOWN;
      }
      virtual const scene::PropsBase *propsBase() const
      {
        return 0;
      }
      virtual bool applyPropsToNode(scene::Node *) const
      {
        assert(false && "PolicyScope has no runtime props target");
        return false;
      }
      virtual scene::IBranchPolicyScopeDefinition *asBranchPolicyScopeDefinition()
      {
        return this;
      }
      virtual scene::NodeDefinitionBase *retainedDefinitionBranch(unsigned index)
      {
        return index == 0 ? static_cast<scene::NodeDefinitionBase *>(&this->content_) : 0;
      }
      virtual scene::BranchPolicies branchPolicies() const
      {
        return this->policies_;
      }
      virtual scene::NodeDefinitionBase *scopedBranchDefinition() const
      {
        return const_cast<FragmentDefinition *>(&this->content_);
      }

    private:
      FragmentDefinition content_;
      scene::BranchPolicies policies_;
    };

    inline PolicyScopeDefinition PolicyScope()
// The message form of the deprecated attribute is GCC 4.5 and later. Apple's
// gcc-4.0 and gcc-4.2 -- the compilers the 10.4u and Leopard SDK paths use --
// define __GNUC__ as 4 but reject it outright, so the version has to be part of
// the condition. Clang accepts the message form while reporting __GNUC__ 4.2,
// hence the separate arm.
#if defined(__clang__) || (defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)))
        __attribute__((deprecated("PolicyScope is deprecated and will be replaced by a branch-root policy modifier (destroyOnDetach()/deliverWhileDetached() applied directly to the branch-root node); see issue #126. It remains valid only as the sole branch root of a Show/Conditional.")));
#elif defined(__GNUC__)
        // Still deprecated on the legacy compilers, just without the reason.
        __attribute__((deprecated));
#else
        ;
#endif

    inline PolicyScopeDefinition PolicyScope()
    {
      return PolicyScopeDefinition();
    }
  } // namespace app
} // namespace loka

#endif // LOKA_APP_POLICY_SCOPE_HPP
