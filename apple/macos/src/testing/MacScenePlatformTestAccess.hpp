#ifndef LOKA_MAC_SCENE_PLATFORM_TEST_ACCESS_HPP
#define LOKA_MAC_SCENE_PLATFORM_TEST_ACCESS_HPP

#include "../MacScenePlatformController.hpp"

namespace loka
{
  namespace dsl
  {
    namespace testing
    {
      class MacScenePlatformTestAccess
      {
      public:
        /** Exercise framed layout at an explicit origin without exposing the
            controller's internal traversal on its production surface. */
        static int layoutNode(::MacScenePlatformController &controller,
                              ::loka::app::scene::Node *node,
                              const ::loka::app::scene::LayoutState &state)
        {
          return controller.layoutNodeFromSceneState(node, state);
        }

        static bool hasPendingRelayout(const ::MacScenePlatformController &controller)
        {
          return controller.relayoutPending_;
        }

        static loka::app::scene::NodeDirtyFlags lastChangeFlags(const ::MacScenePlatformController &controller)
        {
          return controller.lastChangeFlags_;
        }

        static ::loka::app::scene::Node *rootNode(const ::MacScenePlatformController &controller)
        {
          return controller.rootNode_;
        }

        static int clientWidth(const ::MacScenePlatformController &controller)
        {
          return controller.clientWidth_;
        }

        static int clientHeight(const ::MacScenePlatformController &controller)
        {
          return controller.clientHeight_;
        }
      };
    } // namespace testing
  } // namespace dsl
} // namespace loka

#endif // LOKA_MAC_SCENE_PLATFORM_TEST_ACCESS_HPP
