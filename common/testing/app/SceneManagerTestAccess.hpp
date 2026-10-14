#ifndef LOKA_APP_TESTING_SCENE_MANAGER_TEST_ACCESS_HPP
#define LOKA_APP_TESTING_SCENE_MANAGER_TEST_ACCESS_HPP

#include "app/core/SceneManager.hpp"

namespace loka
{
  namespace app
  {
    namespace testing
    {
      /** Testing-only readers for SceneManager seat state. */
      class SceneManagerTestAccess
      {
      public:
        static bool hasPendingRequest(const SceneManager &manager)
        {
          return manager.request_ != SceneManager::REQUEST_NONE;
        }

        static const loka::app::scene::Scene *applying(const SceneManager &m)
        {
          return m.applying_;
        }

        /** Number of scenes waiting in the Window retirement pool. */
        static size_t retiredSceneCount(const SceneManager &manager)
        {
          return manager.retiredScenes_.size();
        }

        static loka::app::scene::Scene *desiredScene(const SceneManager &manager)
        {
          return manager.desired_;
        }

#ifdef TEST_BUILD
        /** Last refused identity, valid until the next App admission or manager destruction. */
        static loka::app::scene::Scene *lastPrepareRefusal(const SceneManager &manager)
        {
          return manager.lastPrepareRefusal_;
        }
#endif
      };
    } // namespace testing
  } // namespace app
} // namespace loka

#endif // LOKA_APP_TESTING_SCENE_MANAGER_TEST_ACCESS_HPP
