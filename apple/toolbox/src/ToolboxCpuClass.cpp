// Paired with common/platform/CpuClass.cpp: exactly one provider per target.
#if defined(macintosh)
#include "platform/CpuClass.hpp"

namespace loka
{
  namespace platform
  {
    bool queryCpuClass(CpuClass &out)
    {
#if defined(__m68k__)
      out = CPU_CLASS_68K;
      return true;
#elif defined(__ppc__) || defined(__POWERPC__)
      // PPC Gestalt classification arrives in #1425 PR 2.
      (void)out;
      return false;
#else
#error Unsupported Macintosh architecture for queryCpuClass
#endif
    }
  } // namespace platform
} // namespace loka
#endif
