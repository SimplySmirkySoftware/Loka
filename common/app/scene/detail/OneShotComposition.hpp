#ifndef LOKA_APP_SCENE_DETAIL_ONE_SHOT_COMPOSITION_HPP
#define LOKA_APP_SCENE_DETAIL_ONE_SHOT_COMPOSITION_HPP

#include "app/scene/Scene.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      namespace detail
      {
        /** Owns a standalone composition for the duration of one synchronous root borrow. */
        class OneShotComposition
        {
          class QuietController : public IPlatformController
          {
            /** Cost: one no-op per Scene change notification, no rows. */
            virtual void onChange(Node *, NodeDirtyFlags, bool) {}
            /** Cost: one no-op per Scene synchronization, no rows. */
            virtual void synchronize() {}
            /** Cost: constant per Scene poll, no rows. */
            virtual bool hasPendingSync() const
            {
              return false;
            }
            /** Cost: one no-op at Scene teardown, no rows. */
            virtual void destroy() {}
          };

          /** Cost: three Scene facts after mount and each outer flush, no rows. */
          static bool admissible(const Scene &scene)
          {
            return scene.rootNode_ != 0 && scene.composed_ && !scene.whiteFlagFullRebuildPending_;
          }

        public:
          /** Lend only a settled, accepted tree. Caller runs on the main thread outside any Operation or Regime.
              Cost: one mount and detach per call, up to 100 outer flushes of this Scene, one writer walk. */
          template <class D, class Writer>
          static bool lend(const D &definition, Writer &writer, typename D::IsBoundaryDefinition * = 0)
          {
            assert(!loka::core::Operation::hasActive());
            QuietController controller;
            Scene scene(definition);
            if (!scene.getRootDefinition())
              return false;
            bool mounted;
            {
              loka::core::NextTickTracker::RunScope run(scene.nextTickTracker_);
              scene.updateAttached(true);
              scene.updateLifecycle(ON_ATTACH);
              mounted = scene.mount(&controller);
            }
            bool accepted = mounted && admissible(scene);
            for (unsigned runs = 0; accepted && runs < 100 && scene.hasPendingInvalidation(); ++runs)
            {
              scene.flushInvalidation();
              accepted = admissible(scene);
            }
            accepted = accepted && !scene.hasPendingInvalidation() && writer(scene.rootNode_);
            scene.updateAttached(false);
            scene.updateLifecycle(ON_DETACH);
            return accepted;
          }
        };
      } // namespace detail
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
