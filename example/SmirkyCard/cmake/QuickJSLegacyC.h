/* Force-included into the QuickJS-ng build only when the C compiler has no
   C11 mode (cmake/QuickJS.cmake): MSVC before 19.28, which is what Visual
   Studio 2017's v141 toolset for the Windows XP profile is, and GCC 4.0 for
   the Tiger UB1 profile. QuickJS-ng v0.16.2 uses one C11 keyword, _Alignas,
   on three struct members in quickjs.c; both compilers place the attribute
   at the start of a member declaration. */
#ifndef LOKA_SMIRKYCARD_QUICKJS_LEGACY_C_H
#define LOKA_SMIRKYCARD_QUICKJS_LEGACY_C_H

#if defined(_MSC_VER)
#define _Alignas(n) __declspec(align(n))
#elif defined(__GNUC__)
#define _Alignas(n) __attribute__((aligned(n)))
#else
#error "QuickJSLegacyC.h knows MSVC and GCC only"
#endif

/* Mac OS X SDKs before 10.12 have no clock_gettime. QuickJS reads a monotonic
   nanosecond clock from it (cutils.h, js__hrtime_ns); mach_absolute_time is
   the monotonic clock those systems provide. SDKs that declare
   CLOCK_MONOTONIC keep their own clock_gettime. */
#if defined(__APPLE__)
#include <stdint.h>
#include <time.h>
#if !defined(CLOCK_MONOTONIC)
#include <mach/mach_time.h>
#define CLOCK_MONOTONIC 1
static inline int loka_smirkycard_clock_gettime(int clock, struct timespec *t)
{
  static mach_timebase_info_data_t timebase;
  uint64_t ticks;
  uint64_t ns;
  (void)clock;
  if (timebase.denom == 0 && mach_timebase_info(&timebase) != 0)
    return -1;
  ticks = mach_absolute_time();
  /* Split so ticks * numer cannot overflow (PowerPC numer is 10^9). */
  ns = (ticks / timebase.denom) * timebase.numer
       + (ticks % timebase.denom) * timebase.numer / timebase.denom;
  t->tv_sec = (time_t)(ns / 1000000000u);
  t->tv_nsec = (long)(ns % 1000000000u);
  return 0;
}
#define clock_gettime loka_smirkycard_clock_gettime
#endif
#endif

#endif
