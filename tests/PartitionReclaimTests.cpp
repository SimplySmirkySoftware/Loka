#include "PartitionReclaimTests.hpp"
#include "support/TestVerify.hpp"
#include "app/scene/boundary/Boundary.hpp"
#include "app/scene/boundary/detail/NodeBuildTicket.hpp"
#include "core/SmallObjectPool.hpp"
#include "support/AutomaticPool.hpp"
#include <cstdlib>
#include <cstring>

using namespace loka::app::scene;
using namespace loka::app::scene::detail;

namespace loka
{
  namespace dsl
  {
    namespace testing
    {
      /** Fixture access to the real parked ledger; no alternate retention mechanism. */
      class PartitionReclaimAccess
      {
      public:
        static void park(BoundaryNode &owner, Node *node, unsigned arm)
        {
          owner.parkBranch(BoundaryParkedBranchKey(9001, 0, 0, 0), node, arm);
        }
        static Node *take(BoundaryNode &owner, unsigned arm)
        {
          return owner.takeParkedBranch(BoundaryParkedBranchKey(9001, 0, 0, 0), arm);
        }
      };
    } // namespace testing
  } // namespace dsl
} // namespace loka

namespace
{
  struct Source
  {
    enum
    {
      kAlignment = 16
    };
    static bool denied;
    static void *chunks[128];
    static void *acquire(size_t n)
    {
      if (denied)
        return 0;
      void *p = std::malloc(n);
      for (size_t i = 0; i < 128; ++i)
        if (!chunks[i])
        {
          chunks[i] = p;
          return p;
        }
      std::abort();
    }
    static void release(void *p)
    {
      for (size_t i = 0; i < 128; ++i)
        if (chunks[i] == p)
        {
          chunks[i] = 0;
          std::free(p);
          return;
        }
      std::abort();
    }
  };
  bool Source::denied = false;
  void *Source::chunks[128] = {};
  typedef loka::testing::AutomaticPool<Source> Pool;
  Pool *activePool = 0;
  int backingFrees = 0;
  int heapNodeFrees = 0;
  void *allocate(size_t n, const loka::core::LokaAllocationSite &)
  {
    return activePool->allocate(n);
  }
  void release(void *p, const loka::core::LokaAllocationSite &site)
  {
    if (std::strcmp(site.ownerTag, "NodePartition") == 0)
      ++backingFrees;
    const loka::core::LokaAllocationSite &heap = NodeHeapAllocationSite();
    if (std::strcmp(site.ownerTag, heap.ownerTag) == 0 && std::strcmp(site.typeTag, heap.typeTag) == 0)
      ++heapNodeFrees;
    activePool->release(p);
  }
  struct Backend
  {
    Pool pool;
    Backend()
    {
      activePool = &this->pool;
      backingFrees = 0;
      heapNodeFrees = 0;
      Source::denied = false;
      loka::core::LokaAllocSetBackend(&allocate, &release);
    }
    ~Backend()
    {
      loka::core::LokaAllocSetBackend(0, 0);
      activePool = 0;
      Source::denied = false;
      for (size_t i = 0; i < 128; ++i)
        if (Source::chunks[i])
          Source::release(Source::chunks[i]);
    }
  };
  struct Owner : BoundaryNode
  {
    using BoundaryNode::retireDetachedNode;
    using BoundaryNode::retireOwnedNodeGeneration;
    virtual void composeWithContext(ComponentContext &, ComposeEvent) {}
  };
  struct Leaf : Node
  {
    int *deaths;
    bool *providerAlive;
    // Exceeds the Classic pool cap: a heap-route mutant must reach Source.
    char payload[300];
    Leaf(int &d, bool *alive = 0)
        : deaths(&d),
          providerAlive(alive)
    {
    }
    ~Leaf()
    {
      assert(!this->providerAlive || *this->providerAlive);
      ++*this->deaths;
    }
  };
  struct Root : NestableNode
  {
    bool *alive;
    int *deaths;
    Root(bool &a, int &d)
        : alive(&a),
          deaths(&d)
    {
      a = true;
    }
    ~Root()
    {
      *this->alive = false;
      ++*this->deaths;
    }
  };
  template <class T> NodePartition *seat(Owner &owner, size_t count)
  {
    SeatLayoutTable table;
    LOKA_VERIFY(table.append(NodeSlotLayout::of<T>(count)));
    NodePartition *bank = owner.installPartitionFixture(table);
    assert(bank);
    return bank;
  }
  Leaf *leaf(NodePartition &bank, int &deaths, Node *owner = 0, bool *alive = 0)
  {
    void *p = bank.allocate(NodeSlotLayout::of<Leaf>(1));
    assert(p);
    Leaf *node = new (p) Leaf(deaths, alive);
    LOKA_VERIFY(bank.registerNode(node, owner));
    return node;
  }
  struct FailedBuild : NodeBuildOperation
  {
    struct Factory
    {
      int &deaths;
      bool &alive;
      Factory(int &d, bool &a)
          : deaths(d),
            alive(a)
      {
      }
      Leaf *construct(void *p)
      {
        return new (p) Leaf(this->deaths, &this->alive);
      }
    } factory;
    Node *provider;
    FailedBuild(int &d, bool &a, Node *p)
        : factory(d, a),
          provider(p)
    {
    }
    bool buildAndAttach(NodeBuildTicket &ticket)
    {
      Leaf *candidate = ticket.create<Leaf>(this->factory, this->provider);
      assert(candidate);
      (void)candidate;
      return false;
    }
  };
  void retire(Owner &owner, Node *node)
  {
    ComponentContext context;
    context.setBoundary(&owner);
    owner.retireDetachedNode(context, node);
  }
  void checkZero(const loka::core::UpstreamGauge &before, const loka::core::UpstreamGauge &after)
  {
    assert(!before.saturated && !after.saturated);
    assert(after.attempts == before.attempts && after.successes == before.successes);
    assert(after.bytesAcquired == before.bytesAcquired && after.failures == before.failures);
    (void)before;
    (void)after;
  }
} // namespace

void testPartitionReclaimClockAndExactlyOnce()
{
  Backend backend;
  int deaths = 0;
  {
    Owner owner;
    NodePartition *bank = seat<Leaf>(owner, 1);
    Leaf *node = leaf(*bank, deaths);
    const NodeSlotLayout layout = NodeSlotLayout::of<Leaf>(1);
    NotifySubtreeNodeDetached(node);
    assert(node->lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
    LOKA_VERIFY(!bank->allocate(layout));
    retire(owner, node);
    assert(deaths == 0);
    LOKA_VERIFY(!bank->allocate(layout));
    owner.drainRetiredSubtreesAtNextTrackerRun();
    assert(deaths == 1);
    // Occupancy must be cleared by the real drain, not just the resident row.
    LOKA_VERIFY(!bank->cancel(node, layout));
    owner.drainRetiredSubtreesAtNextTrackerRun();
    void *returned = bank->allocate(layout);
    assert(returned == node);
    LOKA_VERIFY(!bank->allocate(layout));
    LOKA_VERIFY(bank->cancel(returned, layout));
  }
  assert(deaths == 1 && backingFrees == 1);
}

void testPartitionReclaimRetainsBackingTwentyRounds()
{
  Backend backend;
  int deaths = 0;
  {
    Owner owner;
    NodePartition *bank = seat<Leaf>(owner, 4);
    Leaf *nodes[4];
    void *addresses[4];
    for (int i = 0; i < 4; ++i)
      addresses[i] = nodes[i] = leaf(*bank, deaths);
    const loka::core::UpstreamGauge before = backend.pool.snapshot();
    Source::denied = true;
    for (int round = 0; round < 20; ++round)
    {
      for (int i = 0; i < 4; ++i)
        retire(owner, nodes[i]);
      // Exercise the existing generation-snapshot seam as well as direct drain.
      owner.retireOwnedNodeGeneration();
      owner.drainRetiredSubtreesAtNextTrackerRun();
      bool seen[4] = {};
      for (int i = 0; i < 4; ++i)
      {
        nodes[i] = leaf(*bank, deaths);
        int at = 0;
        while (at < 4 && addresses[at] != nodes[i])
          ++at;
        assert(at < 4 && !seen[at]);
        seen[at] = true;
      }
      assert(deaths == (round + 1) * 4 && backingFrees == 0);
      (void)seen;
    }
    checkZero(before, backend.pool.snapshot());
    std::printf("Partition reclaim: 20 rounds, 80 returns, upstream attempts=0, same four addresses\n");
    // The final generation deliberately remains owner-edge-only at teardown.
  }
  assert(deaths == 84 && backingFrees == 1);
}

void testPartitionReclaimParkedArm()
{
  Backend backend;
  int deaths = 0;
  {
    Owner owner;
    NodePartition *bank = seat<Leaf>(owner, 2);
    Leaf *first = leaf(*bank, deaths);
    Leaf *second = leaf(*bank, deaths);
    typedef loka::dsl::testing::PartitionReclaimAccess Access;
    NotifySubtreeNodeDetached(first);
    Access::park(owner, first, 0);
    owner.drainRetiredSubtreesAtNextTrackerRun();
    LOKA_VERIFY(!bank->allocate(NodeSlotLayout::of<Leaf>(1)));
    NotifySubtreeNodeDetached(second);
    Access::park(owner, second, 1);
    Node *restored = Access::take(owner, 0);
    assert(restored == first && deaths == 0);
    retire(owner, restored);
    owner.drainRetiredSubtreesAtNextTrackerRun();
    assert(deaths == 1);
    void *p = bank->allocate(NodeSlotLayout::of<Leaf>(1));
    assert(p == first);
    LOKA_VERIFY(!bank->allocate(NodeSlotLayout::of<Leaf>(1)));
    LOKA_VERIFY(bank->cancel(p, NodeSlotLayout::of<Leaf>(1)));
    // The parked second arm is drained by landlord teardown.
  }
  assert(deaths == 2 && backingFrees == 1);
}

void testPartitionReclaimNestedAndFailedCandidate()
{
  Backend backend;
  bool alive = false;
  int deaths = 0;
  {
    Owner owner;
    SeatLayoutTable table;
    LOKA_VERIFY(table.append(NodeSlotLayout::of<Root>(1)));
    LOKA_VERIFY(table.append(NodeSlotLayout::of<Owner>(1)));
    LOKA_VERIFY(table.append(NodeSlotLayout::of<Leaf>(1)));
    NodePartition *bank = owner.installPartitionFixture(table);
    assert(bank);
    void *rootStorage = bank->allocate(NodeSlotLayout::of<Root>(1));
    assert(rootStorage);
    Root *root = new (rootStorage) Root(alive, deaths);
    LOKA_VERIFY(bank->registerNode(root, 0));
    void *nestedStorage = bank->allocate(NodeSlotLayout::of<Owner>(1));
    assert(nestedStorage);
    Owner *nested = new (nestedStorage) Owner();
    LOKA_VERIFY(bank->registerNode(nested, root));
    root->addChild(nested);
    NodePartition *inner = seat<Leaf>(*nested, 2);
    nested->addChild(leaf(*inner, deaths, 0, &alive));
    retire(*nested, leaf(*inner, deaths, 0, &alive));
    // Component-style failed materialization: constructed and registered, but
    // never attached/published. The provider edge is its only recovery path.
    FailedBuild failed(deaths, alive, root);
    const NodeSlotLayout demand = NodeSlotLayout::of<Leaf>(1);
    LOKA_VERIFY(!bank->buildFixture(&demand, 1, failed));
    retire(owner, root);
    assert(alive && deaths == 0);
    owner.drainRetiredSubtreesAtNextTrackerRun();
    assert(!alive && deaths == 4 && backingFrees == 1);
    LOKA_VERIFY(!bank->cancel(rootStorage, NodeSlotLayout::of<Root>(1)));
  }
  assert(deaths == 4 && backingFrees == 2);
}

void testPartitionReclaimLandlordCensus()
{
  Backend backend;
#ifdef LOKA_LIFECYCLE_AUDIT
  const int baseline = loka::core::LokaAllocAuditTotalLiveCount();
#endif
  int deaths = 0;
  {
    Owner owner;
    NodePartition *bank = seat<Leaf>(owner, 2);
    owner.addChild(leaf(*bank, deaths));
    leaf(*bank, deaths);
  }
  assert(deaths == 2 && backingFrees == 1);
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(loka::core::LokaAllocAuditTotalLiveCount() == baseline);
#endif
}

namespace
{
  struct MixedOwner : Owner
  {
    NodePartition *bank;
    Leaf *queued;
    int &checked;
    explicit MixedOwner(int &count)
        : bank(0),
          queued(0),
          checked(count)
    {
    }
    ~MixedOwner()
    {
      // Observe the queued slot while its landlord and backing are still alive.
      // The live sibling must remain occupied until Boundary base cleanup.
      this->drainRetiredSubtreesAtNextTrackerRun();
      const NodeSlotLayout layout = NodeSlotLayout::of<Leaf>(1);
      LOKA_VERIFY(!this->bank->cancel(this->queued, layout));
      void *slot = this->bank->allocate(layout);
      assert(slot == this->queued);
      LOKA_VERIFY(!this->bank->allocate(layout));
      LOKA_VERIFY(this->bank->cancel(slot, layout));
      ++this->checked;
    }
  };

  void mixedLegacyLandlord(bool snapshot)
  {
    Backend backend;
    int deaths = 0, checked = 0;
    loka::core::UpstreamGauge before = {};
#ifdef LOKA_LIFECYCLE_AUDIT
    const int baseline = loka::core::LokaAllocAuditTotalLiveCount();
#endif
    {
      Owner outer;
      NodeArena &arena = *outer.nodeArena();
      arena.reserve(sizeof(MixedOwner));
      void *storage = arena.allocate(sizeof(MixedOwner), AlignOf<MixedOwner>::value);
      assert(storage);
      MixedOwner *nested = new (storage) MixedOwner(checked);
      arena.registerNode(nested);
      outer.addChild(nested);
      nested->bank = seat<Leaf>(*nested, 2);
      // On 814ddf9e ASan observed this live child's interior slot address reach
      // DestroyHeapNode in both the arena.clear and generation-snapshot walks.
      nested->addChild(leaf(*nested->bank, deaths));
      nested->queued = leaf(*nested->bank, deaths);
      retire(*nested, nested->queued);
      before = backend.pool.snapshot();
      Source::denied = true;
      if (snapshot)
      {
        std::vector<Node *> detached;
        outer.detachChildrenTo(detached);
        assert(detached.size() == 1 && detached[0] == nested);
        retire(outer, nested);
        outer.retireOwnedNodeGeneration();
        assert(deaths == 0 && checked == 0 && backingFrees == 0);
        outer.drainRetiredSubtreesAtNextTrackerRun();
        assert(deaths == 2 && checked == 1 && backingFrees == 1);
      }
      else
      {
        // Leave the legacy resident to outer landlord teardown's arena.clear().
        assert(deaths == 0 && checked == 0);
      }
    }
    checkZero(before, backend.pool.snapshot());
    assert(deaths == 2 && checked == 1 && backingFrees == 1 && heapNodeFrees == 0);
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(loka::core::LokaAllocAuditTotalLiveCount() == baseline);
#endif
  }
} // namespace

void testPartitionReclaimLegacyLandlordTeardown()
{
  mixedLegacyLandlord(false);
}

void testPartitionReclaimLegacyGenerationSnapshot()
{
  mixedLegacyLandlord(true);
}
