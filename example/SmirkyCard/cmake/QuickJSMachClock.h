/* Force-included into the QuickJS-ng build when the configured Apple SDK
   declares no CLOCK_MONOTONIC (every SDK before 10.12, here the 10.4u SDK of
   the Tiger UB1 profile) or when the deployment target is below 10.12, where
   libSystem does not export clock_gettime even if the SDK declares it
   (cmake/QuickJS.cmake, #1563). QuickJS reads a monotonic nanosecond clock
   through clock_gettime(CLOCK_MONOTONIC) (cutils.h, js__hrtime_ns) and reads
   no other clock; mach_absolute_time is the monotonic clock those systems
   provide. Whenever this header is included it replaces clock_gettime. */
#ifndef LOKA_SMIRKYCARD_QUICKJS_MACH_CLOCK_H
#define LOKA_SMIRKYCARD_QUICKJS_MACH_CLOCK_H

#include <stdint.h>
#include <time.h>
#include <mach/mach_time.h>
#if !defined(CLOCK_MONOTONIC)
#define CLOCK_MONOTONIC 1
#endif
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
