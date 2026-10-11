#ifndef LOKA_LAZY_SCOPE_DEFINITION_HPP
#define LOKA_LAZY_SCOPE_DEFINITION_HPP
#include <cstdlib>
#include "app/scene/boundary/LazyScopeNode.hpp"
#include "app/scene/boundary/detail/BranchSeatDeclaration.hpp"
#include "app/nodes/nestable/Match.hpp"
namespace loka
{
  namespace app
  {
    namespace scene
    {
      template <class K, class NodeT> struct LazyScopeProps : MatchProps<K>
      {
        explicit LazyScopeProps(loka::core::State<K> *key)
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
      namespace detail
      {
        /** Generic LazyScope keeps its existing unreserved internal route. */
        struct UnreservedScope
        {
          bool prepare() { return true; }
          const SeatReservation *reservation() const { return 0; }
          template <class K> bool accepts(const K &) const { return true; }
        };
      }
      /** A keyed branch whose runtime root owns and declares its arm. The key
          must outlive the seat; props are copied values. Replacement uses the
          same plan, publication, and retirement protocol as Keyed. */
      template <class K, class NodeT, class Reservation = detail::UnreservedScope>
      class LazyScopeDefinition : public NodeDefinitionBase, public IBranchSeatDefinition
      {
      public:
        LazyScopeDefinition(loka::core::State<K> &key, const typename NodeT::Props &props,
                            const Reservation &reservation = Reservation())
            : props_(&key),
              nodeProps_(props),
              reservation_(reservation),
              declaration_()
        {
          // This conversion is an always-on C++98 inheritance constraint.
          LazyScopeNode *requiredBase = static_cast<NodeT *>(0);
          (void)requiredBase;
        }
        LazyScopeDefinition(const LazyScopeDefinition &other)
            : NodeDefinitionBase(other),
              IBranchSeatDefinition(other),
              props_(other.props_.state),
              nodeProps_(other.nodeProps_),
              reservation_(other.reservation_),
              declaration_()
        {
#ifdef LOKA_LIFECYCLE_AUDIT
          if (other.declaration_.isSet())
            std::abort();
#endif
        }

        virtual bool prepareSeatReservation() { return this->reservation_.prepare(); }
        virtual const detail::SeatReservation *seatReservation() const { return this->reservation_.reservation(); }

        virtual Node *create() const
        {
          assert(false && "LazyScope seats materialize only through Boundary plan application");
          return 0;
        }
        virtual Node *createInPlace(void *) const
        {
          assert(false && "LazyScope seats have no runtime node");
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
        typedef LazyScopeDefinition CloneType;
        virtual NodeDefinitionBase *clone() const
        {
          return new LazyScopeDefinition(*this);
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
        virtual NodeDefinitionBase *armDefinition(unsigned arm) const
        {
          return arm == 0 && this->declaration_.isSet() ? this->declaration_->composition.root() : 0;
        }
        virtual const void *branchSeatTypeId() const
        {
          return LazyScopeProps<K, NodeT>::staticTypeId();
        }
        virtual NodeDefinitionBase *retainedDefinitionBranch(unsigned arm)
        {
          return this->armDefinition(arm);
        }

        virtual bool needsBranchDeclaration() const
        {
          return !this->declaration_.isSet() || !this->declaration_->matchesCurrentKey();
        }
        virtual BranchSeatDeclaration *declareBranchCandidate(ComponentContext &context)
        {
          if (!context.boundary())
            return 0;
          if (!this->reservation_.accepts(this->props_.state->get()))
          {
            if (this->seatReservation()) this->seatReservation()->request().refuse();
            return 0;
          }
          loka::core::OwnedDef<scene::KeyedGenerationDeclaration<K> > candidate(new scene::KeyedGenerationDeclaration<K>(this->props_.state));
          if (!candidate.isSet())
            return 0;
          NodeT *node = candidate->template createRoot<NodeT>(this->nodeProps_, context);
          if (!node)
            return 0;
          ComponentContext declarationContext(context);
          LazyScopeNode *owner = node;
          owner->prepareScope(declarationContext, candidate->composition);
          if (owner->scopeStatus() != LAZY_SCOPE_READY || !candidate->completeWindow())
            return 0;
          return candidate.take();
        }
        virtual void commitBranchDeclaration(BranchSeatDeclaration *candidate)
        {
          // Boundary retires the outgoing declaration scope and checks its wall
          // before calling this shared commit protocol (as it does for Keyed).
          this->declaration_.reset(candidate);
        }
        virtual BoundaryBranchSeatState *declaredBranchSeats() const
        {
          return this->declaration_.isSet() ? &this->declaration_->seats : 0;
        }

      private:
        LazyScopeProps<K, NodeT> props_;
        const typename NodeT::Props nodeProps_;
        Reservation reservation_;
        loka::core::OwnedDef<BranchSeatDeclaration> declaration_;
        LazyScopeDefinition &operator=(const LazyScopeDefinition &);
      };

      template <class K, class PropsT, class Reservation>
      inline LazyScopeDefinition<K, typename PropsT::NodeType, Reservation>
      LazyScope(loka::core::State<K> &key, const PropsT &props, const Reservation &reservation)
      {
        return LazyScopeDefinition<K, typename PropsT::NodeType, Reservation>(key, props, reservation);
      }

      template <class K, class PropsT>
      inline LazyScopeDefinition<K, typename PropsT::NodeType> LazyScope(loka::core::State<K> &key, const PropsT &props)
      {
        return LazyScopeDefinition<K, typename PropsT::NodeType>(key, props);
      }
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
