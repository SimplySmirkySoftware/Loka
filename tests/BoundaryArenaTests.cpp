#include "BoundaryArenaTests.hpp"
#include "support/TestVerify.hpp"
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <new>
#include <vector>
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include "app/scene/boundary/detail/BoundaryArena.hpp"
#include "app/scene/boundary/detail/NodePartition.hpp"
#include "app/scene/boundary/detail/NodeBuildTicket.hpp"
#include "app/scene/composition/NodeComposition.hpp"
#include "app/scene/state/StateBatchBase.hpp"
#include "core/LokaAlloc.hpp"
#include "core/State.hpp"
#include "core/StateTracker.hpp"
#include <cstring>
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/BoundarySection.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/PolicyScope.hpp"
#include "app/scene/Scene.hpp"
#include "app/scene/node/Conditional.hpp"
#include "app/scene/node/ComponentNode.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "app/nodes/nestable/Keyed.hpp"
#include "support/RecordingPlatformController.hpp"
#include "testing/core/HeldTestAccess.hpp"
#include "testing/scene/ProbeArmSeatDefinition.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "testing/scene/OwnershipDump.hpp"

namespace
{

  // Counting gate backend: proves which acquisitions cross the gate while
  // still serving real storage. It is installed only around
  // allocation-balanced regions so storage always returns through the
  // backend that produced it.
  int g_gateBackendAllocCalls = 0;
  int g_gateBackendFreeCalls = 0;

  void *countingGateBackendAlloc(std::size_t size, const loka::core::LokaAllocationSite &site)
  {
    (void)site;
    ++g_gateBackendAllocCalls;
    return new (std::nothrow) char[size];
  }

  void countingGateBackendFree(void *ptr, const loka::core::LokaAllocationSite &site)
  {
    (void)site;
    ++g_gateBackendFreeCalls;
    delete[] static_cast<char *>(ptr);
  }

  static bool isPowerOfTwo(size_t value)
  {
    return value != 0 && (value & (value - 1)) == 0;
  }

  static bool isAligned(void *ptr, size_t align)
  {
    size_t address = reinterpret_cast<size_t>(ptr);
    return (address & (align - 1)) == 0;
  }

  struct StateArenaDestroyLog
  {
    int count;
    loka::core::StateBase *first;
    loka::core::StateBase *second;
  };

  static StateArenaDestroyLog g_stateArenaDestroyLog = {0, 0, 0};

  static void resetStateArenaDestroyLog()
  {
    g_stateArenaDestroyLog.count = 0;
    g_stateArenaDestroyLog.first = 0;
    g_stateArenaDestroyLog.second = 0;
  }

  static void recordStateDestroy(loka::core::StateBase *state)
  {
    ++g_stateArenaDestroyLog.count;
    if (!g_stateArenaDestroyLog.first)
    {
      g_stateArenaDestroyLog.first = state;
    }
    else if (!g_stateArenaDestroyLog.second)
    {
      g_stateArenaDestroyLog.second = state;
    }
  }

  class TestStateOwner : public loka::app::scene::IStateOwner
  {
  public:
    TestStateOwner()
        : tracker_(),
          ownedStates_(),
          arena_(),
          holdLedger_(this)
    {
    }

    ~TestStateOwner()
    {
      this->clearOwnedStates();
    }

    virtual void adoptState(loka::core::StateBase *state)
    {
      if (!state)
      {
        return;
      }
      ownedStates_.push_back(state);
      tracker_.addState(state);
    }

    virtual void adoptStateUnchecked(loka::core::StateBase *state)
    {
      if (!state)
      {
        return;
      }
      ownedStates_.push_back(state);
      tracker_.addStateUnchecked(state);
    }

    virtual void releaseState(loka::core::StateBase *state)
    {
      if (!state)
      {
        return;
      }
      for (size_t i = 0; i < ownedStates_.size();)
      {
        if (ownedStates_[i] == state)
        {
          ownedStates_.erase(ownedStates_.begin() + i);
        }
        else
        {
          ++i;
        }
      }
      tracker_.removeState(state);
      if (state->isArenaAllocated())
      {
        arena_.releaseState(state);
      }
      else
      {
        loka::app::scene::DestroyAdoptedHeapState(state);
      }
    }

    virtual void reserveStates(size_t count)
    {
      ownedStates_.reserve(ownedStates_.size() + count);
      tracker_.reserveStates(count);
    }

    virtual void reserveStateArena(size_t totalSize)
    {
      arena_.reserve(totalSize);
    }

    virtual void noteStateAllocationFailure()
    {
      assert(false && "unexpected state OOM in this test fixture");
    }

    virtual void *allocateStateMemory(size_t size, size_t align)
    {
      return arena_.allocate(size, align);
    }

    virtual void registerStateMemory(loka::core::StateBase *state, void (*destroy)(loka::core::StateBase *))
    {
      arena_.registerState(state, destroy);
    }

    virtual loka::core::HoldLedger *holdLedger()
    {
      return &this->holdLedger_;
    }
    virtual void reserveHeldArena(size_t totalSize)
    {
      this->arena_.reserve(totalSize);
    }
    virtual void *allocateHeldMemory(size_t size, size_t align)
    {
      return this->arena_.allocate(size, align);
    }
    virtual void registerHeldMemory(loka::core::detail::HeldBlockBase *block)
    {
      this->arena_.registerHeld(block);
    }
    virtual void retireHeldBlock(loka::core::detail::HeldBlockBase *)
    {
      assert(false && "TestStateOwner has no retire pool");
    }

    virtual loka::core::StateTracker *tracker()
    {
      return &tracker_;
    }

  private:
    void clearOwnedStates()
    {
      for (size_t i = 0; i < ownedStates_.size(); ++i)
      {
        loka::core::StateBase *state = ownedStates_[i];
        if (!state)
        {
          continue;
        }
        tracker_.removeState(state);
        if (!state->isArenaAllocated())
        {
          loka::app::scene::DestroyAdoptedHeapState(state);
        }
      }
      ownedStates_.clear();
      arena_.clear();
    }

    loka::core::PushStateTracker tracker_;
    std::vector<loka::core::StateBase *> ownedStates_;
    loka::app::scene::StateArena arena_;
    loka::core::HoldLedger holdLedger_;
  };

  static void testNodeArenaAlignmentAndCapacity()
  {
    const size_t minAlign = loka::app::scene::detail::NormalizeArenaAlign(1);
    (void)minAlign;
    assert(minAlign >= sizeof(void *));
    assert(isPowerOfTwo(minAlign));

    const size_t roundedAlign = loka::app::scene::detail::NormalizeArenaAlign(3);
    (void)roundedAlign;
    assert(roundedAlign >= 3);
    assert(isPowerOfTwo(roundedAlign));

    loka::app::scene::NodeArena arena;
    assert(!arena.hasCapacity());
    LOKA_VERIFY(arena.allocate(4, 4) == 0);

    arena.reserve(32);
    assert(arena.hasCapacity());

    void *first = arena.allocate(4, 4);
    (void)first;
    assert(first != 0);
    assert(isAligned(first, loka::app::scene::detail::NormalizeArenaAlign(4)));

    void *second = arena.allocate(8, 8);
    (void)second;
    assert(second != 0);
    assert(isAligned(second, loka::app::scene::detail::NormalizeArenaAlign(8)));
    assert(second != first);

    LOKA_VERIFY(arena.allocate(1024, 4) == 0);

    arena.reserve(16);
    assert(arena.hasCapacity());
    void *afterReserve = arena.allocate(16, 4);
    (void)afterReserve;
    assert(afterReserve != 0);

    arena.clear();
    assert(!arena.hasCapacity());
    LOKA_VERIFY(arena.allocate(4, 4) == 0);
  }

  static void testNodeArenaRegistrationStampsOwner()
  {
    loka::app::scene::Node *heapNode = new loka::app::scene::Node();
    assert(heapNode->arenaOwner() == 0);
    assert(!heapNode->isArenaAllocated());
    delete heapNode;

    loka::app::scene::NodeArena arena;
    const size_t nodeBytes =
        sizeof(loka::app::scene::Node) + loka::app::scene::detail::AlignOf<loka::app::scene::Node>::value;
    arena.reserve(nodeBytes);
    void *nodeMemory = arena.allocate(sizeof(loka::app::scene::Node),
                                      loka::app::scene::detail::AlignOf<loka::app::scene::Node>::value);
    assert(nodeMemory != 0);
    loka::app::scene::Node *arenaNode = new (nodeMemory) loka::app::scene::Node();
    assert(arenaNode->arenaOwner() == 0);
    assert(!arenaNode->isArenaAllocated());

    arena.registerNode(arenaNode);
    assert(arenaNode->arenaOwner() == &arena);
    assert(arenaNode->isArenaAllocated());
  }

  class NodeArenaDestroyOrderProbe : public loka::app::scene::Node
  {
  public:
    NodeArenaDestroyOrderProbe(std::vector<int> *destroyOrder, int marker)
        : destroyOrder_(destroyOrder),
          marker_(marker)
    {
    }

    virtual ~NodeArenaDestroyOrderProbe()
    {
      this->destroyOrder_->push_back(this->marker_);
    }

  private:
    std::vector<int> *destroyOrder_;
    int marker_;
  };

  class LinkedNodeArenaDestroyOrderProbe : public loka::app::scene::NestableNode
  {
  public:
    LinkedNodeArenaDestroyOrderProbe(std::vector<int> *destroyOrder, int marker)
        : destroyOrder_(destroyOrder),
          marker_(marker)
    {
    }

    virtual ~LinkedNodeArenaDestroyOrderProbe()
    {
      this->destroyOrder_->push_back(this->marker_);
    }

  private:
    std::vector<int> *destroyOrder_;
    int marker_;
  };

  class NodeArenaDestroyCountProbe : public loka::app::scene::Node
  {
  public:
    explicit NodeArenaDestroyCountProbe(int *destructorCalls)
        : destructorCalls_(destructorCalls)
    {
    }

    virtual ~NodeArenaDestroyCountProbe()
    {
      ++*this->destructorCalls_;
    }

  private:
    int *destructorCalls_;
  };

  static void testNodeArenaDestroysChildrenBeforeParents()
  {
    const size_t probeBytes = sizeof(NodeArenaDestroyOrderProbe)
                              + loka::app::scene::detail::AlignOf<NodeArenaDestroyOrderProbe>::value;
    std::vector<int> destroyOrder;
    loka::app::scene::NodeArena arena;
    arena.reserve(probeBytes * 2);

    void *parentMemory = arena.allocate(
        sizeof(NodeArenaDestroyOrderProbe),
        loka::app::scene::detail::AlignOf<NodeArenaDestroyOrderProbe>::value);
    assert(parentMemory != 0);
    NodeArenaDestroyOrderProbe *parent = new (parentMemory) NodeArenaDestroyOrderProbe(&destroyOrder, 1);
    arena.registerNode(parent);

    void *childMemory = arena.allocate(
        sizeof(NodeArenaDestroyOrderProbe),
        loka::app::scene::detail::AlignOf<NodeArenaDestroyOrderProbe>::value);
    assert(childMemory != 0);
    NodeArenaDestroyOrderProbe *child = new (childMemory) NodeArenaDestroyOrderProbe(&destroyOrder, 2);
    arena.registerNode(child);

    arena.clear();

    assert(destroyOrder.size() == 2);
    assert(destroyOrder[0] == 2 &&
           "NodeArena::clear must destroy children before parents");
    assert(destroyOrder[1] == 1);

    const size_t linkedParentBytes = sizeof(LinkedNodeArenaDestroyOrderProbe)
                                     + loka::app::scene::detail::AlignOf<LinkedNodeArenaDestroyOrderProbe>::value;
    destroyOrder.clear();
    arena.reserve(linkedParentBytes + probeBytes);

    void *linkedParentMemory = arena.allocate(
        sizeof(LinkedNodeArenaDestroyOrderProbe),
        loka::app::scene::detail::AlignOf<LinkedNodeArenaDestroyOrderProbe>::value);
    assert(linkedParentMemory != 0);
    LinkedNodeArenaDestroyOrderProbe *linkedParent =
        new (linkedParentMemory) LinkedNodeArenaDestroyOrderProbe(&destroyOrder, 1);
    arena.registerNode(linkedParent);

    void *linkedChildMemory = arena.allocate(
        sizeof(NodeArenaDestroyOrderProbe),
        loka::app::scene::detail::AlignOf<NodeArenaDestroyOrderProbe>::value);
    assert(linkedChildMemory != 0);
    NodeArenaDestroyOrderProbe *linkedChild =
        new (linkedChildMemory) NodeArenaDestroyOrderProbe(&destroyOrder, 2);
    arena.registerNode(linkedChild);
    linkedParent->addChild(linkedChild);

    arena.clear();

    assert(destroyOrder.size() == 2);
    assert(destroyOrder[0] == 2 &&
           "NodeArena::clear must sever linked parents before reverse destruction");
    assert(destroyOrder[1] == 1);
  }

  static void testNodeArenaReleaseTombstonePreventsClearDoubleDestruction()
  {
    const size_t probeBytes = sizeof(NodeArenaDestroyCountProbe)
                              + loka::app::scene::detail::AlignOf<NodeArenaDestroyCountProbe>::value;
    int firstDestructorCalls = 0;
    int secondDestructorCalls = 0;

    {
      loka::app::scene::NodeArena arena;
      arena.reserve(probeBytes * 2);

      void *firstMemory = arena.allocate(
          sizeof(NodeArenaDestroyCountProbe),
          loka::app::scene::detail::AlignOf<NodeArenaDestroyCountProbe>::value);
      assert(firstMemory != 0);
      NodeArenaDestroyCountProbe *first =
          new (firstMemory) NodeArenaDestroyCountProbe(&firstDestructorCalls);
      arena.registerNode(first);

      void *secondMemory = arena.allocate(
          sizeof(NodeArenaDestroyCountProbe),
          loka::app::scene::detail::AlignOf<NodeArenaDestroyCountProbe>::value);
      assert(secondMemory != 0);
      NodeArenaDestroyCountProbe *second =
          new (secondMemory) NodeArenaDestroyCountProbe(&secondDestructorCalls);
      arena.registerNode(second);

      LOKA_VERIFY(arena.releaseNode(first));
      assert(firstDestructorCalls == 1);
      assert(secondDestructorCalls == 0);

      arena.clear();
      assert(firstDestructorCalls == 1 &&
             "NodeArena::clear must skip a released node's tombstoned ledger entry");
      assert(secondDestructorCalls == 1);
    }

    assert(firstDestructorCalls == 1);
    assert(secondDestructorCalls == 1);
  }

  static void testNodeArenaClearDestroysHeapChildOfArenaParentExactlyOnce()
  {
    const size_t parentBytes = sizeof(LinkedNodeArenaDestroyOrderProbe)
                               + loka::app::scene::detail::AlignOf<LinkedNodeArenaDestroyOrderProbe>::value;
    std::vector<int> parentDestroyOrder;
    int heapChildDestructorCalls = 0;

    {
      loka::app::scene::NodeArena arena;
      arena.reserve(parentBytes);

      void *parentMemory = arena.allocate(
          sizeof(LinkedNodeArenaDestroyOrderProbe),
          loka::app::scene::detail::AlignOf<LinkedNodeArenaDestroyOrderProbe>::value);
      assert(parentMemory != 0);
      LinkedNodeArenaDestroyOrderProbe *parent =
          new (parentMemory) LinkedNodeArenaDestroyOrderProbe(&parentDestroyOrder, 1);
      arena.registerNode(parent);

      NodeArenaDestroyCountProbe *heapChild =
          new NodeArenaDestroyCountProbe(&heapChildDestructorCalls);
      assert(!heapChild->isArenaAllocated());
      parent->addChild(heapChild);

      arena.clear();
      assert(heapChildDestructorCalls == 1 &&
             "NodeArena::clear must delete a heap child detached from an arena parent exactly once");
      assert(parentDestroyOrder.size() == 1);
      assert(parentDestroyOrder[0] == 1);

      arena.clear();
      assert(heapChildDestructorCalls == 1);
    }

    assert(heapChildDestructorCalls == 1);
    assert(parentDestroyOrder.size() == 1);
  }

  static void testNodeArenaDetachAndDestroyRetiredGeneration()
  {
    const size_t tombstoneBytes = sizeof(NodeArenaDestroyCountProbe)
                                  + loka::app::scene::detail::AlignOf<NodeArenaDestroyCountProbe>::value;
    const size_t parentBytes = sizeof(LinkedNodeArenaDestroyOrderProbe)
                               + loka::app::scene::detail::AlignOf<LinkedNodeArenaDestroyOrderProbe>::value;
    int tombstoneDestructorCalls = 0;
    int heapChildDestructorCalls = 0;
    std::vector<int> parentDestroyOrder;
    loka::app::scene::NodeArena arena;
    arena.reserve(tombstoneBytes + parentBytes);

    void *tombstoneMemory = arena.allocate(
        sizeof(NodeArenaDestroyCountProbe),
        loka::app::scene::detail::AlignOf<NodeArenaDestroyCountProbe>::value);
    assert(tombstoneMemory != 0);
    NodeArenaDestroyCountProbe *tombstone =
        new (tombstoneMemory) NodeArenaDestroyCountProbe(&tombstoneDestructorCalls);
    arena.registerNode(tombstone);

    void *parentMemory = arena.allocate(
        sizeof(LinkedNodeArenaDestroyOrderProbe),
        loka::app::scene::detail::AlignOf<LinkedNodeArenaDestroyOrderProbe>::value);
    assert(parentMemory != 0);
    LinkedNodeArenaDestroyOrderProbe *parent =
        new (parentMemory) LinkedNodeArenaDestroyOrderProbe(&parentDestroyOrder, 1);
    arena.registerNode(parent);
    parent->addChild(new NodeArenaDestroyCountProbe(&heapChildDestructorCalls));

    LOKA_VERIFY(arena.releaseNode(tombstone));
    assert(tombstoneDestructorCalls == 1);

    loka::app::scene::NodeArena::RetiredNodeGeneration generation;
    LOKA_VERIFY(arena.detachRetiredGeneration(generation));
    assert(!arena.hasCapacity());
    LOKA_VERIFY(arena.allocate(4, 4) == 0);
    assert(generation.nodes.size() == 2);
    assert(generation.nodes[0] == 0 &&
           "detaching a generation must preserve arena ledger tombstones");

    arena.reserve(32);
    assert(arena.hasCapacity());
    LOKA_VERIFY(arena.allocate(4, 4) != 0 &&
           "an emptied arena must accept a fresh reservation while its old generation is retired");

    loka::app::scene::NodeArena::destroyRetiredGeneration(generation);
    assert(tombstoneDestructorCalls == 1 &&
           "retired generation destruction must skip tombstoned ledger entries");
    assert(heapChildDestructorCalls == 1 &&
           "retired generation destruction must delete detached heap children exactly once");
    assert(parentDestroyOrder.size() == 1 && parentDestroyOrder[0] == 1);

    loka::app::scene::NodeArena::destroyRetiredGeneration(generation);
    assert(tombstoneDestructorCalls == 1);
    assert(heapChildDestructorCalls == 1);
    assert(parentDestroyOrder.size() == 1);
  }

  static void testStateArenaAllocation()
  {
    loka::app::scene::StateArena arena;
    assert(!arena.hasBlocks());
    LOKA_VERIFY(arena.allocate(4, 4) == 0);

    arena.reserve(32);
    assert(arena.hasBlocks());

    void *first = arena.allocate(4, 4);
    (void)first;
    assert(first != 0);
    assert(isAligned(first, loka::app::scene::detail::NormalizeArenaAlign(4)));

    void *second = arena.allocate(8, 8);
    (void)second;
    assert(second != 0);
    assert(isAligned(second, loka::app::scene::detail::NormalizeArenaAlign(8)));
    assert(second != first);

    // Allocation may consume reserved capacity, but only reserve() may grow the
    // owner-lifetime arena. Unreserved state can then fall back to the heap.
    LOKA_VERIFY(arena.allocate(1024, 4) == 0);

    arena.reserve(1024);
    void *grown = arena.allocate(1024, 4);
    (void)grown;
    assert(grown != 0);
    assert(isAligned(grown, loka::app::scene::detail::NormalizeArenaAlign(4)));

    arena.clear();
    assert(!arena.hasBlocks());
    LOKA_VERIFY(arena.allocate(4, 4) == 0);
  }

  static void testStateArenaReleaseAndClear()
  {
    resetStateArenaDestroyLog();

    loka::core::MutableState<int> first(1);
    loka::core::MutableState<int> second(2);

    loka::app::scene::StateArena arena;
    arena.registerState(&first, &recordStateDestroy);
    arena.registerState(&second, &recordStateDestroy);

    arena.releaseState(&first);
    assert(g_stateArenaDestroyLog.count == 1);
    assert(g_stateArenaDestroyLog.first == &first);

    arena.releaseState(&first);
    assert(g_stateArenaDestroyLog.count == 1);

    arena.clear();
    assert(g_stateArenaDestroyLog.count == 2);
    assert(g_stateArenaDestroyLog.second == &second);

    arena.clear();
    assert(g_stateArenaDestroyLog.count == 2);
  }

  template <typename T> static void testStateArenaReserveCoversBatchEstimate(const T &sample)
  {
    (void)sample;
    typedef loka::core::MutableState<T> MutableStateType;

    const size_t count = 8;
    loka::app::scene::StateArena arena;
    arena.reserve(loka::app::scene::StateBatchBase::ArenaBytesForState<T>() * count);

    for (size_t i = 0; i < count; ++i)
    {
      void *mem = arena.allocate(sizeof(MutableStateType),
                                 loka::app::scene::detail::AlignOf<MutableStateType>::value);
      (void)mem;
      assert(mem != 0);
    }
  }

  static void testStateArenaReserveMatchesBatchEstimate()
  {
    testStateArenaReserveCoversBatchEstimate(char(0));
    testStateArenaReserveCoversBatchEstimate(double(0));
    testStateArenaReserveCoversBatchEstimate(loka::core::String::Literal("arena"));
  }

  static void testStateArenaGrowsAcrossOwnerBatches()
  {
    TestStateOwner owner;
    loka::app::scene::NodeState<int> firstBatch[4];
    loka::app::scene::NodeState<int> secondBatch[4];
    loka::app::scene::NodeState<int> immediate;

    {
      loka::app::scene::NodeComposition::StateBatch batch(&owner);
      for (int i = 0; i < 4; ++i)
      {
        batch.state(firstBatch[i], 10 + i);
      }
    }

    {
      loka::app::scene::NodeComposition::StateBatch batch(&owner);
      for (int i = 0; i < 4; ++i)
      {
        batch.state(secondBatch[i], 20 + i);
      }
    }

    loka::app::scene::StateBatchBase::CreateImmediateState(&owner, immediate, 99);

    for (int i = 0; i < 4; ++i)
    {
      assert(firstBatch[i].isValid());
      assert(firstBatch[i].dangerouslyMutableState()->isArenaAllocated());
      assert(firstBatch[i].get() == 10 + i);
      assert(secondBatch[i].isValid());
      assert(secondBatch[i].dangerouslyMutableState()->isArenaAllocated());
      assert(secondBatch[i].get() == 20 + i);
    }

    assert(immediate.isValid());
    assert(immediate.dangerouslyMutableState()->isArenaAllocated());
    assert(immediate.get() == 99);
  }

  struct LargeStateValue
  {
    char bytes[128];

    bool operator!=(const LargeStateValue &other) const
    {
      for (size_t i = 0; i < sizeof(bytes); ++i)
      {
        if (bytes[i] != other.bytes[i])
        {
          return true;
        }
      }
      return false;
    }
  };

  static void testUnreservedStateFallsBackAfterReservedCapacityIsConsumed()
  {
    const loka::core::LokaAllocationSite heapStateSite("StateOwner", "MutableState");
#ifdef LOKA_LIFECYCLE_AUDIT
    const int heapLiveBefore = loka::core::LokaAllocAuditLiveCount(heapStateSite);
    const int totalLiveBefore = loka::core::LokaAllocAuditTotalLiveCount();
#endif
    g_gateBackendAllocCalls = 0;
    g_gateBackendFreeCalls = 0;
    loka::core::LokaAllocSetBackend(&countingGateBackendAlloc, &countingGateBackendFree);
    {
      TestStateOwner owner;
      loka::app::scene::NodeState<LargeStateValue> reserved;
      loka::app::scene::NodeState<LargeStateValue> unreserved;
      loka::app::scene::NodeState<LargeStateValue> immediate;
      LargeStateValue initial = {{0}};

      owner.reserveStateArena(
          loka::app::scene::StateBatchBase::ArenaBytesForState<LargeStateValue>());
      loka::app::scene::StateBatchBase::CreateStateFromInitial<LargeStateValue>(
          &owner, reserved, initial);
      loka::app::scene::StateBatchBase::CreateStateFromInitial<LargeStateValue>(
          &owner, unreserved, initial);

      assert(reserved.dangerouslyMutableState()->isArenaAllocated());
      assert(!unreserved.dangerouslyMutableState()->isArenaAllocated());

      // Red before #132 S2: the heap fallback was a plain throwing new that
      // bypassed the gate. Now it crosses the gate and carries provenance so
      // release returns the storage through the backend that produced it.
      assert(unreserved.dangerouslyMutableState()->isGateAllocated());
      assert(!reserved.dangerouslyMutableState()->isGateAllocated());
#ifdef LOKA_LIFECYCLE_AUDIT
      assert(loka::core::LokaAllocAuditLiveCount(heapStateSite) == heapLiveBefore + 1);
#endif

      loka::app::scene::StateBatchBase::CreateImmediateState(&owner, immediate, initial);
      assert(immediate.dangerouslyMutableState()->isArenaAllocated());
    }
    // Slabs, block headers, and the heap-fallback state all returned through
    // the gate: the counting backend saw every acquisition come back.
    assert(g_gateBackendAllocCalls >= 3);
    assert(g_gateBackendFreeCalls == g_gateBackendAllocCalls);
    loka::core::LokaAllocSetBackend(0, 0);
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(loka::core::LokaAllocAuditLiveCount(heapStateSite) == heapLiveBefore);
    assert(loka::core::LokaAllocAuditTotalLiveCount() == totalLiveBefore);
#endif
  }

  // A minimal concrete node/definition to exercise heap-node creation
  // through the allocation gate (#132 S2b). NodeDefinition::create() is the
  // one real heap-node alloc site; this probe reaches it directly.
  class GateProbeNode;
  struct GateProbeTypeTag
  {
  };

  struct GateProbeProps : public loka::app::scene::NodePropsBase<GateProbeProps>
  {
    typedef GateProbeTypeTag TypeTag;
    typedef GateProbeNode NodeType;
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId() ? false
                                                      : this->propsTypeId() < rhs.propsTypeId();
    }
  };

  class GateProbeNode : public loka::app::scene::Node
  {
  public:
    typedef GateProbeTypeTag TypeTag;
    GateProbeProps props;
    GateProbeNode(const GateProbeProps &p)
        : props(p)
    {
    }
  };

  struct GateProbeDefinition : public loka::app::scene::NodeDefinition<GateProbeProps, GateProbeNode>
  {
  };

  int g_heapStateRefusals = 0;

  void *heapStateRefusingBackendAlloc(std::size_t size, const loka::core::LokaAllocationSite &site)
  {
    if (std::strcmp(site.ownerTag, "StateOwner") == 0)
    {
      ++g_heapStateRefusals;
      return 0;
    }
    return new (std::nothrow) char[size];
  }

  void delegatingBackendFree(void *ptr, const loka::core::LokaAllocationSite &site)
  {
    (void)site;
    delete[] static_cast<char *>(ptr);
  }

  // 32-byte payload: fits the StateBatch inline initializer storage while
  // keeping sizeof(MutableState<T>) well above the alignment slack a consumed
  // arena block can have left, so a redeclared batch always needs a new slab.
  struct WhiteFlagStatePayload
  {
    char bytes[32];

    bool operator!=(const WhiteFlagStatePayload &other) const
    {
      for (size_t i = 0; i < sizeof(bytes); ++i)
      {
        if (bytes[i] != other.bytes[i])
        {
          return true;
        }
      }
      return false;
    }
  };

  // Scene-root refusal harness (#132 ruling 3 / #140 P2). On this branch the
  // root create() is still a plain `new NodeT` and cannot return 0 under the
  // gate's refusing backend (S2b/#140 is not merged here), so the refusal is
  // injected through a test-only root definition whose create() returns 0 on
  // demand — standing in faithfully for the gate-routed root create() that
  // #140 will make return 0. isBoundary() stays true; clone() preserves the
  // refusal so the Scene's owned clone refuses too.
  bool g_refuseRootCreate = false;

  class RefusableRootNode;
  typedef loka::app::scene::BoundaryPropsFor<RefusableRootNode> RefusableRootProps;

  class RefusableRootNode : public loka::app::scene::BoundaryNodeFor<RefusableRootNode>
  {
  public:
    explicit RefusableRootNode(const RefusableRootProps &props)
        : loka::app::scene::BoundaryNodeFor<RefusableRootNode>(props)
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      composition.declare(loka::app::FragmentDefinition());
    }
  };

  struct RefusableRootDefinition
      : public loka::app::scene::BoundaryDefinition<RefusableRootProps, RefusableRootNode>
  {
    typedef loka::app::scene::BoundaryDefinition<RefusableRootProps, RefusableRootNode> BaseType;
    RefusableRootDefinition() : BaseType() {}
    RefusableRootDefinition(const RefusableRootDefinition &other) : BaseType(other) {}
    virtual loka::app::scene::Node *create() const
    {
      if (g_refuseRootCreate)
      {
        return 0;
      }
      return BaseType::create();
    }
    typedef RefusableRootDefinition CloneType;
    virtual loka::app::scene::NodeDefinitionBase *clone() const
    {
      return new RefusableRootDefinition(*this);
    }
  };

  // Root-attach-refusal harness (#132 ruling 3, Codex P2 hole 2). The root
  // create() is refusable (to arm the initial white flag via a refused root),
  // and the attach compose declares a node-local state whose materialization
  // the state-refusing backend below can reject even though root create()
  // succeeds — exercising "root retry gets past create() but the attach
  // compose itself raises the allocation flag".
  bool g_attachRefuseState = false;
  int g_attachStateRefusals = 0;

  void *attachStateRefusingBackendAlloc(std::size_t size,
                                        const loka::core::LokaAllocationSite &site)
  {
    if (g_attachRefuseState && std::strncmp(site.ownerTag, "State", 5) == 0)
    {
      ++g_attachStateRefusals;
      return 0;
    }
    return new (std::nothrow) char[size];
  }

  int *g_attachRefusalHeldReleases = 0;

  void releaseAttachRefusalHeld(int **counter)
  {
    ++**counter;
    delete counter;
  }

  void holdAttachRefusalPayload(loka::app::scene::NodeComposition &composition,
                                loka::core::Held<int *> &held)
  {
    if (g_attachRefusalHeldReleases && !held.isValid())
    {
      // Held shares the StateArena backend; refuse the NodeState allocation,
      // then allow this independent payload so DETACH has a releaser to queue.
      const bool refuseState = g_attachRefuseState;
      g_attachRefuseState = false;
      held = composition.hold(
          new int *(g_attachRefusalHeldReleases), &releaseAttachRefusalHeld);
      g_attachRefuseState = refuseState;
      LOKA_VERIFY(held.isValid());
    }
  }

  class AttachRefusalRootNode;
  typedef loka::app::scene::BoundaryPropsFor<AttachRefusalRootNode> AttachRefusalRootProps;

  class AttachRefusalRootNode : public loka::app::scene::BoundaryNodeFor<AttachRefusalRootNode>
  {
  public:
    explicit AttachRefusalRootNode(const AttachRefusalRootProps &props)
        : loka::app::scene::BoundaryNodeFor<AttachRefusalRootNode>(props),
          state_()
    {
      WhiteFlagStatePayload initial = {{0}};
      this->state(this->state_, initial);
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      holdAttachRefusalPayload(composition, this->held_);
      composition.declare(loka::app::FragmentDefinition());
    }

    loka::app::scene::NodeState<WhiteFlagStatePayload> state_;
    loka::core::Held<int *> held_;
  };

  struct AttachRefusalRootDefinition
      : public loka::app::scene::BoundaryDefinition<AttachRefusalRootProps, AttachRefusalRootNode>
  {
    typedef loka::app::scene::BoundaryDefinition<AttachRefusalRootProps, AttachRefusalRootNode> BaseType;
    AttachRefusalRootDefinition() : BaseType() {}
    AttachRefusalRootDefinition(const AttachRefusalRootDefinition &other) : BaseType(other) {}
    virtual loka::app::scene::Node *create() const
    {
      if (g_refuseRootCreate)
      {
        return 0;
      }
      return BaseType::create();
    }
    typedef AttachRefusalRootDefinition CloneType;
    virtual loka::app::scene::NodeDefinitionBase *clone() const
    {
      return new AttachRefusalRootDefinition(*this);
    }
  };

  struct PlainAttachRefusalRecord
  {
    PlainAttachRefusalRecord()
        : constructions(0), detaches(0), destructions(0), detachesAtReplacement(0), wrapper(0), runtimeEmptyAtReplacement(false) {}
    int constructions;
    int detaches;
    int destructions;
    int detachesAtReplacement;
    loka::app::scene::BoundaryNode *wrapper;
    bool runtimeEmptyAtReplacement;
  };

  class PlainAttachRefusalChild;
  struct PlainAttachRefusalProps : loka::app::scene::NodePropsBase<PlainAttachRefusalProps>
  {
    struct TypeTag {};
    typedef PlainAttachRefusalChild NodeType;
    explicit PlainAttachRefusalProps(PlainAttachRefusalRecord *value = 0) : record(value) {}
    bool operator<(const loka::app::scene::PropsBase &) const { return false; }
    PlainAttachRefusalRecord *record;
  };

  class PlainAttachRefusalChild : public loka::app::scene::ComposableNode
  {
  public:
    typedef PlainAttachRefusalProps::TypeTag TypeTag;
    explicit PlainAttachRefusalChild(const PlainAttachRefusalProps &p) : props(p), state_()
    {
      if (++this->props.record->constructions == 2)
      {
        this->props.record->detachesAtReplacement = this->props.record->detaches;
        this->props.record->runtimeEmptyAtReplacement =
            loka::dsl::testing::OwnershipDump::dumpSeatRuntime(*this->props.record->wrapper).empty();
      }
      WhiteFlagStatePayload initial = {{0}};
      this->state(this->state_, initial);
    }
    virtual ~PlainAttachRefusalChild() { ++this->props.record->destructions; }
    virtual void composeWithContext(loka::app::scene::ComponentContext &context,
                                    loka::app::scene::ComposeEvent event)
    {
      if (event == loka::app::scene::COMPOSE_EVENT_DETACH)
        ++this->props.record->detaches;
      if (event == loka::app::scene::COMPOSE_EVENT_ATTACH)
      {
        loka::app::scene::NodeComposition composition;
        composition.setContext(&context);
        holdAttachRefusalPayload(composition, this->held_);
      }
    }
    PlainAttachRefusalProps props;
    loka::app::scene::NodeState<WhiteFlagStatePayload> state_;
    loka::core::Held<int *> held_;
  };

  struct SectionTrackedValue
  {
    SectionTrackedValue(int *aliveCount, int v)
        : alive(aliveCount),
          value(v)
    {
      if (this->alive)
      {
        ++*this->alive;
      }
    }

    SectionTrackedValue(const SectionTrackedValue &other)
        : alive(other.alive),
          value(other.value)
    {
      if (this->alive)
      {
        ++*this->alive;
      }
    }

    ~SectionTrackedValue()
    {
      if (this->alive)
      {
        --*this->alive;
      }
    }

    bool operator!=(const SectionTrackedValue &other) const
    {
      return this->alive != other.alive || this->value != other.value;
    }

    int *alive;
    int value;
  };

  struct SectionOrderingChildTypeTag
  {
  };

  class SectionOrderingChildNode;

  struct SectionOrderingChildProps
      : public loka::app::scene::NodePropsBase<SectionOrderingChildProps>
  {
    typedef SectionOrderingChildTypeTag TypeTag;
    typedef SectionOrderingChildNode NodeType;

    SectionOrderingChildProps()
        : revision(0)
    {
    }

    explicit SectionOrderingChildProps(int revisionValue)
        : revision(revisionValue)
    {
    }

    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
      {
        return this->propsTypeId() < rhs.propsTypeId();
      }
      const SectionOrderingChildProps &other =
          static_cast<const SectionOrderingChildProps &>(rhs);
      return this->revision < other.revision;
    }

    int revision;
  };

  int g_sectionOrderingChildDestructions = 0;
  int g_sectionOrderingChildDetaches = 0;

  struct SectionOrderingObservation
  {
    SectionOrderingObservation(loka::app::scene::NodeState<SectionTrackedValue> &out,
                               const SectionTrackedValue &value)
        : state(out), initial(value) {}
    loka::app::scene::NodeState<SectionTrackedValue> &state;
    const SectionTrackedValue &initial;
  };
  SectionOrderingObservation *g_sectionOrderingObservation = 0;

  class SectionOrderingChildNode : public loka::app::scene::ComponentNode
  {
  public:
    typedef SectionOrderingChildTypeTag TypeTag;
    SectionOrderingChildProps props;

    explicit SectionOrderingChildNode(const SectionOrderingChildProps &p)
        : loka::app::scene::ComponentNode(),
          props(p),
          observed_(0)
    {
    }

    virtual ~SectionOrderingChildNode()
    {
      if (this->observed_)
      {
        assert(this->observed_->get().value == 41 &&
               "Section child observations must detach before owner storage");
      }
      ++g_sectionOrderingChildDestructions;
    }

  protected:
    virtual void declareBindings(loka::app::scene::BindingToken &t)
    {
      if (g_sectionOrderingObservation && !this->observed_)
      {
        // Keep the test's adopted state in the Section owner, but create it
        // before declaring the observation instead of binding after attach.
        SectionOrderingObservation &observation = *g_sectionOrderingObservation;
        {
          loka::app::scene::NodeComposition::StateBatch states(
              this->componentContext()->stateOwner());
          states.state(observation.state, observation.initial);
        }
        this->observed_ = observation.state.dangerouslyMutableState();
      }
      if (this->observed_)
      {
        t.watch(*this->observed_, this, &SectionOrderingChildNode::onObserved);
      }
    }

    virtual void composeChildren(loka::app::scene::NodeComposition &composition)
    {
      (void)composition;
    }

    virtual void detachNode(loka::app::scene::NodeComposition &composition)
    {
      ++g_sectionOrderingChildDetaches;
      loka::app::scene::ComponentNode::detachNode(composition);
    }

  private:
    void onObserved()
    {
    }

    loka::core::MutableState<SectionTrackedValue> *observed_;
  };

  typedef loka::app::scene::NodeDefinition<SectionOrderingChildProps,
                                           SectionOrderingChildNode>
      SectionOrderingChildDefinition;

  loka::app::BoundarySectionNode *findSectionByKey(loka::app::scene::Node *node, loka::app::scene::NodeTag key);

  class KeyedSectionRootNode;
  typedef loka::app::scene::BoundaryPropsFor<KeyedSectionRootNode> KeyedSectionRootProps;
  class KeyedSectionRootNode : public loka::app::scene::BoundaryNodeFor<KeyedSectionRootNode>
  {
  public:
    explicit KeyedSectionRootNode(const KeyedSectionRootProps &props)
        : loka::app::scene::BoundaryNodeFor<KeyedSectionRootNode>(props),
          key_()
    {
      this->state(this->key_, loka::app::scene::NodeTag(1101));
    }
    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }
    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      composition.declare(
          loka::app::Fragment() << loka::app::Keyed(
              *this->key_.state(),
              this,
              &KeyedSectionRootNode::declareSection,
              loka::app::reservation::SeatNodes<loka::app::reservation::Nodes<
                  loka::app::FragmentNode,
                  1,
                  loka::app::reservation::Nodes<
                      loka::app::BoundarySectionNode,
                      1,
                      loka::app::reservation::Nodes<SectionOrderingChildNode, 1, loka::app::reservation::End> > > >()));
    }
    void declareSection(loka::app::scene::NodeComposition &composition)
    {
      composition.declare(loka::app::Fragment()
                          << (loka::app::Section(this->key_.get()) << SectionOrderingChildDefinition()));
    }
    loka::app::BoundarySectionNode *section(loka::app::scene::NodeTag key)
    {
      return findSectionByKey(this, key);
    }
    void replaceFirstSection()
    {
      this->key_.set(1103);
    }

  private:
    loka::app::scene::NodeState<loka::app::scene::NodeTag> key_;
  };

  class SectionLifetimeRootNode;
  typedef loka::app::scene::BoundaryPropsFor<SectionLifetimeRootNode> SectionLifetimeRootProps;
  class SectionLifetimeRootNode : public loka::app::scene::BoundaryNodeFor<SectionLifetimeRootNode>
  {
  public:
    explicit SectionLifetimeRootNode(const SectionLifetimeRootProps &props)
        : loka::app::scene::BoundaryNodeFor<SectionLifetimeRootNode>(props),
          shown_()
    {
      this->state(this->shown_, true);
    }
    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }
    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      loka::app::PolicyScopeDefinition owner;
      owner.destroyOnDetach() << (loka::app::Section(1101) << SectionOrderingChildDefinition());
      composition.declare(loka::app::Fragment() << (loka::app::Show(*this->shown_.state()) << owner));
    }
    void retireSection()
    {
      this->shown_.set(false);
    }

  private:
    loka::app::scene::NodeState<bool> shown_;
  };

  class SectionGridRootNode;
  typedef loka::app::scene::BoundaryPropsFor<SectionGridRootNode>
      SectionGridRootProps;

  class SectionGridRootNode : public loka::app::scene::BoundaryNodeFor<SectionGridRootNode>
  {
  public:
    enum
    {
      kSectionCount = 100
    };

    explicit SectionGridRootNode(const SectionGridRootProps &props)
        : loka::app::scene::BoundaryNodeFor<SectionGridRootNode>(props),
          populatedSections_(0)
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {

      loka::app::Fragment root;
      for (int i = 0; i < kSectionCount; ++i)
      {
        loka::app::Section section(
            static_cast<loka::app::scene::NodeTag>(2000 + i));
        root << section;
      }
      composition.declare(root);
    }

    void populateSections()
    {
      this->populateSectionStates();
    }

    int populatedSections() const
    {
      return this->populatedSections_;
    }

  private:
    void populateSectionStates()
    {
      this->populatedSections_ = 0;
      loka::app::scene::Node *root = this->compositionRootNode();
      loka::app::scene::INestable *nestable = root ? root->asNestable() : 0;
      loka::dsl::CompositionCursor<loka::app::scene::Node> it(
          nestable ? nestable->childrenHead() : 0,
          nestable ? nestable->childrenCount() : 0);
      for (loka::app::scene::Node *node = it.next(); node; node = it.next())
      {
        loka::app::BoundarySectionNode *section = node->asBoundarySectionNode();
        assert(section);
        loka::app::scene::NodeState<int> first;
        loka::app::scene::NodeState<int> second;
        {
          loka::app::scene::NodeComposition::StateBatch states(section);
          states.state(first, this->populatedSections_)
              .state(second, this->populatedSections_ + 1);
        }
        assert(first.isValid() && second.isValid());
        assert(first.dangerouslyMutableState()->isArenaAllocated());
        assert(second.dangerouslyMutableState()->isArenaAllocated());
        ++this->populatedSections_;
      }
    }

    int populatedSections_;
  };

  struct SectionOwnerProbeObservation
  {
    SectionOwnerProbeObservation()
        : state(),
          owner(0),
          tracker(0),
          mutableState(0),
          currentStateValid(false),
          foreignStateValid(false),
          composeCount(0)
    {
    }

    loka::app::scene::NodeState<int> state;
    loka::app::scene::IStateOwner *owner;
    loka::core::StateTracker *tracker;
    loka::core::MutableState<int> *mutableState;
    bool currentStateValid;
    bool foreignStateValid;
    int composeCount;
  };

  struct SectionOwnerProbeTypeTag
  {
  };

  class SectionOwnerProbeNode;

  struct SectionOwnerProbeProps
      : public loka::app::scene::NodePropsBase<SectionOwnerProbeProps>
  {
    typedef SectionOwnerProbeTypeTag TypeTag;
    typedef SectionOwnerProbeNode NodeType;

    SectionOwnerProbeProps()
        : observation(0),
          foreignObservation(0),
          dirtySource(false)
    {
    }

    SectionOwnerProbeProps(SectionOwnerProbeObservation *value,
                           SectionOwnerProbeObservation *foreignValue,
                           bool declaresDirtySource)
        : observation(value),
          foreignObservation(foreignValue),
          dirtySource(declaresDirtySource)
    {
    }

    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
      {
        return this->propsTypeId() < rhs.propsTypeId();
      }
      const SectionOwnerProbeProps &other =
          static_cast<const SectionOwnerProbeProps &>(rhs);
      if (this->observation != other.observation)
      {
        return this->observation < other.observation;
      }
      if (this->foreignObservation != other.foreignObservation)
      {
        return this->foreignObservation < other.foreignObservation;
      }
      return this->dirtySource < other.dirtySource;
    }

    SectionOwnerProbeObservation *observation;
    SectionOwnerProbeObservation *foreignObservation;
    bool dirtySource;
  };

  class SectionOwnerProbeNode : public loka::app::scene::ComposableNode
  {
  public:
    typedef SectionOwnerProbeTypeTag TypeTag;
    SectionOwnerProbeProps props;

    explicit SectionOwnerProbeNode(const SectionOwnerProbeProps &p)
        : loka::app::scene::ComposableNode(),
          props(p)
    {
    }

    virtual void declareDirtySources(loka::app::scene::DirtySourceRegistrar &registrar)
    {
      if (this->props.dirtySource && this->props.observation &&
          this->props.observation->state.isValid())
      {
        registrar.markDirtyOnChange(
            this->props.observation->state.state(),
            loka::app::scene::NODE_DIRTY_PROPS);
      }
    }

  protected:
    virtual void composeWithContext(loka::app::scene::ComponentContext &context,
                                    loka::app::scene::ComposeEvent event)
    {
      if (event == loka::app::scene::COMPOSE_EVENT_DETACH ||
          !this->props.observation)
      {
        return;
      }

      loka::app::scene::NodeComposition composition;
      composition.setContext(&context);
      if (!this->props.observation->state.isValid())
      {
        composition.declareStates().state(this->props.observation->state, 17);
      }
      SectionOwnerProbeObservation &observation = *this->props.observation;
      observation.owner = observation.state.dangerouslyOwner();
      observation.tracker = observation.state.dangerouslyTracker();
      observation.mutableState = observation.state.dangerouslyMutableState();
      observation.currentStateValid =
          composition.currentBoundary().state(observation.state).isValid();
      observation.foreignStateValid =
          this->props.foreignObservation &&
          composition.currentBoundary()
              .state(this->props.foreignObservation->state)
              .isValid();
      ++observation.composeCount;
    }
  };

  typedef loka::app::scene::NodeDefinition<SectionOwnerProbeProps,
                                           SectionOwnerProbeNode>
      SectionOwnerProbeDefinition;

  struct SectionOwnerResolutionScenario
  {
    enum Mode
    {
      MODE_FALLBACK = 0,
      MODE_NEAREST,
      MODE_INNERMOST,
      MODE_INVALIDATION,
      MODE_RETIRE_WHILE_DIRTY_SOURCE,
      MODE_CONDITIONAL,
      MODE_OWNER_MATCH
    };

    explicit SectionOwnerResolutionScenario(Mode value)
        : mode(value),
          primary(),
          secondary(),
          boundary()
    {
    }

    Mode mode;
    SectionOwnerProbeObservation primary;
    SectionOwnerProbeObservation secondary;
    SectionOwnerProbeObservation boundary;
  };

  SectionOwnerResolutionScenario *g_sectionOwnerResolutionScenario = 0;

  class SectionOwnerResolutionRootNode;
  typedef loka::app::scene::BoundaryPropsFor<SectionOwnerResolutionRootNode>
      SectionOwnerResolutionRootProps;

  class SectionOwnerResolutionRootNode : public loka::app::scene::BoundaryNodeFor<SectionOwnerResolutionRootNode>
  {
  public:
    explicit SectionOwnerResolutionRootNode(const SectionOwnerResolutionRootProps &props)
        : loka::app::scene::BoundaryNodeFor<SectionOwnerResolutionRootNode>(props),
          condition_(),
          boundaryPulse_(),
          showSection_(),
          initialized_(false)
    {
      assert(g_sectionOwnerResolutionScenario);
    }

    virtual void attachNode(loka::app::scene::NodeComposition &composition)
    {
      if (this->initialized_)
      {
        return;
      }
      composition.declareStates()
          .state(this->condition_, true)
          .state(this->boundaryPulse_, 0)
          .state(this->showSection_, true);
      this->initialized_ = true;
    }

    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      // Only the retirement scenario needs a replaceable generation. The
      // other scenarios pin Boundary-lifetime owner and arena routing.
      if (g_sectionOwnerResolutionScenario->mode == SectionOwnerResolutionScenario::MODE_RETIRE_WHILE_DIRTY_SOURCE)
        composition.declare(
            loka::app::Fragment() << loka::app::Keyed(
                *this->showSection_.state(),
                this,
                &SectionOwnerResolutionRootNode::declareContent,
                loka::app::reservation::SeatNodes<loka::app::reservation::Nodes<
                    loka::app::FragmentNode,
                    1,
                    loka::app::reservation::Nodes<
                        loka::app::BoundarySectionNode,
                        1,
                        loka::app::reservation::Nodes<SectionOwnerProbeNode, 1, loka::app::reservation::End> > > >()));
      else
        this->declareContent(composition);
    }

    void declareContent(loka::app::scene::NodeComposition &composition)
    {
      assert(g_sectionOwnerResolutionScenario);
      SectionOwnerResolutionScenario &scenario =
          *g_sectionOwnerResolutionScenario;
      loka::app::Fragment root;
      SectionOwnerProbeDefinition primary(
          SectionOwnerProbeProps(&scenario.primary,
                                 scenario.mode == SectionOwnerResolutionScenario::MODE_OWNER_MATCH
                                     ? &scenario.boundary
                                     : 0,
                                 scenario.mode == SectionOwnerResolutionScenario::MODE_INVALIDATION ||
                                     scenario.mode == SectionOwnerResolutionScenario::MODE_RETIRE_WHILE_DIRTY_SOURCE));

      switch (scenario.mode)
      {
      case SectionOwnerResolutionScenario::MODE_FALLBACK:
        root << primary;
        break;
      case SectionOwnerResolutionScenario::MODE_NEAREST:
      case SectionOwnerResolutionScenario::MODE_INVALIDATION:
        {
          loka::app::Section section(4101);
          section << primary;
          root << section;
        }
        break;
      case SectionOwnerResolutionScenario::MODE_INNERMOST:
        {
          loka::app::Section inner(4102);
          inner << primary;
          loka::app::Section outer(4101);
          outer << inner;
          root << outer;
        }
        break;
      case SectionOwnerResolutionScenario::MODE_RETIRE_WHILE_DIRTY_SOURCE:
        if (this->showSection_.get())
        {
          loka::app::Section section(4101);
          section << primary;
          root << section;
        }
        break;
      case SectionOwnerResolutionScenario::MODE_CONDITIONAL:
        {
          SectionOwnerProbeDefinition secondary(
              SectionOwnerProbeProps(&scenario.secondary, 0, false));
          loka::app::scene::ConditionalDefinition conditional(
              loka::app::scene::ConditionalProps(
                  this->condition_.state(), &primary, &secondary));
          loka::app::Section section(4101);
          section << conditional;
          root << section;
        }
        break;
      case SectionOwnerResolutionScenario::MODE_OWNER_MATCH:
        {
          loka::app::Section section(4101);
          section << primary;
          root << section;
          root << SectionOwnerProbeDefinition(
              SectionOwnerProbeProps(&scenario.boundary,
                                     &scenario.primary,
                                     false))
                      .tag(4191);
        }
        break;
      }
      composition.declare(root);
    }

    void flipCondition()
    {
      this->condition_.set(false);
    }

    void retireSection()
    {
      this->showSection_.set(false);
    }

    void mutateBoundaryPulse()
    {
      this->boundaryPulse_.set(this->boundaryPulse_.get() + 1);
    }

    virtual void declareDirtySources(loka::app::scene::DirtySourceRegistrar &registrar)
    {
      if (this->boundaryPulse_.isValid())
        registrar.markDirtyOnChange(this->boundaryPulse_.state(), loka::app::scene::NODE_DIRTY_PROPS);
    }

  private:
    loka::app::scene::NodeState<bool> condition_;
    loka::app::scene::NodeState<int> boundaryPulse_;
    loka::app::scene::NodeState<bool> showSection_;
    bool initialized_;
  };

  loka::app::BoundarySectionNode *findSectionByKey(
      loka::app::scene::Node *node,
      loka::app::scene::NodeTag key)
  {
    if (!node)
    {
      return 0;
    }
    loka::app::BoundarySectionNode *section = node->asBoundarySectionNode();
    if (section && section->props.key() == key)
    {
      return section;
    }
    loka::app::scene::INestable *nestable = node->asNestable();
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(
        nestable ? nestable->childrenHead() : 0,
        nestable ? nestable->childrenCount() : 0);
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      section = findSectionByKey(child, key);
      if (section)
      {
        return section;
      }
    }
    return 0;
  }

  int g_sectionStateArenaBlockAllocs = 0;
  int g_sectionStateArenaSlabAllocs = 0;
  int g_sectionHeapStateAllocs = 0;
  int g_sectionStateArenaBlockFrees = 0;
  int g_sectionStateArenaSlabFrees = 0;
  int g_sectionHeapStateFrees = 0;

  void resetSectionAllocationCounts()
  {
    g_sectionStateArenaBlockAllocs = 0;
    g_sectionStateArenaSlabAllocs = 0;
    g_sectionHeapStateAllocs = 0;
    g_sectionStateArenaBlockFrees = 0;
    g_sectionStateArenaSlabFrees = 0;
    g_sectionHeapStateFrees = 0;
  }

  void *sectionCountingBackendAlloc(std::size_t size,
                                    const loka::core::LokaAllocationSite &site)
  {
    if (std::strcmp(site.ownerTag, "StateArena") == 0 &&
        std::strcmp(site.typeTag, "Block") == 0)
    {
      ++g_sectionStateArenaBlockAllocs;
    }
    else if (std::strcmp(site.ownerTag, "StateArena") == 0 &&
             std::strcmp(site.typeTag, "slab") == 0)
    {
      ++g_sectionStateArenaSlabAllocs;
    }
    else if (std::strcmp(site.ownerTag, "StateOwner") == 0)
    {
      ++g_sectionHeapStateAllocs;
    }
    return new (std::nothrow) char[size];
  }

  void sectionCountingBackendFree(void *ptr,
                                  const loka::core::LokaAllocationSite &site)
  {
    if (std::strcmp(site.ownerTag, "StateArena") == 0 &&
        std::strcmp(site.typeTag, "Block") == 0)
    {
      ++g_sectionStateArenaBlockFrees;
    }
    else if (std::strcmp(site.ownerTag, "StateArena") == 0 &&
             std::strcmp(site.typeTag, "slab") == 0)
    {
      ++g_sectionStateArenaSlabFrees;
    }
    else if (std::strcmp(site.ownerTag, "StateOwner") == 0)
    {
      ++g_sectionHeapStateFrees;
    }
    delete[] static_cast<char *>(ptr);
  }

  int g_sectionFailureHeapPosition = 0;
  int g_sectionFailureHeapCalls = 0;

  void *sectionFailureBackendAlloc(std::size_t size,
                                   const loka::core::LokaAllocationSite &site)
  {
    if (std::strcmp(site.ownerTag, "StateArena") == 0)
    {
      return 0;
    }
    if (std::strcmp(site.ownerTag, "StateOwner") == 0)
    {
      ++g_sectionFailureHeapCalls;
      if (g_sectionFailureHeapCalls == g_sectionFailureHeapPosition)
      {
        return 0;
      }
    }
    return new (std::nothrow) char[size];
  }

  class SectionFailureRootNode;
  typedef loka::app::scene::BoundaryPropsFor<SectionFailureRootNode>
      SectionFailureRootProps;

  class SectionFailureRootNode : public loka::app::scene::BoundaryNodeFor<SectionFailureRootNode>
  {
  public:
    explicit SectionFailureRootNode(const SectionFailureRootProps &props)
        : loka::app::scene::BoundaryNodeFor<SectionFailureRootNode>(props),
          injectStates_()
    {
      this->state(this->injectStates_, false);
    }

    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }
    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      composition.declare(
          loka::app::Fragment()
          << (loka::app::Fragment() << loka::app::Section(3101) << loka::app::Section(3102))
          << loka::app::Keyed(
                 *this->injectStates_.state(),
                 this,
                 &SectionFailureRootNode::declareAttempt,
                 loka::app::reservation::SeatNodes<
                     loka::app::reservation::Nodes<loka::app::FragmentNode, 1, loka::app::reservation::End> >()));
    }
    void declareAttempt(loka::app::scene::NodeComposition &composition)
    {
      if (this->injectStates_.get())
      {
        loka::app::BoundarySectionNode *section = this->liveSection();
        assert(section);
        loka::core::LokaAllocSetBackend(&sectionFailureBackendAlloc, &delegatingBackendFree);
        WhiteFlagStatePayload initial = {{0}};
        loka::app::scene::NodeState<WhiteFlagStatePayload> first;
        loka::app::scene::NodeState<WhiteFlagStatePayload> second;
        {
          loka::app::scene::NodeComposition::StateBatch states(section);
          states.state(first, initial).state(second, initial);
        }
        loka::core::LokaAllocSetBackend(0, 0);
        // A refused Section allocation must not publish a candidate declaration.
        if (!first.isValid() || !second.isValid())
          return;
      }

      composition.declare(loka::app::Fragment());
    }

    void injectStatesOnNextCompose()
    {
      this->injectStates_.set(true);
    }

  private:
    loka::app::BoundarySectionNode *liveSection()
    {
      return findSectionByKey(this, 3101);
    }

    loka::app::scene::NodeState<bool> injectStates_;
  };

  class DuplicateSectionRootNode;
  typedef loka::app::scene::BoundaryPropsFor<DuplicateSectionRootNode>
      DuplicateSectionRootProps;

  class DuplicateSectionRootNode
      : public loka::app::scene::BoundaryNodeFor<DuplicateSectionRootNode>
  {
  public:
    explicit DuplicateSectionRootNode(const DuplicateSectionRootProps &props)
        : loka::app::scene::BoundaryNodeFor<DuplicateSectionRootNode>(props)
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      loka::app::Fragment root;
      loka::app::Section first(4101);
      loka::app::Section duplicate(4101);
      root << first;
      root << duplicate;
      composition.declare(root);
    }
  };

  struct HeldOwnerSlotScenario;

  struct HeldOwnerSlotPayload
  {
    explicit HeldOwnerSlotPayload(HeldOwnerSlotScenario *owner)
        : scenario(owner)
    {
    }

    HeldOwnerSlotScenario *scenario;
  };

  struct HeldOwnerSlotScenario
  {
    enum Mode
    {
      MODE_BASIC = 0,
      MODE_FIVE_OWNERS,
      MODE_CROSS_BRANCH
    };

    explicit HeldOwnerSlotScenario(Mode value = MODE_BASIC)
        : mode(value),
          held(),
          refused(),
          creatorOwner(0),
          descendantOwner(0),
          showCreator(true),
          showDescendant(false),
          repeatCreatorHold(false),
          releaseCount(0),
          order(0),
          detachCallbackReturnOrder(0),
          releaseOrder(0),
          releaseSawCallbackInFlight(false),
          callbackInFlight(false),
          refusalWasFailureAtomic(false)
    {
    }

    Mode mode;
    loka::core::Held<HeldOwnerSlotPayload> held;
    loka::core::Held<HeldOwnerSlotPayload> refused;
    loka::app::scene::IStateOwner *creatorOwner;
    loka::app::scene::IStateOwner *descendantOwner;
    loka::core::MutableState<bool> showCreator;
    loka::core::MutableState<bool> showDescendant;
    bool repeatCreatorHold;
    int releaseCount;
    int order;
    int detachCallbackReturnOrder;
    int releaseOrder;
    bool releaseSawCallbackInFlight;
    bool callbackInFlight;
    bool refusalWasFailureAtomic;
  };

  HeldOwnerSlotScenario *g_heldOwnerSlotScenario = 0;

  void releaseHeldOwnerSlotPayload(HeldOwnerSlotPayload *payload)
  {
    assert(payload && payload->scenario);
    HeldOwnerSlotScenario *scenario = payload->scenario;
    ++scenario->releaseCount;
    scenario->releaseSawCallbackInFlight = scenario->callbackInFlight;
    scenario->releaseOrder = ++scenario->order;
    delete payload;
  }

  struct HeldOwnerSlotProbeTypeTag
  {
  };

  class HeldOwnerSlotProbeNode;

  struct HeldOwnerSlotProbeProps
      : public loka::app::scene::NodePropsBase<HeldOwnerSlotProbeProps>
  {
    typedef HeldOwnerSlotProbeTypeTag TypeTag;
    typedef HeldOwnerSlotProbeNode NodeType;

    enum Role
    {
      ROLE_CREATE = 0,
      ROLE_HOLD,
      ROLE_TRY_HOLD
    };

    explicit HeldOwnerSlotProbeProps(Role value = ROLE_HOLD)
        : role(value)
    {
    }

    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
      {
        return false;
      }
      const HeldOwnerSlotProbeProps &other =
          static_cast<const HeldOwnerSlotProbeProps &>(rhs);
      return this->role < other.role;
    }

    Role role;
  };

  class HeldOwnerSlotProbeNode : public loka::app::scene::ComposableNode
  {
  public:
    typedef HeldOwnerSlotProbeTypeTag TypeTag;
    HeldOwnerSlotProbeProps props;

    explicit HeldOwnerSlotProbeNode(const HeldOwnerSlotProbeProps &value)
        : loka::app::scene::ComposableNode(),
          props(value)
    {
    }

    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<HeldOwnerSlotProbeNode>();
    }

  protected:
    virtual void composeWithContext(loka::app::scene::ComponentContext &context,
                                    loka::app::scene::ComposeEvent event)
    {
      assert(g_heldOwnerSlotScenario);
      HeldOwnerSlotScenario &scenario = *g_heldOwnerSlotScenario;
      if (event == loka::app::scene::COMPOSE_EVENT_DETACH)
      {
        this->detachCallback(scenario);
        return;
      }
      if (event != loka::app::scene::COMPOSE_EVENT_ATTACH)
      {
        return;
      }

      loka::app::scene::NodeComposition composition;
      composition.setContext(&context);
      switch (this->props.role)
      {
      case HeldOwnerSlotProbeProps::ROLE_CREATE:
        if (!scenario.held.isValid())
        {
          scenario.held = composition.hold(
              new HeldOwnerSlotPayload(&scenario),
              &releaseHeldOwnerSlotPayload);
          scenario.creatorOwner = context.stateOwner();
          if (scenario.repeatCreatorHold)
          {
            LOKA_VERIFY(composition.hold(scenario.held).isValid());
          }
        }
        break;
      case HeldOwnerSlotProbeProps::ROLE_HOLD:
        LOKA_VERIFY(composition.hold(scenario.held).isValid());
        scenario.descendantOwner = context.stateOwner();
        break;
      case HeldOwnerSlotProbeProps::ROLE_TRY_HOLD:
        {
          const unsigned slotsBefore =
              loka::core::testing::HeldTestAccess::slotCount(scenario.held);
          scenario.refused = composition.tryHold(scenario.held);
          scenario.refusalWasFailureAtomic =
              !scenario.refused.isValid() &&
              loka::core::testing::HeldTestAccess::slotCount(scenario.held) ==
                  slotsBefore;
        }
        break;
      }
    }

  private:
    void detachCallback(HeldOwnerSlotScenario &scenario)
    {
      if (this->props.role == HeldOwnerSlotProbeProps::ROLE_CREATE)
      {
        scenario.detachCallbackReturnOrder = ++scenario.order;
      }
    }
  };

  typedef loka::app::scene::NodeDefinition<HeldOwnerSlotProbeProps,
                                           HeldOwnerSlotProbeNode>
      HeldOwnerSlotProbeDefinition;

  class HeldOwnerSlotRootNode;
  typedef loka::app::scene::BoundaryPropsFor<HeldOwnerSlotRootNode>
      HeldOwnerSlotRootProps;

  class HeldOwnerSlotRootNode : public loka::app::scene::BoundaryNodeFor<HeldOwnerSlotRootNode>
  {
  public:
    explicit HeldOwnerSlotRootNode(const HeldOwnerSlotRootProps &props)
        : loka::app::scene::BoundaryNodeFor<HeldOwnerSlotRootNode>(props)
    {
      assert(g_heldOwnerSlotScenario);
    }

    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      assert(g_heldOwnerSlotScenario);
      HeldOwnerSlotScenario &scenario = *g_heldOwnerSlotScenario;
      loka::app::Fragment root;
      switch (scenario.mode)
      {
      case HeldOwnerSlotScenario::MODE_BASIC:
        root << loka::app::Keyed(
            scenario.showCreator,
            this,
            &HeldOwnerSlotRootNode::declareCreator,
            loka::app::reservation::SeatNodes<loka::app::reservation::Nodes<
                loka::app::FragmentNode,
                1,
                loka::app::reservation::Nodes<
                    loka::app::BoundarySectionNode,
                    1,
                    loka::app::reservation::Nodes<HeldOwnerSlotProbeNode, 1, loka::app::reservation::End> > > >());
        break;
      case HeldOwnerSlotScenario::MODE_FIVE_OWNERS:
        {
          loka::app::Section fifth(5205);
          fifth << HeldOwnerSlotProbeDefinition(
              HeldOwnerSlotProbeProps(HeldOwnerSlotProbeProps::ROLE_TRY_HOLD));
          loka::app::Section fourth(5204);
          fourth << HeldOwnerSlotProbeDefinition(
              HeldOwnerSlotProbeProps(HeldOwnerSlotProbeProps::ROLE_HOLD))
                        .tag(5291);
          fourth << fifth;
          loka::app::Section third(5203);
          third << HeldOwnerSlotProbeDefinition(
              HeldOwnerSlotProbeProps(HeldOwnerSlotProbeProps::ROLE_HOLD))
                       .tag(5291);
          third << fourth;
          loka::app::Section second(5202);
          second << HeldOwnerSlotProbeDefinition(
              HeldOwnerSlotProbeProps(HeldOwnerSlotProbeProps::ROLE_HOLD))
                        .tag(5291);
          second << third;
          loka::app::Section creator(5201);
          creator << HeldOwnerSlotProbeDefinition(
              HeldOwnerSlotProbeProps(HeldOwnerSlotProbeProps::ROLE_CREATE))
                         .tag(5291);
          creator << second;
          root << creator;
        }
        break;
      case HeldOwnerSlotScenario::MODE_CROSS_BRANCH:
        {
          loka::app::Section creator(5301);
          creator << HeldOwnerSlotProbeDefinition(
              HeldOwnerSlotProbeProps(HeldOwnerSlotProbeProps::ROLE_CREATE));
          loka::app::Section sibling(5302);
          sibling << HeldOwnerSlotProbeDefinition(
              HeldOwnerSlotProbeProps(HeldOwnerSlotProbeProps::ROLE_TRY_HOLD));
          root << creator;
          root << sibling;
        }
        break;
      }
      composition.declare(root);
    }
    void declareCreator(loka::app::scene::NodeComposition &composition)
    {
      HeldOwnerSlotScenario &scenario = *g_heldOwnerSlotScenario;
      loka::app::Fragment root;
      if (scenario.showCreator.get())
      {
        root
            << (loka::app::Section(5101)
                << HeldOwnerSlotProbeDefinition(HeldOwnerSlotProbeProps(HeldOwnerSlotProbeProps::ROLE_CREATE)).tag(5191)
                << loka::app::Keyed(scenario.showDescendant,
                                    this,
                                    &HeldOwnerSlotRootNode::declareDescendant,
                                    loka::app::reservation::SeatNodes<loka::app::reservation::Nodes<
                                        loka::app::FragmentNode,
                                        1,
                                        loka::app::reservation::Nodes<
                                            loka::app::BoundarySectionNode,
                                            1,
                                            loka::app::reservation::
                                                Nodes<HeldOwnerSlotProbeNode, 1, loka::app::reservation::End> > > >()));
      }
      composition.declare(root);
    }

    void declareDescendant(loka::app::scene::NodeComposition &composition)
    {
      loka::app::Fragment root;
      if (g_heldOwnerSlotScenario->showDescendant.get())
      {
        root << (loka::app::Section(5102)
                 << HeldOwnerSlotProbeDefinition(HeldOwnerSlotProbeProps(HeldOwnerSlotProbeProps::ROLE_HOLD)));
      }
      composition.declare(root);
    }
  };

  void runHeldInFlightDetachCallback(
      loka::app::scene::BoundaryNode &owner,
      HeldOwnerSlotScenario &scenario)
  {
    scenario.callbackInFlight = true;
    owner.detachHeldResources();
    assert(scenario.releaseCount == 0 &&
           "the drop site must not run a passive payload releaser inline");
    scenario.callbackInFlight = false;
  }

} // namespace

void testBoundaryArenaContracts()
{
  (void)&isPowerOfTwo;
  (void)&isAligned;
  printf("\n==== [testBoundaryArenaContracts] start ====\n");
  testNodeArenaAlignmentAndCapacity();
  testNodeArenaRegistrationStampsOwner();
  testNodeArenaDestroysChildrenBeforeParents();
  testNodeArenaReleaseTombstonePreventsClearDoubleDestruction();
  testNodeArenaClearDestroysHeapChildOfArenaParentExactlyOnce();
  testStateArenaAllocation();
  testStateArenaReleaseAndClear();
  testStateArenaReserveMatchesBatchEstimate();
  testStateArenaGrowsAcrossOwnerBatches();
  testUnreservedStateFallsBackAfterReservedCapacityIsConsumed();
  printf("==== [testBoundaryArenaContracts] ok ====\n");
}

void testNodeArenaRetiredGenerationContracts()
{
  printf("\n==== [testNodeArenaRetiredGenerationContracts] start ====\n");
  testNodeArenaDetachAndDestroyRetiredGeneration();
  printf("==== [testNodeArenaRetiredGenerationContracts] ok ====\n");
}

void testStateArenaSlabsCrossAllocationGate()
{
  const loka::core::LokaAllocationSite blockSite("StateArena", "Block");
  const loka::core::LokaAllocationSite slabSite("StateArena", "slab");
#ifdef LOKA_LIFECYCLE_AUDIT
  const int blockLiveBefore = loka::core::LokaAllocAuditLiveCount(blockSite);
  const int slabLiveBefore = loka::core::LokaAllocAuditLiveCount(slabSite);
  const int totalLiveBefore = loka::core::LokaAllocAuditTotalLiveCount();
#endif
  g_gateBackendAllocCalls = 0;
  g_gateBackendFreeCalls = 0;
  loka::core::LokaAllocSetBackend(&countingGateBackendAlloc, &countingGateBackendFree);
  {
    TestStateOwner owner;
    loka::app::scene::NodeState<int> local;
    {
      loka::app::scene::NodeComposition::StateBatch batch(&owner);
      batch.state(local, 42);
    }
    assert(local.isValid());
    assert(local.dangerouslyMutableState()->isArenaAllocated());
    assert(local.get() == 42);
    // Red before #132 S2: slab acquisition bypassed the gate, so the backend
    // saw zero traffic. One Block header plus one slab cross the gate now.
    assert(g_gateBackendAllocCalls == 2);
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(loka::core::LokaAllocAuditLiveCount(blockSite) == blockLiveBefore + 1);
    assert(loka::core::LokaAllocAuditLiveCount(slabSite) == slabLiveBefore + 1);
#endif
  }
  assert(g_gateBackendFreeCalls == g_gateBackendAllocCalls);
  loka::core::LokaAllocSetBackend(0, 0);
#ifdef LOKA_LIFECYCLE_AUDIT
  // Full teardown balanced the ledger: the leak detector covers arena slabs.
  assert(loka::core::LokaAllocAuditLiveCount(blockSite) == blockLiveBefore);
  assert(loka::core::LokaAllocAuditLiveCount(slabSite) == slabLiveBefore);
  assert(loka::core::LokaAllocAuditTotalLiveCount() == totalLiveBefore);
#endif
}

void testNodeArenaSlabCrossesAllocationGate()
{
  const loka::core::LokaAllocationSite slabSite("NodeArena", "slab");
#ifdef LOKA_LIFECYCLE_AUDIT
  const int slabLiveBefore = loka::core::LokaAllocAuditLiveCount(slabSite);
  const int totalLiveBefore = loka::core::LokaAllocAuditTotalLiveCount();
#endif
  g_gateBackendAllocCalls = 0;
  g_gateBackendFreeCalls = 0;
  loka::core::LokaAllocSetBackend(&countingGateBackendAlloc, &countingGateBackendFree);
  {
    loka::app::scene::NodeArena arena;
    arena.reserve(64);
    assert(arena.hasCapacity());
    // Red before #132 S2: the node slab was a plain new[] that bypassed the
    // gate, so the backend saw zero traffic.
    assert(g_gateBackendAllocCalls == 1);
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(loka::core::LokaAllocAuditLiveCount(slabSite) == slabLiveBefore + 1);
#endif
    arena.clear();
    assert(!arena.hasCapacity());
    assert(g_gateBackendFreeCalls == 1);
  }
  loka::core::LokaAllocSetBackend(0, 0);
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(loka::core::LokaAllocAuditLiveCount(slabSite) == slabLiveBefore);
  assert(loka::core::LokaAllocAuditTotalLiveCount() == totalLiveBefore);
#endif
}

void testHeapNodeCrossesAllocationGate()
{
  const loka::core::LokaAllocationSite nodeSite("NodeDefinition", "Node");
#ifdef LOKA_LIFECYCLE_AUDIT
  const int nodeLiveBefore = loka::core::LokaAllocAuditLiveCount(nodeSite);
  const int totalLiveBefore = loka::core::LokaAllocAuditTotalLiveCount();
#endif
  g_gateBackendAllocCalls = 0;
  g_gateBackendFreeCalls = 0;
  loka::core::LokaAllocSetBackend(&countingGateBackendAlloc, &countingGateBackendFree);
  {
    GateProbeDefinition definition;
    loka::app::scene::Node *node = definition.create();
    assert(node != 0);
    // Red before #132 S2b: create() was a plain `new NodeT` that bypassed the
    // gate, so the backend saw zero traffic and the node carried no gate
    // provenance. One heap node crosses the gate now.
    assert(node->isGateAllocated());
    assert(!node->isArenaAllocated());
    assert(g_gateBackendAllocCalls == 1);
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(loka::core::LokaAllocAuditLiveCount(nodeSite) == nodeLiveBefore + 1);
#endif
    // The heap node returns to the backend through the same gate site.
    loka::app::scene::DestroyHeapNode(node);
    assert(g_gateBackendFreeCalls == 1);
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(loka::core::LokaAllocAuditLiveCount(nodeSite) == nodeLiveBefore);
#endif
  }
  assert(g_gateBackendFreeCalls == g_gateBackendAllocCalls);
  loka::core::LokaAllocSetBackend(0, 0);
#ifdef LOKA_LIFECYCLE_AUDIT
  // Full teardown balanced the whole ledger, not only the heap-node site.
  assert(loka::core::LokaAllocAuditTotalLiveCount() == totalLiveBefore);
#endif
}

#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
namespace
{
  enum GateDestructionDoor
  {
    GATE_DOOR_PLAIN_DELETE,
    GATE_DOOR_EXPLICIT_DESTRUCTOR
  };

  /** Forks and requires the child to abort. The child installs the counting
      backend so the node's storage really comes from LokaAllocRaw, then ends
      the node's lifetime through a door that is not DestroyHeapNode. Both
      unauthorized forms must be walled before storage can be returned. */
  void requireAbortOnGateDestruction(GateDestructionDoor door)
  {
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0)
    {
      loka::core::LokaAllocSetBackend(&countingGateBackendAlloc, &countingGateBackendFree);
      GateProbeDefinition definition;
      loka::app::scene::Node *node = definition.create();
      assert(node != 0);
      assert(node->isGateAllocated());
      if (door == GATE_DOOR_PLAIN_DELETE)
      {
        delete node;
      }
      else
      {
        node->~Node();
      }
      _exit(0);
    }

    int status = 0;
    LOKA_VERIFY(waitpid(child, &status, 0) == child);
    LOKA_VERIFY(WIFSIGNALED(status));
    LOKA_VERIFY(WTERMSIG(status) == SIGABRT);
  }
  enum LandlordDestructionDoor
  {
    LANDLORD_DOOR_PLAIN_DELETE,
    LANDLORD_DOOR_EXPLICIT_DESTRUCTOR,
    LANDLORD_DOOR_HEAP_DESTROY
  };

  struct PartitionAbortBuild : loka::app::scene::detail::NodeBuildOperation
  {
    PartitionAbortBuild() : node(0) {}
    loka::app::scene::Node *construct(void *storage)
    { return new (storage) loka::app::scene::Node(); }
    virtual bool buildAndAttach(loka::app::scene::detail::NodeBuildTicket &ticket)
    {
      this->node = ticket.create<loka::app::scene::Node>(*this);
      return this->node != 0;
    }
    loka::app::scene::Node *node;
  };

  /** If this destructor runs, the heap door failed to reject foreign storage
      before beginning destruction. Exit normally so the parent rejects it. */
  struct HeapDoorAbortProbe : loka::app::scene::Node
  {
    virtual ~HeapDoorAbortProbe() { _exit(0); }
  };

  void requireAbortOnLandlordDestruction(bool partition, LandlordDestructionDoor door)
  {
    using namespace loka::app::scene;
    const pid_t child = fork();
    LOKA_VERIFY(child >= 0);
    if (child == 0)
    {
      NodeArena arena;
      detail::NodePartition bank;
      PartitionAbortBuild build;
      Node *node = 0;
      if (partition)
      {
        const detail::NodeSlotLayout layout = detail::NodeSlotLayout::of<Node>(1);
        LOKA_VERIFY(bank.boot(&layout, 1));
        LOKA_VERIFY(bank.build(&layout, 1, build));
        node = build.node;
        LOKA_VERIFY(node->partitionOwner() == &bank);
      }
      else
      {
        arena.reserve(sizeof(HeapDoorAbortProbe) + detail::AlignOf<HeapDoorAbortProbe>::value);
        void *storage = arena.allocate(sizeof(HeapDoorAbortProbe), detail::AlignOf<HeapDoorAbortProbe>::value);
        LOKA_VERIFY(storage != 0);
        node = door == LANDLORD_DOOR_HEAP_DESTROY
                   ? new (storage) HeapDoorAbortProbe() : new (storage) Node();
        arena.registerNode(node);
        LOKA_VERIFY(node->arenaOwner() == &arena);
      }
      switch (door)
      {
      case LANDLORD_DOOR_PLAIN_DELETE:
        delete node;
        break;
      case LANDLORD_DOOR_EXPLICIT_DESTRUCTOR:
        node->~Node();
        break;
      case LANDLORD_DOOR_HEAP_DESTROY:
        DestroyHeapNode(node);
        break;
      }
      _exit(0);
    }
    int status = 0;
    LOKA_VERIFY(waitpid(child, &status, 0) == child);
    LOKA_VERIFY(WIFSIGNALED(status));
    LOKA_VERIFY(WTERMSIG(status) == SIGABRT);
  }
} // namespace
#endif

void testGateAllocatedNodeRejectsUnauthorizedDestruction()
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
  requireAbortOnGateDestruction(GATE_DOOR_PLAIN_DELETE);
  requireAbortOnGateDestruction(GATE_DOOR_EXPLICIT_DESTRUCTOR);
#endif
}

void testLandlordNodesRejectUnauthorizedDestruction()
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
  requireAbortOnLandlordDestruction(false, LANDLORD_DOOR_PLAIN_DELETE);
  requireAbortOnLandlordDestruction(false, LANDLORD_DOOR_EXPLICIT_DESTRUCTOR);
  requireAbortOnLandlordDestruction(true, LANDLORD_DOOR_PLAIN_DELETE);
  requireAbortOnLandlordDestruction(true, LANDLORD_DOOR_EXPLICIT_DESTRUCTOR);
  requireAbortOnLandlordDestruction(false, LANDLORD_DOOR_HEAP_DESTROY);
#else
  std::puts("[skip] landlord destruction abort pins require Linux without ASan or NDEBUG");
#endif
}

namespace
{
  struct RefusalCountingNode : loka::app::scene::Node
  {
    explicit RefusalCountingNode(int &deaths) : deaths(deaths) {}
    virtual ~RefusalCountingNode() { ++this->deaths; }
    int &deaths;
  };
  struct RefusalCountingBuild : loka::app::scene::detail::NodeBuildOperation
  {
    RefusalCountingBuild() : deaths(0), node(0) {}
    RefusalCountingNode *construct(void *storage) { return new (storage) RefusalCountingNode(this->deaths); }
    virtual bool buildAndAttach(loka::app::scene::detail::NodeBuildTicket &ticket)
    { this->node = ticket.create<RefusalCountingNode>(*this); return this->node != 0; }
    int deaths;
    RefusalCountingNode *node;
  };
}

/** #1272: DestroyHeapNode refuses landlord storage in every build. Without
    NDEBUG the refusal asserts (testLandlordNodesRejectUnauthorizedDestruction);
    with it the node is neither destroyed nor freed, and its landlord still
    ends it exactly once through its own door. */
void testHeapDoorRefusesLandlordNodesInRelease()
{
#if defined(NDEBUG)
  using namespace loka::app::scene;
  int arenaDeaths = 0;
  {
    NodeArena arena;
    arena.reserve(sizeof(RefusalCountingNode) + detail::AlignOf<RefusalCountingNode>::value);
    void *storage = arena.allocate(sizeof(RefusalCountingNode), detail::AlignOf<RefusalCountingNode>::value);
    LOKA_VERIFY(storage != 0);
    RefusalCountingNode *node = new (storage) RefusalCountingNode(arenaDeaths);
    arena.registerNode(node);
    DestroyHeapNode(node);
    LOKA_VERIFY(arenaDeaths == 0);
    LOKA_VERIFY(node->arenaOwner() == &arena);
    LOKA_VERIFY(arena.releaseNode(node));
    LOKA_VERIFY(arenaDeaths == 1);
  }
  detail::NodePartition bank;
  const detail::NodeSlotLayout layout = detail::NodeSlotLayout::of<RefusalCountingNode>(1);
  LOKA_VERIFY(bank.boot(&layout, 1));
  RefusalCountingBuild build;
  LOKA_VERIFY(bank.build(&layout, 1, build));
  DestroyHeapNode(build.node);
  LOKA_VERIFY(build.deaths == 0);
  LOKA_VERIFY(build.node->partitionOwner() == &bank);
  LOKA_VERIFY(bank.destroy(build.node, layout));
  LOKA_VERIFY(build.deaths == 1);
#else
  std::puts("[skip] Release heap-door refusal pin requires NDEBUG; debug builds assert on misuse.");
#endif
}

/** #132 S3 red test (b): with the owner arena exhausted and the backend
    refusing the gate-routed heap fallback, state creation must raise the
    owner's white flag so the open compose window converts into a failure —
    instead of completing as a success around a silent dead state. */
void testHeapFallbackWhiteFlagFailsBoundaryCompose()
{
  const loka::core::LokaAllocationSite heapStateSite("StateOwner", "MutableState");
#ifdef LOKA_LIFECYCLE_AUDIT
  const int heapLiveBefore = loka::core::LokaAllocAuditLiveCount(heapStateSite);
  const int totalLiveBefore = loka::core::LokaAllocAuditTotalLiveCount();
#endif
  g_heapStateRefusals = 0;
  loka::core::LokaAllocSetBackend(&heapStateRefusingBackendAlloc, &delegatingBackendFree);
  {
    loka::app::scene::StdCompositionBoundaryNode owner((loka::app::scene::StdCompositionProps()));
    loka::app::scene::NodeState<LargeStateValue> reserved;
    loka::app::scene::NodeState<LargeStateValue> unreserved;
    LargeStateValue initial = {{0}};

    // The arena door works normally under the selective backend.
    owner.reserveStateArena(
        loka::app::scene::StateBatchBase::ArenaBytesForState<LargeStateValue>());
    loka::app::scene::StateBatchBase::CreateStateFromInitial<LargeStateValue>(
        &owner, reserved, initial);
    assert(reserved.isValid());
    assert(reserved.dangerouslyMutableState()->isArenaAllocated());

    // Reserved capacity is consumed; the next creation inside an open compose
    // window must take the heap door, which the backend refuses.
    owner.beginComposeResult(loka::app::scene::COMPOSE_EVENT_UPDATE,
                             loka::app::scene::NODE_DIRTY_PROPS);
    loka::app::scene::StateBatchBase::CreateStateFromInitial<LargeStateValue>(
        &owner, unreserved, initial);
    owner.completeComposeResult();
    assert(g_heapStateRefusals == 1);

    // No half-alive state: the out handle is invalid, nothing was adopted.
    assert(!unreserved.isValid());
    // Red before #132 S3: the compose window completed as a success and the
    // refusal was swallowed. Now the white flag converts it at completion.
    assert(!owner.composeResult().composed);
    assert(owner.composeResult().allocationFailed);
    // The state created before the failure is untouched by the conversion.
    assert(reserved.isValid());
  }
  loka::core::LokaAllocSetBackend(0, 0);
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(loka::core::LokaAllocAuditLiveCount(heapStateSite) == heapLiveBefore);
  assert(loka::core::LokaAllocAuditTotalLiveCount() == totalLiveBefore);
#endif
}

/** #132 ruling 3 / #140 P2 red test: a refused scene-root allocation must not
    crash or leave a permanent blank. The refused mount arms the scene white
    flag and leaves the scene uncomposed without self-scheduling a tick; the
    next externally caused refresh retries root creation and, once the backend
    has healed, builds the root, composes, and projects a full rebuild. Red
    before the Scene.hpp guard+retry: the refused root asserted/null-deref'd in
    ensureRootNode/composeIfNeeded, and (guarded) refreshComposition's
    !composed_ early-return stranded the scene as a permanent blank. */
void testSceneRootAllocationRefusalArmsWhiteFlagAndHealsOnRefresh()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  const int totalLiveBefore = loka::core::LokaAllocAuditTotalLiveCount();
#endif
  {
    SceneTestSupport::RecordingPlatformController platform;
    // Refuse the root allocation before the scene is mounted.
    g_refuseRootCreate = true;
    loka::app::scene::Scene scene((RefusableRootDefinition()));
    assert(!scene.composeRefusedForMemory());
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    // No crash, no assert. The root was never created, the scene stays
    // uncomposed, and the white flag is armed for a later external retry.
    assert(loka::dsl::testing::SceneTestAccess::rootBoundary(scene) == 0);
    assert(!loka::dsl::testing::SceneTestAccess::composed(scene));
    assert(loka::dsl::testing::SceneTestAccess::whiteFlagFullRebuildPending(scene));
    assert(platform.changeCount() == 0);
    assert(scene.composeRefusedForMemory());

    // The failure path did not self-schedule a tick: a driverless flush does
    // nothing and the scene stays uncomposed, waiting for an external drive.
    assert(!scene.hasPendingInvalidation());
    LOKA_VERIFY(!scene.flushInvalidation());
    assert(!loka::dsl::testing::SceneTestAccess::composed(scene));
    assert(platform.changeCount() == 0);
    assert(scene.composeRefusedForMemory());

    // Heal the backend and drive ONE externally caused refresh. The armed
    // white flag rides it: root creation retries, the root builds and
    // composes, and the healed content reaches the platform through the same
    // onChange projection a healthy mount uses (a full rebuild of a root that
    // never had prior platform state — so refreshComposition owes no diff
    // cycle and reports no apply-cycle change).
    g_refuseRootCreate = false;
    scene.requestInvalidate(loka::app::scene::NODE_DIRTY_CHILD);
    scene.flushInvalidation();
    assert(loka::dsl::testing::SceneTestAccess::rootBoundary(scene) != 0);
    assert(loka::dsl::testing::SceneTestAccess::composed(scene));
    assert(!loka::dsl::testing::SceneTestAccess::whiteFlagFullRebuildPending(scene));
    assert(platform.changeCount() > 0);
    assert(!scene.composeRefusedForMemory());
    scene.noteComposeAllocationFailure();
    assert(!scene.composeRefusedForMemory());
    assert(platform.changeAt(platform.changeCount() - 1).fullRebuild);
    // The scene is fully healthy: a further external drive runs a normal
    // update cycle through the now-existing root without re-arming the flag.
    scene.requestInvalidate(loka::app::scene::NODE_DIRTY_CHILD);
    scene.flushInvalidation();
    assert(loka::dsl::testing::SceneTestAccess::composed(scene));
    assert(!loka::dsl::testing::SceneTestAccess::whiteFlagFullRebuildPending(scene));
  }
  g_refuseRootCreate = false;
#ifdef LOKA_LIFECYCLE_AUDIT
  // The whole scene lifetime, including the refused mount, must leave the
  // audit ledger balanced.
  assert(loka::core::LokaAllocAuditTotalLiveCount() == totalLiveBefore);
#endif
}

void testNestedConditionalSeatInContextlessMaterialization_Probe()
{
  loka::core::MutableState<bool> condition(true);
  GateProbeDefinition trueBranch;
  loka::app::FragmentDefinition falseBranch;
  loka::app::scene::ConditionalDefinition conditional(
      (loka::app::scene::ConditionalProps(&condition, &trueBranch, &falseBranch)));
  loka::app::FragmentDefinition inner;
  inner << conditional;
  loka::app::FragmentDefinition definition;
  definition << inner;

  loka::app::scene::NodeComposition composition;
  loka::app::scene::NodeMaterializationResult result =
      loka::app::scene::testing::NodeCompositionTestAccess::
          createNodeFromDefinitionResult(composition, &definition);

  assert(result.root != 0);
  assert(result.requiresBoundaryPlan);
  assert(!result.allocationFailed);
  loka::app::scene::DestroyHeapNode(result.root);
}

/** #132 ruling 3 / Codex P2 (hole 2): after a refused root create() arms the
    scene white flag, an external refresh retries. If root create() now
    succeeds but the attach compose itself refuses a node-local state, the
    compose is again a projection failure. composeIfNeeded must NOT publish a
    partial root or mark the scene composed: it must read the root boundary's
    allocationFailed and stay uncomposed so the re-armed white flag survives
    refreshComposition's `if (composed_) clear` block. Red before the hole-2
    guard: the partial root is published, composed_ is set, and the flag is
    cleared — losing the retry. A later fully-healed external tick composes
    cleanly. */
void testRootAttachAllocationRefusalKeepsWhiteFlagArmedForRetry()
{
  // #1107: refuse the initial attach itself, with root creation succeeding.
  {
    SceneTestSupport::RecordingPlatformController platform;
    g_refuseRootCreate = false;
    g_attachRefuseState = true;
    g_attachStateRefusals = 0;
    loka::app::scene::Scene scene((AttachRefusalRootDefinition()));
    assert(!scene.composeRefusedForMemory());
    loka::core::LokaAllocSetBackend(&attachStateRefusingBackendAlloc, &delegatingBackendFree);
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    loka::core::LokaAllocSetBackend(0, 0);
    g_attachRefuseState = false;
    assert(g_attachStateRefusals > 0);
    assert(scene.composeRefusedForMemory());
    assert(platform.changeCount() == 0);
  }
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene((AttachRefusalRootDefinition()));
    assert(!scene.composeRefusedForMemory());
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    assert(loka::dsl::testing::SceneTestAccess::composed(scene));
    assert(!scene.composeRefusedForMemory());
  }

#ifdef LOKA_LIFECYCLE_AUDIT
  const int totalLiveBefore = loka::core::LokaAllocAuditTotalLiveCount();
#endif
  {
    SceneTestSupport::RecordingPlatformController platform;
    // Arm the scene white flag with a refused root create(): the scene mounts
    // uncomposed, exactly the state that drives refreshComposition's retry.
    g_refuseRootCreate = true;
    g_attachRefuseState = false;
    loka::app::scene::Scene scene((AttachRefusalRootDefinition()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    assert(loka::dsl::testing::SceneTestAccess::rootBoundary(scene) == 0);
    assert(!loka::dsl::testing::SceneTestAccess::composed(scene));
    assert(loka::dsl::testing::SceneTestAccess::whiteFlagFullRebuildPending(scene));
    assert(platform.changeCount() == 0);
    assert(scene.composeRefusedForMemory());

    // Retry: root create() now succeeds, but the attach compose refuses the
    // node-local state allocation. The retry gets past create() yet the compose
    // still raises the flag.
    g_refuseRootCreate = false;
    g_attachRefuseState = true;
    g_attachStateRefusals = 0;
    loka::core::LokaAllocSetBackend(&attachStateRefusingBackendAlloc, &delegatingBackendFree);
    scene.requestInvalidate(loka::app::scene::NODE_DIRTY_CHILD);
    scene.flushInvalidation();
    loka::core::LokaAllocSetBackend(0, 0);
    g_attachRefuseState = false;
    assert(g_attachStateRefusals > 0);

    // Red before the hole-2 guard: composeIfNeeded published a partial root and
    // set composed_, and refreshComposition then cleared the just-re-armed
    // flag. Now the scene stays uncomposed, nothing was published, and the
    // white flag is still armed for the next retry.
    assert(!loka::dsl::testing::SceneTestAccess::composed(scene));
    assert(loka::dsl::testing::SceneTestAccess::whiteFlagFullRebuildPending(scene));
    assert(platform.changeCount() == 0);
    assert(scene.composeRefusedForMemory());
    // No self-scheduled tick: a driverless flush does nothing.
    assert(!scene.hasPendingInvalidation());
    LOKA_VERIFY(!scene.flushInvalidation());
    assert(!loka::dsl::testing::SceneTestAccess::composed(scene));
    assert(platform.changeCount() == 0);
    assert(scene.composeRefusedForMemory());

    // Heal the backend and drive one more external refresh: the retry now gets
    // past the attach without raising the flag, the root composes, the flag is
    // consumed, the refused node-local state materializes, and content reaches
    // the platform.
    scene.requestInvalidate(loka::app::scene::NODE_DIRTY_CHILD);
    scene.flushInvalidation();
    assert(loka::dsl::testing::SceneTestAccess::composed(scene));
    assert(!loka::dsl::testing::SceneTestAccess::whiteFlagFullRebuildPending(scene));
    AttachRefusalRootNode *healedRoot = static_cast<AttachRefusalRootNode *>(
        loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
    (void)healedRoot;
    assert(healedRoot && healedRoot->composeResult().composed);
    assert(!healedRoot->composeResult().allocationFailed);
    // The previously refused state is now live: the content healed.
    assert(healedRoot->state_.isValid());
    assert(platform.changeCount() > 0);
  }
  loka::core::LokaAllocSetBackend(0, 0);
  g_refuseRootCreate = false;
  g_attachRefuseState = false;
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(loka::core::LokaAllocAuditTotalLiveCount() == totalLiveBefore);
#endif
}

void testPlainRootAttachAllocationRefusalStaysUncomposedAndRetriesWithDetach()
{
  using namespace loka::app::scene;
  using loka::dsl::testing::SceneTestAccess;
#ifdef LOKA_LIFECYCLE_AUDIT
  const int totalLiveBefore = loka::core::LokaAllocAuditTotalLiveCount();
#endif
  PlainAttachRefusalRecord record;
  loka::core::MutableState<bool> shown(true);
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::FragmentDefinition *definition = new loka::app::FragmentDefinition();
    *definition << NodeDefinition<PlainAttachRefusalProps, PlainAttachRefusalChild>(
        PlainAttachRefusalProps(&record));
    *definition << (loka::app::Show(shown) << loka::app::ButtonDefinition("retry seat"));
    Scene scene(static_cast<NodeDefinitionBase *>(definition));
    g_attachRefuseState = true;
    g_attachStateRefusals = 0;
    loka::core::LokaAllocSetBackend(&attachStateRefusingBackendAlloc, &delegatingBackendFree);
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    loka::core::LokaAllocSetBackend(0, 0);
    g_attachRefuseState = false;

    const bool refusedComposed = SceneTestAccess::composed(scene);
    const bool refusedWhiteFlag = SceneTestAccess::whiteFlagFullRebuildPending(scene);
    const size_t refusedPublications = platform.changeCount();
    LOKA_VERIFY(g_attachStateRefusals > 0 && record.constructions == 1);
    LOKA_VERIFY(!refusedComposed);
    LOKA_VERIFY(refusedWhiteFlag && refusedPublications == 0);
    LOKA_VERIFY(record.detaches == 0 && record.destructions == 0);
    LOKA_VERIFY(!scene.flushInvalidation());

    BoundaryNode *refusedWrapper = SceneTestAccess::rootBoundary(scene);
    record.wrapper = refusedWrapper;
    Node *partial = refusedWrapper->childrenHead()->asNestable()->childrenHead();
    const std::string refusedSeats = loka::dsl::testing::OwnershipDump::dumpSeatRuntime(*refusedWrapper);
    LOKA_VERIFY(refusedSeats == "seat parent=attached active=attached\n");
    scene.requestInvalidate(NODE_DIRTY_CHILD);
    scene.flushInvalidation();
    const bool healedComposed = SceneTestAccess::composed(scene);
    const bool healedWhiteFlag = SceneTestAccess::whiteFlagFullRebuildPending(scene);
    const size_t healedPublications = platform.changeCount();
    BoundaryNode *wrapper = SceneTestAccess::rootBoundary(scene);
    Node *fragment = wrapper ? wrapper->childrenHead() : 0;
    INestable *children = fragment ? fragment->asNestable() : 0;
    LOKA_VERIFY(children);
    PlainAttachRefusalChild *child = static_cast<PlainAttachRefusalChild *>(children->childrenHead());
    LOKA_VERIFY(child);
    const bool stateValid = child->state_.isValid();
    LOKA_VERIFY(healedComposed && !healedWhiteFlag && stateValid && healedPublications > 0);
    LOKA_VERIFY(record.constructions == 2 && record.destructions == 0);
    LOKA_VERIFY(record.detaches == 1 && record.detachesAtReplacement == 1);
    LOKA_VERIFY(record.runtimeEmptyAtReplacement);
    const NodeLifecycleFact partialFact = partial->lifecycleFact();
    LOKA_VERIFY(partialFact == NODE_FACT_RETIRED);
    const std::string healedSeats = loka::dsl::testing::OwnershipDump::dumpSeatRuntime(*wrapper);
    LOKA_VERIFY(healedSeats == "seat parent=attached active=attached\n");
    scene.flushInvalidation();
    LOKA_VERIFY(record.destructions == 1);
  }
  LOKA_VERIFY(record.destructions == 2);
#ifdef LOKA_LIFECYCLE_AUDIT
  const int totalLiveAfter = loka::core::LokaAllocAuditTotalLiveCount();
  LOKA_VERIFY(totalLiveAfter == totalLiveBefore);
#endif
}

namespace
{
  void verifyRefusedAttachUnmountDrainsHeld(bool boundaryRoot)
  {
    using namespace loka::app::scene;
    using loka::dsl::testing::SceneTestAccess;
#ifdef LOKA_LIFECYCLE_AUDIT
    const int totalLiveBefore = loka::core::LokaAllocAuditTotalLiveCount();
#endif
    int releases = 0;
    PlainAttachRefusalRecord record;
    {
      SceneTestSupport::RecordingPlatformController platform;
      NodeDefinitionBase *definition = 0;
      if (boundaryRoot)
        definition = new AttachRefusalRootDefinition();
      else
      {
        loka::app::FragmentDefinition *fragment = new loka::app::FragmentDefinition();
        *fragment << NodeDefinition<PlainAttachRefusalProps, PlainAttachRefusalChild>(
            PlainAttachRefusalProps(&record));
        definition = fragment;
      }
      Scene scene(definition);
      g_attachRefusalHeldReleases = &releases;
      g_attachRefuseState = true;
      g_attachStateRefusals = 0;
      loka::core::LokaAllocSetBackend(&attachStateRefusingBackendAlloc, &delegatingBackendFree);
      scene.mount(&platform);
      loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
      loka::core::LokaAllocSetBackend(0, 0);
      g_attachRefuseState = false;
      g_attachRefusalHeldReleases = 0;
      const bool composed = SceneTestAccess::composed(scene);
      const bool whiteFlag = SceneTestAccess::whiteFlagFullRebuildPending(scene);
      const size_t publications = platform.changeCount();
      LOKA_VERIFY(!composed && whiteFlag && publications == 0 && g_attachStateRefusals > 0);
      LOKA_VERIFY(releases == 0);
      loka::dsl::testing::SceneTestAccess::unmount(scene);
      LOKA_VERIFY(releases == 1);
      if (!boundaryRoot)
        LOKA_VERIFY(record.detaches > 0 && record.destructions == 1);
    }
#ifdef LOKA_LIFECYCLE_AUDIT
    const int totalLiveAfter = loka::core::LokaAllocAuditTotalLiveCount();
    LOKA_VERIFY(totalLiveAfter == totalLiveBefore);
#endif
  }
}

void testPlainRootRefusedAttachUnmountDrainsHeldAndReclaimsNodes()
{
  verifyRefusedAttachUnmountDrainsHeld(false);
}

void testBoundaryRootRefusedAttachUnmountDrainsHeldAndReclaimsNodes()
{
  verifyRefusedAttachUnmountDrainsHeld(true);
}

void testBoundarySectionKeyIdentityAndTwoPhaseStateRetirement()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  const int totalLiveBefore = loka::core::LokaAllocAuditTotalLiveCount();
#endif
  int oldValueAlive = 0;
  int freshValueAlive = 0;
  g_sectionOrderingChildDestructions = 0;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<KeyedSectionRootNode>()));
    scene.mount(&platform);
    loka::app::scene::NodeState<SectionTrackedValue> oldState;
    {
      SectionTrackedValue initial(&oldValueAlive, 41);
      SectionOrderingObservation observation(oldState, initial);
      g_sectionOrderingObservation = &observation;
      loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
      g_sectionOrderingObservation = 0;
    }

    KeyedSectionRootNode *root = static_cast<KeyedSectionRootNode *>(
        loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
    assert(root);
    loka::app::BoundarySectionNode *original = root->section(1101);
    LOKA_VERIFY(original != 0);
    SectionOrderingChildNode *orderingChild =
        static_cast<SectionOrderingChildNode *>(original->childrenHead());
    // The child now declares its observation inside its own window; the
    // test only needs it to exist (kept as a release-build check too).
    LOKA_VERIFY(orderingChild != 0);

    assert(oldValueAlive == 1);
    assert(oldState.isValid());
    assert(!oldState.dangerouslyMutableState()->isArenaAllocated());
    assert(oldState.dangerouslyMutableState()->isGateAllocated());

    // A different value key creates a fresh Section and retires the old one.
    {
      loka::core::StateTrackerGuard guard(root->tracker());
      root->replaceFirstSection();
    }
    scene.requestInvalidate(loka::app::scene::NODE_DIRTY_CHILD);
    LOKA_VERIFY(scene.flushInvalidation());
    assert(root->section(1101) == 0 && root->section(1103) == 0);
    assert(scene.hasPendingInvalidation());
    assert(oldValueAlive == 1 &&
           "retired Section state must remain touchable until the drain");
    assert(oldState.dangerouslyMutableState()->get().value == 41);
    assert(g_sectionOrderingChildDestructions == 0);

    LOKA_VERIFY(scene.flushInvalidation() && "returned slots admit the replacement Section");
    assert(oldValueAlive == 0 && g_sectionOrderingChildDestructions == 1);
    loka::app::BoundarySectionNode *fresh = root->section(1103);
    assert(fresh);
    loka::app::scene::NodeState<SectionTrackedValue> freshState;
    {
      SectionTrackedValue initial(&freshValueAlive, 73);
      loka::app::scene::NodeComposition::StateBatch states(fresh);
      states.state(freshState, initial);
    }
    assert(freshValueAlive == 1);

    assert(oldValueAlive == 0 &&
           "the retired Section must release its state at the drain");
    assert(freshValueAlive == 1);
    assert(g_sectionOrderingChildDestructions == 1 &&
           "children must release observations before Section owner storage");
  }
  assert(oldValueAlive == 0);
  assert(freshValueAlive == 0);
  assert(g_sectionOrderingChildDestructions == 2);
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(loka::core::LokaAllocAuditTotalLiveCount() == totalLiveBefore);
#endif
}

void testBoundarySectionRetainedKeyReconcilesReplacedChild()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  const int totalLiveBefore = loka::core::LokaAllocAuditTotalLiveCount();
#endif
  int stateValueAlive = 0;
  g_sectionOrderingChildDestructions = 0;
  g_sectionOrderingChildDetaches = 0;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene((loka::app::scene::Boundary<SectionLifetimeRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    SectionLifetimeRootNode *root =
        static_cast<SectionLifetimeRootNode *>(loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
    assert(root);
    loka::app::BoundarySectionNode *section = findSectionByKey(root, 1101);
    assert(section);
    loka::app::scene::Node *originalChild = section->childrenHead();
    (void)originalChild;
    assert(originalChild &&
           originalChild->propsTypeId() == SectionOrderingChildProps::staticTypeId());

    loka::app::scene::NodeState<SectionTrackedValue> ownedState;
    {
      SectionTrackedValue initial(&stateValueAlive, 41);
      loka::app::scene::NodeComposition::StateBatch states(section);
      states.state(ownedState, initial);
    }
    assert(ownedState.isValid());
    assert(stateValueAlive == 1);

    {
      loka::core::StateTrackerGuard guard(root->tracker());
      root->retireSection();
    }
    scene.requestInvalidate(loka::app::scene::NODE_DIRTY_CHILD);
    LOKA_VERIFY(scene.flushInvalidation());

    LOKA_VERIFY(findSectionByKey(root, 1101) == 0);
    LOKA_VERIFY(ownedState.isValid() && ownedState.dangerouslyMutableState()->get().value == 41);
    LOKA_VERIFY(stateValueAlive == 1 && "Section state stays readable until reclaim");
    assert(g_sectionOrderingChildDetaches == 1 &&
           "the old child must cross the synchronous detach line");
    assert(g_sectionOrderingChildDestructions == 0 &&
           "the old child must remain allocated until the reclaim drain");
    assert(scene.hasPendingInvalidation());

    LOKA_VERIFY(!scene.flushInvalidation() &&
           "child reclamation must be a silent drain-only tracker run");
    assert(g_sectionOrderingChildDestructions == 1 &&
           "the old child must reclaim at the owning clock boundary");
    LOKA_VERIFY(stateValueAlive == 0 && "the retired Section releases its state at the drain");
  }
  assert(stateValueAlive == 0);
  assert(g_sectionOrderingChildDestructions == 1);
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(loka::core::LokaAllocAuditTotalLiveCount() == totalLiveBefore);
#endif
}

void testBoundarySectionRejectsMissingAndDuplicateSiblingKeys()
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
  for (int misuse = 0; misuse < 2; ++misuse)
  {
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0)
    {
      if (misuse == 0)
      {
        loka::app::Section missing;
        (void)missing;
      }
      else
      {
        SceneTestSupport::RecordingPlatformController platform;
        loka::app::scene::Scene scene(
            (loka::app::scene::Boundary<DuplicateSectionRootNode>()));
        scene.mount(&platform);
        loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
      }
      _exit(0);
    }

    int status = 0;
    LOKA_VERIFY(waitpid(child, &status, 0) == child);
  LOKA_VERIFY(WIFSIGNALED(status));
  LOKA_VERIFY(WTERMSIG(status) == SIGABRT);
  }
#endif
}

void testBoundarySectionAllocationFailureKeepsBoundaryRefusalAtomic()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  const int totalLiveBefore = loka::core::LokaAllocAuditTotalLiveCount();
#endif
  for (int refusalPosition = 1; refusalPosition <= 2; ++refusalPosition)
  {
    g_sectionFailureHeapPosition = refusalPosition;
    g_sectionFailureHeapCalls = 0;
    {
      SceneTestSupport::RecordingPlatformController platform;
      loka::app::scene::Scene scene(
          (loka::app::scene::Boundary<SectionFailureRootNode>()));
      scene.mount(&platform);
      loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
      SectionFailureRootNode *root = static_cast<SectionFailureRootNode *>(
          loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
      assert(root && root->composeResult().composed);
      const size_t appliedBefore = platform.changeCount();

      {
        loka::core::StateTrackerGuard guard(root->tracker());
        root->injectStatesOnNextCompose();
      }
      scene.requestInvalidate(loka::app::scene::NODE_DIRTY_CHILD);
      scene.flushInvalidation();
      const size_t beforeCandidate = platform.changeCount();
      (void)beforeCandidate;
      scene.flushInvalidation();

      assert(g_sectionFailureHeapCalls == 2);
      assert(!root->composeResult().composed);
      assert(root->composeResult().allocationFailed);
      (void)appliedBefore;
      assert(platform.changeCount() == beforeCandidate &&
             "Section-local allocation failure must refuse boundary publish");
      assert(loka::dsl::testing::SceneTestAccess::whiteFlagFullRebuildPending(scene));
      scene.flushInvalidation();
      assert(!scene.hasPendingInvalidation() &&
             "after failed-candidate reclaim, refusal waits for external retry");
    }
    loka::core::LokaAllocSetBackend(0, 0);
  }
  g_sectionFailureHeapPosition = 0;
  g_sectionFailureHeapCalls = 0;
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(loka::core::LokaAllocAuditTotalLiveCount() == totalLiveBefore);
#endif
}

void testBoundarySectionGridUsesEnclosingStateArenaEconomically()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  const int totalLiveBefore = loka::core::LokaAllocAuditTotalLiveCount();
#endif
  resetSectionAllocationCounts();
  loka::core::LokaAllocSetBackend(&sectionCountingBackendAlloc,
                                  &sectionCountingBackendFree);
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<SectionGridRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    SectionGridRootNode *root = static_cast<SectionGridRootNode *>(
        loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
    assert(root);

    root->populateSections();
    scene.requestInvalidate(loka::app::scene::NODE_DIRTY_CHILD);
    LOKA_VERIFY(scene.flushInvalidation());
    assert(root->populatedSections() == SectionGridRootNode::kSectionCount);
    assert(g_sectionHeapStateAllocs == 0);
    assert(g_sectionStateArenaSlabAllocs > 0);
    const int stateStorageAllocations =
        g_sectionStateArenaSlabAllocs + g_sectionHeapStateAllocs;
    (void)stateStorageAllocations;
    assert(stateStorageAllocations < 150 &&
           "200 Section states must use far fewer than 200 storage allocations");
    std::printf("BoundarySection grid: sections=%d states=%d arena-blocks=%d arena-slabs=%d heap-states=%d\n",
                SectionGridRootNode::kSectionCount,
                SectionGridRootNode::kSectionCount * 2,
                g_sectionStateArenaBlockAllocs,
                g_sectionStateArenaSlabAllocs,
                g_sectionHeapStateAllocs);
  }
  loka::core::LokaAllocSetBackend(0, 0);
  assert(g_sectionStateArenaBlockFrees == g_sectionStateArenaBlockAllocs);
  assert(g_sectionStateArenaSlabFrees == g_sectionStateArenaSlabAllocs);
  assert(g_sectionHeapStateFrees == g_sectionHeapStateAllocs);
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(loka::core::LokaAllocAuditTotalLiveCount() == totalLiveBefore);
#endif
}

void testNodeCompositionFallsBackToBoundaryOwnerWithoutSection()
{
  SectionOwnerResolutionScenario scenario(
      SectionOwnerResolutionScenario::MODE_FALLBACK);
  g_sectionOwnerResolutionScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<SectionOwnerResolutionRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    SectionOwnerResolutionRootNode *root =
        static_cast<SectionOwnerResolutionRootNode *>(
            loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
    (void)root;
    assert(root);
    assert(scenario.primary.owner == root);
    assert(scenario.primary.currentStateValid);
  }
  g_sectionOwnerResolutionScenario = 0;
}

void testNodeCompositionResolvesNearestSectionOwner()
{
  resetSectionAllocationCounts();
  loka::core::LokaAllocSetBackend(&sectionCountingBackendAlloc,
                                  &sectionCountingBackendFree);
  SectionOwnerResolutionScenario scenario(
      SectionOwnerResolutionScenario::MODE_NEAREST);
  g_sectionOwnerResolutionScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<SectionOwnerResolutionRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    loka::app::scene::BoundaryNode *root =
        loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
    loka::app::BoundarySectionNode *section = findSectionByKey(root, 4101);
    (void)section;
    assert(section);
    assert(scenario.primary.owner == section);
    assert(scenario.primary.currentStateValid);
    assert(scenario.primary.mutableState &&
           scenario.primary.mutableState->isArenaAllocated());
    assert(g_sectionStateArenaSlabAllocs > 0);
    assert(g_sectionHeapStateAllocs == 0);
  }
  g_sectionOwnerResolutionScenario = 0;
  loka::core::LokaAllocSetBackend(0, 0);
  assert(g_sectionStateArenaBlockFrees == g_sectionStateArenaBlockAllocs);
  assert(g_sectionStateArenaSlabFrees == g_sectionStateArenaSlabAllocs);
  assert(g_sectionHeapStateFrees == g_sectionHeapStateAllocs);
}

void testNodeCompositionResolvesInnermostSectionOwner()
{
  SectionOwnerResolutionScenario scenario(
      SectionOwnerResolutionScenario::MODE_INNERMOST);
  g_sectionOwnerResolutionScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<SectionOwnerResolutionRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    loka::app::scene::BoundaryNode *root =
        loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
    loka::app::BoundarySectionNode *outer = findSectionByKey(root, 4101);
    loka::app::BoundarySectionNode *inner = findSectionByKey(root, 4102);
    (void)outer;
    (void)inner;
    assert(outer && inner && outer != inner);
    assert(scenario.primary.owner == inner);
    assert(scenario.primary.owner != outer);
  }
  g_sectionOwnerResolutionScenario = 0;
}

void testBoundarySectionOwnedStateInvalidatesEnclosingBoundary()
{
  SectionOwnerResolutionScenario scenario(
      SectionOwnerResolutionScenario::MODE_INVALIDATION);
  g_sectionOwnerResolutionScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<SectionOwnerResolutionRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    loka::app::scene::BoundaryNode *root =
        loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
    (void)root;
    LOKA_VERIFY(findSectionByKey(root, 4101));

    scene.requestInvalidate(loka::app::scene::NODE_DIRTY_PROPS);
    LOKA_VERIFY(scene.flushInvalidation());
    platform.clearChanges();
    assert(scenario.primary.tracker && scenario.primary.mutableState);
    {
      loka::core::StateTrackerGuard guard(scenario.primary.tracker);
      scenario.primary.mutableState->set(23);
    }
    LOKA_VERIFY(scene.flushInvalidation());
    assert(platform.changeCount() == 1);
    assert(platform.changeCountForNode(root) == 1);
    assert(platform.flagsSeenForNode(root) ==
           loka::app::scene::NODE_DIRTY_PROPS);
  }
  g_sectionOwnerResolutionScenario = 0;
}

void testBoundarySectionRetireWhileDirtySourceDeregistersAncestorEdges()
{
  g_sectionFailureHeapPosition = 0;
  g_sectionFailureHeapCalls = 0;
  loka::core::LokaAllocSetBackend(&sectionFailureBackendAlloc,
                                  &delegatingBackendFree);
  SectionOwnerResolutionScenario scenario(
      SectionOwnerResolutionScenario::MODE_RETIRE_WHILE_DIRTY_SOURCE);
  g_sectionOwnerResolutionScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<SectionOwnerResolutionRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    SectionOwnerResolutionRootNode *root =
        static_cast<SectionOwnerResolutionRootNode *>(
            loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
    LOKA_VERIFY(root && findSectionByKey(root, 4101));

    // The second compose registers the now-materialized Section-owned state as
    // both a boundary tracker source and an observed-state ledger entry.
    scene.requestInvalidate(loka::app::scene::NODE_DIRTY_PROPS);
    LOKA_VERIFY(scene.flushInvalidation());
    assert(scenario.primary.mutableState &&
           !scenario.primary.mutableState->isArenaAllocated());
    root->retireSection();
    scene.requestInvalidate(loka::app::scene::NODE_DIRTY_CHILD);
    LOKA_VERIFY(scene.flushInvalidation());
    LOKA_VERIFY(!findSectionByKey(root, 4101));
    assert(scene.hasPendingInvalidation());
    LOKA_VERIFY(scene.flushInvalidation());

    platform.clearChanges();
    root->mutateBoundaryPulse();
    LOKA_VERIFY(scene.flushInvalidation());
    assert(platform.changeCount() == 1 &&
           "the surviving boundary tracker and observed ledger must remain usable");
  }
  g_sectionOwnerResolutionScenario = 0;
  loka::core::LokaAllocSetBackend(0, 0);
}

void testConditionalBranchFlipInsideSectionKeepsSectionOwner()
{
  SectionOwnerResolutionScenario scenario(
      SectionOwnerResolutionScenario::MODE_CONDITIONAL);
  g_sectionOwnerResolutionScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<SectionOwnerResolutionRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    SectionOwnerResolutionRootNode *root =
        static_cast<SectionOwnerResolutionRootNode *>(
            loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
    loka::app::BoundarySectionNode *section = findSectionByKey(root, 4101);
    (void)section;
    assert(root && section);
    assert(scenario.primary.owner == section);
    root->flipCondition();
    LOKA_VERIFY(scene.flushInvalidation());
    assert(scenario.secondary.composeCount > 0);
    assert(scenario.secondary.owner == section);
    assert(scenario.secondary.mutableState &&
           scenario.secondary.mutableState->isArenaAllocated());
  }
  g_sectionOwnerResolutionScenario = 0;
}

void testCurrentBoundaryStateRequiresResolvedOwnerMatchBothDirections()
{
  SectionOwnerResolutionScenario scenario(
      SectionOwnerResolutionScenario::MODE_OWNER_MATCH);
  g_sectionOwnerResolutionScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<SectionOwnerResolutionRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    loka::app::scene::BoundaryNode *root =
        loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
    loka::app::BoundarySectionNode *section = findSectionByKey(root, 4101);
    (void)section;
    assert(section);
    assert(scenario.primary.owner == section);
    assert(scenario.primary.currentStateValid);
    assert(!scenario.primary.foreignStateValid &&
           "Boundary-owned state is foreign under a Section");
    assert(scenario.boundary.owner == root);
    assert(scenario.boundary.currentStateValid);
    assert(!scenario.boundary.foreignStateValid &&
           "Section-owned state is foreign at Boundary scope");
  }
  g_sectionOwnerResolutionScenario = 0;
}

namespace
{

  class HeldNestedBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<HeldNestedBoundaryNode>
      HeldNestedBoundaryProps;

  class HeldNestedBoundaryNode : public loka::app::scene::BoundaryNodeFor<HeldNestedBoundaryNode>
  {
  public:
    explicit HeldNestedBoundaryNode(const HeldNestedBoundaryProps &props)
        : loka::app::scene::BoundaryNodeFor<HeldNestedBoundaryNode>(props)
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      loka::app::Fragment root;
      root << HeldOwnerSlotProbeDefinition(
          HeldOwnerSlotProbeProps(HeldOwnerSlotProbeProps::ROLE_CREATE));
      composition.declare(root);
    }
  };

  class HeldNestedRootNode;
  typedef loka::app::scene::BoundaryPropsFor<HeldNestedRootNode>
      HeldNestedRootProps;

  class HeldNestedRootNode : public loka::app::scene::BoundaryNodeFor<HeldNestedRootNode>
  {
  public:
    explicit HeldNestedRootNode(const HeldNestedRootProps &props)
        : loka::app::scene::BoundaryNodeFor<HeldNestedRootNode>(props)
    {
    }

    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      composition.declare(
          loka::app::Fragment() << loka::app::Keyed(
              g_heldOwnerSlotScenario->showCreator,
              this,
              &HeldNestedRootNode::declareContent,
              loka::app::reservation::SeatNodes<loka::app::reservation::Nodes<
                  loka::app::FragmentNode,
                  1,
                  loka::app::reservation::Nodes<HeldNestedBoundaryNode, 1, loka::app::reservation::End> > >()));
    }

    void declareContent(loka::app::scene::NodeComposition &composition)
    {
      loka::app::Fragment root;
      if (g_heldOwnerSlotScenario->showCreator.get())
      {
        loka::app::scene::BoundaryDefinition<HeldNestedBoundaryProps,
                                             HeldNestedBoundaryNode>
            nested = loka::app::scene::Boundary<HeldNestedBoundaryNode>();
        root << nested;
      }
      composition.declare(root);
    }
  };
} // namespace

void testHeldNestedBoundaryRetireReleasesAtParentDrain()
{
  HeldOwnerSlotScenario scenario;
  g_heldOwnerSlotScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<HeldNestedRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    assert(loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
    assert(scenario.held.isValid());
    assert(scenario.releaseCount == 0);

    // A retired Boundary leaves the live tree carrying its own queue. The
    // releaser must still ride a tick boundary — the retiring parent's drain —
    // and never run from the retired Boundary's destructor.
    {
      loka::core::StateTrackerGuard guard(scenario.showCreator.trackerOwner());
      scenario.showCreator.set(false);
    }
    scene.requestInvalidate(loka::app::scene::NODE_DIRTY_CHILD);
    LOKA_VERIFY(scene.flushInvalidation());
    assert(scenario.releaseCount == 0 &&
           "retiring a Boundary must only queue its Held releasers");
    assert(scene.hasPendingInvalidation());

    LOKA_VERIFY(scene.flushInvalidation());
    assert(scenario.releaseCount == 1);
    // The handle is only a view into the creator's storage. This creator was
    // the retired Boundary itself, so its arena went with it; only the
    // observer's own record is readable now. Reading the block here is the
    // upward-borrow rule broken from the test side (ASan sees it).
  }
  assert(scenario.releaseCount == 1);
  g_heldOwnerSlotScenario = 0;
}

void testHeldCreationStartsWithSectionOwnerSlot()
{
  HeldOwnerSlotScenario scenario;
  g_heldOwnerSlotScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<HeldOwnerSlotRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    loka::app::scene::BoundaryNode *root =
        loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
    loka::app::BoundarySectionNode *creator =
        findSectionByKey(root, 5101);
    (void)creator;
    assert(root && creator);
    assert(scenario.held.isValid());
    assert(scenario.releaseCount == 0);
    assert(scenario.creatorOwner == creator);
    assert(loka::core::testing::HeldTestAccess::creator(scenario.held) ==
           creator);
    assert(loka::core::testing::HeldTestAccess::slotCount(scenario.held) == 1);
    assert(loka::core::testing::HeldTestAccess::holdCountForOwner(
               scenario.held, creator) == 1);
  }
  assert(scenario.releaseCount == 1);
  g_heldOwnerSlotScenario = 0;
}

void testHeldDescendantAndRepeatedOwnerHoldsShareOneBlock()
{
  HeldOwnerSlotScenario scenario;
  {
    loka::core::StateTrackerGuard guard(scenario.showDescendant.trackerOwner());
    scenario.showDescendant.set(true);
  }
  scenario.repeatCreatorHold = true;
  g_heldOwnerSlotScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<HeldOwnerSlotRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    loka::app::scene::BoundaryNode *root =
        loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
    loka::app::BoundarySectionNode *creator =
        findSectionByKey(root, 5101);
    loka::app::BoundarySectionNode *descendant =
        findSectionByKey(root, 5102);
    (void)creator;
    (void)descendant;
    assert(creator && descendant && creator != descendant);
    assert(scenario.held.isValid() && scenario.held.get()->scenario == &scenario);
    assert(loka::core::testing::HeldTestAccess::slotCount(scenario.held) == 2);
    assert(loka::core::testing::HeldTestAccess::holdCountForOwner(
               scenario.held, creator) == 2);
    assert(loka::core::testing::HeldTestAccess::holdCountForOwner(
               scenario.held, descendant) == 1);
  }
  assert(scenario.releaseCount == 1);
  g_heldOwnerSlotScenario = 0;
}

void testHeldDescendantDetachDropsOnlyItsOwnerSlot()
{
  HeldOwnerSlotScenario scenario;
  {
    loka::core::StateTrackerGuard guard(scenario.showDescendant.trackerOwner());
    scenario.showDescendant.set(true);
  }
  g_heldOwnerSlotScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<HeldOwnerSlotRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    HeldOwnerSlotRootNode *root = static_cast<HeldOwnerSlotRootNode *>(
        loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
    loka::app::BoundarySectionNode *creator =
        findSectionByKey(root, 5101);
    (void)creator;
    LOKA_VERIFY(root && creator && findSectionByKey(root, 5102));
    assert(loka::core::testing::HeldTestAccess::slotCount(scenario.held) == 2);

    {

      loka::core::StateTrackerGuard guard(scenario.showDescendant.trackerOwner());

      scenario.showDescendant.set(false);
    }
    scene.requestInvalidate(loka::app::scene::NODE_DIRTY_CHILD);
    LOKA_VERIFY(scene.flushInvalidation());
    LOKA_VERIFY(!findSectionByKey(root, 5102));
    assert(loka::core::testing::HeldTestAccess::slotCount(scenario.held) == 1);
    assert(loka::core::testing::HeldTestAccess::holdCountForOwner(
               scenario.held, creator) == 1);
    assert(scenario.releaseCount == 0 && scenario.held.isValid());
    assert(scene.hasPendingInvalidation());
    LOKA_VERIFY(scene.flushInvalidation());
    assert(scenario.releaseCount == 0 && scenario.held.isValid());
  }
  assert(scenario.releaseCount == 1);
  g_heldOwnerSlotScenario = 0;
}

void testHeldLastDropDefersReleaserToRetirePoolDrain()
{
  HeldOwnerSlotScenario scenario;
  g_heldOwnerSlotScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<HeldOwnerSlotRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    HeldOwnerSlotRootNode *root = static_cast<HeldOwnerSlotRootNode *>(
        loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
    (void)root;
    LOKA_VERIFY(root && findSectionByKey(root, 5101));

    {

      loka::core::StateTrackerGuard guard(scenario.showCreator.trackerOwner());

      scenario.showCreator.set(false);
    }
    scene.requestInvalidate(loka::app::scene::NODE_DIRTY_CHILD);
    LOKA_VERIFY(scene.flushInvalidation());
    LOKA_VERIFY(!findSectionByKey(root, 5101));
    assert(scenario.detachCallbackReturnOrder > 0);
    assert(scenario.releaseCount == 0 &&
           "last drop must only queue the releaser");
    assert(loka::core::testing::HeldTestAccess::slotCount(scenario.held) == 0);
    assert(scene.hasPendingInvalidation());

    LOKA_VERIFY(scene.flushInvalidation());
    assert(scenario.releaseCount == 1);
    assert(scenario.releaseOrder > scenario.detachCallbackReturnOrder);
    assert(loka::core::testing::HeldTestAccess::released(scenario.held));
  }
  assert(scenario.releaseCount == 1);
  g_heldOwnerSlotScenario = 0;
}

void testHeldLastDropCannotReleaseInsideInFlightCallback()
{
  HeldOwnerSlotScenario scenario;
  loka::app::scene::StdCompositionBoundaryNode owner(
      (loka::app::scene::StdCompositionProps()));
  loka::app::scene::ComponentContext context;
  context.setStateOwner(&owner);
  loka::app::scene::NodeComposition composition;
  composition.setContext(&context);
  scenario.held = composition.hold(
      new HeldOwnerSlotPayload(&scenario),
      &releaseHeldOwnerSlotPayload);
  assert(scenario.held.isValid());

  runHeldInFlightDetachCallback(owner, scenario);
  assert(!scenario.releaseSawCallbackInFlight);
  owner.drainRetiredSubtreesAtNextTrackerRun();
  assert(scenario.releaseCount == 1);
  assert(!scenario.releaseSawCallbackInFlight &&
         "the in-flight callback must return before the releaser runs");
}

void testHeldFifthOwnerRefusalIsFailureAtomic()
{
  HeldOwnerSlotScenario scenario(HeldOwnerSlotScenario::MODE_FIVE_OWNERS);
  g_heldOwnerSlotScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<HeldOwnerSlotRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    assert(scenario.held.isValid());
    assert(!scenario.refused.isValid());
    assert(scenario.refusalWasFailureAtomic);
    assert(loka::core::testing::HeldTestAccess::slotCount(scenario.held) == 4);
    for (unsigned i = 0;
         i < loka::core::detail::HeldBlockBase::kSlotCapacity;
         ++i)
    {
      assert(loka::core::testing::HeldTestAccess::slotHoldCount(
                 scenario.held, i) == 1);
    }
    assert(scenario.releaseCount == 0);
  }
  assert(scenario.releaseCount == 1);
  g_heldOwnerSlotScenario = 0;
}

void testHeldCrossBranchHoldIsRefusedBySubtreeWall()
{
  HeldOwnerSlotScenario scenario(HeldOwnerSlotScenario::MODE_CROSS_BRANCH);
  g_heldOwnerSlotScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<HeldOwnerSlotRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    assert(scenario.held.isValid());
    assert(!scenario.refused.isValid());
    assert(scenario.refusalWasFailureAtomic);
    assert(loka::core::testing::HeldTestAccess::slotCount(scenario.held) == 1);
  }
  assert(scenario.releaseCount == 1);
  g_heldOwnerSlotScenario = 0;
}

void testHeldHandleCopiesDoNotChangeOwnerSlots()
{
  HeldOwnerSlotScenario scenario;
  g_heldOwnerSlotScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<HeldOwnerSlotRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    loka::app::scene::IStateOwner *creator = scenario.creatorOwner;
    assert(creator);
    const unsigned slotsBefore =
        loka::core::testing::HeldTestAccess::slotCount(scenario.held);
    const unsigned countBefore =
        loka::core::testing::HeldTestAccess::holdCountForOwner(
            scenario.held, creator);
    {
      loka::core::Held<HeldOwnerSlotPayload> first(scenario.held);
      loka::core::Held<HeldOwnerSlotPayload> second;
      second = first;
      loka::core::Held<HeldOwnerSlotPayload> third(second);
      assert(first == second && second == third);
      assert(first.get() == scenario.held.get());
    }
    (void)slotsBefore;
    assert(loka::core::testing::HeldTestAccess::slotCount(scenario.held) ==
           slotsBefore);
    (void)countBefore;
    assert(loka::core::testing::HeldTestAccess::holdCountForOwner(
               scenario.held, creator) == countBefore);
    assert(scenario.releaseCount == 0);
  }
  assert(scenario.releaseCount == 1);
  g_heldOwnerSlotScenario = 0;
}

void testHeldStorageRefusalReleasesPayloadInsteadOfLeaking()
{
  HeldOwnerSlotScenario scenario;
  loka::core::LokaAllocSetBackend(&sectionFailureBackendAlloc,
                                  &delegatingBackendFree);
  {
    loka::app::scene::StdCompositionBoundaryNode owner(
        (loka::app::scene::StdCompositionProps()));
    loka::app::scene::ComponentContext context;
    context.setStateOwner(&owner);
    loka::app::scene::NodeComposition composition;
    composition.setContext(&context);

    // The arena refuses, so no block is created. Passing the payload and its
    // releaser handed the payload over, so the refusal must release it rather
    // than leave a pointer nobody owns.
    loka::core::Held<HeldOwnerSlotPayload> held = composition.hold(
        new HeldOwnerSlotPayload(&scenario),
        &releaseHeldOwnerSlotPayload);
    (void)held;
    assert(!held.isValid());
    assert(scenario.releaseCount == 1 &&
           "a refused creation must release the payload it was handed");
    assert(!scenario.releaseSawCallbackInFlight);
  }
  loka::core::LokaAllocSetBackend(0, 0);
  assert(scenario.releaseCount == 1);
}

void testHeldBlockUsesEnclosingBoundaryArenaWithoutHeapControlBlock()
{
  resetSectionAllocationCounts();
  loka::core::LokaAllocSetBackend(&sectionCountingBackendAlloc,
                                  &sectionCountingBackendFree);
  HeldOwnerSlotScenario scenario;
  g_heldOwnerSlotScenario = &scenario;
  {
    SceneTestSupport::RecordingPlatformController platform;
    loka::app::scene::Scene scene(
        (loka::app::scene::Boundary<HeldOwnerSlotRootNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    loka::app::scene::BoundaryNode *root =
        loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
    loka::app::BoundarySectionNode *creator =
        findSectionByKey(root, 5101);
    (void)creator;
    assert(creator && scenario.creatorOwner == creator);
    assert(g_sectionStateArenaBlockAllocs == 1);
    assert(g_sectionStateArenaSlabAllocs == 1);
    assert(g_sectionHeapStateAllocs == 0 &&
           "Held control blocks must not use the heap-state fallback door");
  }
  assert(scenario.releaseCount == 1);
  g_heldOwnerSlotScenario = 0;
  loka::core::LokaAllocSetBackend(0, 0);
  assert(g_sectionStateArenaBlockFrees == g_sectionStateArenaBlockAllocs);
  assert(g_sectionStateArenaSlabFrees == g_sectionStateArenaSlabAllocs);
  assert(g_sectionHeapStateFrees == 0);

  // The releaser clock does not reclaim the block bytes. They remain readable
  // in the landlord arena and cannot be reused by a later block before the
  // Boundary itself reclaims its StateArena (M3 / audit-before-free pin).
  HeldOwnerSlotScenario firstScenario;
  HeldOwnerSlotScenario secondScenario;
  loka::app::scene::StdCompositionBoundaryNode owner(
      (loka::app::scene::StdCompositionProps()));
  loka::app::scene::ComponentContext context;
  context.setStateOwner(&owner);
  loka::app::scene::NodeComposition composition;
  composition.setContext(&context);
  firstScenario.held = composition.hold(
      new HeldOwnerSlotPayload(&firstScenario),
      &releaseHeldOwnerSlotPayload);
  const void *firstAddress =
      loka::core::testing::HeldTestAccess::blockAddress(firstScenario.held);
  owner.detachHeldResources();
  owner.drainRetiredSubtreesAtNextTrackerRun();
  assert(firstScenario.releaseCount == 1);
  assert(loka::core::testing::HeldTestAccess::released(firstScenario.held));
  assert(loka::core::testing::HeldTestAccess::slotCount(firstScenario.held) == 0);

  secondScenario.held = composition.hold(
      new HeldOwnerSlotPayload(&secondScenario),
      &releaseHeldOwnerSlotPayload);
  assert(secondScenario.held.isValid());
  (void)firstAddress;
  assert(loka::core::testing::HeldTestAccess::blockAddress(secondScenario.held) !=
         firstAddress);
  assert(loka::core::testing::HeldTestAccess::released(firstScenario.held));
  owner.detachHeldResources();
  owner.drainRetiredSubtreesAtNextTrackerRun();
  assert(secondScenario.releaseCount == 1);
}

void testIndexedSeatSlotPassTraversesArmsPastAnEmptyOne()
{
  // An empty arm (null definition) before populated ones must not stop the
  // slot pass: the nested seats behind it need distinct anonymous slots, or
  // they collide on one anonymous key and the whole seat is refused.
  loka::core::MutableState<unsigned> selection(1);
  loka::core::MutableState<bool> showA(false);
  loka::core::MutableState<bool> showB(false);
  loka::app::ButtonDefinition button("slot-button");
  loka::app::ShowDefinition nestedA = loka::app::Show(showA);
  nestedA << button;
  loka::app::ShowDefinition nestedB = loka::app::Show(showB);
  nestedB << button;
  loka::app::scene::NodeDefinitionBase *arms[3] = {0, &nestedA, &nestedB};
  loka::app::scene::testing::ProbeArmSeatDefinition seat(&selection, arms, 3);
  loka::app::Fragment root;
  root << seat;
  loka::app::scene::NodeComposition composition;
  composition.declare(root);
  composition.assignCompositionSeatSlots();

  loka::app::scene::INestableDefinition *rootNestable =
      composition.root()->asNestableDefinition();
  LOKA_VERIFY(rootNestable && rootNestable->childrenHead());
  loka::app::scene::IBranchSeatDefinition *seatClone =
      rootNestable->childrenHead()->asBranchSeatDefinition();
  LOKA_VERIFY(seatClone && seatClone->armCount() == 3 && !seatClone->armDefinition(0));
  loka::app::scene::NodeDefinitionBase *armA = seatClone->armDefinition(1);
  loka::app::scene::NodeDefinitionBase *armB = seatClone->armDefinition(2);
  LOKA_VERIFY(armA && armB);
  LOKA_VERIFY(armA->compositionSeatSlot() >= 0 && armB->compositionSeatSlot() >= 0 &&
              armA->compositionSeatSlot() != armB->compositionSeatSlot() &&
              "arms behind an empty one still receive their own composition slots");
}

void testBranchSeatSiblingsRejectDuplicateTags()
{
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
  const pid_t child = fork();
  LOKA_VERIFY(child >= 0);
  if (child == 0)
  {
    loka::core::MutableState<unsigned> selection(0);
    loka::app::FragmentDefinition empty;
    loka::app::scene::NodeDefinitionBase *arms[3] = {&empty, &empty, &empty};
    loka::app::scene::testing::ProbeArmSeatDefinition first(
        &selection, arms, 3);
    loka::app::scene::testing::ProbeArmSeatDefinition second(
        &selection, arms, 3);
    first.setNodeTag(8101);
    second.setNodeTag(8101);
    loka::app::Fragment root;
    root << first << second;
    loka::app::scene::NodeComposition composition;
    composition.declare(root);
    // Tagged seat keys ignore the slot, so no slot pass is needed to mint
    // them; capture is the wall and asserts on the second seat under one key.
    loka::app::scene::BoundaryBranchSeatState seats;
    seats.capture(composition.root());
    _exit(0);
  }

  int status = 0;
  LOKA_VERIFY(waitpid(child, &status, 0) == child);
  LOKA_VERIFY(WIFSIGNALED(status));
  LOKA_VERIFY(WTERMSIG(status) == SIGABRT);
#elif defined(NDEBUG)
  loka::core::MutableState<unsigned> selection(0);
  loka::app::FragmentDefinition empty;
  loka::app::scene::NodeDefinitionBase *arms[3] = {&empty, &empty, &empty};
  loka::app::scene::testing::ProbeArmSeatDefinition first(
      &selection, arms, 3);
  loka::app::scene::testing::ProbeArmSeatDefinition second(
      &selection, arms, 3);
  first.setNodeTag(8101);
  second.setNodeTag(8101);
  loka::app::Fragment root;
  root << first << second;
  loka::app::scene::NodeComposition composition;
  composition.declare(root);
  composition.assignCompositionSeatSlots();
  loka::app::scene::BoundaryBranchSeatState seats;
  seats.capture(composition.root());
  // Release builds keep every claimant and refuse the key: findPlan() answers
  // 0 while both entries stand, so both seats materialize as seats without a
  // plan (requiresBoundaryPlan in its own meaning). A later append() under
  // the same key (a nested composition) cannot revive it.
  const loka::app::scene::BoundaryParkedBranchKey key(8101, 0, first.branchSeatTypeId(), &seats);
  LOKA_VERIFY(seats.plans().size() == 2 && seats.findPlan(key) == 0 &&
              "a key with two claimants has no plan");
  loka::app::scene::testing::ProbeArmSeatDefinition third(&selection, arms, 3);
  third.setNodeTag(8101);
  loka::app::Fragment appended;
  appended << third;
  loka::app::scene::NodeComposition nestedComposition;
  nestedComposition.declare(appended);
  nestedComposition.assignCompositionSeatSlots();
  seats.append(nestedComposition.root());
  LOKA_VERIFY(seats.plans().size() == 3 && seats.findPlan(key) == 0 &&
              "an appended third claimant does not revive a collided key");
#endif
}

void testUnattachedSceneWhiteFlagWaitsForAttach()
{
  SceneTestSupport::RecordingPlatformController platform;
  g_refuseRootCreate = true;
  loka::app::scene::Scene scene((RefusableRootDefinition()));
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::prepareComposition(scene);
  const bool refused = loka::dsl::testing::SceneTestAccess::whiteFlagFullRebuildPending(scene);
  LOKA_VERIFY(refused);
  g_refuseRootCreate = false;
  scene.invalidate(loka::app::scene::NODE_DIRTY_LAYOUT);
  const bool prematureCompose = loka::dsl::testing::SceneTestAccess::composed(scene);
  const size_t prematureChanges = platform.changeCount();
  LOKA_VERIFY(!loka::dsl::testing::SceneTestAccess::rootNode(scene));
  LOKA_VERIFY(!prematureCompose && prematureChanges == 0);
  LOKA_VERIFY(!scene.hasPendingInvalidation());
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  const bool attachedCompose = loka::dsl::testing::SceneTestAccess::composed(scene);
  const size_t attachedChanges = platform.changeCount();
  LOKA_VERIFY(attachedCompose && attachedChanges == 1);
}
