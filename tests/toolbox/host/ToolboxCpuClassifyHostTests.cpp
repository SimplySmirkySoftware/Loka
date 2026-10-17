#include "ToolboxCpuClassify.hpp"
#include "support/TestVerify.hpp"
#include <cstdio>

int main()
{
  using namespace loka::platform;
  struct Known
  {
    long code;
    CpuClass expected;
  };
  // Literal oracle values are independent of the production constants.
  const Known known[] = {
      {0x0101, CPU_CLASS_PPC_60X},
      {0x0103, CPU_CLASS_PPC_60X},
      {0x0104, CPU_CLASS_PPC_60X},
      {0x0106, CPU_CLASS_PPC_60X},
      {0x0107, CPU_CLASS_PPC_60X},
      {0x0109, CPU_CLASS_PPC_60X},
      {0x010A, CPU_CLASS_PPC_60X},
      {0x0108, CPU_CLASS_G3_OR_FASTER},
      {0x010C, CPU_CLASS_G3_OR_FASTER},
      {0x0110, CPU_CLASS_G3_OR_FASTER},
      {0x0111, CPU_CLASS_G3_OR_FASTER},
      {0x0112, CPU_CLASS_G3_OR_FASTER},
  };
  unsigned checks = 0;
  for (unsigned i = 0; i < sizeof(known) / sizeof(known[0]); ++i)
  {
    const Known &cpu = known[i];
    const long unknown = 0x7fff;
    const long types[] = {cpu.code, unknown, cpu.code, cpu.code};
    const long families[] = {unknown, cpu.code, 0x0101, 0x0108};
    for (unsigned pair = 0; pair < 4; ++pair)
    {
      CpuClass out = CPU_CLASS_68K;
      LOKA_VERIFY(loka::toolbox::ClassifyPowerPCCpu(true, types[pair], true, families[pair], out));
      LOKA_VERIFY(out == cpu.expected);
      ++checks;
    }
    CpuClass out = CPU_CLASS_68K;
    LOKA_VERIFY(loka::toolbox::ClassifyPowerPCCpu(true, cpu.code, false, 0x0108, out));
    LOKA_VERIFY(out == cpu.expected);
    LOKA_VERIFY(loka::toolbox::ClassifyPowerPCCpu(false, 0x0101, true, cpu.code, out));
    LOKA_VERIFY(out == cpu.expected);
    checks += 2;
  }
  for (int answered = 0; answered < 4; ++answered)
    for (int initial = CPU_CLASS_68K; initial < CPU_CLASS_COUNT; ++initial)
    {
      CpuClass out = static_cast<CpuClass>(initial);
      // A known value in an unavailable slot must never be consumed.
      LOKA_VERIFY(!loka::toolbox::ClassifyPowerPCCpu((answered & 1) != 0,
                                                     (answered & 1) ? 0x7fff : 0x0108,
                                                     (answered & 2) != 0,
                                                     (answered & 2) ? 0x7fff : 0x0101,
                                                     out));
      LOKA_VERIFY(out == static_cast<CpuClass>(initial));
      ++checks;
    }
  std::printf("PowerPC classifier: %u cases passed\n", checks);
  return 0;
}
