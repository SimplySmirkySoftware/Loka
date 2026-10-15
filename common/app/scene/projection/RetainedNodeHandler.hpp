#ifndef LOKA_CORE2_SCENE_PROJECTION_RETAINED_NODE_HANDLER_HPP
#define LOKA_CORE2_SCENE_PROJECTION_RETAINED_NODE_HANDLER_HPP

#include "app/scene/projection/PlatformNodeHandler.hpp"
#include "app/scene/projection/SeamKey.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      /** Shared retained-context installation wall. Successful adoption must precede
          readLifecycleFactOnAttach, and afterAttach must remain last. In
          particular, Boundary compose may already have consumed pendingAttach
          before an OpenFileDialog context is created, so its afterAttach hook
          must present after the lifecycle fact has been read. The hook remains
          creation-only so an existing dialog does not present twice. */
      template <typename Derived, typename NodeT, typename CtxT>
      class RetainedNodeHandler : public IPlatformNodeHandler
      {
      protected:
        /** Supply only this handler's node-type capability at context creation. */
        static SeamKey<NodeT> seamKey()
        {
          return SeamKey<NodeT>();
        }

      public:
        virtual const void *nodeTypeKey() const
        {
          return NodeTypeToken<NodeT>();
        }

        virtual NodeContext *ensureContext(Node *node,
                                           IPlatformController *controller,
                                           const LayoutState &state)
        {
          NodeT *typed = Derived::cast(node);
          if (!typed || !controller)
          {
            return 0;
          }

          CtxT *ctx = static_cast<CtxT *>(typed->getContext());
          if (ctx)
          {
            Derived::refresh(ctx, state);
            return ctx;
          }

          ctx = Derived::create(typed, controller, state);
          if (!ctx)
          {
            return 0;
          }
          if (!typed->adoptContext(ctx))
          {
            return 0;
          }
          ctx->readLifecycleFactOnAttach();
          Derived::afterAttach(ctx);
          // The attach read / after-attach hook can run arbitrary reentrant
          // app code and retire this node's context. Toolbox file dialogs
          // only enroll here; their modal runs at App presentation completion.
          // Hand the caller what the node holds now — not the
          // pre-reentrancy local, which may already be freed.
          return static_cast<CtxT *>(typed->getContext());
        }

        static void afterAttach(CtxT *)
        {
        }

        static void refresh(CtxT *, const LayoutState &)
        {
        }
      };
    } // namespace scene
  } // namespace app
} // namespace loka

#endif // LOKA_CORE2_SCENE_PROJECTION_RETAINED_NODE_HANDLER_HPP
