#include "ToolboxArrivalProbe.hpp"
#if LOKA_TOOLBOX_UNICODE
#include "unicode/MacTextEditorSubset.h"
#include <Events.h>
#include <cstdio>

namespace
{
  FILE *gLog = 0;
  bool gOpened = false;
  bool gProcessAE = false;
  OpaqueTXNObject *gFocused = 0;
  unsigned long gLast = 0;
  unsigned long gLastSize = 0;
  unsigned long gIteration = 0;

  FILE *Log()
  {
    if (!gOpened)
    {
      gOpened = true;
      gLog = std::fopen("ARRIVAL.TXT", "w");
      FILE *toggle = std::fopen("ARRIVAL-AE", "r");
      gProcessAE = toggle != 0;
      if (toggle)
        std::fclose(toggle);
      if (gLog)
      {
        std::fprintf(gLog, "arrival probe: process-apple-events=%d\n", gProcessAE ? 1 : 0);
        std::fflush(gLog);
      }
    }
    return gLog;
  }
}

namespace loka
{
  namespace toolbox
  {
    namespace probe
    {
      bool ArrivalProcessesAppleEvents()
      {
        Log();
        return gProcessAE;
      }
      void ArrivalFocus(OpaqueTXNObject *object, bool focused)
      {
        if (focused)
          gFocused = object;
        else if (gFocused == object)
          gFocused = 0;
        static OpaqueTXNObject *lastObject = 0;
        static int lastFocused = -1;
        FILE *f = Log();
        if (f && (lastObject != object || lastFocused != (focused ? 1 : 0)))
        {
          lastObject = object;
          lastFocused = focused ? 1 : 0;
          std::fprintf(f, "%lu t=%lu focus object=%p focused=%d chg=%lu\n", gIteration,
                       static_cast<unsigned long>(TickCount()), static_cast<void *>(object), focused ? 1 : 0,
                       static_cast<unsigned long>(object ? TXNGetChangeCount(object) : 0));
          std::fflush(f);
        }
      }
      void ArrivalForget(OpaqueTXNObject *object)
      {
        if (gFocused == object)
        {
          gFocused = 0;
          ArrivalNote("forget focused object", reinterpret_cast<long>(object));
        }
      }
      void ArrivalNote(const char *text, long value)
      {
        FILE *f = Log();
        if (!f)
          return;
        std::fprintf(f, "%lu t=%lu note %s value=%ld chg=%lu\n", gIteration, static_cast<unsigned long>(TickCount()),
                     text, value, static_cast<unsigned long>(gFocused ? TXNGetChangeCount(gFocused) : 0));
        std::fflush(f);
      }
      void ArrivalSample(const char *stage, const EventRecord *record)
      {
        FILE *f = Log();
        if (!f)
          return;
        if (record)
          ++gIteration;
        const unsigned long changes = gFocused ? static_cast<unsigned long>(TXNGetChangeCount(gFocused)) : 0;
        const unsigned long size = gFocused ? static_cast<unsigned long>(TXNDataSize(gFocused)) : 0;
        bool interesting = changes != gLast || size != gLastSize;
        if (record)
        {
          const short what = record->what;
          interesting = interesting || what == keyDown || what == autoKey || what == mouseDown ||
                        what == kHighLevelEvent || what == osEvt || what == activateEvt ||
                        (what == nullEvent && record->message != 0);
        }
        if (!interesting)
          return;
        std::fprintf(f, "%lu t=%lu %s chg=%lu size=%lu", gIteration, static_cast<unsigned long>(TickCount()), stage, changes,
                     size);
        if (changes != gLast)
          std::fprintf(f, " (chg+%lu)", changes - gLast);
        if (size != gLastSize)
          std::fprintf(f, " (size%+ld)", static_cast<long>(size) - static_cast<long>(gLastSize));
        if (record)
        {
          std::fprintf(f, " what=%d msg=%08lx", static_cast<int>(record->what),
                       static_cast<unsigned long>(record->message));
          if (record->what == kHighLevelEvent)
          {
            const unsigned long cls = static_cast<unsigned long>(record->message);
            const unsigned long id = (static_cast<unsigned long>(static_cast<unsigned short>(record->where.v)) << 16) |
                                     static_cast<unsigned short>(record->where.h);
            std::fprintf(f, " class=%c%c%c%c id=%c%c%c%c", static_cast<int>(cls >> 24), static_cast<int>(cls >> 16),
                         static_cast<int>(cls >> 8), static_cast<int>(cls), static_cast<int>(id >> 24),
                         static_cast<int>(id >> 16), static_cast<int>(id >> 8), static_cast<int>(id));
          }
          if (record->what == keyDown || record->what == autoKey)
            std::fprintf(f, " char=%02lx code=%02lx", static_cast<unsigned long>(record->message & charCodeMask),
                         static_cast<unsigned long>((record->message & keyCodeMask) >> 8));
        }
        std::fprintf(f, "\n");
        std::fflush(f);
        gLast = changes;
        gLastSize = size;
      }
    }
  }
}
#endif
