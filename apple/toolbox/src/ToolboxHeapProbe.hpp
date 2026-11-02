#pragma once
// PROBE (branch probe/unicode-heap; never for main). Page 2b section 2.9 /
// page 2d section 2.5 of the Toolbox Unicode flavor rally: sample the
// application zone {extent, FreeMem, MaxBlock} at points (a)-(e) and at idle,
// and time one key door round trip, appending lines to HEAP.TXT beside the
// application. sampled peak = max(extent - free); min MaxBlock is reported.
namespace loka
{
  namespace toolbox
  {
    namespace probe
    {
#if LOKA_TOOLBOX_UNICODE
      void HeapSample(const char *point, long value);
      void HeapIdle();
      void HeapScrap(const char *point);
      unsigned long HeapMicros();
      void HeapKey(const char *kind, unsigned long startMicros, long size);
#else
      inline void HeapSample(const char *, long) {}
      inline void HeapIdle() {}
      inline void HeapScrap(const char *) {}
      inline unsigned long HeapMicros() { return 0; }
      inline void HeapKey(const char *, unsigned long, long) {}
#endif
    }
  }
}
