#include "NodeObservedUsesTests.hpp"
#include "support/TestVerify.hpp"
#include "support/ObservedUseCounts.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "testing/scene/NodeObservedUsesTestAccess.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/Keyed.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/scene/node/ComponentNode.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "core/util/StateTrackerGuard.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using loka::app::testing::NodeObservedUsesTestAccess;
  using loka::dsl::testing::SceneTestAccess;

  const NodeDirtyFlags both = static_cast<NodeDirtyFlags>(NODE_DIRTY_PROPS | NODE_DIRTY_LAYOUT);

  class EmptyBoundary : public BoundaryNodeFor<EmptyBoundary>
  {
  public:
    explicit EmptyBoundary(const BoundaryPropsFor<EmptyBoundary> &props = BoundaryPropsFor<EmptyBoundary>())
        : BoundaryNodeFor<EmptyBoundary>(props) {}
    virtual void composeNode(NodeComposition &) {}
    using BoundaryNode::composeTree;
  };

  class InputContext : public NodeContext
  {
  public:
    InputContext() : seen(NODE_DIRTY_NONE) {}
    virtual short layout(IPlatformController *, LayoutState &state)
    {
      this->seen = state.inputs;
      return 17;
    }
    NodeDirtyFlags seen;
  };

  void consumeMark(Node &node)
  {
    node.setContext(new InputContext());
    LayoutState state;
    node.layout(0, state);
    node.setContext(0);
  }

  struct LifecycleTrace
  {
    LifecycleTrace() : tracker(0), source(0), retired(0), attaches(0), attachMark(NODE_DIRTY_NONE) {}
    StateTracker *tracker;
    MutableState<String> *source;
    unsigned retired;
    unsigned attaches;
    NodeDirtyFlags attachMark;
  };
  LifecycleTrace *activeTrace = 0;

  class TracedText : public TextNode
  {
  public:
    explicit TracedText(const TextProps &props) : TextNode(props) {}
    virtual void onLifecycleFactChanged(NodeLifecycleFact, NodeLifecycleFact next)
    {
      LOKA_VERIFY(activeTrace != 0);
      if (next == NODE_FACT_ATTACHED)
      {
        ++activeTrace->attaches;
        activeTrace->attachMark = NodeObservedUsesTestAccess::mark(*this);
      }
      if (next == NODE_FACT_RETIRED && activeTrace->source)
      {
        ++activeTrace->retired;
        LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(*this) == 0);
        StateTrackerGuard guard(activeTrace->tracker);
        activeTrace->source->set(String::Literal("retirement hook write"));
      }
    }
  };

  class RetainedTextComponent;
  struct RetainedTextComponentTag {};
  struct RetainedTextComponentProps : NodePropsBase<RetainedTextComponentProps>
  {
    typedef RetainedTextComponentTag TypeTag;
    typedef RetainedTextComponent NodeType;
    explicit RetainedTextComponentProps(State<String> *value = 0) : source(value) {}
    bool operator<(const PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId()) return false;
      return this->source < static_cast<const RetainedTextComponentProps &>(rhs).source;
    }
    State<String> *source;
  };
  class RetainedTextComponent : public ComponentNodeWithProps<RetainedTextComponentProps>
  {
  public:
    explicit RetainedTextComponent(const RetainedTextComponentProps &props)
        : ComponentNodeWithProps<RetainedTextComponentProps>(props) {}
    virtual void composeChildren(NodeComposition &composition)
    {
      composition.declare(NodeDefinition<TextProps, TracedText>(TextProps(this->props.source)));
    }
  };

  template <bool Destroy> class LifecycleRoot : public BoundaryNodeFor<LifecycleRoot<Destroy> >
  {
  public:
    NodeState<bool> shown;
    NodeState<String> text;
    explicit LifecycleRoot(const BoundaryPropsFor<LifecycleRoot<Destroy> > &props)
        : BoundaryNodeFor<LifecycleRoot<Destroy> >(props)
    {
      this->state(this->shown, true);
      this->state(this->text, String::Literal("before"));
    }
    virtual bool flushViewDirtyImmediately(NodeDirtyFlags) const { return false; }
    virtual void composeNode(NodeComposition &composition)
    {
      if (Destroy)
        composition.declare(Show(*this->shown.state()).destroyOnDetach() <<
          NodeDefinition<TextProps, TracedText>(TextProps(this->text.state())));
      else
        composition.declare(Show(*this->shown.state()) <<
          NodeDefinition<RetainedTextComponentProps, RetainedTextComponent>(
              RetainedTextComponentProps(this->text.state())));
    }
  };

  Node *findText(Node *node)
  {
    if (!node) return 0;
    if (node->nodeTypeKey() == NodeTypeToken<TextNode>()) return node;
    INestable *nestable = node->asNestable();
    for (Node *child = nestable ? nestable->childrenHead() : 0; child; child = child->nextInComposition)
    {
      Node *found = findText(child);
      if (found) return found;
    }
    return 0;
  }
}

void testObservedUsesSharedStateMarksBothNodes()
{
  std::printf("Observed-use sizeof host: Node=%lu ObservedUses=%lu ObservedUse=%lu LayoutState=%lu entry=%lu binding=%lu\n",
      static_cast<unsigned long>(sizeof(Node)), static_cast<unsigned long>(sizeof(ObservedUses)),
      static_cast<unsigned long>(sizeof(ObservedUse)), static_cast<unsigned long>(sizeof(LayoutState)),
      static_cast<unsigned long>(sizeof(BoundaryObservedStateEntry)),
      static_cast<unsigned long>(sizeof(BoundaryObservedStateBinding)));
  MutableState<String> source(String::Literal("before"));
  PushStateTracker tracker;
  tracker.addState(&source);
  TextNode first((TextProps(&source))), second((TextProps(&source)));
  EmptyBoundary owner;
  BoundaryNode::declareBoundaryDirtySources(&first, &owner);
  const NodeDirtyFlags singleUnion = owner.observedDirtyFlags();
  BoundaryNode::declareBoundaryDirtySources(&second, &owner);
  LOKA_VERIFY(owner.observedDirtyFlags() == singleUnion);
  LOKA_VERIFY(singleUnion != NODE_DIRTY_NONE);
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(first) == 1);
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(second) == 1);
  { StateTrackerGuard guard(&tracker); source.set(String::Literal("after")); }
  LOKA_VERIFY(NodeObservedUsesTestAccess::mark(first) == singleUnion);
  LOKA_VERIFY(NodeObservedUsesTestAccess::mark(second) == singleUnion);
  LOKA_VERIFY(owner.observedDirtyFlags() == singleUnion);
  LifecycleFactTestAccess::MarkSubtreeRetired(&first);
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(first) == 0);
  LOKA_VERIFY(owner.observedDirtyFlags() == singleUnion);
  consumeMark(second);
  { StateTrackerGuard guard(&tracker); source.set(String::Literal("survivor")); }
  LOKA_VERIFY(NodeObservedUsesTestAccess::mark(second) == singleUnion);
  owner.clearObservedStateEntries();
  owner.clearObservedDirtyFlags();
  TextNode propsOnly((TextProps(&source))), layoutOnly((TextProps(&source)));
  LOKA_VERIFY(owner.registerObservedState(&source, NODE_DIRTY_PROPS, &propsOnly));
  LOKA_VERIFY(owner.registerObservedState(&source, NODE_DIRTY_LAYOUT, &layoutOnly));
  LOKA_VERIFY(owner.observedDirtyFlags() == both);
  { StateTrackerGuard guard(&tracker); source.set(String::Literal("mixed flags")); }
  LOKA_VERIFY(NodeObservedUsesTestAccess::mark(propsOnly) == NODE_DIRTY_PROPS);
  LOKA_VERIFY(NodeObservedUsesTestAccess::mark(layoutOnly) == NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(owner.observedDirtyFlags() == both);
  owner.clearObservedStateEntries();
}

void testObservedUsesRetiredBorrowerWithdrawsBeforeCommit()
{
  MutableState<String> source(String::Literal("before"));
  PushStateTracker tracker;
  tracker.addState(&source);
  EmptyBoundary owner;
  TextNode *leaf = new TextNode(TextProps(&source));
  BoundaryNode::declareBoundaryDirtySources(leaf, &owner);
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(*leaf) == 1);
  LifecycleFactTestAccess::MarkSubtreeRetired(leaf);
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(*leaf) == 0);
  { StateTrackerGuard guard(&tracker); source.set(String::Literal("retired, alive")); }
  delete leaf;
  // Repeat after reclaim: ASan also checks that no callback borrows freed storage.
  { StateTrackerGuard guard(&tracker); source.set(String::Literal("reclaimed")); }
  owner.clearObservedStateEntries();
}

void testObservedUsesSceneRetireHookWithdrawsBeforeWrite()
{
  LifecycleTrace trace;
  activeTrace = &trace;
  NullScenePlatformController platform;
  Scene scene((Boundary<LifecycleRoot<true> >()));
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  LifecycleRoot<true> *root = static_cast<LifecycleRoot<true> *>(SceneTestAccess::rootBoundary(scene));
  LOKA_VERIFY(findText(root) != 0);
  trace.tracker = root->tracker();
  trace.source = static_cast<MutableState<String> *>(root->text.state());
  { StateTrackerGuard guard(root->tracker()); root->shown.set(false); }
  scene.flushInvalidation();
  LOKA_VERIFY(trace.retired == 1);
  LOKA_VERIFY(root->text.get().equals(String::Literal("retirement hook write")));
  trace.source = 0;
  SceneTestAccess::unmount(scene);
  activeTrace = 0;
}

void testObservedUsesParkedWriteAndReattach()
{
  // Construction already reports ATTACHED; the first attach application still
  // marks even though it does not produce a fact-change callback.
  TextNode initial((TextProps("initial")));
  LOKA_VERIFY(NodeObservedUsesTestAccess::mark(initial) == NODE_DIRTY_NONE);
  NotifySubtreeNodeAttached(&initial);
  LOKA_VERIFY(NodeObservedUsesTestAccess::mark(initial) == both);
  LifecycleTrace trace;
  activeTrace = &trace;
  NullScenePlatformController platform;
  Scene scene((Boundary<LifecycleRoot<false> >()));
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  LifecycleRoot<false> *root = static_cast<LifecycleRoot<false> *>(SceneTestAccess::rootBoundary(scene));
  Node *leaf = findText(root);
  LOKA_VERIFY(leaf != 0);
  consumeMark(*leaf);
  { StateTrackerGuard guard(root->tracker()); root->shown.set(false); }
  scene.flushInvalidation();
  LOKA_VERIFY(leaf->lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(*leaf) == 0);
  { StateTrackerGuard guard(root->tracker()); root->text.set(String::Literal("parked write")); }
  LOKA_VERIFY(NodeObservedUsesTestAccess::mark(*leaf) == NODE_DIRTY_NONE);
  const unsigned before = trace.attaches;
  { StateTrackerGuard guard(root->tracker()); root->shown.set(true); }
  scene.flushInvalidation();
  LOKA_VERIFY(findText(root) == leaf);
  LOKA_VERIFY(trace.attaches == before + 1);
  LOKA_VERIFY(trace.attachMark == both);
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(*leaf) == 1);
  SceneTestAccess::unmount(scene);
  activeTrace = 0;
}

void testObservedUsesPropsSuccessAndFailure()
{
  TextNode leaf((TextProps("before")));
  leaf.setPropsTypeId(TextProps::staticTypeId());
  Text next("after");
  LOKA_VERIFY(NodeObservedUsesTestAccess::mark(leaf) == NODE_DIRTY_NONE);
  Button wrong("wrong type");
  LOKA_VERIFY(!wrong.applyPropsToNode(&leaf));
  LOKA_VERIFY(NodeObservedUsesTestAccess::mark(leaf) == NODE_DIRTY_NONE);
  LOKA_VERIFY(next.applyPropsToNode(&leaf));
  LOKA_VERIFY(NodeObservedUsesTestAccess::mark(leaf) == both);

  // This matches the props type but its production applier deliberately refuses:
  // runtime generation roots are transferred once, never props-reconciled.
  NodeDefinition<KeyedGenerationProps, KeyedGenerationRoot> generation((KeyedGenerationProps()));
  Node *root = generation.create();
  LOKA_VERIFY(root != 0);
  LOKA_VERIFY(generation.isCompatibleWithNode(root));
  LOKA_VERIFY(NodeObservedUsesTestAccess::mark(*root) == NODE_DIRTY_NONE);
  LOKA_VERIFY(!generation.applyPropsToNode(root));
  LOKA_VERIFY(NodeObservedUsesTestAccess::mark(*root) == NODE_DIRTY_NONE);
  DestroyHeapNode(root);
}

void testObservedUsesLayoutInputsAreNodeLocal()
{
  TextNode first((TextProps("first"))), sibling((TextProps("sibling")));
  first.setPropsTypeId(TextProps::staticTypeId());
  InputContext *firstContext = new InputContext();
  InputContext *siblingContext = new InputContext();
  first.setContext(firstContext);
  sibling.setContext(siblingContext);
  Text changed("changed");
  LOKA_VERIFY(changed.applyPropsToNode(&first));
  LayoutState shared;
  LOKA_VERIFY(shared.inputs == NODE_DIRTY_NONE);
  LOKA_VERIFY(first.layout(0, shared) == 17);
  LOKA_VERIFY(firstContext->seen == both);
  LOKA_VERIFY(shared.inputs == NODE_DIRTY_NONE);
  LOKA_VERIFY(NodeObservedUsesTestAccess::mark(first) == NODE_DIRTY_NONE);
  sibling.layout(0, shared);
  LOKA_VERIFY(siblingContext->seen == NODE_DIRTY_NONE);
  shared.inputs = NODE_DIRTY_PROPS;
  first.layout(0, shared);
  LOKA_VERIFY(firstContext->seen == NODE_DIRTY_NONE);
  LOKA_VERIFY(shared.inputs == NODE_DIRTY_PROPS);
}

void testObservedUsesFinishPassPrunesOneSharedBorrower()
{
  MutableState<String> source(String::Literal("before"));
  TextNode first((TextProps(&source))), second((TextProps(&source)));
  EmptyBoundary owner;
  owner.beginObservedStatePass();
  BoundaryNode::declareBoundaryDirtySources(&first, &owner);
  BoundaryNode::declareBoundaryDirtySources(&second, &owner);
  owner.completeObservedStatePass();
  const NodeDirtyFlags originalUnion = owner.observedDirtyFlags();
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(first) == 1);
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(second) == 1);
  // A following declaration walk omits the parked borrower while the sibling
  // still renews the shared subscription. Subscription-only pruning misses this.
  owner.beginObservedStatePass();
  BoundaryNode::declareBoundaryDirtySources(&second, &owner);
  owner.completeObservedStatePass();
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(first) == 0);
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(second) == 1);
  LOKA_VERIFY(owner.observedDirtyFlags() == originalUnion);
  owner.beginObservedStatePass();
  owner.completeObservedStatePass();
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(second) == 0);
}

#include "support/LokaAllocFailure.hpp"
namespace
{
  State<String> *refusalSource = 0;
  class RefusalRoot : public BoundaryNodeFor<RefusalRoot>
  {
  public:
    explicit RefusalRoot(const BoundaryPropsFor<RefusalRoot> &props)
        : BoundaryNodeFor<RefusalRoot>(props) {}
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Row() << Text(refusalSource) << Text(refusalSource));
    }
  };
}

void testObservedUsesAllocationRefusesWholeCandidate()
{
  for (int failure = 1; failure <= 2; ++failure)
  {
    loka::core::testing::failLokaAllocRaw("Node", "ObservedUse", failure);
    {
      MutableState<String> source(String::Literal("borrowed"));
      refusalSource = &source;
      NullScenePlatformController platform;
      {
        Scene scene((Boundary<RefusalRoot>()));
        scene.mount(&platform);
        SceneTestAccess::updateAttached(scene, true);
        BoundaryNode *root = SceneTestAccess::rootBoundary(scene);
        LOKA_VERIFY(root != 0);
        LOKA_VERIFY(root->childrenHead() == 0);
        LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(*root) == 0);
        SceneTestAccess::unmount(scene);
      }
      // The fail-count hook refuses once. A new mount is an explicit outside
      // retry, after the incomplete candidate has been discarded in full.
      {
        Scene retry((Boundary<RefusalRoot>()));
        retry.mount(&platform);
        SceneTestAccess::updateAttached(retry, true);
        BoundaryNode *root = SceneTestAccess::rootBoundary(retry);
        LOKA_VERIFY(root != 0);
        LOKA_VERIFY(findText(root) != 0);
        const unsigned restoredUses = ObservedUseTestSupport::activeUses(root);
        std::printf("ObservedUse refusal %d recovered uses=%u\n", failure, restoredUses);
        LOKA_VERIFY(restoredUses == 2);
        SceneTestAccess::unmount(retry);
      }
      refusalSource = 0;
    }
    LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
    loka::core::testing::allowLokaAllocRaw();
  }
}

namespace
{
  class TwoBorrowedSources : public Node
  {
  public:
    TwoBorrowedSources(StateBase &first, StateBase &second) : first_(first), second_(second) {}
    virtual void declareDirtySources(DirtySourceRegistrar &registrar)
    {
      registrar.markDirtyOnChange(&this->first_, NODE_DIRTY_PROPS);
      registrar.markDirtyOnChange(&this->second_, NODE_DIRTY_LAYOUT);
    }
  private:
    StateBase &first_;
    StateBase &second_;
  };
}

void testObservedUsesRegistrationRefusalPublishesNoPartialUses()
{
  loka::core::testing::failLokaAllocRaw("Node", "ObservedUse", 0);
  {
    MutableState<bool> previous(false), first(false), second(false);
    EmptyBoundary owner;
    TwoBorrowedSources node(first, second);
    owner.beginObservedStatePass();
    LOKA_VERIFY(owner.registerObservedState(&previous, NODE_DIRTY_PROPS, &node));
    owner.completeObservedStatePass();
    LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(node) == 1);

    // A refused declaration keeps its old observation through pass completion;
    // neither new source may publish if the second row refuses.
    loka::core::testing::failLokaAllocRaw("Node", "ObservedUse", 2);
    owner.beginComposeResult(COMPOSE_EVENT_UPDATE, NODE_DIRTY_PROPS);
    owner.beginObservedStatePass();
    BoundaryNode::declareBoundaryDirtySources(&node, &owner);
    LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(node) == 1);
    owner.completeObservedStatePass();
    owner.completeComposeResult();
    LOKA_VERIFY(owner.composeResult().allocationFailed);
    LOKA_VERIFY(!owner.composeResult().composed);
    LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(node) == 1);

    // Outside retry reuses prepared storage only after the failed pass closed.
    owner.beginComposeResult(COMPOSE_EVENT_UPDATE, NODE_DIRTY_PROPS);
    owner.beginObservedStatePass();
    BoundaryNode::declareBoundaryDirtySources(&node, &owner);
    owner.completeObservedStatePass();
    owner.completeComposeResult();
    LOKA_VERIFY(!owner.composeResult().allocationFailed);
    LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(node) == 2);
    owner.clearObservedStateEntries();
  }
  LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
  loka::core::testing::allowLokaAllocRaw();
}

namespace
{
  class RepeatedSeatRefusalRoot : public BoundaryNodeFor<RepeatedSeatRefusalRoot>
  {
  public:
    NodeState<bool> shown;
    NodeState<bool> other;
    explicit RepeatedSeatRefusalRoot(const BoundaryPropsFor<RepeatedSeatRefusalRoot> &props)
        : BoundaryNodeFor<RepeatedSeatRefusalRoot>(props)
    {
      this->state(this->shown, true);
      this->state(this->other, false);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Row()
          << (Show(*this->shown.state()) << Text("seat child"))
          << (Show(*this->other.state()) << Text("hidden child")));
    }
  };
}

void testObservedUsesRepeatedSeatRefusal()
{
  loka::core::testing::failLokaAllocRaw("Node", "ObservedUse", 1);
  {
    NullScenePlatformController platform;
    Scene scene((Boundary<RepeatedSeatRefusalRoot>()));
    scene.mount(&platform);
    SceneTestAccess::updateAttached(scene, true);
    RepeatedSeatRefusalRoot *root =
        static_cast<RepeatedSeatRefusalRoot *>(SceneTestAccess::rootBoundary(scene));
    LOKA_VERIFY(root != 0);
    LOKA_VERIFY(root->childrenHead() == 0);
    LOKA_VERIFY(root->composeResult().allocationFailed);
    LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(*root) == 0);

    // Retry on the same owner: observation-pass admission happens again before
    // declaration. Its refusal must remain recorded through compose completion.
    loka::core::testing::failLokaAllocRaw("Node", "ObservedUse", 1);
    scene.requestInvalidate(NODE_DIRTY_CHILD);
    scene.flushInvalidation();
    LOKA_VERIFY(root->childrenHead() == 0);
    LOKA_VERIFY(root->composeResult().allocationFailed);
    LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(*root) == 0);

    loka::core::testing::failLokaAllocRaw("Node", "ObservedUse", 0);
    scene.requestInvalidate(NODE_DIRTY_CHILD);
    scene.flushInvalidation();
    Node *leaf = findText(root);
    LOKA_VERIFY(leaf != 0);
    LOKA_VERIFY(!root->composeResult().allocationFailed);
    LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(*root) == 2);
    { StateTrackerGuard guard(root->tracker()); root->shown.set(false); }
    scene.flushInvalidation();
    LOKA_VERIFY(findText(root) == 0);
    { StateTrackerGuard guard(root->tracker()); root->shown.set(true); }
    scene.flushInvalidation();
    LOKA_VERIFY(findText(root) == leaf);
    SceneTestAccess::unmount(scene);
  }
  LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
  loka::core::testing::allowLokaAllocRaw();
}

namespace
{
  class UpdateRefusalRoot : public BoundaryNodeFor<UpdateRefusalRoot>
  {
  public:
    NodeState<String> text;
    explicit UpdateRefusalRoot(const BoundaryPropsFor<UpdateRefusalRoot> &props)
        : BoundaryNodeFor<UpdateRefusalRoot>(props)
    {
      this->state(this->text, String::Literal("before refusal"));
    }
    virtual bool flushViewDirtyImmediately(NodeDirtyFlags) const { return false; }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Row() << Text(this->text.state()) << Text(this->text.state()));
    }
  };
}

void testObservedUsesRefusedUpdateKeepsStateRetry()
{
  loka::core::testing::failLokaAllocRaw("Node", "ObservedUse", 0);
  {
    NullScenePlatformController platform;
    Scene scene((Boundary<UpdateRefusalRoot>()));
    scene.mount(&platform);
    SceneTestAccess::updateAttached(scene, true);
    UpdateRefusalRoot *root =
        static_cast<UpdateRefusalRoot *>(SceneTestAccess::rootBoundary(scene));
    LOKA_VERIFY(root != 0);
    LOKA_VERIFY(ObservedUseTestSupport::activeUses(root) == 2);
    const NodeDirtyFlags originalUnion = root->observedDirtyFlags();
    LOKA_VERIFY(originalUnion == NODE_DIRTY_PROPS);
    LOKA_VERIFY(!scene.hasPendingInvalidation());

    // Stop an UPDATE declaration before visiting either retained Text. The
    // existing compose-failure fact closes the same scope as the production walk.
    MutableState<bool> first(false), second(false);
    TwoBorrowedSources candidate(first, second);
    loka::core::testing::failLokaAllocRaw("Node", "ObservedUse", 2);
    {
      BoundaryNode::ObservedStatePassScope pass(root, COMPOSE_EVENT_UPDATE);
      root->beginComposeResult(COMPOSE_EVENT_UPDATE, NODE_DIRTY_PROPS);
      root->clearObservedDirtyFlags();
      BoundaryNode::declareBoundaryDirtySources(&candidate, root);
      root->completeComposeResult();
      LOKA_VERIFY(root->composeResult().allocationFailed);
      LOKA_VERIFY(!root->composeResult().composed);
    }
    loka::core::testing::failLokaAllocRaw("Node", "ObservedUse", 0);
    LOKA_VERIFY(!scene.hasPendingInvalidation());
    { StateTrackerGuard guard(root->tracker()); root->text.set(String::Literal("retry input")); }
    LOKA_VERIFY(SceneTestAccess::requestedDirtyFlags(scene) == originalUnion);
    LOKA_VERIFY(scene.hasPendingInvalidation());
    LOKA_VERIFY(root->observedDirtyFlags() == originalUnion);
    LOKA_VERIFY(ObservedUseTestSupport::activeUses(root) == 2);
    LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(candidate) == 0);
    scene.flushInvalidation();
    LOKA_VERIFY(!root->composeResult().allocationFailed);
    LOKA_VERIFY(root->composeResult().composed);
    LOKA_VERIFY(ObservedUseTestSupport::activeUses(root) == 2);
    LOKA_VERIFY(!scene.hasPendingInvalidation());
    SceneTestAccess::unmount(scene);
  }
  LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
  loka::core::testing::allowLokaAllocRaw();
}

#include "testing/core/StateTrackerTestAccess.hpp"
#include "testing/scene/OwnershipDump.hpp"
namespace
{
  /** Test resident only: production nodes do not declare own input yet. */
  class OwnInputLeaf : public TextNode
  {
  public:
    explicit OwnInputLeaf(const TextProps &props = TextProps("own input"))
        : TextNode(props),
          declared(both),
          source(0),
          ownFirst(true)
    {
    }
    virtual void declareDirtySources(DirtySourceRegistrar &registrar)
    {
      if (this->ownFirst)
        registrar.markDirtyOnOwnInput(this->declared);
      registrar.markDirtyOnChange(this->source, NODE_DIRTY_PROPS);
      if (!this->ownFirst)
        registrar.markDirtyOnOwnInput(this->declared);
    }
    void request(NodeDirtyFlags flags)
    {
      this->requestOwnInput(flags);
    }
    NodeDirtyFlags declared;
    StateBase *source;
    bool ownFirst;
  };

  class OwnInputPlatform : public IPlatformController
  {
  public:
    OwnInputPlatform()
        : passes(0)
    {
    }
    virtual void onChange(Node *, NodeDirtyFlags, bool)
    {
      ++this->passes;
    }
    virtual void synchronize() {}
    virtual bool hasPendingSync() const
    {
      return false;
    }
    virtual void destroy() {}
    unsigned passes;
  };

  NodeDirtyFlags pending(Scene &scene, BoundaryNode *owner)
  {
    return SceneTestAccess::director(scene).pendingDirtyFlagsForBoundary(owner);
  }

  struct RemovedIdentity
  {
    PushStateTracker *tracker;
    StateBase *state;
    static void run(void *data)
    {
      RemovedIdentity &self = *static_cast<RemovedIdentity *>(data);
      self.tracker->removeState(self.state);
    }
  };

  void innerCommit(void *data)
  {
    BoundaryNode *owner = static_cast<BoundaryNode *>(data);
    owner->markViewDirty(NODE_DIRTY_PROPS);
  }
} // namespace

void testOwnInputKeyReuseAndReservation()
{
  MutableState<int> state(0);
  LOKA_VERIFY(DirtySourceKey::none().isFree());
  LOKA_VERIFY(!DirtySourceKey::ownInput().isFree());
  LOKA_VERIFY(DirtySourceKey::ownInput().isOwnInput());
  LOKA_VERIFY(DirtySourceKey::ownInput().stateOrNull() == 0);
  LOKA_VERIFY(DirtySourceKey::ofState(&state).stateOrNull() == &state);
  for (unsigned order = 0; order != 2; ++order)
  {
    OwnInputLeaf leaf;
    EmptyBoundary owner;
    leaf.ownFirst = order == 0;
    leaf.source = &state;
    BoundaryNode::declareBoundaryDirtySources(&leaf, &owner);
    LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(leaf) == 2);
    leaf.request(NODE_DIRTY_LAYOUT);
    LOKA_VERIFY(leaf.takeLayoutInputs() == NODE_DIRTY_LAYOUT);
    state.set(order + 1);
    LOKA_VERIFY(leaf.takeLayoutInputs() == NODE_DIRTY_PROPS);
    owner.clearObservedStateEntries();
    leaf.source = 0;
    BoundaryNode::declareBoundaryDirtySources(&leaf, &owner);
    LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(leaf) == 1);
    LOKA_VERIFY(NodeObservedUsesTestAccess::storageCount(leaf) == 2);
    owner.clearObservedStateEntries();
  }
  for (int failure = 1; failure <= 2; ++failure)
  {
    loka::core::testing::failLokaAllocRaw("Node", "ObservedUse", failure);
    {
      MutableState<int> local(0);
      OwnInputLeaf leaf;
      EmptyBoundary owner;
      leaf.source = &local;
      const bool prepared = leaf.prepareObservedUses();
      loka::core::testing::failLokaAllocRaw("Node", "ObservedUse", 0);
      LOKA_VERIFY(!prepared);
      LOKA_VERIFY(NodeObservedUsesTestAccess::reservedCount(leaf) == 0);
      LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(leaf) == 0);
      BoundaryNode::declareBoundaryDirtySources(&leaf, &owner);
      LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(leaf) == 2);
      owner.clearObservedStateEntries();
    }
    loka::core::testing::allowLokaAllocRaw();
  }
}

void testOwnInputLastUserAndAllClearPaths()
{
  const unsigned long baseline = loka::app::scene::testing::paintBaselineStats().observedUses;
  OwnInputLeaf first, second;
  {
    EmptyBoundary owner;
    BoundaryNode::declareBoundaryDirtySources(&first, &owner);
    BoundaryNode::declareBoundaryDirtySources(&second, &owner);
    LOKA_VERIFY(loka::app::scene::testing::paintBaselineStats().observedUses == baseline + 2);
    LifecycleFactTestAccess::MarkSubtreeRetired(&first);
    LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(first) == 0);
    LifecycleFactTestAccess::MarkSubtreeRetired(&second);
    LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(second) == 0);
    LOKA_VERIFY(loka::app::scene::testing::paintBaselineStats().observedUses == baseline);
    // No pass runs between the last retirement and owner destruction.
  }
  OwnInputLeaf survives;
  {
    EmptyBoundary owner;
    BoundaryNode::declareBoundaryDirtySources(&survives, &owner);
    owner.clearObservedStateEntries();
    LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(survives) == 0);
    BoundaryNode::declareBoundaryDirtySources(&survives, &owner);
  }
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(survives) == 0);
  survives.request(NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(survives.takeLayoutInputs() == NODE_DIRTY_NONE);
  LOKA_VERIFY(loka::app::scene::testing::paintBaselineStats().observedUses == baseline);
}

void testOwnInputSuccessfulAndRefusedPass()
{
  OwnInputLeaf first, second;
  EmptyBoundary owner;
  owner.beginObservedStatePass();
  BoundaryNode::declareBoundaryDirtySources(&first, &owner);
  BoundaryNode::declareBoundaryDirtySources(&second, &owner);
  owner.completeObservedStatePass();
  owner.beginObservedStatePass();
  BoundaryNode::declareBoundaryDirtySources(&second, &owner);
  owner.noteComposeAllocationFailure();
  owner.completeObservedStatePass();
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(first) == 1);
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(second) == 1);
  LOKA_VERIFY(owner.observedDirtyFlags() == NODE_DIRTY_NONE);
  // A separate successful owner pass also exercises transfer of a live row.
  EmptyBoundary next;
  next.beginObservedStatePass();
  BoundaryNode::declareBoundaryDirtySources(&first, &next);
  next.completeObservedStatePass();
  next.beginObservedStatePass();
  next.completeObservedStatePass();
  first.request(NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(first.takeLayoutInputs() == NODE_DIRTY_NONE);
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(first) == 0);
  owner.clearObservedStateEntries();
}

void testOwnInputDeclarationAndSubsetRefusal()
{
  const NodeDirtyFlags masks[] = {NODE_DIRTY_NONE,
                                  NODE_DIRTY_CHILD,
                                  NODE_DIRTY_INITIAL,
                                  NODE_DIRTY_MYSELF,
                                  static_cast<NodeDirtyFlags>(NODE_DIRTY_LAYOUT | NODE_DIRTY_CHILD)};
  for (unsigned i = 0; i < sizeof(masks) / sizeof(masks[0]); ++i)
  {
    OwnInputLeaf leaf;
    EmptyBoundary owner;
    leaf.declared = masks[i];
    BoundaryNode::declareBoundaryDirtySources(&leaf, &owner);
    LOKA_VERIFY(NodeObservedUsesTestAccess::storageCount(leaf) == 0);
    LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(leaf) == 0);
  }
  OwnInputLeaf leaf;
  EmptyBoundary owner;
  leaf.declared = NODE_DIRTY_LAYOUT;
  BoundaryNode::declareBoundaryDirtySources(&leaf, &owner);
  leaf.request(NODE_DIRTY_NONE);
  leaf.request(both);
  leaf.request(NODE_DIRTY_CHILD);
  leaf.request(NODE_DIRTY_INITIAL);
  leaf.request(NODE_DIRTY_MYSELF);
  LOKA_VERIFY(leaf.takeLayoutInputs() == NODE_DIRTY_NONE);
  leaf.request(NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(leaf.takeLayoutInputs() == NODE_DIRTY_LAYOUT);
  owner.clearObservedStateEntries();
}

void testOwnInputLifecycleFactAdmission()
{
  OwnInputLeaf leaf;
  EmptyBoundary owner;
  BoundaryNode::declareBoundaryDirtySources(&leaf, &owner);
  NotifySubtreeNodeDetached(&leaf);
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(leaf) == 1);
  leaf.request(NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(leaf.takeLayoutInputs() == NODE_DIRTY_NONE);
  NotifySubtreeNodeAttached(&leaf);
  leaf.takeLayoutInputs();
  // Reentry before re-registration may use the surviving old edge.
  leaf.request(NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(leaf.takeLayoutInputs() == NODE_DIRTY_LAYOUT);
  owner.clearObservedStateEntries();
  NotifySubtreeNodeDetached(&leaf);
  NotifySubtreeNodeAttached(&leaf);
  leaf.takeLayoutInputs();
  leaf.request(NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(leaf.takeLayoutInputs() == NODE_DIRTY_NONE);
  LifecycleFactTestAccess::MarkSubtreeRetired(&leaf);
  leaf.request(NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(leaf.takeLayoutInputs() == NODE_DIRTY_NONE);
}

void testOwnInputOwnerAdmissionAndSiblings()
{
  OwnInputPlatform platform;
  OwnInputLeaf leaf, sibling;
  Scene scene((Boundary<EmptyBoundary>()));
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  BoundaryNode *owner = SceneTestAccess::rootBoundary(scene);
  BoundaryNode::declareBoundaryDirtySources(&leaf, owner);
  BoundaryNode::declareBoundaryDirtySources(&sibling, owner);
  const unsigned before = platform.passes;
  leaf.request(NODE_DIRTY_NONE);
  LOKA_VERIFY(!scene.hasPendingInvalidation());
  leaf.request(NODE_DIRTY_PROPS);
  leaf.request(NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(platform.passes == before);
  LOKA_VERIFY(pending(scene, owner) == both);
  LOKA_VERIFY(leaf.takeLayoutInputs() == both);
  LOKA_VERIFY(sibling.takeLayoutInputs() == NODE_DIRTY_NONE);
  scene.flushInvalidation();
  LOKA_VERIFY(platform.passes == before + 1);
  BoundaryNode::declareBoundaryDirtySources(&leaf, owner);
  owner->setFrozen(true);
  leaf.request(NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(!scene.hasPendingInvalidation());
  LOKA_VERIFY(leaf.takeLayoutInputs() == NODE_DIRTY_LAYOUT);
  owner->setFrozen(false);
  LOKA_VERIFY(!scene.hasPendingInvalidation());
  SceneTestAccess::notifyComposeEvent(scene, COMPOSE_EVENT_DETACH);
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(leaf) == 0);
  leaf.request(NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(!scene.hasPendingInvalidation());
  SceneTestAccess::unmount(scene);
}

void testOwnInputExcludedFromUnknownCommitUnion()
{
  OwnInputPlatform platform;
  OwnInputLeaf leaf;
  MutableState<int> privateState(0), paint(0);
  Scene scene((Boundary<EmptyBoundary>()));
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  BoundaryNode *owner = SceneTestAccess::rootBoundary(scene);
  leaf.source = &paint;
  BoundaryNode::declareBoundaryDirtySources(&leaf, owner);
  owner->tracker()->asPushTracker()->addState(&privateState);
  owner->tracker()->asPushTracker()->addState(&paint);
  LOKA_VERIFY(owner->observedDirtyFlags() == NODE_DIRTY_PROPS);
  RemovedIdentity removal = {owner->tracker()->asPushTracker(), &privateState};
  {
    BorrowScope borrow(platform);
    StateTrackerGuard transaction(owner->tracker());
    privateState.set(1);
    loka::core::testing::PushStateTrackerTestAccess::defer(*owner->tracker(), &RemovedIdentity::run, &removal);
  }
  LOKA_VERIFY(!removal.tracker->committedSet().complete());
  LOKA_VERIFY(pending(scene, owner) == NODE_DIRTY_PROPS);
  scene.flushInvalidation();
  owner->beginObservedStatePass();
  BoundaryNode::declareBoundaryDirtySources(&leaf, owner);
  owner->noteComposeAllocationFailure();
  owner->completeObservedStatePass();
  LOKA_VERIFY(owner->observedDirtyFlags() == NODE_DIRTY_PROPS);
  SceneTestAccess::unmount(scene);
}

namespace
{
  enum OwnClock
  {
    BORROW_UNTRACKED,
    SAME_OWNER,
    INNER_PUSH,
    OTHER_TRACKER,
    OPEN_OUTSIDE_BORROW,
    OBSERVER_IMMEDIATE
  };
  struct ObserverFlush
  {
    BoundaryNode *owner;
    static void run(void *data)
    {
      static_cast<ObserverFlush *>(data)->owner->markViewDirty(NODE_DIRTY_PROPS);
    }
  };

  StateBase *clockSource = 0;
  bool clockOwn = false;
  class ClockRoot : public BoundaryNodeFor<ClockRoot>
  {
  public:
    explicit ClockRoot(const BoundaryPropsFor<ClockRoot> &props)
        : BoundaryNodeFor<ClockRoot>(props),
          source(clockSource),
          own(clockOwn)
    {
    }
    virtual void composeNode(NodeComposition &) {}
    virtual void declareDirtySources(DirtySourceRegistrar &registrar)
    {
      if (this->own)
        registrar.markDirtyOnOwnInput(NODE_DIRTY_LAYOUT);
      else
        registrar.markDirtyOnChange(this->source, NODE_DIRTY_LAYOUT);
    }
    void request()
    {
      this->requestOwnInput(NODE_DIRTY_LAYOUT);
    }
    StateBase *source;
    bool own;
  };

  unsigned runOwnClock(OwnClock clock, bool own)
  {
    OwnInputPlatform platform;
    MutableState<int> source(0);
    PushStateTracker foreign;
    clockSource = &source;
    clockOwn = own;
    Scene scene((Boundary<ClockRoot>()));
    scene.mount(&platform);
    SceneTestAccess::updateAttached(scene, true);
    ClockRoot *owner = static_cast<ClockRoot *>(SceneTestAccess::rootBoundary(scene));
    StateTracker *tracker = 0;
    if (clock == SAME_OWNER)
      tracker = owner->tracker();
    if (clock == INNER_PUSH || clock == OTHER_TRACKER || clock == OPEN_OUTSIDE_BORROW)
      tracker = &foreign;
    if (tracker)
      tracker->asPushTracker()->addState(&source);
    if (clock == INNER_PUSH)
    {
      foreign.setInvalidateTarget(owner);
      foreign.setInvalidateCallback(&innerCommit, owner);
    }
    ObserverFlush observer = {owner};
    if (clock == OBSERVER_IMMEDIATE)
      source.bind(&ObserverFlush::run, &observer, false);
    const unsigned before = platform.passes;
    if (clock <= OTHER_TRACKER)
    {
      BorrowScope borrow(platform);
      {
        StateTrackerGuard transaction(tracker);
        source.set(1);
        const unsigned atDoor = platform.passes;
        if (own)
          owner->request();
        LOKA_VERIFY(platform.passes == atDoor); // Door never flushes immediately.
        scene.flushInvalidation();
        LOKA_VERIFY(platform.passes == before); // Borrow supplies the wall.
      }
    }
    else if (clock == OPEN_OUTSIDE_BORROW)
    {
      StateTrackerGuard transaction(tracker);
      source.set(1);
      const unsigned atDoor = platform.passes;
      if (own)
        owner->request();
      LOKA_VERIFY(platform.passes == atDoor); // Door never flushes immediately.
      scene.flushInvalidation();
      // A transaction is not a borrow: own input may run before commit.
      LOKA_VERIFY(platform.passes == before + (own ? 1 : 0));
    }
    else
    {
      source.set(1); // An independent observer can flush before the door.
      LOKA_VERIFY(platform.passes > before);
      const unsigned atDoor = platform.passes;
      if (own)
        owner->request();
      LOKA_VERIFY(platform.passes == atDoor); // Door never flushes immediately.
    }
    scene.flushInvalidation();
    const unsigned total = platform.passes - before;
    const unsigned completed = platform.passes;
    scene.flushInvalidation();
    LOKA_VERIFY(platform.passes == completed);
    if (clock == OBSERVER_IMMEDIATE)
      source.unbind(&ObserverFlush::run, &observer);
    SceneTestAccess::unmount(scene);
    return total;
  }

  void compareOwnClock(OwnClock clock)
  {
    const unsigned oldTotal = runOwnClock(clock, false);
    const unsigned newTotal = runOwnClock(clock, true);
    LOKA_VERIFY(oldTotal == 1);
    LOKA_VERIFY(newTotal == oldTotal); // Total pass count unchanged, separate from timing.
  }
} // namespace

void testOwnInputClockBorrowUntracked()
{
  compareOwnClock(BORROW_UNTRACKED);
}
void testOwnInputClockSameOwner()
{
  compareOwnClock(SAME_OWNER);
}
void testOwnInputClockInnerPush()
{
  compareOwnClock(INNER_PUSH);
}
void testOwnInputClockOtherTracker()
{
  compareOwnClock(OTHER_TRACKER);
}
void testOwnInputClockOpenOutsideBorrow()
{
  compareOwnClock(OPEN_OUTSIDE_BORROW);
}
void testOwnInputClockObserverImmediate()
{
  const unsigned oldTotal = runOwnClock(OBSERVER_IMMEDIATE, false);
  const unsigned newTotal = runOwnClock(OBSERVER_IMMEDIATE, true);
  // Outside a borrow, observer-triggered immediate work and the later door
  // each cost a pass; no publication-wide barrier is promised.
  LOKA_VERIFY(oldTotal == 2);
  LOKA_VERIFY(newTotal == oldTotal);
}

void testOwnInputNeverFlushesImmediately()
{
  OwnInputPlatform platform;
  OwnInputLeaf leaf;
  Scene scene((Boundary<EmptyBoundary>()));
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  BoundaryNode *owner = SceneTestAccess::rootBoundary(scene);
  BoundaryNode::declareBoundaryDirtySources(&leaf, owner);
  const unsigned before = platform.passes;
  leaf.request(NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(platform.passes == before);
  LOKA_VERIFY(scene.hasPendingInvalidation());
  scene.flushInvalidation();
  LOKA_VERIFY(platform.passes == before + 1);
  SceneTestAccess::unmount(scene);
}

namespace
{
  struct ReregisterOwnInput
  {
    OwnInputLeaf *leaf;
    BoundaryNode *owner;
    static void run(void *data)
    {
      ReregisterOwnInput &self = *static_cast<ReregisterOwnInput *>(data);
      NotifySubtreeNodeDetached(self.leaf);
      NotifySubtreeNodeAttached(self.leaf);
      BoundaryNode::declareBoundaryDirtySources(self.leaf, self.owner);
    }
  };
} // namespace
void testOwnInputNotificationUsesCurrentOwner()
{
  OwnInputPlatform platform;
  OwnInputLeaf leaf;
  MutableState<int> source(0);
  Scene first((Boundary<EmptyBoundary>())), second((Boundary<EmptyBoundary>()));
  first.mount(&platform);
  second.mount(&platform);
  SceneTestAccess::updateAttached(first, true);
  SceneTestAccess::updateAttached(second, true);
  BoundaryNode *oldOwner = SceneTestAccess::rootBoundary(first);
  BoundaryNode *newOwner = SceneTestAccess::rootBoundary(second);
  BoundaryNode::declareBoundaryDirtySources(&leaf, oldOwner);
  ReregisterOwnInput move = {&leaf, newOwner};
  source.bind(&ReregisterOwnInput::run, &move, false);
  source.set(1);
  leaf.request(NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(!first.hasPendingInvalidation());
  LOKA_VERIFY(pending(second, newOwner) == NODE_DIRTY_LAYOUT);
  const unsigned before = platform.passes;
  second.flushInvalidation();
  LOKA_VERIFY(platform.passes == before + 1);
  source.unbind(&ReregisterOwnInput::run, &move);
  SceneTestAccess::unmount(first);
  SceneTestAccess::unmount(second);
}

#include "app/scene/boundary/LazyScopeDefinition.hpp"
namespace
{
  struct OwnComponentTag
  {
  };
  class OwnComponent;
  struct OwnComponentProps : NodePropsBase<OwnComponentProps>
  {
    typedef OwnComponentTag TypeTag;
    typedef OwnComponent NodeType;
    bool operator<(const PropsBase &) const
    {
      return false;
    }
  };
  class OwnComponent : public ComponentNodeWithProps<OwnComponentProps>
  {
  public:
    explicit OwnComponent(const OwnComponentProps &props)
        : ComponentNodeWithProps<OwnComponentProps>(props)
    {
    }
    virtual void declareDirtySources(DirtySourceRegistrar &registrar)
    {
      registrar.markDirtyOnOwnInput(NODE_DIRTY_LAYOUT);
    }
    void request()
    {
      this->requestOwnInput(NODE_DIRTY_LAYOUT);
    }
    virtual const void *nodeTypeKey() const
    {
      return NodeTypeToken<OwnComponent>();
    }
    virtual void composeChildren(NodeComposition &) {}
    virtual void detachNode(NodeComposition &)
    {
      LOKA_VERIFY(this->lifecycleFact() == NODE_FACT_ATTACHED);
      this->request();
    }
  };

  struct OwnScopeTag
  {
  };
  class OwnScope;
  struct OwnScopeProps : NodePropsBase<OwnScopeProps>
  {
    typedef OwnScopeTag TypeTag;
    typedef OwnScope NodeType;
    bool operator<(const PropsBase &) const
    {
      return false;
    }
  };
  class OwnScope : public LazyScopeNode
  {
  public:
    typedef OwnScopeTag TypeTag;
    typedef OwnScopeProps Props;
    OwnScopeProps props;
    explicit OwnScope(const OwnScopeProps &p)
        : props(p)
    {
    }
    virtual void declareScope(NodeComposition &c)
    {
      c.declare(Component(OwnComponentProps()));
    }
  };

  template <bool Lazy> class OwnGenerationRoot : public BoundaryNodeFor<OwnGenerationRoot<Lazy> >
  {
  public:
    explicit OwnGenerationRoot(const BoundaryPropsFor<OwnGenerationRoot<Lazy> > &props)
        : BoundaryNodeFor<OwnGenerationRoot<Lazy> >(props)
    {
      this->state(this->key, 0);
    }
    virtual bool flushViewDirtyImmediately(NodeDirtyFlags) const
    {
      return false;
    }
    virtual void composeNode(NodeComposition &c)
    {
      if (Lazy)
        c.declare(Fragment() << LazyScope(*this->key.state(), OwnScopeProps()));
      else
        c.declare(
            Fragment() << Keyed(*this->key.state(),
                                this,
                                &OwnGenerationRoot::declareArm,
                                reservation::SeatNodes<reservation::Nodes<OwnComponent, 1, reservation::End> >()));
    }
    void declareArm(NodeComposition &c)
    {
      c.declare(Component(OwnComponentProps()));
    }
    NodeState<int> key;
  };

  OwnComponent *findOwnComponent(Node *node)
  {
    if (!node)
      return 0;
    if (node->nodeTypeKey() == NodeTypeToken<OwnComponent>())
      return static_cast<OwnComponent *>(node);
    INestable *nestable = node->asNestable();
    for (Node *child = nestable ? nestable->childrenHead() : 0; child; child = child->nextInComposition)
    {
      OwnComponent *found = findOwnComponent(child);
      if (found)
        return found;
    }
    return 0;
  }

  template <bool Lazy> void generationOwnInput()
  {
    loka::core::testing::failLokaAllocRaw("Node", "ObservedUse", 0);
    {
      NullScenePlatformController platform;
      Scene scene((Boundary<OwnGenerationRoot<Lazy> >()));
      scene.mount(&platform);
      SceneTestAccess::updateAttached(scene, true);
      OwnGenerationRoot<Lazy> *owner = static_cast<OwnGenerationRoot<Lazy> *>(SceneTestAccess::rootBoundary(scene));
      OwnComponent *original = findOwnComponent(owner);
      LOKA_VERIFY(original != 0);
      original->request();
      LOKA_VERIFY(pending(scene, owner) == NODE_DIRTY_LAYOUT);
      scene.flushInvalidation();
      const unsigned long baseline = loka::app::scene::testing::paintBaselineStats().observedUses;
      // LazyScope preserves its installed generation. Reserved Keyed uses
      // RETIRE_BEFORE_BUILD: vacate, then try admission on the following run.
      loka::core::testing::failLokaAllocRaw("Node", "ObservedUse", 1);
      {
        StateTrackerGuard transaction(owner->tracker());
        owner->key.set(1);
      }
      scene.flushInvalidation();
      if (!Lazy)
      {
        LOKA_VERIFY(findOwnComponent(owner) == 0);
        LOKA_VERIFY(loka::app::scene::testing::paintBaselineStats().observedUses == baseline - 1);
        scene.flushInvalidation();
      }
      LOKA_VERIFY(loka::core::testing::lokaAllocRawAttempts() > 0);
      loka::core::testing::failLokaAllocRaw("Node", "ObservedUse", 0);
      if (Lazy)
      {
        LOKA_VERIFY(findOwnComponent(owner) == original);
        LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(*original) == 1);
        LOKA_VERIFY(loka::app::scene::testing::paintBaselineStats().observedUses == baseline);
        original->request();
      }
      else
      {
        LOKA_VERIFY(findOwnComponent(owner) == 0);
        LOKA_VERIFY(loka::app::scene::testing::paintBaselineStats().observedUses == baseline - 1);
      }
      scene.flushInvalidation();
      OwnComponent *current = findOwnComponent(owner);
      LOKA_VERIFY(current != 0);
      if (Lazy)
        LOKA_VERIFY(current != original);
      LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(*current) == 1);
      // Drain any conservative DETACH-hook request before measuring this door.
      scene.flushInvalidation();
      const unsigned long before = loka::app::scene::testing::paintBaselineStats().boundaryApplyCallbacks;
      current->request();
      LOKA_VERIFY(loka::app::scene::testing::paintBaselineStats().boundaryApplyCallbacks == before);
      LOKA_VERIFY(pending(scene, owner) == NODE_DIRTY_LAYOUT);
      scene.flushInvalidation();
      LOKA_VERIFY(loka::app::scene::testing::paintBaselineStats().boundaryApplyCallbacks == before + 1);
      SceneTestAccess::unmount(scene);
    }
    LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
    loka::core::testing::allowLokaAllocRaw();
  }
} // namespace

void testOwnInputKeyedGenerationAndRefusal()
{
  generationOwnInput<false>();
}
void testOwnInputLazyScopeGenerationAndRefusal()
{
  generationOwnInput<true>();
}

void testOwnInputLeafDetachHookSchedulesUntilRetirement()
{
  OwnInputPlatform platform;
  OwnComponent leaf((OwnComponentProps()));
  Scene scene((Boundary<EmptyBoundary>()));
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  BoundaryNode *owner = SceneTestAccess::rootBoundary(scene);
  BoundaryNode::declareBoundaryDirtySources(&leaf, owner);
  ComponentContext context;
  context.setBoundary(owner);
  context.setScene(&scene);
  const unsigned before = platform.passes;
  EmptyBoundary::composeTree(&leaf, context, COMPOSE_EVENT_DETACH, owner);
  LOKA_VERIFY(pending(scene, owner) == NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(platform.passes == before);
  LifecycleFactTestAccess::MarkSubtreeRetired(&leaf);
  scene.flushInvalidation();
  LOKA_VERIFY(platform.passes == before + 1);
  leaf.request();
  LOKA_VERIFY(!scene.hasPendingInvalidation());
  scene.flushInvalidation();
  LOKA_VERIFY(platform.passes == before + 1);
  SceneTestAccess::unmount(scene);
}

void testOwnInputDiagnosticsAndNestedDetach()
{
  OwnInputPlatform platform;
  OwnInputLeaf leaf;
  MutableState<int> state(0);
  Scene scene((Boundary<EmptyBoundary>()));
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  BoundaryNode *root = SceneTestAccess::rootBoundary(scene);
  EmptyBoundary nested;
  nested.setScene(&scene);
  nested.setParentBoundary(root);
  leaf.source = &state;
  BoundaryNode::declareBoundaryDirtySources(&leaf, &nested);
  // Exercise nested DETACH via the same compose door as a normal child walk.
  ComponentContext context;
  context.setBoundary(root);
  context.setScene(&scene);
  EmptyBoundary::composeTree(&nested, context, COMPOSE_EVENT_DETACH, root);
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(leaf) == 0);
  leaf.request(NODE_DIRTY_LAYOUT);
  LOKA_VERIFY(!scene.hasPendingInvalidation());
  BoundaryNode::declareBoundaryDirtySources(&leaf, root);
  const std::string dump = loka::dsl::testing::OwnershipDump::dump(scene);
  LOKA_VERIFY(dump.find("observed: 1") != std::string::npos);
  LOKA_VERIFY(dump.find("own-input: 1") != std::string::npos);
  LifecycleFactTestAccess::MarkSubtreeRetired(&leaf);
  const std::string empty = loka::dsl::testing::OwnershipDump::dump(scene);
  LOKA_VERIFY(empty.find("own-input:") == std::string::npos);
  LOKA_VERIFY(empty.find("observed:") == std::string::npos);
  SceneTestAccess::unmount(scene);
}

// Numeric values measured from object symbols, before and after this change.
// The 68k branch is also compiled directly by the target-size probe.
#if defined(__LP64__)
typedef char OwnInputUseSizePin[sizeof(ObservedUse) == 56 ? 1 : -1];
typedef char OwnInputLedgerDeltaPin[sizeof(BoundaryObservedState) == 40 + 16 ? 1 : -1];
#elif defined(__m68k__)
typedef char OwnInputUseSizePin[sizeof(ObservedUse) == 28 ? 1 : -1];
typedef char OwnInputLedgerDeltaPin[sizeof(BoundaryObservedState) == 20 + 8 ? 1 : -1];
#endif

void testOwnInputRecordSizes()
{
  LOKA_VERIFY(sizeof(DirtySourceKey) == sizeof(StateBase *));
  std::printf("Own-input sizeof: use=%lu ledger=%lu route=%lu\n",
              static_cast<unsigned long>(sizeof(ObservedUse)),
              static_cast<unsigned long>(sizeof(BoundaryObservedState)),
              static_cast<unsigned long>(sizeof(ObservedUseChain)));
#if !defined(__LP64__) && !defined(__m68k__)
  std::printf("[skip] numeric own-input size baselines cover LP64 and m68k; key width still pinned.\n");
#endif
}

void testObservedUsesDeferredStateBindingSurvivesWithdrawal()
{
  NullScenePlatformController platform;
  MutableState<int> source(0);
  PushStateTracker foreign;
  foreign.addState(&source);
  Scene scene((Boundary<EmptyBoundary>()));
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  BoundaryNode *owner = SceneTestAccess::rootBoundary(scene);
  TextNode leaf((TextProps("deferred borrower")));
  LOKA_VERIFY(owner->registerObservedState(&source, NODE_DIRTY_PROPS, &leaf));
  {
    StateTrackerGuard transaction(&foreign);
    source.set(1); // Retains the State binding until the foreign commit.
    LifecycleFactTestAccess::MarkSubtreeRetired(&leaf);
    LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(leaf) == 0);
    SceneTestAccess::unmount(scene); // Reclaim the owner before that commit.
  }
  LOKA_VERIFY(!scene.hasPendingInvalidation());
  LOKA_VERIFY(NodeObservedUsesTestAccess::useCount(leaf) == 0);
}
