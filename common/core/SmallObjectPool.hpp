#ifndef LOKA_CORE_SMALL_OBJECT_POOL_HPP
#define LOKA_CORE_SMALL_OBJECT_POOL_HPP

#include <stddef.h>
#include <limits.h>
#include <string.h>
#include <functional>

namespace loka
{
  namespace core
  {
    /** Cumulative Source acquisitions, independent of releases and diagnostic builds.
     * bytesAcquired sums requested bytes of successful calls (including full chunks),
     * not allocator overhead or live storage. saturated means at least one count
     * reached ULONG_MAX; only unsaturated snapshots support exact subtraction.
     * Native allocations and operator-new calls bypassing this pool are excluded.
     */
    struct UpstreamGauge
    {
      unsigned long attempts, successes, bytesAcquired, failures;
      bool saturated;
    };

#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
    /** Allocation-free diagnostic snapshot; invalid lists leave live/unusedBytes zero. */
    struct SmallObjectPoolReport
    {
      unsigned long rows, chunks[10], freeSlots[10], live, unusedBytes;
      // Legacy saturating event counts (no overflow fact): refills = acquired
      // chunks; larges = successful >256 requests; directs = successful small
      // requests with all 128 rows occupied; fallbacks = successful individual
      // requests after refused refill; refused = failed fallback ONLY (not all
      // Source failures). poisonViolations = damaged poison words found on pop.
      unsigned long refills, larges, directs, fallbacks, refused, poisonViolations;
      bool valid;
    };
#endif

    /** Process-lifetime raw storage, for a static, zero-initialized instance.
     * Source supplies static acquire(size_t), release(void *), and kAlignment;
     * live acquisitions are disjoint and retain their address and size.
     * Never copy an active pool. An automatic instance must start from zeroed
     * storage: C++98 zeroes this constructor-less class only in static storage,
     * and GCC 4.0/4.2 leave `Pool p = Pool();` indeterminate (#1278).
     * No constructor/destructor: storage remains usable through static teardown.
     * Source alignment must accommodate a pointer. Slots promise only
     * min(Source::kAlignment, 8) alignment. No per-allocation header is stored.
     * Diagnostics poison one word after the link; an 8-byte slot on a 64-bit
     * host has no poison word. Freshly carved slots receive the same poison.
     *
     * Validity invariant: every registered chunk stays at its address and size until process exit; the rows are sorted by base with disjoint extents; every usable slot of a registered chunk is either handed out (allocated and not yet released) or occurs exactly once in the free list of its chunk's class.
     * Transitions: allocate-pop, allocate-refill (row insertion + carve), allocate-large/direct/fallback (no pool state change), release-push, release-foreign (no pool state change), refused acquisition (no pool state change).
     * Upstream and diagnostic event counters are the exception to "no pool state change".
     *
     * Costs, per caller request, over this pool's own rows only:
     * allocate = class lookup (<= 10 compares) + pop O(1); on refill +
     * 1 acquisition + sorted insertion O(rows) + carving O(2048/stride).
     * release = binary search O(log rows) + push O(1).
     * report = one walk over own rows + one bounded walk per class list;
     * each visited link is validated by binary search O(log rows).
     * Allocation/release callers are the raw allocator; report callers are
     * explicit diagnostic samplers. No door walks another owner's rows.
     */
    template <class Source>
    class SmallObjectPool
    {
    public:
      enum { kSlotAlignment = Source::kAlignment < 8 ? Source::kAlignment : 8 };
      typedef char SourceAlignmentMustStorePointer[
          Source::kAlignment % sizeof(void *) == 0 ? 1 : -1];

      void *allocate(size_t size)
      {
        if (size > 256)
        {
          void *p = this->acquireUpstream(size);
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
          if (p) increment(this->larges_);
#endif
          return p;
        }
        if (size == 0) size = 1;
        unsigned char cls = 0;
        while (size > classSize(cls)) ++cls;
        if (this->free_[cls]) return this->pop(cls);
        if (this->rows_ == 128)
        {
          void *p = this->acquireUpstream(size);
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
          if (p) increment(this->directs_);
#endif
          return p;
        }
        char *base = static_cast<char *>(this->acquireUpstream(2048));
        if (!base)
        {
          void *p = this->acquireUpstream(size);
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
          if (p) increment(this->fallbacks_);
          else increment(this->refused_);
#endif
          return p;
        }
        unsigned int at = this->rows_;
        while (at && before(base, this->table_[at - 1].base))
        {
          this->table_[at] = this->table_[at - 1];
          --at;
        }
        this->table_[at].base = base;
        this->table_[at].cls = cls;
        ++this->rows_;
        const size_t stride = classSize(cls);
        for (size_t n = 2048 / stride; n; --n)
          this->push(base + (n - 1) * stride, cls);
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
        increment(this->refills_);
#endif
        return this->pop(cls);
      }

      /** Explicit samplers call once per interval endpoint; O(1), no row walk.
       * A value copy remains independent of subsequent allocations/releases.
       */
      UpstreamGauge snapshot() const
      {
        const UpstreamGauge out = {this->upstream_.attempts, this->upstream_.successes,
                                   this->upstream_.bytesAcquired, this->upstream_.failures,
                                   this->upstream_.attempts == ULONG_MAX ||
                                   this->upstream_.successes == ULONG_MAX ||
                                   this->upstream_.bytesAcquired == ULONG_MAX ||
                                   this->upstream_.failures == ULONG_MAX};
        return out;
      }

      void release(void *p)
      {
        if (!p) return;
        char *slot = static_cast<char *>(p);
        const Row *row = this->findRow(slot);
        if (!row) Source::release(p);
        else this->push(slot, row->cls);
      }

#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
      void report(SmallObjectPoolReport &out) const
      {
        SmallObjectPoolReport empty = {};
        out = empty;
        out.rows = this->rows_;
        out.refills = this->refills_;
        out.larges = this->larges_;
        out.directs = this->directs_;
        out.fallbacks = this->fallbacks_;
        out.refused = this->refused_;
        out.poisonViolations = this->poisonViolations_;
        for (unsigned int i = 0; i < this->rows_; ++i)
          ++out.chunks[this->table_[i].cls];
        unsigned long live = 0, used = 0;
        for (unsigned char cls = 0; cls < 10; ++cls)
        {
          const size_t stride = classSize(cls);
          const unsigned long total = out.chunks[cls] * (2048 / stride);
          const char *slot = this->free_[cls];
          while (slot)
          {
            if (out.freeSlots[cls] == total) return;
            const Row *row = this->findRow(slot);
            if (!row || row->cls != cls) return;
            const size_t offset = static_cast<size_t>(slot - row->base);
            if (offset % stride || offset + stride > 2048) return;
            ++out.freeSlots[cls];
            slot = next(slot);
          }
          live += total - out.freeSlots[cls];
          used += stride * (total - out.freeSlots[cls]);
        }
        out.live = live;
        out.unusedBytes = 2048UL * this->rows_ - used;
        out.valid = true;
      }
#endif

    private:
      /** Edge history owned by this pool; acquireUpstream is the sole writer. */
      struct UpstreamCounts
      {
        unsigned long attempts, successes, bytesAcquired, failures;
      };
      UpstreamCounts upstream_;

      static unsigned long cappedAdd(unsigned long count, size_t amount)
      {
        if (amount > ULONG_MAX || count >= ULONG_MAX - amount) return ULONG_MAX;
        return count + static_cast<unsigned long>(amount);
      }

      /** All four allocation paths call once per actual Source attempt; O(1),
       * no rows walked, no allocation beyond the unchanged Source request.
       */
      void *acquireUpstream(size_t size)
      {
        this->upstream_.attempts = cappedAdd(this->upstream_.attempts, 1);
        void *p = Source::acquire(size);
        if (p)
        {
          this->upstream_.successes = cappedAdd(this->upstream_.successes, 1);
          this->upstream_.bytesAcquired = cappedAdd(this->upstream_.bytesAcquired, size);
        }
        else this->upstream_.failures = cappedAdd(this->upstream_.failures, 1);
        return p;
      }

      struct Row { char *base; unsigned char cls; };
      Row table_[128];
      unsigned int rows_;
      char *free_[10];
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
      unsigned long refills_, larges_, directs_, fallbacks_, refused_, poisonViolations_;

      static void increment(unsigned long &counter)
      {
        if (counter != ULONG_MAX) ++counter;
      }
      static size_t poisonSize(unsigned char cls)
      {
        const size_t room = classSize(cls) - sizeof(void *);
        return room < sizeof(unsigned long) ? room : sizeof(unsigned long);
      }
#endif
      static size_t classSize(unsigned char cls)
      {
        static const unsigned short kClassSizes[10] = {8, 16, 24, 32, 48, 64, 96, 128, 192, 256};
        return kClassSizes[cls];
      }
      static bool before(const char *a, const char *b)
      {
        return std::less<const char *>()(a, b);
      }
      const Row *findRow(const char *p) const
      {
        unsigned int lo = 0, hi = this->rows_;
        while (lo < hi)
        {
          const unsigned int mid = lo + (hi - lo) / 2;
          const Row &row = this->table_[mid];
          if (before(p, row.base)) hi = mid;
          else if (!before(p, row.base + 2048)) lo = mid + 1;
          else return &row;
        }
        return 0;
      }
      static char *next(const char *slot)
      {
        char *link;
        memcpy(&link, slot, sizeof(link));
        return link;
      }
      void push(char *slot, unsigned char cls)
      {
        memcpy(slot, &this->free_[cls], sizeof(char *));
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
        const unsigned long poison = 0xDEADBEEFUL;
        memcpy(slot + sizeof(void *), &poison, poisonSize(cls));
#endif
        this->free_[cls] = slot;
      }
      void *pop(unsigned char cls)
      {
        char *slot = this->free_[cls];
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
        const unsigned long poison = 0xDEADBEEFUL;
        if (memcmp(slot + sizeof(void *), &poison, poisonSize(cls)) != 0)
          increment(this->poisonViolations_);
#endif
        this->free_[cls] = next(slot);
        return slot;
      }
    };
  }
}
#endif
