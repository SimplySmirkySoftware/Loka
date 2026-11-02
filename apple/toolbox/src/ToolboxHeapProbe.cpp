#include "ToolboxHeapProbe.hpp"
#if LOKA_TOOLBOX_UNICODE
#include <Events.h>
#include <MacMemory.h>
#include <Timer.h>
#include <cstdio>

namespace
{
  FILE *gLog = 0;
  bool gOpened = false;
  long gPeak = 0;
  long gMinMaxBlock = 0x7fffffffL;
  long gLastUsed = -1;
  unsigned long gLastIdleTick = 0;

  FILE *Log()
  {
    if (!gOpened)
    {
      gOpened = true;
      gLog = std::fopen("HEAP.TXT", "w");
      if (gLog)
      {
        std::fprintf(gLog, "heap probe: t point value extent free maxblock used peak minmax samezone\n");
        std::fflush(gLog);
      }
    }
    return gLog;
  }
  void Write(const char *point, long value, bool force)
  {
    FILE *f = Log();
    if (!f)
      return;
    THz current = GetZone();
    THz app = ApplicationZone();
    const bool same = current == app;
    if (!same)
      SetZone(app);
    const long extent = static_cast<long>(reinterpret_cast<char *>(app->bkLim) - reinterpret_cast<char *>(app));
    const long freeBytes = FreeMem();
    const long maxBlock = MaxBlock();
    if (!same)
      SetZone(current);
    const long used = extent - freeBytes;
    if (used > gPeak)
      gPeak = used;
    if (maxBlock < gMinMaxBlock)
      gMinMaxBlock = maxBlock;
    if (!force && used == gLastUsed)
      return;
    gLastUsed = used;
    std::fprintf(f, "t=%lu %s value=%ld extent=%ld free=%ld maxblock=%ld used=%ld peak=%ld minmax=%ld same=%d\n",
                 static_cast<unsigned long>(TickCount()), point, value, extent, freeBytes, maxBlock, used, gPeak,
                 gMinMaxBlock, same ? 1 : 0);
    std::fflush(f);
  }
}

namespace loka
{
  namespace toolbox
  {
    namespace probe
    {
      void HeapSample(const char *point, long value)
      {
        Write(point, value, true);
      }
      void HeapIdle()
      {
        const unsigned long now = static_cast<unsigned long>(TickCount());
        if (now - gLastIdleTick < 60)
          return;
        gLastIdleTick = now;
        Write("idle", 0, false);
      }
      unsigned long HeapMicros()
      {
        UnsignedWide w;
        Microseconds(&w);
        return w.lo;
      }
      void HeapKey(const char *kind, unsigned long startMicros, long size)
      {
        FILE *f = Log();
        if (!f)
          return;
        std::fprintf(f, "t=%lu key %s us=%lu size=%ld\n", static_cast<unsigned long>(TickCount()), kind,
                     HeapMicros() - startMicros, size);
        std::fflush(f);
      }
    }
  }
}
#endif
