#include "ToolboxMemorySource.hpp"
#include "app/FileImageSource.hpp"
#include "core/SmallObjectPool.hpp"
#include "support/AutomaticPool.hpp"
#include "support/TestVerify.hpp"
#include <climits>
#include <cstring>

static long growZone(Size) { return 0; }
static long processManagerWrapper(Size) { return 0; }
static Zone zone = {processManagerWrapper};
static GrowZoneUPP applicationProcedure;
Zone *GetZone() { return &zone; }
void SetGrowZone(GrowZoneUPP procedure) { applicationProcedure = procedure; }

namespace
{
  /** Retains fake Memory Manager blocks, including process-lifetime pool chunks. */
  struct FakeMemoryManager
  {
    static FakeMemoryManager *active;
    struct Block
    {
      void *allocation;
      Ptr original;
      Size size;
      bool released;
    };
    Block blocks[132];
    unsigned int count, releases, calls;
    unsigned int residue;
    bool refuseChunks, refuseAll;
    Size ceiling;

    FakeMemoryManager()
        : count(0), releases(0), calls(0), residue(6),
          refuseChunks(false), refuseAll(false), ceiling(INT32_MAX)
    {
      LOKA_VERIFY(!active);
      active = this;
    }
    ~FakeMemoryManager()
    {
      for (unsigned int i = 0; i < this->count; ++i)
      {
        LOKA_VERIFY(this->blocks[i].original[this->blocks[i].size] == 0x5A);
        std::free(this->blocks[i].allocation);
      }
      active = 0;
    }
  };
  FakeMemoryManager *FakeMemoryManager::active = 0;
  typedef loka::toolbox::ClassicMemorySource Source;

  /** Only this fixture strengthens the promise: originals are 6 mod 8,
   * so the production four-byte adjustment yields 0 mod 8. This satisfies
   * the host pool's sizeof(void*) constraint without changing Classic's ABI.
   */
  struct HostPoolSource : Source { enum { kAlignment = 8 }; };
  typedef loka::testing::AutomaticPool<HostPoolSource> Pool;

  void checkPayload(void *p, size_t size)
  {
    LOKA_VERIFY(p && reinterpret_cast<uintptr_t>(p) % 4 == 0);
    std::memset(p, 0xA5, size);
  }

  void testPoolPaths()
  {
    FakeMemoryManager memory;
    Pool pool;
    const size_t sizes[] = {8, 16, 24, 32, 48, 64, 96, 128, 192, 256};
    for (unsigned int c = 0; c < 10; ++c)
      for (size_t i = 0; i < 2048 / sizes[c]; ++i)
      {
        void *p = pool.allocate(sizes[c]);
        checkPayload(p, sizes[c]);
        pool.release(p);
        LOKA_VERIFY(pool.allocate(sizes[c]) == p);
      }
    LOKA_VERIFY(memory.count == 10 && memory.releases == 0);
    for (unsigned int i = 0; i < memory.count; ++i)
      LOKA_VERIFY(memory.blocks[i].size == 2052);
    void *large = pool.allocate(1408);
    checkPayload(large, 1408);
    pool.release(large);
    LOKA_VERIFY(memory.releases == 1 && memory.blocks[10].size == 1412);
    memory.refuseChunks = true;
    void *fallback = pool.allocate(24);
    checkPayload(fallback, 24);
    pool.release(fallback);
    LOKA_VERIFY(memory.releases == 2 && memory.blocks[11].size == 28);
    memory.refuseChunks = false;
    while (memory.count < 130) checkPayload(pool.allocate(256), 256);
    // 128 retained chunks plus the released large and fallback blocks.
    void *direct = pool.allocate(32);
    checkPayload(direct, 32);
    pool.release(direct);
    LOKA_VERIFY(memory.releases == 3 && memory.blocks[130].size == 36);
    const loka::core::UpstreamGauge gauge = pool.snapshot();
    LOKA_VERIFY(gauge.bytesAcquired == 128UL * 2048 + 1408 + 24 + 32);
    memory.refuseAll = true;
    LOKA_VERIFY(pool.allocate(300) == 0);
    pool.release(0);
    LOKA_VERIFY(memory.releases == 3);
  }

  void testSourceEdges()
  {
    FakeMemoryManager memory;
    LOKA_VERIFY(Source::kAlignment == 4);
    // Exercise every possible prefix distance, including already aligned input.
    for (unsigned int residue = 0; residue < 4; ++residue)
    {
      memory.residue = residue;
      void *p = Source::acquire(257);
      checkPayload(p, 257);
      Source::release(p);
      LOKA_VERIFY(memory.blocks[residue].size == 261);
    }
    const unsigned int calls = memory.calls;
    LOKA_VERIFY(Source::acquire(static_cast<size_t>(-1)) == 0);
    LOKA_VERIFY(Source::acquire(static_cast<size_t>(INT32_MAX) - 3) == 0);
    LOKA_VERIFY(memory.calls == calls);
    void *zero = Source::acquire(0);
    checkPayload(zero, 0);
    Source::release(zero);
    memory.refuseAll = true;
    LOKA_VERIFY(Source::acquire(static_cast<size_t>(INT32_MAX) - 4) == 0);
    LOKA_VERIFY(Source::acquire(1) == 0);
    LOKA_VERIFY(memory.calls == calls + 3 && memory.releases == 5);
  }
}

Ptr NewPtr(Size size)
{
  LOKA_VERIFY(applicationProcedure == 0);
  FakeMemoryManager &memory = *FakeMemoryManager::active;
  ++memory.calls;
  if (size > memory.ceiling || memory.refuseAll || (memory.refuseChunks && size == 2052)) return 0;
  LOKA_VERIFY(size >= 0 && memory.count < 132);
  FakeMemoryManager::Block &block = memory.blocks[memory.count++];
  block.allocation = std::malloc(static_cast<size_t>(size) + 16);
  LOKA_VERIFY(block.allocation);
  block.original = reinterpret_cast<Ptr>(
      (reinterpret_cast<uintptr_t>(block.allocation) + 7) & ~uintptr_t(7)) + memory.residue;
  block.size = size;
  block.original[size] = 0x5A;
  block.released = false;
  LOKA_VERIFY(reinterpret_cast<uintptr_t>(block.original) % 8 == memory.residue);
  return block.original;
}

void DisposePtr(Ptr storage)
{
  FakeMemoryManager &memory = *FakeMemoryManager::active;
  for (unsigned int i = 0; i < memory.count; ++i)
    if (memory.blocks[i].original == storage)
    {
      LOKA_VERIFY(!memory.blocks[i].released);
      memory.blocks[i].released = true;
      ++memory.releases;
      return;
    }
  LOKA_VERIFY(false); // An interior pointer must never reach the Memory Manager.
}

namespace
{
  void *sourceAllocate(std::size_t n, const loka::core::LokaAllocationSite &) { return Source::acquire(n); }
  void sourceFree(void *p, const loka::core::LokaAllocationSite &) { Source::release(p); }
  void testBlobReadRefusesAlignmentOverhead()
  {
    const char *path = "_blob_plus_four.bin";
    const std::size_t M = 1024;
    unsigned char bytes[M];
    std::memset(bytes, 42, M);
    FILE *file = std::fopen(path, "wb");
    LOKA_VERIFY(file && std::fwrite(bytes, 1, M, file) == M);
    LOKA_VERIFY(std::fclose(file) == 0);
    FakeMemoryManager memory;
    memory.ceiling = M;
    loka::core::LokaAllocSetBackend(&sourceAllocate, &sourceFree);
    {
      loka::core::resource::Blob out = loka::core::resource::Blob::Create();
      LOKA_VERIFY(out.tryAssign(bytes, 1));
      const loka::core::resource::Blob before = out;
      const loka::file::File input = loka::file::File::FromPath(path);
      LOKA_VERIFY(loka::app::ReadFileImageBlob(0, input, out) == loka::platform::file::READ_ALLOCATION_REFUSED);
      LOKA_VERIFY(out == before && out.size() == 1 && out.data()[0] == 42);
    }
    LOKA_VERIFY(memory.count == memory.releases);
    loka::core::LokaAllocSetBackend(0, 0);
    LOKA_VERIFY(std::remove(path) == 0);
  }
}

int main()
{
  testSourceEdges();
  LOKA_VERIFY(applicationProcedure == 0);
  loka::toolbox::InstallReserveGrowZone(growZone);
  LOKA_VERIFY(applicationProcedure == growZone && zone.gzProc != growZone);
  testPoolPaths();
  LOKA_VERIFY(applicationProcedure == growZone);
  testSourceEdges();
  LOKA_VERIFY(applicationProcedure == growZone);
  testBlobReadRefusesAlignmentOverhead();
  LOKA_VERIFY(applicationProcedure == growZone);
  std::puts("Classic memory source alignment and recovery pins passed");
}
