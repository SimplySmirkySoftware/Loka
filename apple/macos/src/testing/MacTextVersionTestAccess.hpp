#ifndef LOKA_MAC_TEXT_VERSION_TEST_ACCESS_HPP
#define LOKA_MAC_TEXT_VERSION_TEST_ACCESS_HPP
#include "../context/MacTextContext.hpp"
namespace loka
{
  namespace dsl
  {
    namespace testing
    {
      /** Read-only snapshot access, kept off the context's production API. */
      class MacTextVersionTestAccess
      {
      public:
        static const MacTextVersion &version(const MacTextContext &context)
        {
          return context.version_;
        }
      };
    } // namespace testing
  } // namespace dsl
} // namespace loka
#endif
