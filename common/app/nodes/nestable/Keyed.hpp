#ifndef LOKA_APP_KEYED_HPP
#define LOKA_APP_KEYED_HPP

#include <cstdlib>
#include "app/reservation/SeatNodes.hpp"
#include "app/nodes/nestable/Match.hpp"
#include "app/scene/boundary/GenerationRoot.hpp"
#include "app/scene/boundary/detail/BranchSeatDeclaration.hpp"

namespace loka
{
  namespace app
  {

    namespace scene
    {
      class KeyedGenerationRoot;
      struct KeyedGenerationTypeTag
      {
      };
      struct KeyedGenerationProps : NodePropsBase<KeyedGenerationProps>
      {
        typedef KeyedGenerationTypeTag TypeTag;
        typedef KeyedGenerationRoot NodeType;
        bool operator<(const PropsBase &) const
        {
          return false;
        }
      };

      /** Runtime owner of one Keyed arm, independent of its definition lifetime. */
      class KeyedGenerationRoot : public GenerationRoot
      {
      public:
        typedef KeyedGenerationTypeTag TypeTag;
        explicit KeyedGenerationRoot(const KeyedGenerationProps &) {}
        virtual const void *nodeTypeKey() const
        {
          return NodeTypeToken<KeyedGenerationRoot>();
        }
      };
      /** Runtime generation roots are transferred once, never props-reconciled. */
      template <> struct NodePropsApplier<KeyedGenerationRoot, KeyedGenerationProps>
      {
        static bool apply(KeyedGenerationRoot *, const KeyedGenerationProps &)
        {
          return false;
        }
      };
      /** Generated scaffold for the root factory and completed declaration window. */
      template <class List> struct KeyedNodeRecipe
      {
        // createRoot below and BranchSeatDeclaration::completeWindow insert these two nodes.
        typedef reservation::Nodes<scene::KeyedGenerationRoot, 1, reservation::Nodes<FragmentNode, 1, List> > Type;
      };
    } // namespace scene

    /** Live key identity for a declaration seat. */
    template <class K> struct KeyedProps : MatchProps<K>
    {
      explicit KeyedProps(loka::core::State<K> *key)
          : MatchProps<K>(key)
      {
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
    };

    /** One branch, declared again from its enclosing boundary's current members
        when the key changes. The boundary and key must outlive the seat.
        Replacement never retains or reconciles the outgoing subtree. */
    template <class K> class KeyedDefinition : public scene::NodeDefinitionBase, public scene::IBranchSeatDefinition
    {
      /** Cloned declaration instruction; borrows the enclosing boundary. */
      struct DeclarerDefinition
      {
        virtual ~DeclarerDefinition() {}
        virtual DeclarerDefinition *clone() const = 0;
        virtual scene::BoundaryNode *owner() const = 0;
        virtual void declare(scene::NodeComposition &) const = 0;
        virtual bool installReservation() = 0;
        virtual const scene::detail::SeatReservation *reservation() const = 0;
      };
      template <class N, class List> struct MemberDeclarer : DeclarerDefinition
      {
        MemberDeclarer(N *node, void (N::*method)(scene::NodeComposition &))
            : node_(node),
              method_(method),
              reservation_(0)
        {
        }
        virtual DeclarerDefinition *clone() const
        {
          return new MemberDeclarer(this->node_, this->method_);
        }
        virtual scene::BoundaryNode *owner() const
        {
          return this->method_ ? this->node_ : 0;
        }
        virtual void declare(scene::NodeComposition &c) const
        {
          (this->node_->*this->method_)(c);
        }
        virtual bool installReservation()
        {
          if (this->reservation_)
            return true;
          scene::detail::SeatLayoutTable table;
          if (!reservation::detail::Emitter<typename scene::KeyedNodeRecipe<List>::Type>::emit(table))
            return false;
          this->reservation_ = this->node_->installSeatReservation(table);
          return this->reservation_ != 0;
        }
        virtual const scene::detail::SeatReservation *reservation() const { return this->reservation_; }
        N *node_;
        void (N::*method_)(scene::NodeComposition &);
        const scene::detail::SeatReservation *reservation_;
      };

    public:
      template <class N, class List>
      KeyedDefinition(loka::core::State<K> &key,
                      N *owner,
                      void (N::*method)(scene::NodeComposition &),
                      reservation::SeatNodes<List>)
          : props_(&key),
            declarer_(new MemberDeclarer<N, List>(owner, method)),
            declaration_()
      {
        reservation::detail::validate<typename scene::KeyedNodeRecipe<List>::Type>();
        assert(owner && method && "Keyed requires an enclosing boundary member");
      }
      KeyedDefinition(const KeyedDefinition &other)
          : scene::NodeDefinitionBase(other),
            scene::IBranchSeatDefinition(other),
            props_(other.props_.state),
            declarer_(other.declarer_.isSet() ? other.declarer_->clone() : 0),
            declaration_()
      {
#ifdef LOKA_LIFECYCLE_AUDIT
        if (other.declaration_.isSet())
        {
          assert(false && "committed Keyed declarations cannot be copied");
          std::abort();
        }
#endif
      }

      virtual bool prepareSeatReservation()
      {
        return this->declarer_.isSet() && this->declarer_->installReservation();
      }

      virtual const scene::detail::SeatReservation *seatReservation() const
      {
        return this->declarer_.isSet() ? this->declarer_->reservation() : 0;
      }

      virtual scene::Node *create() const
      {
        assert(false && "Keyed seats materialize only through Boundary plan application");
        return 0;
      }
      virtual scene::Node *createInPlace(void *) const
      {
        assert(false && "Keyed seats have no runtime node");
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
      typedef KeyedDefinition CloneType;
      virtual scene::NodeDefinitionBase *clone() const
      {
        KeyedDefinition *copy = new KeyedDefinition(*this);
        if (copy && !copy->declarer_.isSet())
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
        armOut = 0;
        return this->props_.state != 0;
      }
      virtual unsigned armCount() const
      {
        return 1;
      }
      virtual scene::NodeDefinitionBase *armDefinition(unsigned arm) const
      {
        return arm == 0 && this->declaration_.isSet() ? this->declaration_->composition.root() : 0;
      }
      virtual const void *branchSeatTypeId() const
      {
        return KeyedProps<K>::staticTypeId();
      }
      virtual scene::NodeDefinitionBase *retainedDefinitionBranch(unsigned arm)
      {
        return this->armDefinition(arm);
      }

      virtual bool needsBranchDeclaration() const
      {
        return !this->declaration_.isSet() || !this->declaration_->matchesCurrentKey();
      }
      virtual scene::BranchSeatDeclaration *declareBranchCandidate(scene::ComponentContext &context)
      {
        const bool valid = this->declarer_.isSet() && this->declarer_->owner() == context.boundary();
        assert(valid && "Keyed declarer must be a member of the enclosing boundary");
        if (!valid || !this->declarer_->installReservation())
          return 0;
        loka::core::OwnedDef<scene::KeyedGenerationDeclaration<K> > candidate(new scene::KeyedGenerationDeclaration<K>(this->props_.state));
        if (!candidate.isSet())
          return 0;
        scene::KeyedGenerationRoot *root =
            candidate->template createRoot<scene::KeyedGenerationRoot>(scene::KeyedGenerationProps(), context);
        if (!root)
          return 0;
        scene::ComponentContext declarationContext(context);
        declarationContext.setStateOwner(root->asStateOwner());
        candidate->composition.setContext(&declarationContext);
        {
          scene::NodeComposition::CompositionScope window(candidate->composition);
          this->declarer_->declare(candidate->composition);
        }
        candidate->composition.setContext(0);
        if (root->scopeStatus() != scene::LAZY_SCOPE_READY || !candidate->completeWindow())
          return 0;
        return candidate.take();
      }
      virtual void commitBranchDeclaration(scene::BranchSeatDeclaration *candidate)
      {
#ifdef LOKA_LIFECYCLE_AUDIT
        if (this->declaration_.isSet())
          this->declarer_->owner()->assertNoBranchSeatScopeReferences(&this->declaration_->seats);
#endif
        this->declaration_.reset(candidate);
      }
      virtual scene::BoundaryBranchSeatState *declaredBranchSeats() const
      {
        return this->declaration_.isSet() ? &this->declaration_->seats : 0;
      }

    private:
      KeyedProps<K> props_;
      loka::core::OwnedDef<DeclarerDefinition> declarer_;
      loka::core::OwnedDef<scene::BranchSeatDeclaration> declaration_;
      KeyedDefinition &operator=(const KeyedDefinition &);
    };

    template <class K, class N, class List>
    inline KeyedDefinition<K> Keyed(loka::core::State<K> &key,
                                    N *owner,
                                    void (N::*method)(scene::NodeComposition &),
                                    reservation::SeatNodes<List> descriptor)
    {
      return KeyedDefinition<K>(key, owner, method, descriptor);
    }

  } // namespace app
} // namespace loka
#endif
