#include "app/style/CardStyle.hpp"

namespace loka
{
  namespace app
  {
    CardStyle StandardCardStyleFor(platform::CpuClass cpuClass)
    {
      switch (cpuClass)
      {
      case platform::CPU_CLASS_68K:
        return CardStyle(CARD_SHADOW_DROP, CARD_SHADOW_STENCIL);
      case platform::CPU_CLASS_PPC_60X:
        return CardStyle(CARD_SHADOW_WIDE, CARD_SHADOW_STENCIL);
      case platform::CPU_CLASS_G3_OR_FASTER:
        return CardStyle(CARD_SHADOW_WIDE, CARD_SHADOW_ALPHA);
      case platform::CPU_CLASS_COUNT:
        break;
      }
      assert(false && "Invalid CPU class");
      return CardStyle(CARD_SHADOW_DROP, CARD_SHADOW_STENCIL);
    }

    CardStyle StandardCardStyle()
    {
      platform::CpuClass cpuClass;
      if (!platform::queryCpuClass(cpuClass))
        cpuClass = platform::CPU_CLASS_68K;
      return StandardCardStyleFor(cpuClass);
    }

    CardStyle::CardStyle()
    {
      const CardStyle standard = StandardCardStyle();
      this->extent_ = standard.extent();
      this->ink_ = standard.ink();
    }
  } // namespace app
} // namespace loka
