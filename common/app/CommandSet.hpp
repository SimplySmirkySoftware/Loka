#ifndef LOKA_APP_COMMAND_SET_HPP
#define LOKA_APP_COMMAND_SET_HPP

#include "core/State.hpp"

namespace loka
{
  namespace app
  {
    /** Stable, noncopyable owner of command endpoints, normally a Node member.
        E's commands must be dense, zero-based and have no aliases. Pass E's
        trailing _COUNT sentinel as N; that sentinel is the domain authority.
        Borrowed endpoints remain valid only for this owner's lifetime. */
    template <class E, int N> class CommandSet
    {
    public:
      CommandSet() {}

      template <E Id> loka::core::EmitterState *slot()
      {
        typedef char Wall[(Id >= 0 && Id < N) ? 1 : -1];
        (void)sizeof(Wall);
        return &this->slots_[Id];
      }

      // Not named `id`: GCC 4.0's Objective-C++ reads `id <` as a protocol
      // qualifier in every .mm that includes this header (Tiger UB1).
      loka::core::EmitterState *emitter(E command)
      {
        if (command < 0 || command >= N)
          return 0;
        return &this->slots_[command];
      }

      static unsigned count() { return N; }

    private:
      CommandSet(const CommandSet &);
      CommandSet &operator=(const CommandSet &);
      loka::core::EmitterState slots_[N];
    };
  } // namespace app
} // namespace loka

#endif
