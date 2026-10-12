#include "StrictNodeRouteTests.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/OwnershipDump.hpp"
#include "../example/MineSweeper/src/MainNode.hpp"
#include "app/scene/Scene.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "core/SmallObjectPool.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>

using namespace loka::app;
using namespace loka::app::scene;
namespace
{
  struct Source
  {
    enum { kAlignment = 16 };
    static bool denied;
    static void *acquire(size_t bytes) { return denied ? 0 : std::malloc(bytes); }
    static void release(void *storage) { std::free(storage); }
  };
  bool Source::denied = false;
  loka::core::SmallObjectPool<Source> nodePool;
  unsigned boots = 0;
  unsigned strictHeapAttempts = 0;
  bool nodeSite(const loka::core::LokaAllocationSite &site)
  { return std::strcmp(site.ownerTag, "NodeDefinition") == 0 || std::strcmp(site.ownerTag, "NodePartition") == 0; }
  void *allocate(size_t bytes, const loka::core::LokaAllocationSite &site)
  {
    if (std::strcmp(site.ownerTag, "NodePartition") == 0) ++boots;
    if (boots && std::strcmp(site.ownerTag, "NodeDefinition") == 0) ++strictHeapAttempts;
    return nodeSite(site) ? nodePool.allocate(bytes) : std::malloc(bytes);
  }
  void release(void *storage, const loka::core::LokaAllocationSite &site)
  { if (nodeSite(site)) nodePool.release(storage); else std::free(storage); }
  void collect(Node *node, std::vector<Node *> &slots, unsigned &cells, bool generation = false)
  {
    if (!node) return;
    generation = generation || node->nodeTypeKey() == NodeTypeToken<KeyedGenerationRoot>();
    if (generation) slots.push_back(node);
    if (node->asCellNode()) ++cells;
    INestable *nestable = node->asNestable();
    for (Node *child = nestable ? nestable->childrenHead() : 0; child; child = child->nextInComposition)
      collect(child, slots, cells, generation);
  }
  unsigned retiredNodes = 0;
  struct RetirementObserver : NodeContext
  {
    virtual void onFactChanged(NodeLifecycleFact, NodeLifecycleFact next)
    { if (next == NODE_FACT_RETIRED) ++retiredNodes; }
  };
  void newGame(Scene &scene)
  {
    Scene *input = &scene, *output = 0;
    loka::dsl::FlowError error;
    const loka::dsl::StepRunStatus clicked =
        loka::dsl::testing::ClickButton("MineSweeper.NewGameButton").run(input, output, error);
    LOKA_VERIFY(clicked == loka::dsl::FLOW_STEP_SUCCEEDED);
    for (int i = 0; i < 4; ++i)
      loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  }
}
void testStrictNodeRouteMineSweeperReuse()
{
  boots = retiredNodes = 0;
  const bool baseline = std::getenv("LOKA_PR6_BASELINE") != 0;
  loka::core::LokaAllocSetBackend(allocate, release);
  {
    NullScenePlatformController platform;
    Scene scene((Boundary<minesweeper::MainNode>(minesweeper::MainProps(12345))));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    std::vector<Node *> cold;
    unsigned cells = 0;
    collect(loka::dsl::testing::SceneTestAccess::rootNode(scene), cold, cells);
    LOKA_VERIFY(boots == 1);
    LOKA_VERIFY(cells == 64);
    LOKA_VERIFY(cold.size() == 195);
    std::sort(cold.begin(), cold.end(), std::less<Node *>());
    newGame(scene);
    std::vector<Node *> warm;
    cells = 0;
    collect(loka::dsl::testing::SceneTestAccess::rootNode(scene), warm, cells);
    for (size_t i = 0; i < warm.size(); ++i) warm[i]->setContext(new RetirementObserver());
    const loka::core::UpstreamGauge before = nodePool.snapshot();
    Source::denied = !baseline;
    newGame(scene);
    const loka::core::UpstreamGauge after = nodePool.snapshot();
    Source::denied = false;
    std::vector<Node *> reused;
    cells = 0;
    collect(loka::dsl::testing::SceneTestAccess::rootNode(scene), reused, cells);
    std::sort(reused.begin(), reused.end(), std::less<Node *>());
    std::fprintf(stderr, "strict New Game: node upstream attempts=%lu; slots=%lu; cells=%u; boots=%u\n",
                 after.attempts - before.attempts, static_cast<unsigned long>(reused.size()), cells, boots);
    std::fprintf(stderr, "node gauge: attempts=%lu bytes=%lu retired=%u baseline=%d\n",
                 after.attempts-before.attempts, after.bytesAcquired-before.bytesAcquired, retiredNodes, baseline ? 1 : 0);
    if (!baseline) LOKA_VERIFY(after.attempts == before.attempts);
    LOKA_VERIFY(retiredNodes == 195);
    LOKA_VERIFY(cells == 64);
    if (!baseline) LOKA_VERIFY(cold == reused);
    bool seen[64] = {false};
    const int mines[] = {1, 2, 9, 11, 15, 29, 33, 40, 57, 59};
    bool expected[64] = {false};
    for (size_t i = 0; i < sizeof(mines)/sizeof(mines[0]); ++i) expected[mines[i]] = true;
    for (size_t i = 0; i < reused.size(); ++i)
    {
      Node *node = reused[i];
      if (node->propsTypeId() != minesweeper::MineCellProps::staticTypeId()) continue;
      const minesweeper::MineCellProps &props = static_cast<minesweeper::MineCellNode *>(node)->props;
      const int index = props.cellIndex;
      LOKA_VERIFY(index >= 0 && index < 64 && !seen[index]);
      seen[index] = true;
      LOKA_VERIFY(props.isMine == expected[index]);
      int adjacent = 0;
      for (int row = index/8-1; row <= index/8+1; ++row)
        for (int col = index%8-1; col <= index%8+1; ++col)
          if (row >= 0 && row < 8 && col >= 0 && col < 8 && row*8+col != index && expected[row*8+col]) ++adjacent;
      LOKA_VERIFY(props.adjacentCount == adjacent);
    }
    for (int i = 0; i < 64; ++i) LOKA_VERIFY(seen[i]);
    LOKA_VERIFY(boots == 1);
  }
  loka::core::LokaAllocSetBackend(0, 0);
}

#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#endif
namespace
{
  struct RouteOwner : BoundaryNode
  {
    using BoundaryNode::retireDetachedNode;
    virtual void composeWithContext(ComponentContext &, ComposeEvent) {}
  };
  struct RefusalBuild : loka::app::scene::detail::NodeBuildOperation
  {
    RefusalBuild(RouteOwner &owner, int route) : owner(owner), route(route) {}
    virtual bool buildAndAttach(loka::app::scene::detail::NodeBuildTicket &ticket)
    {
      loka::app::scene::detail::SeatNodeStorageView storage(ticket);
      ComponentContext context;
      context.setBoundary(&this->owner);
      context.setStateOwner(&this->owner);
      context.setOwner(&this->owner);
      context.setNodeStorage(&storage);
      FragmentDefinition definition = Fragment() << Fragment();
      NodeMaterializationResult result = {0, false, false};
      const loka::core::UpstreamGauge before = nodePool.snapshot();
      if (this->route == 2)
      {
        result.root = this->owner.materializeLocalRebuildNode(context, &definition);
        result.allocationFailed = this->owner.composeResult().allocationFailed;
      }
      else
      {
        NodeComposition composition;
        if (this->route == 1) context.setBoundary(0);
        composition.setContext(&context);
        result = loka::app::scene::testing::NodeCompositionTestAccess::createNodeFromDefinitionResult(composition, &definition);
        context.setBoundary(&this->owner);
        composition.setContext(0);
      }
      const loka::core::UpstreamGauge after = nodePool.snapshot();
      LOKA_VERIFY(result.allocationFailed);
      LOKA_VERIFY(before.attempts == after.attempts);
      if (result.root) this->owner.retireDetachedNode(context, result.root);
      return false;
    }
    RouteOwner &owner;
    const int route;
  };
  void refusalRoute(int route)
  {
    loka::core::LokaAllocSetBackend(allocate, release);
    {
      RouteOwner owner;
      loka::app::scene::detail::SeatLayoutTable table;
      LOKA_VERIFY(table.append(loka::app::scene::detail::NodeSlotLayout::of<FragmentNode>(1)));
      const loka::app::scene::detail::SeatReservation *seat = owner.installSeatReservation(table);
      LOKA_VERIFY(seat);
      RefusalBuild build(owner, route);
      const bool completed = seat->partition().build(table.layouts(), table.count(), build);
      LOKA_VERIFY(!completed);
      owner.drainRetiredSubtreesAtNextTrackerRun();
      const loka::app::scene::detail::NodePartition::BuildCapacity capacity = seat->partition().buildCapacity(table.layouts(), table.count());
      LOKA_VERIFY(capacity == loka::app::scene::detail::NodePartition::BUILD_AVAILABLE);
    }
    loka::core::LokaAllocSetBackend(0, 0);
  }
#if !defined(NDEBUG) && defined(__linux__)
  void verifyCapacityDeath(pid_t pid, int readFd)
  {
    char diagnostic[2048];
    size_t used = 0;
    ssize_t bytes = 0;
    while (used + 1 < sizeof(diagnostic)
           && (bytes = read(readFd, diagnostic + used, sizeof(diagnostic) - used - 1)) > 0)
      used += static_cast<size_t>(bytes);
    diagnostic[used] = 0;
    close(readFd);
    int status = 0;
    const pid_t waited = waitpid(pid, &status, 0);
    LOKA_VERIFY(waited == pid);
    LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    LOKA_VERIFY(std::strstr(diagnostic, "seat node declaration incomplete") != 0);
  }
#endif
  void checkedRefusal(int route)
  {
#if !defined(NDEBUG) && defined(__linux__)
    int diagnostic[2];
    LOKA_VERIFY(pipe(diagnostic) == 0);
    const pid_t pid = fork();
    LOKA_VERIFY(pid >= 0);
    if (!pid)
    {
      close(diagnostic[0]);
      LOKA_VERIFY(dup2(diagnostic[1], STDERR_FILENO) >= 0);
      close(diagnostic[1]);
      refusalRoute(route);
      _exit(0);
    }
    close(diagnostic[1]);
    verifyCapacityDeath(pid, diagnostic[0]);
#elif defined(NDEBUG)
    refusalRoute(route);
#else
    // Debug without fork (macOS CI): keep the helper referenced for -Wunused-function.
    (void)route;
    (void)&refusalRoute;
#endif
  }
}
void testStrictNodeRouteArenaRefusal() { checkedRefusal(0); }
void testStrictNodeRouteRecursiveRefusal() { checkedRefusal(1); }
void testStrictNodeRouteLocalRefusal() { checkedRefusal(2); }

namespace
{
  struct ShortTag {};
  class ShortComponent;
  struct ShortProps : NodePropsBase<ShortProps>
  {
    typedef ShortTag TypeTag;
    typedef ShortComponent NodeType;
    bool operator<(const PropsBase &) const { return false; }
  };
  unsigned coldRefusalConstructions = 0;
  class ShortComponent : public ComponentNodeWithProps<ShortProps>
  {
  public:
    explicit ShortComponent(const ShortProps &props) : ComponentNodeWithProps<ShortProps>(props) { ++coldRefusalConstructions; }
    virtual void composeChildren(NodeComposition &composition)
    { composition.declare(Fragment() << Fragment()); }
  };
  struct ShortOwner : BoundaryNodeFor<ShortOwner>
  {
    explicit ShortOwner(const BoundaryPropsFor<ShortOwner> &p) : BoundaryNodeFor<ShortOwner>(p)
    { this->state(this->key, 0); }
    void composeNode(NodeComposition &composition)
    {
      typedef reservation::Nodes<ShortComponent, 1, reservation::Nodes<FragmentNode, 1> > Payload;
      composition.declare(Fragment() << Keyed(*this->key.state(), this, &ShortOwner::arm, reservation::SeatNodes<Payload>()));
    }
    void arm(NodeComposition &composition) { composition.declare(Component(ShortProps())); }
    const loka::app::scene::detail::SeatReservation *installedReservation()
    {
      NodeDefinitionBase *seat = this->composition().root()->asNestableDefinition()->childrenHead();
      return seat->asBranchSeatDefinition()->seatReservation();
    }
    NodeState<int> key;
  };
  void keyedRefusal()
  {
    boots = strictHeapAttempts = 0;
    loka::core::LokaAllocSetBackend(allocate, release);
    {
      NullScenePlatformController platform;
      Scene scene((Boundary<ShortOwner>()));
      scene.mount(&platform);
      loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
      BoundaryNode *owner = loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
      LOKA_VERIFY(owner && owner->composeResult().allocationFailed);
      std::vector<Node *> published;
      unsigned cells = 0;
      collect(loka::dsl::testing::SceneTestAccess::rootNode(scene), published, cells);
      LOKA_VERIFY(published.empty());
      LOKA_VERIFY(strictHeapAttempts == 0);
      const loka::app::scene::detail::SeatReservation *seat = static_cast<ShortOwner *>(owner)->installedReservation();
      LOKA_VERIFY(seat);
      const unsigned before = coldRefusalConstructions;
      for (unsigned turn = 0; turn != 5; ++turn)
      {
        loka::dsl::testing::SceneTestAccess::flushInvalidation(scene);
      }
      // Cold replay is shared with Keyed: drain-only never retries, while an
      // external update may retry the refused mount by design (#704 M5).
      LOKA_VERIFY(coldRefusalConstructions == before && !scene.hasPendingInvalidation());
      owner->drainRetiredSubtreesAtNextTrackerRun();
      const loka::app::scene::detail::NodePartition::BuildCapacity returned =
          seat->partition().buildCapacity(seat->layoutTable().layouts(), seat->layoutTable().count());
      LOKA_VERIFY(returned == loka::app::scene::detail::NodePartition::BUILD_AVAILABLE);
      LOKA_VERIFY(boots == 1);
    }
    loka::core::LokaAllocSetBackend(0, 0);
  }
}
void testStrictNodeRouteKeyedAttachRefusal()
{
#if !defined(NDEBUG) && defined(__linux__)
  int diagnostic[2];
  LOKA_VERIFY(pipe(diagnostic) == 0);
  const pid_t pid = fork();
  LOKA_VERIFY(pid >= 0);
  if (!pid)
  {
    close(diagnostic[0]);
    LOKA_VERIFY(dup2(diagnostic[1], STDERR_FILENO) >= 0);
    close(diagnostic[1]);
    keyedRefusal();
    _exit(0);
  }
  close(diagnostic[1]);
  verifyCapacityDeath(pid, diagnostic[0]);
#elif defined(NDEBUG)
  keyedRefusal();
#else
  (void)&keyedRefusal;
#endif
}

namespace
{
  struct DeleteGuardNode : Node
  {
    explicit DeleteGuardNode(int &deaths) : deaths(deaths) {}
    virtual ~DeleteGuardNode() { ++this->deaths; }
    int &deaths;
  };
  struct DeleteGuardBuild : loka::app::scene::detail::NodeBuildOperation
  {
    DeleteGuardBuild() : deaths(0), node(0) {}
    DeleteGuardNode *construct(void *storage) { return new (storage) DeleteGuardNode(this->deaths); }
    virtual bool buildAndAttach(loka::app::scene::detail::NodeBuildTicket &ticket)
    { this->node = ticket.create<DeleteGuardNode>(*this); return this->node != 0; }
    int deaths;
    DeleteGuardNode *node;
  };
}
void testStrictNodeRoutePartitionDestroysThroughItsOwnDoor()
{
  loka::app::scene::detail::NodePartition bank;
  const loka::app::scene::detail::NodeSlotLayout layout = loka::app::scene::detail::NodeSlotLayout::of<DeleteGuardNode>(1);
  LOKA_VERIFY(bank.boot(&layout, 1));
  DeleteGuardBuild build;
  LOKA_VERIFY(bank.build(&layout, 1, build));
  LOKA_VERIFY(build.node->arenaOwner() == 0 && build.node->partitionOwner() == &bank);
  // The partition remains the sole destruction and storage-return owner.
  LOKA_VERIFY(bank.destroy(build.node, layout));
  LOKA_VERIFY(build.deaths == 1);
  LOKA_VERIFY(bank.build(&layout, 1, build));
  LOKA_VERIFY(bank.destroy(build.node, layout));
  LOKA_VERIFY(build.deaths == 2);
}

namespace
{
  unsigned mountStateRefusals = 0, mountPartitionBoots = 0;
  void *refuseFirstMountState(size_t bytes, const loka::core::LokaAllocationSite &site)
  {
    if (std::strcmp(site.ownerTag, "NodePartition") == 0) ++mountPartitionBoots;
    if (!mountStateRefusals && std::strcmp(site.ownerTag, "StateOwner") == 0)
    {
      ++mountStateRefusals;
      return 0;
    }
    return std::malloc(bytes);
  }
  void freeMountState(void *storage, const loka::core::LokaAllocationSite &) { std::free(storage); }
  class RetryMountComponent;
  struct RetryMountTag {};
  struct RetryMountProps : NodePropsBase<RetryMountProps>
  {
    typedef RetryMountTag TypeTag;
    typedef RetryMountComponent NodeType;
    bool operator<(const PropsBase &) const { return false; }
  };
  class RetryMountComponent : public ComponentNodeWithProps<RetryMountProps>
  {
  public:
    explicit RetryMountComponent(const RetryMountProps &props) : ComponentNodeWithProps<RetryMountProps>(props)
    { this->state(this->value, 73); }
    virtual void composeChildren(NodeComposition &composition)
    {
      if (this->value.isValid())
        composition.declare(Fragment());
    }
    NodeState<int> value;
  };
  struct RetryMountOwner : BoundaryNodeFor<RetryMountOwner>
  {
    explicit RetryMountOwner(const BoundaryPropsFor<RetryMountOwner> &props) : BoundaryNodeFor<RetryMountOwner>(props)
    { this->state(this->key, 0); }
    void composeNode(NodeComposition &composition)
    {
      typedef reservation::Nodes<RetryMountComponent, 1, reservation::Nodes<FragmentNode, 1> > Payload;
      composition.declare(Fragment() << Keyed(*this->key.state(), this, &RetryMountOwner::arm, reservation::SeatNodes<Payload>()));
    }
    void arm(NodeComposition &composition) { composition.declare(Component(RetryMountProps())); }
    NodeState<int> key;
  };
}
void testStrictNodeRouteInitialAttachRetry()
{
  using loka::dsl::testing::SceneTestAccess;
  using loka::dsl::testing::OwnershipDump;
  mountStateRefusals = mountPartitionBoots = 0;
  loka::core::LokaAllocSetBackend(&refuseFirstMountState, &freeMountState);
  {
    NullScenePlatformController platform;
    Scene scene((Boundary<RetryMountOwner>()));
    scene.mount(&platform);
    SceneTestAccess::updateAttached(scene, true);
    RetryMountOwner *owner = static_cast<RetryMountOwner *>(SceneTestAccess::rootBoundary(scene));
    LOKA_VERIFY(mountStateRefusals == 1);
    LOKA_VERIFY(owner && owner->composeResult().allocationFailed);
    LOKA_VERIFY(owner->childrenHead() == 0);
    // Read before reclaim: this detects retired runtime identities without
    // dereferencing dangling pointers merely to establish the HEAD failure.
    const std::string refusedRows = OwnershipDump::dumpSeatRuntime(*owner);
    LOKA_VERIFY(refusedRows.empty());
    owner->drainRetiredSubtreesAtNextTrackerRun();
    const std::string reclaimedRows = OwnershipDump::dumpSeatRuntime(*owner);
    LOKA_VERIFY(reclaimedRows.empty());
    SceneTestAccess::updateAttached(scene, true);
    LOKA_VERIFY(owner->childrenHead() != 0);
    LOKA_VERIFY(!owner->composeResult().allocationFailed);
    std::vector<Node *> recovered;
    unsigned cells = 0, values = 0;
    collect(owner, recovered, cells);
    for (size_t i = 0; i < recovered.size(); ++i)
      if (recovered[i]->propsTypeId() == RetryMountProps::staticTypeId())
      {
        RetryMountComponent *component = static_cast<RetryMountComponent *>(recovered[i]);
        LOKA_VERIFY(component->value.isValid() && component->value.get() == 73);
        ++values;
      }
    LOKA_VERIFY(values == 1 && mountPartitionBoots == 1);
    const std::string recoveredRows = OwnershipDump::dumpSeatRuntime(*owner);
    LOKA_VERIFY(recoveredRows == "seat parent=attached active=attached\n");
    // The recovered reservation must still observe later replacement demand.
    {
      loka::core::StateTrackerGuard transaction(owner->tracker());
      owner->key.set(1);
    }
    for (int i = 0; i < 4; ++i)
    {
      owner->drainRetiredSubtreesAtNextTrackerRun();
      SceneTestAccess::updateAttached(scene, true);
    }
    const std::string replacedRows = OwnershipDump::dumpSeatRuntime(*owner);
    LOKA_VERIFY(replacedRows == recoveredRows);
    LOKA_VERIFY(owner->childrenHead() != 0 && mountPartitionBoots == 1);
  }
  loka::core::LokaAllocSetBackend(0, 0);
}

namespace
{
  unsigned snapshotDeaths = 0;
  class SnapshotComponent;
  struct SnapshotTag {};
  struct SnapshotProps : NodePropsBase<SnapshotProps>
  {
    typedef SnapshotTag TypeTag;
    typedef SnapshotComponent NodeType;
    bool operator<(const PropsBase &) const { return false; }
  };
  class SnapshotComponent : public ComponentNodeWithProps<SnapshotProps>
  {
  public:
    explicit SnapshotComponent(const SnapshotProps &props) : ComponentNodeWithProps<SnapshotProps>(props) {}
    virtual ~SnapshotComponent() { ++snapshotDeaths; }
    virtual void composeChildren(NodeComposition &composition) { composition.declare(Fragment()); }
  };
  struct SnapshotOwner : BoundaryNodeFor<SnapshotOwner>
  {
    explicit SnapshotOwner(const BoundaryPropsFor<SnapshotOwner> &props) : BoundaryNodeFor<SnapshotOwner>(props)
    { this->state(this->key, 0); }
    virtual bool flushViewDirtyImmediately(NodeDirtyFlags) const { return false; }
    void composeNode(NodeComposition &composition)
    {
      typedef reservation::Nodes<SnapshotComponent, 1, reservation::Nodes<FragmentNode, 2> > Payload;
      composition.declare(Fragment() << Keyed(*this->key.state(), this, &SnapshotOwner::arm, reservation::SeatNodes<Payload>()));
    }
    void arm(NodeComposition &composition) { composition.declare(Fragment() << Component(SnapshotProps())); }
    using BoundaryNode::retireOwnedNodeGeneration;
    using BoundaryNode::retireDetachedNode;
    using BoundaryNode::composeTree;
    using ComposableNode::composition;
    NodeState<int> key;
  };
  struct SnapshotReadmission : loka::app::scene::detail::NodeBuildOperation
  {
    SnapshotReadmission() : node(0) {}
    SnapshotComponent *construct(void *storage) { return new (storage) SnapshotComponent(SnapshotProps()); }
    virtual bool buildAndAttach(loka::app::scene::detail::NodeBuildTicket &ticket)
    {
      this->node = ticket.create<SnapshotComponent>(*this);
      return this->node != 0;
    }
    SnapshotComponent *node;
  };
  void snapshotPartitionChild(bool planned)
  {
    using namespace loka::app::scene::detail;
    using loka::dsl::testing::SceneTestAccess;
    snapshotDeaths = 0;
    NullScenePlatformController platform;
    Scene scene((Boundary<SnapshotOwner>()));
    scene.mount(&platform);
    SceneTestAccess::updateAttached(scene, true);
    SnapshotOwner *owner = static_cast<SnapshotOwner *>(SceneTestAccess::rootBoundary(scene));
    Node *parent = owner->childrenHead();
    Node *generationRoot = parent->asNestable()->childrenHead();
    const SeatReservation *seat = owner->composition().root()->asNestableDefinition()->childrenHead()->asBranchSeatDefinition()->seatReservation();
    SeatBuildRequest &pending = seat->request();
    LOKA_VERIFY(parent->arenaOwner() == owner->nodeArena());
    LOKA_VERIFY(generationRoot->partitionOwner() == &seat->partition());
    if (planned)
    {
      LOKA_VERIFY(owner->nodeArena()->reserveReclaimScratch(16));
      LOKA_VERIFY(seat->partition().reserveReclaimScratch(16));
    }
    // The root travels inside its arena parent's snapshot, not as a separately
    // queued partition root. The request must complete from that generation drain.
    pending.mark(); pending.recordReturn(generationRoot);
    Node *detached = owner->detachChildren();
    LOKA_VERIFY(detached == parent);
    ComponentContext context;
    context.setBoundary(owner);
    owner->composeTree(parent, context, COMPOSE_EVENT_DETACH, owner);
    owner->retireDetachedNode(context, parent);
    owner->retireOwnedNodeGeneration(context);
    LOKA_VERIFY(snapshotDeaths == 0 && pending.retiring());
    owner->drainRetiredSubtreesAtNextTrackerRun();
    LOKA_VERIFY(snapshotDeaths == 1);
    const NodePartition::BuildCapacity capacity = seat->partition().buildCapacity(seat->layoutTable().layouts(), seat->layoutTable().count());
    LOKA_VERIFY(capacity == NodePartition::BUILD_AVAILABLE);
    LOKA_VERIFY(!pending.retiring());
    SnapshotReadmission next;
    LOKA_VERIFY(pending.admit(seat->layoutTable(), next));
    LOKA_VERIFY(next.node && next.node->partitionOwner() == &seat->partition());
    owner->retireDetachedNode(context, next.node);
    owner->drainRetiredSubtreesAtNextTrackerRun();
    LOKA_VERIFY(snapshotDeaths == 2);
  }
}
void testStrictNodeRouteLegacyGenerationPartitionChild() { snapshotPartitionChild(false); }
void testStrictNodeRoutePlannedGenerationPartitionChild() { snapshotPartitionChild(true); }
