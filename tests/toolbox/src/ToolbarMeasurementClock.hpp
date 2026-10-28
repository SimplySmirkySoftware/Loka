#ifndef LOKA_TESTS_TOOLBAR_MEASUREMENT_CLOCK_HPP
#define LOKA_TESTS_TOOLBAR_MEASUREMENT_CLOCK_HPP
#include <Timer.h>
namespace loka
{
  namespace testing
  {
#if defined(LOKA_TOOLBOX_MULTIVERSAL_INTERFACES)
    /** Same ABI shim as ToolboxProbeTiming.hpp; no TickCount or heap Sample. */
    struct UnsignedWide
    {
      unsigned long hi;
      unsigned long lo;
    };
    extern "C"
    {
      pascal void Microseconds(UnsignedWide *value) M68K_INLINE(0xA193, 0x225F, 0x22C8, 0x2280);
    }
#endif
    inline unsigned long MeasurementMicroseconds()
    {
      UnsignedWide value;
      Microseconds(&value);
      return value.lo;
    }
  } // namespace testing
} // namespace loka
#endif
