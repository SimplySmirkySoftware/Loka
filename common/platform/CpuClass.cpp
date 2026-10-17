// Retro68's 68k and PPC compilers define macintosh; the Linux host does not,
// including host tests compiled with LOKA_RETRO68. Paired with ToolboxCpuClass.cpp.
#if !defined(macintosh)
#include "platform/CpuClass.hpp"

namespace loka
{
  namespace platform
  {
    bool queryCpuClass(CpuClass &)
    {
      // Win32 and macOS answer CPU_CLASS_G3_OR_FASTER in #1425 PR 3 and PR 4.
      return false;
    }
  } // namespace platform
} // namespace loka
#endif
