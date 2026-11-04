#ifndef LOKA_TESTING_APP_TEST_ACCESS_HPP
#define LOKA_TESTING_APP_TEST_ACCESS_HPP

#include "app/core/App.hpp"
#include "core/Operation.hpp"

namespace loka
{
  namespace app
  {
    namespace testing
    {
      /** Scenario pumps use the production App admission between driver steps. */
      class AppTestAccess
      {
      public:
        /** Test completion mirrors the production rail order, once per turn. */
        static bool completeTurn(App &app)
        {
          const App::CompletionEntry entry =
              core::Operation::hasActive() ? App::COMPLETION_INHERITED_CLOCK : App::COMPLETION_OUTERMOST;
          core::Operation turn;
          turn.settle();
          app.admitAndApplyWindows();
          app.reconcileAtCompletion();
          const bool served = app.serveURLsAtCompletion(entry);
          if (served)
            app.admitAndApplyWindows();
          turn.close();
          app.reclaimWindows();
          return served;
        }

        static void retireComponents(App &app)
        {
          app.retireComponents();
        }

        static bool hasQuitAttempt(const App &app)
        {
          return app.hasQuitAttempt();
        }

        static size_t pendingWindowCloseCount(const App &app)
        {
          return app.pendingWindowClosures_.size();
        }

        static void flushWindowInvalidations(App &app)
        {
          app.flushWindowInvalidations();
          app.reconcileAtCompletion();
        }
      };
    }
  }
}

#endif
