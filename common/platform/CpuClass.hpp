#ifndef LOKA_PLATFORM_CPU_CLASS_HPP
#define LOKA_PLATFORM_CPU_CLASS_HPP

namespace loka
{
  namespace platform
  {
    /** A capability class for choosing default expression, not a CPU identity:
        a 68k binary under the PowerPC emulator answers CPU_CLASS_68K, and every
        Mac OS X and Windows machine answers CPU_CLASS_G3_OR_FASTER. */
    enum CpuClass
    {
      CPU_CLASS_68K,
      CPU_CLASS_PPC_60X,
      CPU_CLASS_G3_OR_FASTER,
      CPU_CLASS_COUNT
    };
    /** Presence and value from one query; a target that cannot tell declines.
        Exactly one implementation is compiled per target (CpuClass.cpp or
        ToolboxCpuClass.cpp). */
    bool queryCpuClass(CpuClass &out);
  } // namespace platform
} // namespace loka
#endif
