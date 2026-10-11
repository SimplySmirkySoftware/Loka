#ifndef LOKA_APP_MATCH_HPP
#define LOKA_APP_MATCH_HPP

#include <cassert>
#include "app/nodes/nestable/Fragment.hpp"
#include "core/State.hpp"
#include "core/util/OwnedDef.hpp"

namespace loka
{
  namespace app
  {
    /** Compose-invariant input for one typed Match seat. Arm shape and
        contents are compared separately by the branch-seat kernel. */
    template <typename T> struct MatchProps : public scene::PropsBase
    {
      explicit MatchProps(loka::core::State<T> *observedState = 0)
          : state(observedState)
      {
      }

      static scene::Node *rejectRuntimeFactory(const scene::PropsBase &)
      {
        assert(false && "Match props have no runtime node factory");
        return 0;
      }
      virtual scene::PropsBase::NodeFactoryFunc nodeFactory() const
      {
        return &MatchProps::rejectRuntimeFactory;
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
      bool operator<(const scene::PropsBase &rhs) const
      {
        if (rhs.propsTypeId() != this->propsTypeId())
        {
          return false;
        }
        const MatchProps &other = static_cast<const MatchProps &>(rhs);
        return this->state < other.state;
      }

      loka::core::State<T> *state;
    };

    /** Definition-only ordered branch seat selected from a live State<T>.
        Arm identity is declaration order and the first matching arm wins. */
    template <typename T>
    class MatchDefinition : public scene::NodeDefinitionBase,
                            public scene::IBranchSeatDefinition
    {
    public:
      /** Predicates are pure selection queries: they must not write State.
          userData is borrowed and must outlive every visit of this seat.
          Declared before the matcher types below: a nested class may only
          name members already declared, and Clang enforces that. */
      typedef bool (*PredicateFn)(const T &, void *);

    private:
      struct Matcher
      {
        virtual ~Matcher() {}
        virtual bool matches(const T &value) const = 0;
        virtual Matcher *clone() const = 0;
        virtual bool isOtherwise() const
        {
          return false;
        }
      };

      struct ValueMatcher : public Matcher
      {
        explicit ValueMatcher(const T &matchedValue)
            : value(matchedValue)
        {
        }
        virtual bool matches(const T &candidate) const
        {
          return candidate == this->value;
        }
        virtual Matcher *clone() const
        {
          return new ValueMatcher(this->value);
        }

        T value;
      };

      struct PredicateMatcher : public Matcher
      {
        PredicateMatcher(PredicateFn predicateValue,
                         void *userDataValue)
            : predicate(predicateValue),
              userData(userDataValue)
        {
        }
        virtual bool matches(const T &candidate) const
        {
          return this->predicate && this->predicate(candidate, this->userData);
        }
        virtual Matcher *clone() const
        {
          return new PredicateMatcher(this->predicate, this->userData);
        }

        PredicateFn predicate;
        void *userData;
      };

      struct OtherwiseMatcher : public Matcher
      {
        virtual bool matches(const T &) const
        {
          return true;
        }
        virtual Matcher *clone() const
        {
          return new OtherwiseMatcher();
        }
        virtual bool isOtherwise() const
        {
          return true;
        }
      };

      class Arm
      {
      public:
        Arm()
            : matcher_(0),
              branch_()
        {
        }
        ~Arm()
        {
          delete this->matcher_;
        }
        void adopt(Matcher *matcher, FragmentDefinition *branch)
        {
          delete this->matcher_;
          this->matcher_ = matcher;
          this->branch_.reset(branch);
        }
        bool matches(const T &value) const
        {
          return this->matcher_ && this->matcher_->matches(value);
        }
        bool isOtherwise() const
        {
          return this->matcher_ && this->matcher_->isOtherwise();
        }
        FragmentDefinition *branch() const
        {
          return this->branch_.get();
        }
        Arm *clone() const
        {
          Matcher *matcherCopy = this->matcher_ ? this->matcher_->clone() : 0;
          if (!matcherCopy)
          {
            return 0;
          }
          loka::core::OwnedDef<scene::NodeDefinitionBase> branchCopy(
              this->branch_.isSet() ? this->branch_->clone() : 0);
          if (!branchCopy.isSet())
          {
            delete matcherCopy;
            return 0;
          }
          Arm *copy = new Arm();
          if (!copy)
          {
            delete matcherCopy;
            return 0;
          }
          copy->adopt(matcherCopy,
                      static_cast<FragmentDefinition *>(branchCopy.take()));
          return copy;
        }

      private:
        Matcher *matcher_;
        loka::core::OwnedDef<FragmentDefinition> branch_;

        Arm(const Arm &);
        Arm &operator=(const Arm &);
      };

    public:
      enum
      {
        MAX_ARMS = 8
      };

      explicit MatchDefinition(loka::core::State<T> *state)
          : scene::NodeDefinitionBase(),
            scene::IBranchSeatDefinition(),
            props_(state)
      {
        this->initializeArms();
      }
      MatchDefinition(const MatchDefinition &other)
          : scene::NodeDefinitionBase(other),
            scene::IBranchSeatDefinition(other),
            props_(other.props_.state)
      {
        this->initializeArms();
        this->copyArms(other);
      }
      virtual ~MatchDefinition()
      {
        this->clearArms();
      }

      virtual scene::Node *create() const
      {
        assert(false && "Match seats materialize only through Boundary plan application");
        return 0;
      }
      virtual scene::Node *createInPlace(void *) const
      {
        assert(false && "Match seats have no runtime node");
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
      typedef MatchDefinition CloneType;
      virtual scene::NodeDefinitionBase *clone() const
      {
        MatchDefinition *copy = new MatchDefinition(*this);
        if (copy && copy->armCount() != this->armCount())
        {
          delete copy;
          return 0;
        }
        return copy;
      }
      virtual scene::NodeKind nodeKind() const
      {
        return scene::NODE_KIND_UNKNOWN;
      }
      virtual const scene::PropsBase *propsBase() const
      {
        return &this->props_;
      }
      virtual bool applyPropsToNode(scene::Node *) const
      {
        return false;
      }
      virtual bool isCompatibleWithNode(const scene::Node *) const
      {
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
        return this->props_.state;
      }
      virtual bool selectArm(unsigned &armOut) const
      {
        if (!this->props_.state)
        {
          return false;
        }
        const T &value = this->props_.state->get();
        const unsigned count = this->armCount();
        for (unsigned arm = 0; arm < count; ++arm)
        {
          if (this->arms_[arm]->matches(value))
          {
            armOut = arm;
            return true;
          }
        }
        return false;
      }
      virtual unsigned armCount() const
      {
        unsigned count = 0;
        while (count < MAX_ARMS && this->arms_[count])
        {
          ++count;
        }
        return count;
      }
      virtual scene::NodeDefinitionBase *armDefinition(unsigned arm) const
      {
        if (arm >= this->armCount())
        {
          return 0;
        }
        FragmentDefinition *branch = this->arms_[arm]->branch();
        scene::NodeDefinitionBase *onlyChild =
            branch && branch->childrenCount() == 1 ? branch->childrenHead() : 0;
        return onlyChild && onlyChild->asBranchPolicyScopeDefinition()
                   ? onlyChild
                   : static_cast<scene::NodeDefinitionBase *>(branch);
      }
      virtual const void *branchSeatTypeId() const
      {
        return MatchProps<T>::staticTypeId();
      }
      virtual scene::NodeDefinitionBase *retainedDefinitionBranch(unsigned arm)
      {
        return this->armDefinition(arm);
      }

      MatchDefinition &arm(const T &value, scene::NodeDefinitionBase &definition)
      {
        return this->appendValueClone(value, definition);
      }
      MatchDefinition &arm(const T &value, const scene::NodeDefinitionBase &definition)
      {
        return this->appendValueClone(value, definition);
      }
      MatchDefinition &arm(const T &value, scene::NodeDefinitionBase *ownedDefinition)
      {
        return this->appendOwned(new ValueMatcher(value), ownedDefinition);
      }
      MatchDefinition &arm(PredicateFn predicate,
                           void *userData,
                           scene::NodeDefinitionBase &definition)
      {
        return this->appendPredicateClone(predicate, userData, definition);
      }
      MatchDefinition &arm(PredicateFn predicate,
                           void *userData,
                           const scene::NodeDefinitionBase &definition)
      {
        return this->appendPredicateClone(predicate, userData, definition);
      }
      MatchDefinition &arm(PredicateFn predicate,
                           void *userData,
                           scene::NodeDefinitionBase *ownedDefinition)
      {
        assert(predicate && "Match predicate arms require a predicate");
        if (!predicate)
        {
          delete ownedDefinition;
          return *this;
        }
        return this->appendOwned(new PredicateMatcher(predicate, userData),
                                 ownedDefinition);
      }
      MatchDefinition &otherwise(scene::NodeDefinitionBase &definition)
      {
        return this->appendOtherwiseClone(definition);
      }
      MatchDefinition &otherwise(const scene::NodeDefinitionBase &definition)
      {
        return this->appendOtherwiseClone(definition);
      }
      MatchDefinition &otherwise(scene::NodeDefinitionBase *ownedDefinition)
      {
        return this->appendOwned(new OtherwiseMatcher(), ownedDefinition);
      }

      const MatchProps<T> &props() const
      {
        return this->props_;
      }

    private:
      void initializeArms()
      {
        for (unsigned arm = 0; arm < MAX_ARMS; ++arm)
        {
          this->arms_[arm] = 0;
        }
      }
      void clearArms()
      {
        for (unsigned arm = 0; arm < MAX_ARMS; ++arm)
        {
          delete this->arms_[arm];
          this->arms_[arm] = 0;
        }
      }
      void copyArms(const MatchDefinition &other)
      {
        const unsigned count = other.armCount();
        for (unsigned arm = 0; arm < count; ++arm)
        {
          this->arms_[arm] = other.arms_[arm]->clone();
          if (!this->arms_[arm])
          {
            this->clearArms();
            return;
          }
        }
      }
      MatchDefinition &appendValueClone(
          const T &value,
          const scene::NodeDefinitionBase &definition)
      {
        loka::core::OwnedDef<scene::NodeDefinitionBase> root(definition.clone());
        if (!root.isSet())
        {
          return *this;
        }
        return this->appendOwned(new ValueMatcher(value), root.take());
      }
      MatchDefinition &appendPredicateClone(
          PredicateFn predicate,
          void *userData,
          const scene::NodeDefinitionBase &definition)
      {
        assert(predicate && "Match predicate arms require a predicate");
        if (!predicate)
        {
          return *this;
        }
        loka::core::OwnedDef<scene::NodeDefinitionBase> root(definition.clone());
        if (!root.isSet())
        {
          return *this;
        }
        return this->appendOwned(new PredicateMatcher(predicate, userData),
                                 root.take());
      }
      MatchDefinition &appendOtherwiseClone(
          const scene::NodeDefinitionBase &definition)
      {
        loka::core::OwnedDef<scene::NodeDefinitionBase> root(definition.clone());
        if (!root.isSet())
        {
          return *this;
        }
        return this->appendOwned(new OtherwiseMatcher(), root.take());
      }
      MatchDefinition &appendOwned(Matcher *ownedMatcher,
                                   scene::NodeDefinitionBase *ownedDefinition)
      {
        loka::core::OwnedDef<scene::NodeDefinitionBase> root(ownedDefinition);
        const unsigned index = this->armCount();
        const bool followsOtherwise =
            index > 0 && this->arms_[index - 1]->isOtherwise();
        assert(!followsOtherwise && "Match otherwise must be the final arm");
        assert(index < MAX_ARMS && "Match arm capacity exceeded");
        if (followsOtherwise || index >= MAX_ARMS || !ownedMatcher)
        {
          delete ownedMatcher;
          return *this;
        }

        loka::core::OwnedDef<FragmentDefinition> branch(new FragmentDefinition());
        if (!branch.isSet())
        {
          delete ownedMatcher;
          return *this;
        }
        if (root.isSet())
        {
          branch->addOwnedChild(root.take());
        }

        Arm *candidate = new Arm();
        if (!candidate)
        {
          delete ownedMatcher;
          return *this;
        }
        candidate->adopt(ownedMatcher, branch.take());
        this->arms_[index] = candidate;
        return *this;
      }

      MatchProps<T> props_;
      Arm *arms_[MAX_ARMS];

      MatchDefinition &operator=(const MatchDefinition &);
    };

    template <typename T>
    inline MatchDefinition<T> Match(loka::core::State<T> &state)
    {
      return MatchDefinition<T>(&state);
    }

    template <typename T>
    inline MatchDefinition<T> Match(const loka::core::State<T> &state)
    {
      return MatchDefinition<T>(const_cast<loka::core::State<T> *>(&state));
    }
  } // namespace app
} // namespace loka

#endif // LOKA_APP_MATCH_HPP
