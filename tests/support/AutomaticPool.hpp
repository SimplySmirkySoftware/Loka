#ifndef LOKA_TESTS_SUPPORT_AUTOMATIC_POOL_HPP
#define LOKA_TESTS_SUPPORT_AUTOMATIC_POOL_HPP

#include <cstring>

#include "core/SmallObjectPool.hpp"

namespace loka
{
  namespace testing
  {
    /** A SmallObjectPool in automatic storage, started from the zeroed state
     * a static pool has before main. The pool has no constructor, and C++98
     * zeroes such a class only in static storage: GCC 4.0 and 4.2 leave
     * `Pool p = Pool();` indeterminate, so a pin that relied on it read
     * garbage counters on the 10.6 toolchain (#1278).
     */
    template <class Source>
    struct AutomaticPool : loka::core::SmallObjectPool<Source>
    {
      AutomaticPool()
      {
        loka::core::SmallObjectPool<Source> &pool = *this;
        std::memset(static_cast<void *>(&pool), 0, sizeof(pool));
      }
    };
  } // namespace testing
} // namespace loka

#endif
