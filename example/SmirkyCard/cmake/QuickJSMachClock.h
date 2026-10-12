/* Force-included into the QuickJS-ng build only when the configured Apple
   SDK declares no CLOCK_MONOTONIC (cmake/QuickJS.cmake), which is every SDK
   before 10.12 and here the 10.4u SDK of the Tiger UB1 profile, whichever
   compiler targets it. QuickJS reads a monotonic nanosecond clock through
   clock_gettime (cutils.h, js__hrtime_ns); mach_absolute_time is the
   monotonic clock those systems provide. */
#ifndef LOKA_SMIRKYCARD_QUICKJS_MACH_CLOCK_H
#define LOKA_SMIRKYCARD_QUICKJS_MACH_CLOCK_H

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
