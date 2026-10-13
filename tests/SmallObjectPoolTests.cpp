#include "SmallObjectPoolTests.hpp"
#include "UpstreamGaugeTests.hpp"
#include "core/SmallObjectPool.hpp"
#include "support/AutomaticPool.hpp"
#include "support/TestVerify.hpp"
#include <cstdlib>
#include <cstring>
#include <functional>
#include <stdint.h>

namespace
{
  const unsigned int sizes[10] = {8, 16, 24, 32, 48, 64, 96, 128, 192, 256};

  /** Owns all backing allocations until fixture teardown, including retained chunks.
   * A single aligned arena makes adjacency and address order deterministic.
   * Foreign releases are checked against actual acquisitions, never freed blindly.
   */
  struct FakeSource
  {
    enum { kAlignment = 16 };
    enum Order { Ascending, Descending, Interleaved };
    static FakeSource *active;
    void *storage;
    char *arena;
    Order order;
    bool refuseChunks, refuseAll;
    unsigned int calls, releases, chunks, singles;
    size_t bytes, releasedBytes, requests[1024];
    void *single[1024];
    size_t singleSize[1024];
    bool singleReleased[1024];
    char *chunk[128];

    explicit FakeSource(Order o = Ascending)
        : storage(std::malloc(129 * 2048 + 15)), arena(0), order(o),
          refuseChunks(false), refuseAll(false), calls(0), releases(0),
          chunks(0), singles(0), bytes(0), releasedBytes(0)
    {
      LOKA_VERIFY(this->storage);
      this->arena = reinterpret_cast<char *>((reinterpret_cast<uintptr_t>(this->storage) + 15) & ~uintptr_t(15));
      active = this;
    }
    ~FakeSource()
    {
      for (unsigned int i = 0; i < this->singles; ++i)
        if (this->single[i] != this->arena + 2048) std::free(this->single[i]);
      std::free(this->storage);
      active = 0;
    }
    static void *acquire(size_t size)
    {
      FakeSource &f = *active;
      LOKA_VERIFY(f.calls < 1024);
      f.requests[f.calls++] = size;
      f.bytes += size;
      if (f.refuseAll || (f.refuseChunks && size == 2048)) return 0;
      if (size == 2048)
      {
        LOKA_VERIFY(f.chunks < 128);
        unsigned int index = f.chunks;
        if (f.order == Descending) index = 127 - index;
        if (f.order == Interleaved) index = index % 2 ? 127 - index / 2 : index / 2;
        char *p = f.arena + index * 2048;
        f.chunk[f.chunks++] = p;
        return p;
      }
      void *p = std::malloc(size);
      LOKA_VERIFY(p);
      f.recordSingle(p, size);
      return p;
    }
    void recordSingle(void *p, size_t size)
    {
      LOKA_VERIFY(this->singles < 1024);
      this->single[this->singles] = p;
      this->singleSize[this->singles] = size;
      this->singleReleased[this->singles++] = false;
    }
    static void release(void *p)
    {
      FakeSource &f = *active;
      ++f.releases;
      for (unsigned int i = 0; i < f.chunks; ++i)
        LOKA_VERIFY(p != f.chunk[i]); // Chunks must remain retained by the pool.
      for (unsigned int i = 0; i < f.singles; ++i)
        if (p == f.single[i] && !f.singleReleased[i])
        {
          f.singleReleased[i] = true;
          f.releasedBytes += f.singleSize[i];
          return;
        }
      LOKA_VERIFY(false);
    }
  };
  FakeSource *FakeSource::active = 0;
  typedef loka::testing::AutomaticPool<FakeSource> Pool;
  struct Fixture
  {
    FakeSource source;
    Pool pool;
    explicit Fixture(FakeSource::Order order = FakeSource::Ascending) : source(order) {}
  };
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
  typedef loka::core::SmallObjectPoolReport Report;
  Report snapshot(const Pool &pool)
  {
    Report r;
    pool.report(r);
    return r;
  }
  void sameStorage(const Report &a, const Report &b)
  {
    LOKA_VERIFY(a.valid && b.valid && a.rows == b.rows);
    LOKA_VERIFY(a.live == b.live && a.unusedBytes == b.unusedBytes);
    for (unsigned int c = 0; c < 10; ++c)
      LOKA_VERIFY(a.chunks[c] == b.chunks[c] && a.freeSlots[c] == b.freeSlots[c]);
  }
#endif
}

void testSmallObjectPoolClassesAndAlignment()
{
  for (unsigned int c = 0; c < 10; ++c)
  {
    const unsigned int requests[2] = {c ? sizes[c - 1] + 1 : 0, sizes[c]};
    for (unsigned int edge = 0; edge < 2; ++edge)
    {
      Fixture f;
      const unsigned int slots = 2048 / sizes[c];
      for (unsigned int i = 0; i < slots; ++i)
      {
        void *p = f.pool.allocate(requests[edge]);
        LOKA_VERIFY(p == f.source.arena + i * sizes[c]);
        LOKA_VERIFY(reinterpret_cast<uintptr_t>(p) % Pool::kSlotAlignment == 0);
      }
      LOKA_VERIFY(f.source.calls == 1 && f.source.requests[0] == 2048);
      LOKA_VERIFY(f.pool.allocate(requests[edge]) == f.source.arena + 2048);
      LOKA_VERIFY(f.source.chunks == 2 && f.source.releases == 0);
    }
  }
  Fixture f;
  void *p = f.pool.allocate(257);
  LOKA_VERIFY(p && f.source.calls == 1 && f.source.requests[0] == 257 && f.source.chunks == 0);
  f.pool.release(p);
  LOKA_VERIFY(f.source.releases == 1 && f.source.releasedBytes == 257);
}

void testSmallObjectPoolReuseAndNull()
{
  Fixture f;
  f.pool.release(0);
  LOKA_VERIFY(f.source.calls == 0 && f.source.releases == 0);
  void *a = f.pool.allocate(32);
  void *b = f.pool.allocate(32);
  LOKA_VERIFY(b == static_cast<char *>(a) + 32);
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
  const Report before = snapshot(f.pool);
#endif
  f.pool.release(0);
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
  sameStorage(before, snapshot(f.pool));
#endif
  f.pool.release(a);
  f.pool.release(b);
  LOKA_VERIFY(f.pool.allocate(32) == b);
  LOKA_VERIFY(f.pool.allocate(32) == a);
  LOKA_VERIFY(f.source.calls == 1 && f.source.releases == 0);
}

void testSmallObjectPoolSortedRowsAndEdges()
{
  const FakeSource::Order orders[] = {FakeSource::Ascending, FakeSource::Descending, FakeSource::Interleaved};
  for (unsigned int order = 0; order < 3; ++order)
  {
    Fixture f(orders[order]);
    void *slots[12][256];
    for (unsigned int row = 0; row < 12; ++row)
    {
      for (unsigned int i = 0; i < 2048 / sizes[row % 10]; ++i)
        slots[row][i] = f.pool.allocate(sizes[row % 10]);
      for (unsigned int previous = 0; previous <= row; ++previous)
        for (unsigned int i = 0; i < 2048 / sizes[previous % 10]; ++i)
        {
          f.pool.release(slots[previous][i]);
          LOKA_VERIFY(f.pool.allocate(sizes[previous % 10]) == slots[previous][i]);
        }
      LOKA_VERIFY(f.source.calls == row + 1 && f.source.releases == 0);
    }
  }
  {
    Fixture f;
    void *foreign = FakeSource::acquire(300);
    f.pool.release(foreign); // Zero registered rows.
    LOKA_VERIFY(f.source.releases == 1);
    LOKA_VERIFY(f.pool.allocate(256) == f.source.arena);
    void *onePast = f.source.arena + 2048;
    f.source.recordSingle(onePast, 16); // Real arena-owned foreign allocation.
    f.pool.release(onePast);
    LOKA_VERIFY(f.source.releases == 2);
  }
  {
    Fixture f;
    for (unsigned int i = 0; i < 8; ++i) LOKA_VERIFY(f.pool.allocate(256));
    void *adjacent = f.pool.allocate(16);
    LOKA_VERIFY(adjacent == f.source.arena + 2048);
    f.pool.release(adjacent);
    LOKA_VERIFY(f.pool.allocate(16) == adjacent);
    LOKA_VERIFY(f.source.calls == 2 && f.source.releases == 0);
  }
}

void testSmallObjectPoolFullAndRefusal()
{
  {
    Fixture f;
    f.source.refuseChunks = true;
    void *zero = f.pool.allocate(0);
    LOKA_VERIFY(zero && f.source.calls == 2 && f.source.requests[1] == 1);
    f.pool.release(zero);
  }
  {
    Fixture f;
    void *first = 0;
    for (unsigned int i = 0; i < 128 * 8; ++i)
    {
      void *p = f.pool.allocate(256);
      LOKA_VERIFY(p);
      if (!i) first = p;
    }
    LOKA_VERIFY(f.source.calls == 128 && f.source.chunks == 128);
    void *direct = f.pool.allocate(24);
    LOKA_VERIFY(direct && f.source.calls == 129 && f.source.requests[128] == 24);
    f.pool.release(direct);
    LOKA_VERIFY(f.source.releases == 1);
    f.pool.release(first);
    LOKA_VERIFY(f.pool.allocate(256) == first && f.source.calls == 129);
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
    Report r = snapshot(f.pool);
    LOKA_VERIFY(r.valid && r.rows == 128 && r.refills == 128 && r.directs == 1);
    LOKA_VERIFY(r.live == 1024 && r.unusedBytes == 0);
#endif
  }
  {
    Fixture f;
    LOKA_VERIFY(f.pool.allocate(32));
    f.source.refuseChunks = true;
    unsigned int calls = f.source.calls;
    void *fallback = f.pool.allocate(48);
    LOKA_VERIFY(fallback && f.source.calls == calls + 2);
    LOKA_VERIFY(f.source.requests[calls] == 2048 && f.source.requests[calls + 1] == 48);
    f.pool.release(fallback);
    LOKA_VERIFY(f.source.releases == 1);
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
    const Report before = snapshot(f.pool);
#endif
    f.source.refuseAll = true;
    calls = f.source.calls;
    LOKA_VERIFY(f.pool.allocate(64) == 0);
    LOKA_VERIFY(f.source.calls == calls + 2 && f.source.requests[calls] == 2048 && f.source.requests[calls + 1] == 64);
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
    sameStorage(before, snapshot(f.pool));
#endif
    // The previous class remains usable even when the source refuses everything.
    LOKA_VERIFY(f.pool.allocate(32) == f.source.arena + 32);
    LOKA_VERIFY(f.source.calls == calls + 2);
  }
}

void testSmallObjectPoolCountersAndPoison()
{
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
  Fixture f;
  void *p = f.pool.allocate(16);
  f.pool.release(p);
  LOKA_VERIFY(f.pool.allocate(16) == p);
  LOKA_VERIFY(snapshot(f.pool).poisonViolations == 0);
  f.pool.release(p);
  static_cast<unsigned char *>(p)[sizeof(void *)] ^= 1;
  LOKA_VERIFY(f.pool.allocate(16) == p);
  void *large = f.pool.allocate(257);
  f.pool.release(large);
  f.source.refuseChunks = true;
  void *fallback = f.pool.allocate(32);
  f.pool.release(fallback);
  f.source.refuseAll = true;
  LOKA_VERIFY(f.pool.allocate(64) == 0);
  LOKA_VERIFY(f.pool.allocate(300) == 0);
  Report r = snapshot(f.pool);
  LOKA_VERIFY(r.valid && r.refills == 1 && r.larges == 1 && r.directs == 0);
  LOKA_VERIFY(r.fallbacks == 1 && r.refused == 1 && r.poisonViolations == 1);
  LOKA_VERIFY(f.source.calls == 7 && f.source.chunks == r.refills && f.source.singles == r.larges + r.fallbacks);
  LOKA_VERIFY(f.source.bytes == 2048 + 257 + 2048 + 32 + 2048 + 64 + 300);
#else
  std::puts("[skip] SmallObjectPool diagnostic counters: diagnostics disabled");
#endif
}

void testSmallObjectPoolReportValidity()
{
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
  Fixture f;
  void *allocated[10];
  unsigned long used = 0;
  for (unsigned int c = 0; c < 10; ++c)
  {
    allocated[c] = f.pool.allocate(sizes[c]);
    LOKA_VERIFY(allocated[c]);
    used += sizes[c];
  }
  Report r = snapshot(f.pool);
  LOKA_VERIFY(r.valid && r.rows == 10 && r.live == 10 && r.unusedBytes == 20480 - used);
  for (unsigned int c = 0; c < 10; ++c)
    LOKA_VERIFY(r.chunks[c] == 1 && r.freeSlots[c] == 2048 / sizes[c] - 1);
  char *interior = static_cast<char *>(f.pool.allocate(16)) + 1;
  char *head = static_cast<char *>(allocated[1]);
  f.pool.release(head);
  char *saved;
  std::memcpy(&saved, head, sizeof(saved));
  char outside = 0;
  char *bad[4] = {&outside, interior, static_cast<char *>(allocated[2]), head};
  for (unsigned int corruption = 0; corruption < 4; ++corruption)
  {
    // Terminate the invalid target independently: later garbage must not mask
    // a missing same-class or exact-slot check (mutation positive controls).
    char targetBytes[sizeof(char *)];
    if (corruption == 1 || corruption == 2)
    {
      std::memcpy(targetBytes, bad[corruption], sizeof(targetBytes));
      char *nil = 0;
      std::memcpy(bad[corruption], &nil, sizeof(nil));
    }
    std::memcpy(head, &bad[corruption], sizeof(char *));
    r = snapshot(f.pool);
    LOKA_VERIFY(!r.valid && r.live == 0 && r.unusedBytes == 0);
    std::memcpy(head, &saved, sizeof(saved));
    if (corruption == 1 || corruption == 2)
      std::memcpy(bad[corruption], targetBytes, sizeof(targetBytes));
    LOKA_VERIFY(snapshot(f.pool).valid);
  }
  // 192-byte chunks have a tail that is inside the extent but is not a slot.
  char *tailHead = static_cast<char *>(allocated[8]);
  f.pool.release(tailHead);
  std::memcpy(&saved, tailHead, sizeof(saved));
  char *tail = tailHead + (2048 / 192) * 192;
  std::memcpy(tailHead, &tail, sizeof(tail));
  r = snapshot(f.pool);
  LOKA_VERIFY(!r.valid && r.live == 0 && r.unusedBytes == 0);
  std::memcpy(tailHead, &saved, sizeof(saved));
  LOKA_VERIFY(snapshot(f.pool).valid && f.source.releases == 0);
#else
  std::puts("[skip] SmallObjectPool report validity: diagnostics disabled");
#endif
}

namespace
{
  void gaugeEquals(const Pool &pool, unsigned long attempts, unsigned long successes,
                   unsigned long bytes, unsigned long failures)
  {
    const loka::core::UpstreamGauge g = pool.snapshot();
    LOKA_VERIFY(!g.saturated);
    LOKA_VERIFY(g.attempts == attempts);
    LOKA_VERIFY(g.successes == successes);
    LOKA_VERIFY(g.bytesAcquired == bytes);
    LOKA_VERIFY(g.failures == failures);
  }
}

void testUpstreamGaugeLarge()
{
  Fixture f;
  const loka::core::UpstreamGauge before = f.pool.snapshot();
  gaugeEquals(f.pool, 0, 0, 0, 0);
  void *p = f.pool.allocate(257);
  LOKA_VERIFY(p);
  gaugeEquals(f.pool, 1, 1, 257, 0);
  f.pool.release(p);
  f.source.refuseAll = true;
  LOKA_VERIFY(f.pool.allocate(300) == 0);
  gaugeEquals(f.pool, 2, 1, 257, 1);
  LOKA_VERIFY(before.attempts == 0 && before.bytesAcquired == 0);
}

void testUpstreamGaugeRefillAndFallback()
{
  Fixture f;
  void *p = f.pool.allocate(16);
  LOKA_VERIFY(p);
  gaugeEquals(f.pool, 1, 1, 2048, 0);
  f.pool.release(p);
  LOKA_VERIFY(f.pool.allocate(16) == p);
  gaugeEquals(f.pool, 1, 1, 2048, 0);
  f.source.refuseChunks = true;
  void *fallback = f.pool.allocate(24);
  LOKA_VERIFY(fallback);
  gaugeEquals(f.pool, 3, 2, 2072, 1);
  f.pool.release(fallback);
  f.source.refuseAll = true;
  LOKA_VERIFY(f.pool.allocate(32) == 0);
  gaugeEquals(f.pool, 5, 2, 2072, 3);
}

void testUpstreamGaugeDirect()
{
  Fixture f;
  for (unsigned int i = 0; i < 128 * 8; ++i)
    LOKA_VERIFY(f.pool.allocate(256));
  gaugeEquals(f.pool, 128, 128, 128UL * 2048, 0);
  void *p = f.pool.allocate(24);
  LOKA_VERIFY(p);
  gaugeEquals(f.pool, 129, 129, 128UL * 2048 + 24, 0);
  f.pool.release(p);
  f.source.refuseAll = true;
  LOKA_VERIFY(f.pool.allocate(24) == 0);
  gaugeEquals(f.pool, 130, 129, 128UL * 2048 + 24, 1);
}

namespace
{
  // Accounting-only source: the huge successful request is an opaque token.
  // No storage is dereferenced or released; this tests arithmetic, not malloc.
  struct GaugeLimitSource
  {
    enum { kAlignment = 16 };
    static void *acquire(size_t) { static int token; return &token; }
    static void release(void *) {}
  };
}
void testUpstreamGaugeSaturation()
{
  loka::testing::AutomaticPool<GaugeLimitSource> pool;
  LOKA_VERIFY(pool.allocate(ULONG_MAX - 1));
  const loka::core::UpstreamGauge before = pool.snapshot();
  LOKA_VERIFY(!before.saturated && before.bytesAcquired == ULONG_MAX - 1);
  LOKA_VERIFY(pool.allocate(257));
  const loka::core::UpstreamGauge after = pool.snapshot();
  LOKA_VERIFY(after.saturated && after.bytesAcquired == ULONG_MAX);
  LOKA_VERIFY(after.attempts == 2 && after.successes == 2 && after.failures == 0);
  LOKA_VERIFY(pool.allocate(300));
  LOKA_VERIFY(pool.snapshot().saturated && pool.snapshot().bytesAcquired == ULONG_MAX);
  LOKA_VERIFY(!before.saturated && before.bytesAcquired == ULONG_MAX - 1);
}
