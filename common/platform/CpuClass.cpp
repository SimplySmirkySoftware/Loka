// Retro68's 68k and PPC compilers define macintosh; the Linux host does not,
// including host tests compiled with LOKA_RETRO68. Paired with ToolboxCpuClass.cpp.
#if !defined(macintosh)
#include "platform/CpuClass.hpp"

namespace loka
{
  namespace platform
  {
    bool queryCpuClass(CpuClass &out)
    {
#if defined(_WIN32) || (defined(__APPLE__) && defined(__MACH__))
      out = CPU_CLASS_G3_OR_FASTER;
      return true;
#else
      (void)out;
      return false;
#endif
    }
  } // namespace platform
} // namespace loka
#endif
