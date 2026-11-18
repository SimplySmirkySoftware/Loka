// Zero-allocation pin: counting global operator new/delete + census report.
//
// This translation unit replaces the global allocation operators for the
// LokaAllocPinTests binary ONLY (that is why the pin lives in its own
// executable: the replacement must not perturb the other test binaries).
// While a capture window is open every allocation records a glibc
// backtrace; aggregation and symbolization happen at report time.
//
// Linux-only test scaffolding (glibc backtrace); never built for Retro68.

#include "StateTrackerAllocationTests.hpp"
#include "support/AllocCensus.hpp"

#include <execinfo.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace allocpin
{
  enum
  {
    kMaxFrames = 24,
    // Frame 0 = NoteAllocation, frame 1 = operator new; hide them in the
    // aggregation key and the printed stacks so the census starts at the
    // real caller.
    kSkipFrames = 2,
    kMaxRecords = 20000,
    kMaxAggregates = 1024,
    kCaptureCount = 2,
    kFreePhaseAfterWindow = 100
  };

  struct AllocRecord
  {
    void *ptr;
    unsigned long bytes;
    int phase;
    int depth;
    int freed;
    int freePhase; // phase of the free, or kFreePhaseAfterWindow
    void *stack[kMaxFrames];
  };

  struct CaptureStore
  {
    AllocRecord records[kMaxRecords];
    long count;
    long overflow;
    long freesOfOutsideMemory; // frees (during the window) of pre-window memory
    long phaseCounts[PHASE_COUNT];
    unsigned long phaseBytes[PHASE_COUNT];
  };

  static CaptureStore g_captures[kCaptureCount];
  static int g_activeCapture = -1;
  static int g_phase = PHASE_OUTSIDE;
  static int g_inHook = 0;
  static unsigned long g_lifetimeAllocs = 0;
  static unsigned long g_lifetimeBytes = 0;

  void SetPhase(int phase)
  {
    g_phase = phase;
  }

  void BeginCapture(int captureIndex)
  {
    assert(captureIndex >= 0 && captureIndex < kCaptureCount);
    // Warm up glibc backtrace: its first call may load libgcc via malloc.
    // Do that before the window opens so it never pollutes the census.
    void *warm[kMaxFrames];
    (void)backtrace(warm, kMaxFrames);
    std::memset(&g_captures[captureIndex], 0, sizeof(CaptureStore));
    g_phase = PHASE_OUTSIDE;
    g_activeCapture = captureIndex;
  }

  void EndCapture()
  {
    g_activeCapture = -1;
    g_phase = PHASE_OUTSIDE;
  }

  unsigned long CaptureAllocCount(int captureIndex)
  {
    assert(captureIndex >= 0 && captureIndex < kCaptureCount);
    const CaptureStore &cap = g_captures[captureIndex];
    return static_cast<unsigned long>(cap.count) + static_cast<unsigned long>(cap.overflow);
  }

  unsigned long CaptureAllocBytes(int captureIndex)
  {
    assert(captureIndex >= 0 && captureIndex < kCaptureCount);
    const CaptureStore &cap = g_captures[captureIndex];
    unsigned long total = 0;
    for (int p = 0; p < PHASE_COUNT; ++p)
    {
      total += cap.phaseBytes[p];
    }
    return total;
  }

  // Called from the replaced operator new. Must not allocate via operator
  // new itself (backtrace() does not; malloc from libgcc lazy-init is
  // avoided by the BeginCapture warmup).
  static void NoteAllocation(void *ptr, std::size_t bytes)
  {
    ++g_lifetimeAllocs;
    g_lifetimeBytes += static_cast<unsigned long>(bytes);
    if (g_activeCapture < 0 || g_inHook)
    {
      return;
    }
    g_inHook = 1;
    CaptureStore &cap = g_captures[g_activeCapture];
    if (cap.count >= static_cast<long>(kMaxRecords))
    {
      ++cap.overflow;
      g_inHook = 0;
      return;
    }
    AllocRecord &r = cap.records[cap.count++];
    r.ptr = ptr;
    r.bytes = static_cast<unsigned long>(bytes);
    r.phase = g_phase;
    r.freed = 0;
    r.freePhase = -1;
    r.depth = backtrace(r.stack, kMaxFrames);
    ++cap.phaseCounts[g_phase];
    cap.phaseBytes[g_phase] += static_cast<unsigned long>(bytes);
    g_inHook = 0;
  }

  // Called from the replaced operator delete, capture window or not, so a
  // captured allocation freed later (e.g. replaced during the NEXT
  // interaction) is still distinguishable from one retained forever.
  static void NoteFree(void *ptr)
  {
    if (!ptr || g_inHook)
    {
      return;
    }
    g_inHook = 1;
    // Newest record first so a pointer value reused after an in-window free
    // matches its latest allocation.
    for (int c = kCaptureCount - 1; c >= 0; --c)
    {
      CaptureStore &cap = g_captures[c];
      for (long i = cap.count - 1; i >= 0; --i)
      {
        AllocRecord &r = cap.records[i];
        if (r.ptr == ptr && !r.freed)
        {
          r.freed = 1;
          r.freePhase = (g_activeCapture == c) ? g_phase : kFreePhaseAfterWindow;
          g_inHook = 0;
          return;
        }
      }
    }
    if (g_activeCapture >= 0)
    {
      ++g_captures[g_activeCapture].freesOfOutsideMemory;
    }
    g_inHook = 0;
  }

  // ---- report ----

  struct Aggregate
  {
    const AllocRecord *rep;
    int phase;
    long count;
    unsigned long bytes;
    long transientCount;  // freed within the same interaction window
    long freedAfterCount; // freed later (after the window closed)
    unsigned long hash;   // stack-only hash (cross-capture identity)
  };

  static unsigned long StackHash(const AllocRecord &r)
  {
    unsigned long h = 2166136261UL;
    for (int i = kSkipFrames; i < r.depth; ++i)
    {
      unsigned long v = reinterpret_cast<unsigned long>(r.stack[i]);
      for (unsigned int b = 0; b < sizeof(unsigned long); ++b)
      {
        h ^= (v >> (b * 8)) & 0xffUL;
        h *= 16777619UL;
      }
    }
    return h;
  }

  static const char *PhaseName(int phase)
  {
    switch (phase)
    {
    case PHASE_OUTSIDE:
      return "outside";
    case PHASE_HANDLER:
      return "handler";
    case PHASE_COMMIT:
      return "commit";
    case PHASE_FLUSH:
      return "flush";
    default:
      return "?";
    }
  }

  // Report-time phase refinement. The default BoundaryNode policy is
  // flushViewDirtyImmediately() == true, so the entire pipeline (propagation,
  // compose, apply) runs synchronously nested inside the handler's set()
  // call: the runtime phase marker reads "handler" for everything. Classify
  // by the innermost meaningful frame instead: allocations under
  // Scene/SceneDirector belong to flush (compose+apply), allocations under
  // tracker propagation belong to commit, the rest to the handler body.
  static int ClassifyFrame(const char *sym)
  {
    if (std::strstr(sym, "flushInvalidation") || std::strstr(sym, "NextTickTracker")
        || std::strstr(sym, "13SceneDirector") || std::strstr(sym, "5scene5Scene")
        || std::strstr(sym, "RefreshThunk"))
    {
      return PHASE_FLUSH;
    }
    if (std::strstr(sym, "markDirty") || std::strstr(sym, "16PushStateTracker3end")
        || std::strstr(sym, "markViewDirty") || std::strstr(sym, "requestBoundaryUpdate")
        || std::strstr(sym, "InvalidateThunk") || std::strstr(sym, "InvalidateSceneThunk"))
    {
      return PHASE_COMMIT;
    }
    if (std::strstr(sym, "8MainNode") || std::strstr(sym, "CallbackEntry") || std::strstr(sym, "notifyHandlers")
        || std::strstr(sym, "12EmitterState") || std::strstr(sym, "notifyStateChanged"))
    {
      return PHASE_HANDLER;
    }
    return -1;
  }

  static int EffectivePhase(const AllocRecord &r)
  {
    char **symbols = backtrace_symbols(const_cast<void *const *>(r.stack), r.depth);
    int phase = r.phase;
    if (symbols)
    {
      for (int i = kSkipFrames; i < r.depth; ++i)
      {
        const int classified = symbols[i] ? ClassifyFrame(symbols[i]) : -1;
        if (classified >= 0)
        {
          phase = classified;
          break;
        }
      }
      std::free(symbols);
    }
    return phase;
  }

  static long BuildAggregates(const CaptureStore &cap, Aggregate *out, long maxOut)
  {
    long aggregateCount = 0;
    for (long i = 0; i < cap.count; ++i)
    {
      const AllocRecord &r = cap.records[i];
      const unsigned long h = StackHash(r);
      long found = -1;
      for (long a = 0; a < aggregateCount; ++a)
      {
        if (out[a].hash == h && out[a].phase == r.phase)
        {
          found = a;
          break;
        }
      }
      if (found < 0)
      {
        if (aggregateCount >= maxOut)
        {
          continue;
        }
        found = aggregateCount++;
        out[found].rep = &r;
        out[found].phase = r.phase;
        out[found].count = 0;
        out[found].bytes = 0;
        out[found].transientCount = 0;
        out[found].freedAfterCount = 0;
        out[found].hash = h;
      }
      ++out[found].count;
      out[found].bytes += r.bytes;
      if (r.freed)
      {
        if (r.freePhase == kFreePhaseAfterWindow)
        {
          ++out[found].freedAfterCount;
        }
        else
        {
          ++out[found].transientCount;
        }
      }
    }
    // Sort by (phase, count desc) with a simple insertion sort.
    for (long a = 1; a < aggregateCount; ++a)
    {
      Aggregate key = out[a];
      long b = a - 1;
      while (b >= 0
             && (out[b].phase > key.phase || (out[b].phase == key.phase && out[b].count < key.count)))
      {
        out[b + 1] = out[b];
        --b;
      }
      out[b + 1] = key;
    }
    return aggregateCount;
  }

  static void PrintStack(const AllocRecord &r)
  {
    char **symbols = backtrace_symbols(const_cast<void *const *>(r.stack), r.depth);
    for (int i = kSkipFrames; i < r.depth; ++i)
    {
      if (symbols && symbols[i])
      {
        std::printf("      %2d  %s\n", i - kSkipFrames, symbols[i]);
      }
      else
      {
        std::printf("      %2d  [%p]\n", i - kSkipFrames, r.stack[i]);
      }
    }
    if (symbols)
    {
      // backtrace_symbols memory comes from malloc; free() directly so it
      // never routes through the replaced operator delete.
      std::free(symbols);
    }
  }

  static bool HashPresent(const CaptureStore &cap, unsigned long hash, int phase)
  {
    for (long i = 0; i < cap.count; ++i)
    {
      if (cap.records[i].phase == phase && StackHash(cap.records[i]) == hash)
      {
        return true;
      }
    }
    return false;
  }

  void PrintCensusReport()
  {
    static Aggregate aggregates[kCaptureCount][kMaxAggregates];
    long aggregateCounts[kCaptureCount];

    std::printf("==== zero-allocation pin census ====\n");
    std::printf("lifetime (process, uncaptured included): allocs=%lu bytes=%lu\n\n", g_lifetimeAllocs,
                g_lifetimeBytes);

    for (int c = 0; c < kCaptureCount; ++c)
    {
      const CaptureStore &cap = g_captures[c];
      aggregateCounts[c] = BuildAggregates(cap, aggregates[c], kMaxAggregates);

      std::printf("---- capture %d (%s) ----\n", c,
                  c == 0 ? "first captured interaction" : "second captured interaction / steady state");
      std::printf("total: allocs=%ld bytes=%lu overflow=%ld frees-of-pre-window-memory=%ld\n", cap.count,
                  CaptureAllocBytes(c), cap.overflow, cap.freesOfOutsideMemory);
      for (int p = 0; p < PHASE_COUNT; ++p)
      {
        std::printf("  runtime phase %-8s allocs=%ld bytes=%lu\n", PhaseName(p), cap.phaseCounts[p],
                    cap.phaseBytes[p]);
      }

      long effectiveCounts[PHASE_COUNT];
      unsigned long effectiveBytes[PHASE_COUNT];
      for (int p = 0; p < PHASE_COUNT; ++p)
      {
        effectiveCounts[p] = 0;
        effectiveBytes[p] = 0;
      }
      for (long a = 0; a < aggregateCounts[c]; ++a)
      {
        const int p = EffectivePhase(*aggregates[c][a].rep);
        effectiveCounts[p] += aggregates[c][a].count;
        effectiveBytes[p] += aggregates[c][a].bytes;
      }
      for (int p = 0; p < PHASE_COUNT; ++p)
      {
        std::printf("  effective phase %-8s allocs=%ld bytes=%lu\n", PhaseName(p), effectiveCounts[p],
                    effectiveBytes[p]);
      }
      std::printf("\n");

      for (long a = 0; a < aggregateCounts[c]; ++a)
      {
        const Aggregate &agg = aggregates[c][a];
        const long retained = agg.count - agg.transientCount - agg.freedAfterCount;
        std::printf("  [capture %d stack %ld] phase=%s count=%ld bytes=%lu"
                    " transient=%ld freed-later=%ld retained=%ld hash=%08lx\n",
                    c, a, PhaseName(EffectivePhase(*agg.rep)), agg.count, agg.bytes, agg.transientCount,
                    agg.freedAfterCount, retained, agg.hash);
        PrintStack(*agg.rep);
      }
      std::printf("\n");
    }

    std::printf("---- cross-capture classification ----\n");
    for (long a = 0; a < aggregateCounts[0]; ++a)
    {
      const Aggregate &agg = aggregates[0][a];
      const bool steady = HashPresent(g_captures[1], agg.hash, agg.phase);
      std::printf("  capture0 stack %ld (phase=%s hash=%08lx): %s\n", a, PhaseName(agg.phase), agg.hash,
                  steady ? "steady-state (present in both captures)" : "warmup-residual (first capture only)");
    }
    for (long a = 0; a < aggregateCounts[1]; ++a)
    {
      const Aggregate &agg = aggregates[1][a];
      if (!HashPresent(g_captures[0], agg.hash, agg.phase))
      {
        std::printf("  capture1 stack %ld (phase=%s hash=%08lx): capture-1 only (unexpected)\n", a,
                    PhaseName(agg.phase), agg.hash);
      }
    }
    std::printf("==== end census ====\n");
    std::fflush(stdout);
  }
} // namespace allocpin

// ---- global allocation operator replacements (binary-wide) ----

#ifdef LOKA_UPSTREAM_GAUGE_PIN
#include "support/UpstreamGaugePin.inc"
#include "SeatReservationTests.hpp"
#else
static void *upstreamPinAllocate(std::size_t size) { return std::malloc(size); }
static void upstreamPinRelease(void *p) { std::free(p); }
#endif

void *operator new(std::size_t size) throw(std::bad_alloc)
{
  void *p = upstreamPinAllocate(size ? size : 1);
  if (!p)
  {
    std::fprintf(stderr, "AllocPin: operator new(%lu) failed\n", static_cast<unsigned long>(size));
    std::abort();
  }
  allocpin::NoteAllocation(p, size);
  return p;
}

void *operator new[](std::size_t size) throw(std::bad_alloc)
{
  void *p = upstreamPinAllocate(size ? size : 1);
  if (!p)
  {
    std::fprintf(stderr, "AllocPin: operator new[](%lu) failed\n", static_cast<unsigned long>(size));
    std::abort();
  }
  allocpin::NoteAllocation(p, size);
  return p;
}

void *operator new(std::size_t size, const std::nothrow_t &) throw()
{
  void *p = upstreamPinAllocate(size ? size : 1);
  if (p)
  {
    allocpin::NoteAllocation(p, size);
  }
  return p;
}

void *operator new[](std::size_t size, const std::nothrow_t &) throw()
{
  void *p = upstreamPinAllocate(size ? size : 1);
  if (p)
  {
    allocpin::NoteAllocation(p, size);
  }
  return p;
}

void operator delete(void *p) throw()
{
  allocpin::NoteFree(p);
  upstreamPinRelease(p);
}

void operator delete[](void *p) throw()
{
  allocpin::NoteFree(p);
  upstreamPinRelease(p);
}

void operator delete(void *p, const std::nothrow_t &) throw()
{
  allocpin::NoteFree(p);
  upstreamPinRelease(p);
}

void operator delete[](void *p, const std::nothrow_t &) throw()
{
  allocpin::NoteFree(p);
  upstreamPinRelease(p);
}

int main()
{
  allocpin::RunDocumentMarkdownAllocPin();
  allocpin::RunFocusCompletionAllocPin();
  allocpin::RunAttributedStringEqualsAllocPin();
  testStateTrackerRegistrationGrowth();
  testStateTrackerReservedPropagation();
  testStateTrackerCycleAndDiamond();
  testStateTrackerRemovesSettlementBorrow();
#ifdef LOKA_UPSTREAM_GAUGE_PIN
  runUpstreamFlowPin();
  testSeatReservationMineSweeperColdBacking();
#endif
  allocpin::RunLazyViewPageFlipAllocPin();
#ifndef LOKA_UPSTREAM_GAUGE_PIN
  allocpin::RunStateLifetimeTokenAllocPin();
#endif
  allocpin::RunZeroAllocPin();
#ifndef LOKA_UPSTREAM_GAUGE_PIN
  allocpin::RunFloppyBirdSurfaceAllocPin();
  allocpin::RunFloppyBirdScoreAllocPin();
#endif
  return 0;
}
