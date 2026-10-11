#ifndef LOKA_TESTS_NULL_WINDOW_TEST_ACCESS_HPP
#define LOKA_TESTS_NULL_WINDOW_TEST_ACCESS_HPP

#include "platform/null/NullWindow.hpp"

namespace loka
{
  namespace app
  {
    namespace testing
    {
      /** Reads the Null rail's edited projection record. */
      class NullWindowTestAccess
      {
      public:
        static bool projectedEdited(const NullWindow &window)
        {
          return window.projectedEdited_;
        }
        static unsigned editedProjectionCount(const NullWindow &window)
        {
          return window.editedProjectionCount_;
        }
      };
    } // namespace testing
  } // namespace app
} // namespace loka

#endif
