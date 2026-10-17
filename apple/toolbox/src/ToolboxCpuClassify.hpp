#ifndef LOKA_TOOLBOX_CPU_CLASSIFY_HPP
#define LOKA_TOOLBOX_CPU_CLASSIFY_HPP

#include "platform/CpuClass.hpp"

namespace loka
{
  namespace toolbox
  {
    // Retro68 Universal Interfaces Gestalt.h, gestaltCPU601.
    const long kCpu601 = 0x0101;
    // Retro68 Universal Interfaces Gestalt.h, gestaltCPU603.
    const long kCpu603 = 0x0103;
    // Retro68 Universal Interfaces Gestalt.h, gestaltCPU604.
    const long kCpu604 = 0x0104;
    // Retro68 Universal Interfaces Gestalt.h, gestaltCPU603e.
    const long kCpu603e = 0x0106;
    // Retro68 Universal Interfaces Gestalt.h, gestaltCPU603ev.
    const long kCpu603ev = 0x0107;
    // Retro68 Universal Interfaces Gestalt.h, gestaltCPU604e.
    const long kCpu604e = 0x0109;
    // Retro68 Universal Interfaces Gestalt.h, gestaltCPU604ev.
    const long kCpu604ev = 0x010A;
    // Retro68 Universal Interfaces Gestalt.h, gestaltCPU750.
    const long kCpu750 = 0x0108;
    // Retro68 Universal Interfaces Gestalt.h, gestaltCPUG4.
    const long kCpuG4 = 0x010C;
    // Apple CarbonCore Gestalt.h, gestaltCPUG47450.
    const long kCpu7450 = 0x0110;
    // Apple CarbonCore Gestalt.h, gestaltCPUApollo (7455).
    const long kCpu7455 = 0x0111;
    // Apple CarbonCore Gestalt.h, gestaltCPUG47447.
    const long kCpu7447 = 0x0112;

    /** One native PowerPC Gestalt value (a 'cput' type or a 'cpuf' family) ->
        capability class; unknown values decline with out untouched. Pure. */
    inline bool ClassifyPowerPCValue(long value, platform::CpuClass &out)
    {
      switch (value)
      {
      case kCpu601:
      case kCpu603:
      case kCpu604:
      case kCpu603e:
      case kCpu603ev:
      case kCpu604e:
      case kCpu604ev:
        out = platform::CPU_CLASS_PPC_60X;
        return true;
      case kCpu750:
      case kCpuG4:
      case kCpu7450:
      case kCpu7455:
      case kCpu7447:
        out = platform::CPU_CLASS_G3_OR_FASTER;
        return true;
      }
      return false;
    }

    /** Native PowerPC Gestalt answers -> capability class. Type ('cput') wins;
        family ('cpuf') is consulted only when type is unavailable or unknown.
        Unknown/unavailable on both declines with out untouched; the caller
        treats a decline as CPU_CLASS_68K. Pure; no Gestalt.
        Called by queryCpuClass once or twice per query; at most two fixed
        switches per call, no allocation and no owned rows walked. */
    inline bool
    ClassifyPowerPCCpu(bool typeAnswered, long type, bool familyAnswered, long family, platform::CpuClass &out)
    {
      if (typeAnswered && ClassifyPowerPCValue(type, out))
        return true;
      return familyAnswered && ClassifyPowerPCValue(family, out);
    }
  } // namespace toolbox
} // namespace loka
#endif
