#include "app/scene/node/ComposableNode.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "app/scene/boundary/detail/BoundaryObservedState.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      namespace
      {
        const loka::core::LokaAllocationSite ObservedUseSite("Node", "ObservedUse");
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

      ObservedUse *ObservedUses::reserve(loka::core::StateBase *state)
      {
        ObservedUse *unused = 0;
        for (ObservedUse *row = this->head; row; row = row->next)
        {
          if (row->state == state) return row;
          if (!row->state) unused = row;
        }
        if (!unused)
        {
          unused = loka::core::LokaNew<ObservedUse>(ObservedUseSite);
          if (!unused) return 0;
          unused->next = this->head;
          this->head = unused;
        }
        unused->state = state;
        return unused;
      }

      void ObservedUses::discardPrepared()
      {
        for (ObservedUse *row = this->head; row; row = row->next)
          if (!row->subscription) row->state = 0;
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
            if (state && flags != NODE_DIRTY_NONE && !this->uses_.reserve(state))
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
            row->subscription->owner->withdraw(*row);
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
