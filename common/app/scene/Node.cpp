#include "app/scene/node/ComposableNode.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "app/scene/boundary/Boundary.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      namespace
      {
        char OwnInputSentinel;
        const loka::core::LokaAllocationSite ObservedUseSite("Node", "ObservedUse");
      }

      DirtySourceKey DirtySourceKey::ownInput()
      {
        return DirtySourceKey(&OwnInputSentinel);
      }

      bool DirtySourceKey::isOwnInput() const
      {
        return this->value_ == &OwnInputSentinel;
      }

      void BoundaryObservedState::registerUse(
          ObservedUse &use, ObservedUseChain &chain, DirtySourceKey source, Node *node, NodeDirtyFlags flags)
      {
        if (use.subscription && use.subscription != &chain)
          use.subscription->boundary->observedState_.withdraw(use);
        if (!use.subscription)
          chain.link(use, source, node);
        if (use.generation != this->pass.generation)
          use.flags = NODE_DIRTY_NONE;
        use.generation = this->pass.generation;
        use.flags = static_cast<NodeDirtyFlags>(use.flags | flags);
      }

      void Node::requestOwnInput(NodeDirtyFlags flags)
      {
        if (flags == NODE_DIRTY_NONE)
          return;
        if (this->lifecycleFact() != NODE_FACT_ATTACHED)
          return;
        for (ObservedUse *use = this->uses_.head; use; use = use->next)
        {
          if (!use->source.isOwnInput())
            continue;
          if (!use->subscription)
            return;
          if ((flags & ~use->flags) != 0)
            return;
          this->uses_.include(flags);
          use->subscription->boundary->requestOwnInputUpdate(flags);
          return;
        }
      }

      ObservedUses::~ObservedUses()
      {
        while (this->head)
        {
          ObservedUse *row = this->head;
          this->head = row->next;
          assert(!row->subscription);
          loka::core::LokaDelete(row, ObservedUseSite);
        }
      }

      ObservedUse *ObservedUses::reserve(DirtySourceKey source)
      {
        ObservedUse *unused = 0;
        for (ObservedUse *row = this->head; row; row = row->next)
        {
          if (row->source == source)
            return row;
          if (row->source.isFree())
            unused = row;
        }
        if (!unused)
        {
          unused = loka::core::LokaNew<ObservedUse>(ObservedUseSite);
          if (!unused) return 0;
          unused->next = this->head;
          this->head = unused;
        }
        unused->source = source;
        return unused;
      }

      void ObservedUses::discardPrepared()
      {
        for (ObservedUse *row = this->head; row; row = row->next)
          if (!row->subscription)
            row->source = DirtySourceKey::none();
      }

      void Node::render(IPlatformController *controller)
      {
        if (this->context)
          this->context->render(controller);
        else if (controller)
          controller->noteMissingRenderContext();
      }

      bool Node::prepareObservedUses()
      {
        class Reserve : public DirtySourceRegistrar
        {
        public:
          explicit Reserve(ObservedUses &uses) : uses_(uses), complete(true) {}
          virtual void markDirtyOnChange(loka::core::StateBase *state, NodeDirtyFlags flags)
          {
            if (state && flags != NODE_DIRTY_NONE && !this->uses_.reserve(DirtySourceKey::ofState(state)))
              this->complete = false;
          }
          virtual void markDirtyOnOwnInput(NodeDirtyFlags flags)
          {
            if (flags != NODE_DIRTY_NONE && acceptsOwnInput(flags) && !this->uses_.reserve(DirtySourceKey::ownInput()))
              this->complete = false;
          }
          ObservedUses &uses_;
          bool complete;
        } registrar(this->uses_);
        this->declareDirtySources(registrar);
        if (!registrar.complete) this->uses_.discardPrepared();
        return registrar.complete;
      }

      void ObservedUses::withdraw()
      {
        for (ObservedUse *row = this->head; row; row = row->next)
          if (row->subscription)
            row->subscription->boundary->observedState_.withdraw(*row);
        this->discardPrepared();
      }

      void Node::withdrawStateParticipation()
      {
        this->uses_.withdraw();
        IStateOwner *owner = this->asStateOwner();
        if (owner)
          owner->withdrawOwnedStates();
        ComposableNode *composable = this->asComposable();
        if (composable)
          composable->withdrawParticipants();
      }

      void Node::bindingsFollowProps()
      {
        ComposableNode *composable = this->asComposable();
        // "Composed and not detached" is the boundary edge, not the full
        // attached triple: a scene mounted without a Window (the public
        // Scene::mount(IPlatformController*) path) composes with a null
        // window, and its retained nodes must still follow their props.
        if (!composable || !composable->attached_.boundary_)
        {
          return;
        }
        composable->releaseCallbacks();
        composable->declareBindingsWithToken();
      }
    } // namespace scene
  } // namespace app
} // namespace loka
