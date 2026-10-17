// Paired with common/platform/CpuClass.cpp: exactly one provider per target.
#if defined(macintosh)
#include "ToolboxCpuClassify.hpp"
#include <Gestalt.h>

namespace
{
  typedef char Cpu601MatchesGestalt[(loka::toolbox::kCpu601 == gestaltCPU601) ? 1 : -1];
  typedef char Cpu603MatchesGestalt[(loka::toolbox::kCpu603 == gestaltCPU603) ? 1 : -1];
  typedef char Cpu604MatchesGestalt[(loka::toolbox::kCpu604 == gestaltCPU604) ? 1 : -1];
  typedef char Cpu603eMatchesGestalt[(loka::toolbox::kCpu603e == gestaltCPU603e) ? 1 : -1];
  typedef char Cpu603evMatchesGestalt[(loka::toolbox::kCpu603ev == gestaltCPU603ev) ? 1 : -1];
  typedef char Cpu604eMatchesGestalt[(loka::toolbox::kCpu604e == gestaltCPU604e) ? 1 : -1];
  typedef char Cpu604evMatchesGestalt[(loka::toolbox::kCpu604ev == gestaltCPU604ev) ? 1 : -1];
  typedef char Cpu750MatchesGestalt[(loka::toolbox::kCpu750 == gestaltCPU750) ? 1 : -1];
  typedef char CpuG4MatchesGestalt[(loka::toolbox::kCpuG4 == gestaltCPUG4) ? 1 : -1];
} // namespace

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
      // One type query per StandardCardStyle snapshot; family only on decline.
      // No allocation or row walk. Never consume a failed Gestalt response.
      long type;
      const bool typeAnswered = Gestalt(gestaltNativeCPUtype, &type) == noErr;
      if (toolbox::ClassifyPowerPCCpu(typeAnswered, typeAnswered ? type : 0, false, 0, out))
        return true;
      long family;
      const bool familyAnswered = Gestalt(gestaltNativeCPUfamily, &family) == noErr;
      return toolbox::ClassifyPowerPCCpu(false, 0, familyAnswered, familyAnswered ? family : 0, out);
#else
#error Unsupported Macintosh architecture for queryCpuClass
#endif
    }
  } // namespace platform
} // namespace loka
#endif
