#include "testing/scene/SceneTestFlow.hpp"
#include "LifecycleDetachTests.hpp"
#include "support/TestVerify.hpp"
#include "support/ObservedUseCounts.hpp"

#include <cassert>

#include "app/nodes/Text.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/nestable/PolicyScope.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/scene/Scene.hpp"
#include "app/scene/context/ComponentContext.hpp"
#include "app/scene/node/ComponentNode.hpp"
#include "app/scene/node/Conditional.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/NullWindow.hpp"
#include "support/Headless.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/LocalRebuildRefusal.hpp"
#include "app/nodes/nestable/Keyed.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "testing/scene/SceneFocusTestAccess.hpp"

namespace
{
  /** Isolated bump-arena retirement fixture. These tests intentionally retain
      the old build-before-retire schedule to exercise NodeArena snapshots;
      production Keyed partition reuse is pinned in StrictNodeRouteTests. */
  template <class K> class ArenaDeclarationFixture : public loka::app::KeyedDefinition<K>
  {
    typedef loka::app::KeyedDefinition<K> Base;
  public:
    template <class N, class List>
    ArenaDeclarationFixture(loka::core::State<K> &key, N *owner,
                            void (N::*declare)(loka::app::scene::NodeComposition &),
                            loka::app::reservation::SeatNodes<List> nodes)
        : Base(key, owner, declare, nodes) {}
    ArenaDeclarationFixture(const ArenaDeclarationFixture &other) : Base(other) {}
    virtual const loka::app::scene::detail::SeatReservation *seatReservation() const { return 0; }
    typedef ArenaDeclarationFixture CloneType;
    virtual loka::app::scene::NodeDefinitionBase *clone() const
    { return new ArenaDeclarationFixture(*this); }
  };

  struct DetachHookCounts
  {
    DetachHookCounts()
        : attachCalls(0),
          detachCalls(0)
    {
    }

    int attachCalls;
    int detachCalls;
  };

  class PlainDetachProbeContext : public loka::app::scene::NodeContext
  {
  public:
    explicit PlainDetachProbeContext(DetachHookCounts *counts)
        : counts_(counts)
    {
    }

    /** Attach-time read: the announce successor — counts the attach the
        installing site observes from the current fact. */
    void readLifecycleFactOnAttach()
    {
      if (this->owner() && this->owner()->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
      {
        ++this->counts_->attachCalls;
      }
    }

    // Living transitions and the terminal arrive through the fact channel;
    // the counters keep their original meaning — RETIRED counts as the
    // destruction-time detach.
    virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                               loka::app::scene::NodeLifecycleFact next)
    {
      (void)previous;
      if (next == loka::app::scene::NODE_FACT_ATTACHED)
      {
        ++this->counts_->attachCalls;
      }
      else
      {
        ++this->counts_->detachCalls;
      }
    }

  private:
    DetachHookCounts *counts_;
  };

  class PlainDetachProbeNode;

  struct PlainDetachProbeTypeTag
  {
  };

  struct PlainDetachProbeProps : public loka::app::scene::NodePropsBase<PlainDetachProbeProps>
  {
    typedef PlainDetachProbeTypeTag TypeTag;
    typedef PlainDetachProbeNode NodeType;

    explicit PlainDetachProbeProps(DetachHookCounts *counts = 0)
        : counts(counts)
    {
    }

    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId() ? false : this->propsTypeId() < rhs.propsTypeId();
    }

    DetachHookCounts *counts;
  };

  class PlainDetachProbeNode : public loka::app::scene::Node
  {
  public:
    typedef PlainDetachProbeTypeTag TypeTag;

    explicit PlainDetachProbeNode(const PlainDetachProbeProps &props)
        : props(props)
    {
      PlainDetachProbeContext *context = new PlainDetachProbeContext(props.counts);
      this->setContext(context);
      context->readLifecycleFactOnAttach();
    }

    PlainDetachProbeProps props;
  };

  DetachHookCounts *g_boundaryInternalCounts = 0;

  DetachHookCounts *g_conditionalSceneTrueCounts = 0;
  DetachHookCounts *g_conditionalSceneFalseCounts = 0;
  loka::core::MutableState<bool> *g_conditionalSceneCondition = 0;

  class ConditionalSceneProbeNode;
  typedef loka::app::scene::BoundaryPropsFor<ConditionalSceneProbeNode> ConditionalSceneProbeProps;

  class ConditionalSceneProbeNode : public loka::app::scene::BoundaryNodeFor<ConditionalSceneProbeNode>
  {
  public:
    explicit ConditionalSceneProbeNode(const ConditionalSceneProbeProps &props)
        : loka::app::scene::BoundaryNodeFor<ConditionalSceneProbeNode>(props)
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      typedef loka::app::scene::NodeDefinition<PlainDetachProbeProps, PlainDetachProbeNode>
          PlainDetachProbeDefinition;
      PlainDetachProbeDefinition trueDefinition((PlainDetachProbeProps(g_conditionalSceneTrueCounts)));
      PlainDetachProbeDefinition falseDefinition((PlainDetachProbeProps(g_conditionalSceneFalseCounts)));
      composition.declare(
          composition.conditional(*g_conditionalSceneCondition, trueDefinition, falseDefinition));
    }
  };

  DetachHookCounts *g_detachWindowTrueCounts = 0;
  DetachHookCounts *g_detachWindowFalseCounts = 0;
  loka::core::MutableState<bool> *g_detachWindowCondition = 0;

  class DetachWindowConditionalBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<DetachWindowConditionalBoundaryNode>
      DetachWindowConditionalBoundaryProps;

  class DetachWindowConditionalBoundaryNode
      : public loka::app::scene::BoundaryNodeFor<DetachWindowConditionalBoundaryNode>
  {
  public:
    explicit DetachWindowConditionalBoundaryNode(const DetachWindowConditionalBoundaryProps &props)
        : loka::app::scene::BoundaryNodeFor<DetachWindowConditionalBoundaryNode>(props)
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      typedef loka::app::scene::NodeDefinition<PlainDetachProbeProps, PlainDetachProbeNode>
          PlainDetachProbeDefinition;
      PlainDetachProbeDefinition trueDefinition((PlainDetachProbeProps(g_detachWindowTrueCounts)));
      PlainDetachProbeDefinition falseDefinition((PlainDetachProbeProps(g_detachWindowFalseCounts)));
      composition.declare(
          composition.conditional(*g_detachWindowCondition, trueDefinition, falseDefinition));
    }

  protected:
    virtual void detachNode(loka::app::scene::NodeComposition &composition)
    {
      (void)composition;
      g_detachWindowCondition->set(true);
    }
  };

  class BoundaryInternalProbeNode;
  typedef loka::app::scene::BoundaryPropsFor<BoundaryInternalProbeNode> BoundaryInternalProbeProps;

  class BoundaryInternalProbeNode : public loka::app::scene::BoundaryNodeFor<BoundaryInternalProbeNode>
  {
  public:
    explicit BoundaryInternalProbeNode(const BoundaryInternalProbeProps &props)
        : loka::app::scene::BoundaryNodeFor<BoundaryInternalProbeNode>(props)
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      typedef loka::app::scene::NodeDefinition<PlainDetachProbeProps, PlainDetachProbeNode>
          PlainDetachProbeDefinition;
      composition.declare(PlainDetachProbeDefinition((PlainDetachProbeProps(g_boundaryInternalCounts))));
    }
  };

  struct DetachStateOwnerObservation
  {
    DetachStateOwnerObservation()
        : detachCalls(0),
          currentBoundaryValid(false),
          parentStateReadable(false),
          parentStateValue(0)
    {
    }

    int detachCalls;
    bool currentBoundaryValid;
    bool parentStateReadable;
    int parentStateValue;
  };

  DetachStateOwnerObservation *g_detachStateOwnerObservation = 0;
  loka::app::scene::NodeState<int> *g_detachParentState = 0;

  class DetachStateOwnerProbeNode;

  struct DetachStateOwnerProbeTypeTag
  {
  };

  struct DetachStateOwnerProbeProps
      : public loka::app::scene::NodePropsBase<DetachStateOwnerProbeProps>
  {
    typedef DetachStateOwnerProbeTypeTag TypeTag;
    typedef DetachStateOwnerProbeNode NodeType;

    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId() ? false : this->propsTypeId() < rhs.propsTypeId();
    }
  };

  class DetachStateOwnerProbeNode : public loka::app::scene::ComposableNode
  {
  public:
    typedef DetachStateOwnerProbeTypeTag TypeTag;

    explicit DetachStateOwnerProbeNode(const DetachStateOwnerProbeProps &props)
        : props(props)
    {
    }

    DetachStateOwnerProbeProps props;

  protected:
    virtual void composeWithContext(loka::app::scene::ComponentContext &context,
                                    loka::app::scene::ComposeEvent event)
    {
      if (event == loka::app::scene::COMPOSE_EVENT_DETACH)
      {
        loka::app::scene::NodeComposition &composition = this->beginComposition(context);
        this->detachNode(composition);
      }
    }

    virtual void detachNode(loka::app::scene::NodeComposition &composition)
    {
      typedef loka::app::scene::NodeComposition::CurrentBoundary CurrentBoundary;
      typedef CurrentBoundary::CurrentState<int> CurrentState;

      assert(g_detachStateOwnerObservation != 0);
      assert(g_detachParentState != 0);
      ++g_detachStateOwnerObservation->detachCalls;
      const CurrentBoundary current = composition.currentBoundary();
      g_detachStateOwnerObservation->currentBoundaryValid = current.isValid();
      const CurrentState parentState = current.state(*g_detachParentState);
      g_detachStateOwnerObservation->parentStateReadable = parentState.isValid();
      if (parentState.isValid())
      {
        g_detachStateOwnerObservation->parentStateValue = parentState.get();
      }
    }
  };

  class RootDetachStateOwnerBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<RootDetachStateOwnerBoundaryNode>
      RootDetachStateOwnerBoundaryProps;

  class RootDetachStateOwnerBoundaryNode
      : public loka::app::scene::BoundaryNodeFor<RootDetachStateOwnerBoundaryNode>
  {
  public:
    explicit RootDetachStateOwnerBoundaryNode(const RootDetachStateOwnerBoundaryProps &props)
        : loka::app::scene::BoundaryNodeFor<RootDetachStateOwnerBoundaryNode>(props),
          parentState_(),
          initialized_(false)
    {
    }

    virtual void attachNode(loka::app::scene::NodeComposition &composition)
    {
      if (this->initialized_)
      {
        return;
      }
      composition.declareStates().state(this->parentState_, 37);
      g_detachParentState = &this->parentState_;
      this->initialized_ = true;
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      typedef loka::app::scene::NodeDefinition<DetachStateOwnerProbeProps,
                                               DetachStateOwnerProbeNode>
          DetachStateOwnerProbeDefinition;
      composition.declare(DetachStateOwnerProbeDefinition());
    }

  private:
    loka::app::scene::NodeState<int> parentState_;
    bool initialized_;
  };

  class NestedDetachStateOwnerBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<NestedDetachStateOwnerBoundaryNode>
      NestedDetachStateOwnerBoundaryProps;

  class NestedDetachStateOwnerBoundaryNode
      : public loka::app::scene::BoundaryNodeFor<NestedDetachStateOwnerBoundaryNode>
  {
  public:
    explicit NestedDetachStateOwnerBoundaryNode(const NestedDetachStateOwnerBoundaryProps &props)
        : loka::app::scene::BoundaryNodeFor<NestedDetachStateOwnerBoundaryNode>(props),
          parentState_(),
          initialized_(false)
    {
    }

    virtual void attachNode(loka::app::scene::NodeComposition &composition)
    {
      if (this->initialized_)
      {
        return;
      }
      composition.declareStates().state(this->parentState_, 73);
      g_detachParentState = &this->parentState_;
      this->initialized_ = true;
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      typedef loka::app::scene::NodeDefinition<DetachStateOwnerProbeProps,
                                               DetachStateOwnerProbeNode>
          DetachStateOwnerProbeDefinition;
      composition.declare(DetachStateOwnerProbeDefinition());
    }

  private:
    loka::app::scene::NodeState<int> parentState_;
    bool initialized_;
  };

  class NestedDetachStateOwnerHarnessNode;
  typedef loka::app::scene::BoundaryPropsFor<NestedDetachStateOwnerHarnessNode>
      NestedDetachStateOwnerHarnessProps;

  class NestedDetachStateOwnerHarnessNode
      : public loka::app::scene::BoundaryNodeFor<NestedDetachStateOwnerHarnessNode>
  {
  public:
    explicit NestedDetachStateOwnerHarnessNode(const NestedDetachStateOwnerHarnessProps &props)
        : loka::app::scene::BoundaryNodeFor<NestedDetachStateOwnerHarnessNode>(props)
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      composition.declare(loka::app::scene::Boundary<NestedDetachStateOwnerBoundaryNode>());
    }
  };

  class ArenaRetireProbeNode;

  struct ArenaRetireProbeTypeTag
  {
  };

  struct ArenaRetireProbeProps : public loka::app::scene::NodePropsBase<ArenaRetireProbeProps>
  {
    typedef ArenaRetireProbeTypeTag TypeTag;
    typedef ArenaRetireProbeNode NodeType;

    explicit ArenaRetireProbeProps(int *destructorCalls = 0)
        : destructorCalls(destructorCalls)
    {
    }

    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId() ? false : this->propsTypeId() < rhs.propsTypeId();
    }

    int *destructorCalls;
  };

  class ArenaRetireProbeNode : public loka::app::scene::Node
  {
  public:
    typedef ArenaRetireProbeTypeTag TypeTag;

    explicit ArenaRetireProbeNode(const ArenaRetireProbeProps &props)
        : props(props)
    {
    }

    virtual ~ArenaRetireProbeNode()
    {
      if (this->props.destructorCalls)
      {
        ++*this->props.destructorCalls;
      }
    }

    ArenaRetireProbeProps props;
  };

  class RootReplacementArenaRetireBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<RootReplacementArenaRetireBoundaryNode>
      RootReplacementArenaRetireBoundaryProps;

  int *g_rootReplacementRetireDestructorCalls = 0;
  RootReplacementArenaRetireBoundaryNode *g_rootReplacementArenaRetireBoundary = 0;

  class RootReplacementArenaRetireBoundaryNode
      : public loka::app::scene::BoundaryNodeFor<RootReplacementArenaRetireBoundaryNode>
  {
  public:
    explicit RootReplacementArenaRetireBoundaryNode(const RootReplacementArenaRetireBoundaryProps &props)
        : loka::app::scene::BoundaryNodeFor<RootReplacementArenaRetireBoundaryNode>(props),
          showReplacement_(),
          initialized_(false)
    {
    }

    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }

    virtual void attachNode(loka::app::scene::NodeComposition &composition)
    {
      if (this->initialized_)
      {
        return;
      }
      composition.declareStates().state(this->showReplacement_, false);
      // Reserve explicitly: these pins require arena residents even with the generation root.
      this->nodeArena()->reserve(16384);
      g_rootReplacementArenaRetireBoundary = this;
      this->initialized_ = true;
    }

    virtual void detachNode(loka::app::scene::NodeComposition &composition)
    {
      (void)composition;
      if (g_rootReplacementArenaRetireBoundary == this)
      {
        g_rootReplacementArenaRetireBoundary = 0;
      }
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      composition.declare(ArenaDeclarationFixture<bool>(
          *this->showReplacement_.state(),
          this,
          &RootReplacementArenaRetireBoundaryNode::declareContent,
          loka::app::reservation::SeatNodes<loka::app::reservation::Nodes<
              loka::app::TextNode,
              1,
              loka::app::reservation::Nodes<ArenaRetireProbeNode, 1, loka::app::reservation::End> > >()));
    }

    void declareContent(loka::app::scene::NodeComposition &composition)
    {
      if (this->showReplacement_.get())
      {
        // The probe needs the stored base definition, not a typed clone wrapper.
        composition.declare(static_cast<const loka::app::scene::NodeDefinitionBase &>(
            loka::app::Text("Replacement root")));
      }
      else
      {
        typedef loka::app::scene::NodeDefinition<ArenaRetireProbeProps, ArenaRetireProbeNode>
            ArenaRetireProbeDefinition;
        composition.declare(
            ArenaRetireProbeDefinition((ArenaRetireProbeProps(g_rootReplacementRetireDestructorCalls))));
      }
    }

    loka::app::scene::Node *activeRootNode() const
    {
      // Keyed now encloses its declaration Fragment in a runtime generation owner.
      loka::app::scene::INestable *generation = this->compositionRootNode()->asNestable();
      loka::app::scene::INestable *seat = generation->childrenHead()->asNestable();
      return seat ? seat->childrenHead() : 0;
    }

    void showReplacement()
    {
      this->showReplacement_.set(true, true);
      this->markViewDirty(loka::app::scene::NODE_DIRTY_CHILD);
    }

  private:
    loka::app::scene::NodeState<bool> showReplacement_;
    bool initialized_;
  };

  class RetiredGenerationOverlapBoundary : public loka::app::scene::BoundaryNode
  {
  public:
    void reserveProbeNodes(size_t count)
    {
      const size_t probeBytes = sizeof(ArenaRetireProbeNode)
                                + loka::app::scene::detail::AlignOf<ArenaRetireProbeNode>::value;
      this->nodeArena()->reserve(probeBytes * count);
    }

    loka::app::scene::Node *createProbeNode(int *destructorCalls)
    {
      void *memory = this->nodeArena()->allocate(
          sizeof(ArenaRetireProbeNode),
          loka::app::scene::detail::AlignOf<ArenaRetireProbeNode>::value);
      assert(memory != 0);
      ArenaRetireProbeNode *node =
          new (memory) ArenaRetireProbeNode((ArenaRetireProbeProps(destructorCalls)));
      this->nodeArena()->registerNode(node);
      return node;
    }

    void queueSubtree(loka::app::scene::Node *subtree)
    {
      using namespace loka::app::scene;

      ComponentContext context;
      context.setBoundary(this);
      context.setStateOwner(this);
      NestableNode root;
      root.addChild(subtree);
      BoundaryLocalRebuildPlan plan;
      plan.entries.push_back(
          BoundaryLocalRebuildPlanEntry::retire(subtree, subtree->nodeTag()));
      std::vector<Node *> retainedChildren;
      LOKA_VERIFY(this->applyLocalRebuildPlan(context, root, plan, retainedChildren));
      assert(retainedChildren.empty());
    }

    void queueSubtreeThenRetireGeneration(loka::app::scene::Node *subtree)
    {
      this->queueSubtree(subtree);
      this->retireOwnedNodeGeneration();
    }

  protected:
    virtual void composeWithContext(loka::app::scene::ComponentContext &,
                                    loka::app::scene::ComposeEvent)
    {
    }
  };

  class ArenaRetireOrderLeafNode;
  struct ArenaRetireOrderLeafTypeTag
  {
  };
  struct ArenaRetireOrderLeafProps : public loka::app::scene::NodePropsBase<ArenaRetireOrderLeafProps>
  {
    typedef ArenaRetireOrderLeafTypeTag TypeTag;
    typedef ArenaRetireOrderLeafNode NodeType;
    ArenaRetireOrderLeafProps(std::vector<int> *order = 0, int marker = 0)
        : order(order), marker(marker) {}
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId() ? false : this->propsTypeId() < rhs.propsTypeId();
    }
    std::vector<int> *order;
    int marker;
  };
  class ArenaRetireOrderLeafNode : public loka::app::scene::Node
  {
  public:
    typedef ArenaRetireOrderLeafTypeTag TypeTag;
    explicit ArenaRetireOrderLeafNode(const ArenaRetireOrderLeafProps &props) : props(props) {}
    virtual ~ArenaRetireOrderLeafNode()
    {
      if (this->props.order)
      {
        this->props.order->push_back(this->props.marker);
      }
    }
    ArenaRetireOrderLeafProps props;
  };

  class ArenaRetireOrderParentNode;
  struct ArenaRetireOrderParentTypeTag
  {
  };
  struct ArenaRetireOrderParentProps : public loka::app::scene::NodePropsBase<ArenaRetireOrderParentProps>
  {
    typedef ArenaRetireOrderParentTypeTag TypeTag;
    typedef ArenaRetireOrderParentNode NodeType;
    ArenaRetireOrderParentProps(std::vector<int> *order = 0, int marker = 0)
        : order(order), marker(marker) {}
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId() ? false : this->propsTypeId() < rhs.propsTypeId();
    }
    std::vector<int> *order;
    int marker;
  };
  class ArenaRetireOrderParentNode : public loka::app::scene::NestableNode
  {
  public:
    typedef ArenaRetireOrderParentTypeTag TypeTag;
    explicit ArenaRetireOrderParentNode(const ArenaRetireOrderParentProps &props) : props(props) {}
    virtual ~ArenaRetireOrderParentNode()
    {
      if (this->props.order)
      {
        this->props.order->push_back(this->props.marker);
      }
    }
    ArenaRetireOrderParentProps props;
  };
  struct ArenaRetireOrderParentDefinition
      : public loka::app::scene::NestableNodeDefinition<ArenaRetireOrderParentProps,
                                                       ArenaRetireOrderParentNode,
                                                       ArenaRetireOrderParentDefinition>
  {
    typedef loka::app::scene::NestableNodeDefinition<ArenaRetireOrderParentProps,
                                                     ArenaRetireOrderParentNode,
                                                     ArenaRetireOrderParentDefinition> BaseType;
    using BaseType::operator<<;
    ArenaRetireOrderParentDefinition() : BaseType() {}
    explicit ArenaRetireOrderParentDefinition(const ArenaRetireOrderParentProps &props) : BaseType(props) {}
    ArenaRetireOrderParentDefinition(const ArenaRetireOrderParentDefinition &other) : BaseType(other) {}
  };

  class HeapRetireChildNode : public loka::app::scene::Node
  {
  public:
    explicit HeapRetireChildNode(int *destructorCalls)
        : destructorCalls_(destructorCalls)
    {
    }

    virtual ~HeapRetireChildNode()
    {
      if (this->destructorCalls_)
      {
        ++*this->destructorCalls_;
      }
    }

  private:
    int *destructorCalls_;
  };

  class ArenaParentWithHeapChildNode;
  struct ArenaParentWithHeapChildTypeTag
  {
  };
  struct ArenaParentWithHeapChildProps
      : public loka::app::scene::NodePropsBase<ArenaParentWithHeapChildProps>
  {
    typedef ArenaParentWithHeapChildTypeTag TypeTag;
    typedef ArenaParentWithHeapChildNode NodeType;
    ArenaParentWithHeapChildProps(int *parentDestructorCalls = 0,
                                  int *childDestructorCalls = 0)
        : parentDestructorCalls(parentDestructorCalls),
          childDestructorCalls(childDestructorCalls)
    {
    }
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId() ? false : this->propsTypeId() < rhs.propsTypeId();
    }
    int *parentDestructorCalls;
    int *childDestructorCalls;
  };
  class ArenaParentWithHeapChildNode : public loka::app::scene::NestableNode
  {
  public:
    typedef ArenaParentWithHeapChildTypeTag TypeTag;
    explicit ArenaParentWithHeapChildNode(const ArenaParentWithHeapChildProps &props)
        : props(props)
    {
      this->addChild(new HeapRetireChildNode(props.childDestructorCalls));
    }
    virtual ~ArenaParentWithHeapChildNode()
    {
      if (this->props.parentDestructorCalls)
      {
        ++*this->props.parentDestructorCalls;
      }
    }
    ArenaParentWithHeapChildProps props;
  };
  struct ArenaParentWithHeapChildDefinition
      : public loka::app::scene::NestableNodeDefinition<ArenaParentWithHeapChildProps,
                                                       ArenaParentWithHeapChildNode,
                                                       ArenaParentWithHeapChildDefinition>
  {
    typedef loka::app::scene::NestableNodeDefinition<ArenaParentWithHeapChildProps,
                                                     ArenaParentWithHeapChildNode,
                                                     ArenaParentWithHeapChildDefinition> BaseType;
    using BaseType::operator<<;
    ArenaParentWithHeapChildDefinition() : BaseType() {}
    explicit ArenaParentWithHeapChildDefinition(const ArenaParentWithHeapChildProps &props) : BaseType(props) {}
    ArenaParentWithHeapChildDefinition(const ArenaParentWithHeapChildDefinition &other) : BaseType(other) {}
  };

  int *g_heapRetireParentDestructorCalls = 0;
  int *g_heapRetireChildDestructorCalls = 0;

  class NestedArenaRetireLeafNode;
  struct NestedArenaRetireLeafTypeTag
  {
  };
  struct NestedArenaRetireLeafProps
      : public loka::app::scene::NodePropsBase<NestedArenaRetireLeafProps>
  {
    typedef NestedArenaRetireLeafTypeTag TypeTag;
    typedef NestedArenaRetireLeafNode NodeType;
    explicit NestedArenaRetireLeafProps(int *destructorCalls = 0)
        : destructorCalls(destructorCalls)
    {
    }
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId() ? false : this->propsTypeId() < rhs.propsTypeId();
    }
    int *destructorCalls;
  };
  class NestedArenaRetireLeafNode : public loka::app::scene::Node
  {
  public:
    typedef NestedArenaRetireLeafTypeTag TypeTag;
    explicit NestedArenaRetireLeafNode(const NestedArenaRetireLeafProps &props)
        : props(props)
    {
    }
    virtual ~NestedArenaRetireLeafNode()
    {
      if (this->props.destructorCalls)
      {
        ++*this->props.destructorCalls;
      }
    }
    NestedArenaRetireLeafProps props;
  };

  class NestedArenaRetireBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<NestedArenaRetireBoundaryNode>
      NestedArenaRetireBoundaryProps;

  int *g_nestedArenaRetireBoundaryDestructorCalls = 0;
  int *g_nestedArenaRetireLeafDestructorCalls = 0;

  class NestedArenaRetireBoundaryNode
      : public loka::app::scene::BoundaryNodeFor<NestedArenaRetireBoundaryNode>
  {
  public:
    explicit NestedArenaRetireBoundaryNode(const NestedArenaRetireBoundaryProps &props)
        : loka::app::scene::BoundaryNodeFor<NestedArenaRetireBoundaryNode>(props)
    {
    }

    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      // Keep dirtied instances NextTick-pending so pins can observe the
      // pending-target entry across a structural replacement.
      return false;
    }

    virtual ~NestedArenaRetireBoundaryNode()
    {
      if (g_nestedArenaRetireBoundaryDestructorCalls)
      {
        ++*g_nestedArenaRetireBoundaryDestructorCalls;
      }
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      typedef loka::app::scene::NodeDefinition<NestedArenaRetireLeafProps,
                                               NestedArenaRetireLeafNode>
          NestedArenaRetireLeafDefinition;
      loka::app::Fragment root;
      root << NestedArenaRetireLeafDefinition(
          (NestedArenaRetireLeafProps(g_nestedArenaRetireLeafDestructorCalls)));
      composition.declare(root);
    }

    loka::app::scene::Node *innerArenaChild() const
    {
      loka::app::scene::Node *root = this->compositionRootNode();
      loka::app::scene::INestable *nestable = root ? root->asNestable() : 0;
      return nestable ? nestable->childrenHead() : 0;
    }
  };

  class NestedArenaRetireOuterNode;
  struct NestedArenaRetireOuterTypeTag
  {
  };
  struct NestedArenaRetireOuterProps
      : public loka::app::scene::NodePropsBase<NestedArenaRetireOuterProps>
  {
    typedef NestedArenaRetireOuterTypeTag TypeTag;
    typedef NestedArenaRetireOuterNode NodeType;
    explicit NestedArenaRetireOuterProps(int *destructorCalls = 0)
        : destructorCalls(destructorCalls)
    {
    }
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId() ? false : this->propsTypeId() < rhs.propsTypeId();
    }
    int *destructorCalls;
  };
  class NestedArenaRetireOuterNode : public loka::app::scene::NestableNode
  {
  public:
    typedef NestedArenaRetireOuterTypeTag TypeTag;
    explicit NestedArenaRetireOuterNode(const NestedArenaRetireOuterProps &props)
        : props(props)
    {
    }
    virtual ~NestedArenaRetireOuterNode()
    {
      if (this->props.destructorCalls)
      {
        ++*this->props.destructorCalls;
      }
    }
    NestedArenaRetireOuterProps props;
  };
  struct NestedArenaRetireOuterDefinition
      : public loka::app::scene::NestableNodeDefinition<NestedArenaRetireOuterProps,
                                                       NestedArenaRetireOuterNode,
                                                       NestedArenaRetireOuterDefinition>
  {
    typedef loka::app::scene::NestableNodeDefinition<NestedArenaRetireOuterProps,
                                                     NestedArenaRetireOuterNode,
                                                     NestedArenaRetireOuterDefinition> BaseType;
    using BaseType::operator<<;
    NestedArenaRetireOuterDefinition() : BaseType() {}
    explicit NestedArenaRetireOuterDefinition(const NestedArenaRetireOuterProps &props) : BaseType(props) {}
    NestedArenaRetireOuterDefinition(const NestedArenaRetireOuterDefinition &other) : BaseType(other) {}
  };

  int *g_nestedArenaRetireOuterDestructorCalls = 0;
  bool g_heapReplacementNestedBranch = false;

  class NativeBindingStateBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<NativeBindingStateBoundaryNode>
      NativeBindingStateBoundaryProps;

  NativeBindingStateBoundaryNode *g_nativeBindingStateBoundary = 0;
  bool g_includeNativeBindingRetireBranch = false;
  bool *g_nativeBindingStateNodeDestroyed = 0;

  class NativeBindingStateContext : public loka::app::scene::NodeContext
  {
  public:
    NativeBindingStateContext(loka::core::State<int> *state,
                              bool *unboundWhileStateAlive,
                              const bool *nodeDestroyed = 0)
        : state_(state),
          unboundWhileStateAlive_(unboundWhileStateAlive),
          nodeDestroyed_(nodeDestroyed)
    {
      assert(this->state_ != 0);
      this->state_->bind(&NativeBindingStateContext::StateChanged, this, false);
    }

    virtual ~NativeBindingStateContext()
    {
      const bool nodeAlive = !this->nodeDestroyed_ || !*this->nodeDestroyed_;
      const int value = this->state_->get();
      (void)value;
      this->state_->unbind(&NativeBindingStateContext::StateChanged, this);
      *this->unboundWhileStateAlive_ = nodeAlive;
    }

  private:
    static void StateChanged(void *)
    {
    }

    loka::core::State<int> *state_;
    bool *unboundWhileStateAlive_;
    const bool *nodeDestroyed_;
  };

  class NativeBindingStateBoundaryNode
      : public loka::app::scene::BoundaryNodeFor<NativeBindingStateBoundaryNode>
  {
  public:
    explicit NativeBindingStateBoundaryNode(const NativeBindingStateBoundaryProps &props)
        : loka::app::scene::BoundaryNodeFor<NativeBindingStateBoundaryNode>(props),
          nativeValue_(),
          initialized_(false)
    {
    }

    virtual ~NativeBindingStateBoundaryNode()
    {
      if (g_nativeBindingStateNodeDestroyed)
      {
        *g_nativeBindingStateNodeDestroyed = true;
      }
      if (g_nativeBindingStateBoundary == this)
      {
        g_nativeBindingStateBoundary = 0;
      }
    }

    virtual void attachNode(loka::app::scene::NodeComposition &composition)
    {
      (void)composition;
      if (this->initialized_)
      {
        return;
      }
      this->declareStates(1).state(this->nativeValue_, 42);
      g_nativeBindingStateBoundary = this;
      this->initialized_ = true;
    }

    virtual void detachNode(loka::app::scene::NodeComposition &composition)
    {
      (void)composition;
      if (g_nativeBindingStateBoundary == this)
      {
        g_nativeBindingStateBoundary = 0;
      }
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      loka::app::Fragment root;
      composition.declare(root);
    }

    loka::core::State<int> *nativeValueState() const
    {
      return this->nativeValue_.state();
    }

  private:
    loka::app::scene::NodeState<int> nativeValue_;
    bool initialized_;
  };

  class OwnedStateCorpseBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<OwnedStateCorpseBoundaryNode>
      OwnedStateCorpseBoundaryProps;

  int *g_ownedStateCorpseDestructorCalls = 0;
  OwnedStateCorpseBoundaryNode *g_ownedStateCorpseBoundary = 0;

  class OwnedStateCorpseBoundaryNode
      : public loka::app::scene::BoundaryNodeFor<OwnedStateCorpseBoundaryNode>
  {
  public:
    explicit OwnedStateCorpseBoundaryNode(const OwnedStateCorpseBoundaryProps &props)
        : loka::app::scene::BoundaryNodeFor<OwnedStateCorpseBoundaryNode>(props),
          ownedState_(),
          initialized_(false)
    {
    }

    virtual ~OwnedStateCorpseBoundaryNode()
    {
      if (g_ownedStateCorpseBoundary == this)
      {
        g_ownedStateCorpseBoundary = 0;
      }
      if (g_ownedStateCorpseDestructorCalls)
      {
        ++*g_ownedStateCorpseDestructorCalls;
      }
    }

    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }

    virtual void attachNode(loka::app::scene::NodeComposition &composition)
    {
      if (this->initialized_)
      {
        return;
      }
      composition.declareStates().state(this->ownedState_, 0);
      g_ownedStateCorpseBoundary = this;
      this->initialized_ = true;
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      loka::app::Fragment root;
      composition.declare(root);
    }

    bool ownsDeclaredState() const
    {
      return this->ownedState_.isValid() && this->ownedState_.dangerouslyOwner() == this;
    }

    void mutateOwnedStateDuringCorpse()
    {
      this->ownedState_.set(this->ownedState_.get() + 1);
      this->markViewDirty(loka::app::scene::NODE_DIRTY_PROPS);
    }

  private:
    loka::app::scene::NodeState<int> ownedState_;
    bool initialized_;
  };

  class ConditionalArenaRetireProbeNode;
  typedef loka::app::scene::BoundaryPropsFor<ConditionalArenaRetireProbeNode>
      ConditionalArenaRetireProbeProps;

  int *g_conditionalArenaRetireDestructorCalls = 0;
  int *g_conditionalArenaActiveDestructorCalls = 0;
  std::vector<int> *g_conditionalArenaRetireOrder = 0;
  std::vector<int> *g_conditionalArenaActiveOrder = 0;
  ConditionalArenaRetireProbeNode *g_conditionalArenaRetireProbe = 0;

  class ConditionalArenaRetireProbeNode : public loka::app::scene::BoundaryNodeFor<ConditionalArenaRetireProbeNode>
  {
  public:
    explicit ConditionalArenaRetireProbeNode(const ConditionalArenaRetireProbeProps &props)
        : loka::app::scene::BoundaryNodeFor<ConditionalArenaRetireProbeNode>(props),
          showAlternate_(),
          initialized_(false)
    {
    }

    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }

    virtual void attachNode(loka::app::scene::NodeComposition &composition)
    {
      if (this->initialized_)
      {
        return;
      }
      composition.declareStates().state(this->showAlternate_, false);
      // Initial residents must use the arena; replacement tests exhaust it explicitly.
      this->nodeArena()->reserve(16384);
      g_conditionalArenaRetireProbe = this;
      this->initialized_ = true;
    }

    virtual void detachNode(loka::app::scene::NodeComposition &composition)
    {
      (void)composition;
      if (g_conditionalArenaRetireProbe == this)
      {
        g_conditionalArenaRetireProbe = 0;
      }
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      composition.declare(
          ArenaDeclarationFixture<bool>(*this->showAlternate_.state(),
                           this,
                           &ConditionalArenaRetireProbeNode::declareContent,
                           loka::app::reservation::SeatNodes<loka::app::reservation::Nodes<
                               loka::app::FragmentNode,
                               1,
                               loka::app::reservation::Nodes<
                                   ArenaRetireOrderParentNode,
                                   1,
                                   loka::app::reservation::Nodes<
                                       ArenaRetireOrderLeafNode,
                                       1,
                                       loka::app::reservation::Nodes<
                                           ArenaParentWithHeapChildNode,
                                           1,
                                           loka::app::reservation::Nodes<
                                               HeapRetireChildNode,
                                               1,
                                               loka::app::reservation::Nodes<
                                                   NestedArenaRetireOuterNode,
                                                   1,
                                                   loka::app::reservation::Nodes<
                                                       NestedArenaRetireBoundaryNode,
                                                       1,
                                                       loka::app::reservation::Nodes<
                                                           NativeBindingStateBoundaryNode,
                                                           1,
                                                           loka::app::reservation::Nodes<
                                                               OwnedStateCorpseBoundaryNode,
                                                               1,
                                                               loka::app::reservation::Nodes<
                                                                   ArenaRetireProbeNode,
                                                                   1,
                                                                   loka::app::reservation::End> > > > > > > > > > >()));
    }

    void declareContent(loka::app::scene::NodeComposition &composition)
    {
      typedef loka::app::scene::NodeDefinition<ArenaRetireProbeProps, ArenaRetireProbeNode>
          ArenaRetireProbeDefinition;
      loka::app::Fragment root;
      const bool alternate = this->showAlternate_.get();
      if (g_conditionalArenaRetireOrder || g_conditionalArenaActiveOrder)
      {
        typedef loka::app::scene::NodeDefinition<ArenaRetireOrderLeafProps, ArenaRetireOrderLeafNode>
            ArenaRetireOrderLeafDefinition;
        std::vector<int> *order = alternate ? g_conditionalArenaActiveOrder : g_conditionalArenaRetireOrder;
        ArenaRetireOrderParentDefinition branch(
            (ArenaRetireOrderParentProps(order, alternate ? 4 : 2)));
        ArenaRetireOrderLeafDefinition child(
            (ArenaRetireOrderLeafProps(order, alternate ? 3 : 1)));
        branch << child;
        branch.tag(alternate ? 2 : 1);
        root << branch;
      }
      else if ((g_heapRetireParentDestructorCalls || g_heapRetireChildDestructorCalls) && !alternate)
      {
        ArenaParentWithHeapChildDefinition branch(
            (ArenaParentWithHeapChildProps(g_heapRetireParentDestructorCalls,
                                           g_heapRetireChildDestructorCalls)));
        branch.tag(1);
        root << branch;
      }
      else if (g_heapReplacementNestedBranch)
      {
        if (alternate)
        {
          NestedArenaRetireOuterDefinition branch(
              (NestedArenaRetireOuterProps(g_nestedArenaRetireOuterDestructorCalls)));
          loka::app::scene::BoundaryDefinition<NestedArenaRetireBoundaryProps,
                                               NestedArenaRetireBoundaryNode>
              nested = loka::app::scene::Boundary<NestedArenaRetireBoundaryNode>();
          branch << nested;
          branch.tag(2);
          root << branch;
        }
        else
        {
          ArenaRetireProbeDefinition branch(
              (ArenaRetireProbeProps(g_conditionalArenaRetireDestructorCalls)));
          branch.tag(1);
          root << branch;
        }
      }
      else if ((g_nestedArenaRetireOuterDestructorCalls ||
                g_nestedArenaRetireBoundaryDestructorCalls ||
                g_nestedArenaRetireLeafDestructorCalls) &&
               !alternate)
      {
        NestedArenaRetireOuterDefinition branch(
            (NestedArenaRetireOuterProps(g_nestedArenaRetireOuterDestructorCalls)));
        loka::app::scene::BoundaryDefinition<NestedArenaRetireBoundaryProps,
                                             NestedArenaRetireBoundaryNode>
            nested = loka::app::scene::Boundary<NestedArenaRetireBoundaryNode>();
        branch << nested;
        branch.tag(1);
        root << branch;
      }
      else if (g_includeNativeBindingRetireBranch && !alternate)
      {
        loka::app::scene::BoundaryDefinition<NativeBindingStateBoundaryProps,
                                             NativeBindingStateBoundaryNode>
            branch = loka::app::scene::Boundary<NativeBindingStateBoundaryNode>();
        branch.tag(1);
        root << branch;
      }
      else if (g_ownedStateCorpseDestructorCalls && !alternate)
      {
        loka::app::scene::BoundaryDefinition<OwnedStateCorpseBoundaryProps,
                                             OwnedStateCorpseBoundaryNode>
            branch = loka::app::scene::Boundary<OwnedStateCorpseBoundaryNode>();
        branch.tag(1);
        root << branch;
      }
      else
      {
        int *destructorCalls = alternate ? g_conditionalArenaActiveDestructorCalls
                                         : g_conditionalArenaRetireDestructorCalls;
        ArenaRetireProbeDefinition branch((ArenaRetireProbeProps(destructorCalls)));
        branch.tag(alternate ? 2 : 1);
        root << branch;
      }
      composition.declare(root);
    }

    loka::app::scene::Node *activeBranchNode() const
    {
      // Keyed now encloses its declaration Fragment in a runtime generation owner.
      loka::app::scene::INestable *generation = this->compositionRootNode()->asNestable();
      loka::app::scene::INestable *seat = generation->childrenHead()->asNestable();
      loka::app::scene::Node *root = seat ? seat->childrenHead() : 0;
      loka::app::scene::INestable *branch = root ? root->asNestable() : 0;
      return branch ? branch->childrenHead() : 0;
    }

    void showAlternate()
    {
      this->setAlternate(true);
    }

    void setAlternate(bool value)
    {
      // Pin heap fallback independently of runtime wrapper sizes.
      while (this->nodeArena()->allocate(256, 1))
      {
      }
      while (this->nodeArena()->allocate(1, 1))
      {
      }

      this->showAlternate_.set(value, true);
      this->markViewDirty(loka::app::scene::NODE_DIRTY_CHILD);
    }

  private:
    loka::app::scene::NodeState<bool> showAlternate_;
    bool initialized_;
  };

  class ObservedCorpseBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<ObservedCorpseBoundaryNode> ObservedCorpseBoundaryProps;

  loka::core::MutableState<int> *g_observedCorpseExternalState = 0;
  int *g_observedCorpseDestructorCalls = 0;

  class ObservedCorpseBoundaryNode : public loka::app::scene::BoundaryNodeFor<ObservedCorpseBoundaryNode>
  {
  public:
    explicit ObservedCorpseBoundaryNode(const ObservedCorpseBoundaryProps &props)
        : loka::app::scene::BoundaryNodeFor<ObservedCorpseBoundaryNode>(props)
    {
    }

    virtual ~ObservedCorpseBoundaryNode()
    {
      ++*g_observedCorpseDestructorCalls;
    }

    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }

    virtual void declareDirtySources(loka::app::scene::DirtySourceRegistrar &registrar)
    {
      registrar.markDirtyOnChange(g_observedCorpseExternalState, loka::app::scene::NODE_DIRTY_PROPS);
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      loka::app::Fragment root;
      composition.declare(root);
    }
  };

  class DirectRootObservedBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<DirectRootObservedBoundaryNode> DirectRootObservedBoundaryProps;

  loka::core::MutableState<int> *g_directRootObservedState = 0;
  DirectRootObservedBoundaryNode *g_directRootObserved = 0;

  // #127: a boundary mounted DIRECTLY at the scene root (rootDefinition_ is a
  // Boundary) that observes external state via declareDirtySources. The
  // direct-root compose path (Scene::prepareRootBoundaryCompose) must register
  // these sources the same way the generic composeTree walk does for nested
  // boundaries, on every non-DETACH compose window.
  class DirectRootObservedBoundaryNode
      : public loka::app::scene::BoundaryNodeFor<DirectRootObservedBoundaryNode>
  {
  public:
    explicit DirectRootObservedBoundaryNode(const DirectRootObservedBoundaryProps &props)
        : loka::app::scene::BoundaryNodeFor<DirectRootObservedBoundaryNode>(props)
    {
      g_directRootObserved = this;
    }

    virtual ~DirectRootObservedBoundaryNode()
    {
      if (g_directRootObserved == this)
      {
        g_directRootObserved = 0;
      }
    }

    // Defer projection work (like ObservedCorpseBoundaryNode) so an observed-state
    // write leaves a pending boundary update we can observe, rather than being
    // consumed by an immediate synchronous re-compose.
    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }

    virtual void declareDirtySources(loka::app::scene::DirtySourceRegistrar &registrar)
    {
      if (g_directRootObservedState)
      {
        registrar.markDirtyOnChange(g_directRootObservedState, loka::app::scene::NODE_DIRTY_PROPS);
      }
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      loka::app::Fragment root;
      composition.declare(root);
    }
  };

  class CorpseRetireHarnessBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<CorpseRetireHarnessBoundaryNode> CorpseRetireHarnessBoundaryProps;

  CorpseRetireHarnessBoundaryNode *g_corpseRetireHarness = 0;

  class CorpseRetireHarnessBoundaryNode
      : public loka::app::scene::BoundaryNodeFor<CorpseRetireHarnessBoundaryNode>
  {
  public:
    explicit CorpseRetireHarnessBoundaryNode(const CorpseRetireHarnessBoundaryProps &props)
        : loka::app::scene::BoundaryNodeFor<CorpseRetireHarnessBoundaryNode>(props)
    {
      g_corpseRetireHarness = this;
    }

    virtual ~CorpseRetireHarnessBoundaryNode()
    {
      if (g_corpseRetireHarness == this)
      {
        g_corpseRetireHarness = 0;
      }
    }

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      loka::app::Fragment root;
      loka::app::scene::BoundaryDefinition<ObservedCorpseBoundaryProps, ObservedCorpseBoundaryNode> branch =
          loka::app::scene::Boundary<ObservedCorpseBoundaryNode>();
      branch.tag(1);
      root << branch;
      composition.declare(root);
    }

    ObservedCorpseBoundaryNode *observedBranch() const
    {
      loka::app::scene::Node *root = this->compositionRootNode();
      loka::app::scene::INestable *nestable = root ? root->asNestable() : 0;
      loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0;
      return child ? static_cast<ObservedCorpseBoundaryNode *>(child->asBoundary()) : 0;
    }

    ObservedCorpseBoundaryNode *retireObservedBranch(loka::app::scene::Scene &scene,
                                                     loka::app::scene::IPlatformController &platform)
    {
      using namespace loka::app::scene;
      ObservedCorpseBoundaryNode *retired = this->observedBranch();
      assert(retired != 0);

      ComponentContext context;
      context.setBoundary(this);
      context.setStateOwner(this);
      context.setScene(&scene);
      context.setPlatformController(&platform);

      NodeDefinition<ArenaRetireProbeProps, ArenaRetireProbeNode> replacement(
          (ArenaRetireProbeProps(g_observedCorpseDestructorCalls)));
      replacement.tag(2);
      NodeComposition composition;
      composition.setContext(&context);
      NodeMaterializationResult result =
          loka::app::scene::testing::NodeCompositionTestAccess::
              createNodeFromDefinitionResult(composition, &replacement);
      assert(!result.allocationFailed);
      Node *created = result.root;
      assert(created != 0);

      BoundaryLocalRebuildPlan plan;
      plan.entries.push_back(BoundaryLocalRebuildPlanEntry::replace(created, retired, 2));
      std::vector<Node *> retainedChildren;
      INestable *root = this->compositionRootNestable();
      assert(root != 0);
      LOKA_VERIFY(this->applyLocalRebuildPlan(context, *root, plan, retainedChildren));
      return retired;
    }
  };

  struct DetachProbePlatformController : public loka::app::scene::IPlatformController
  {
    virtual void onChange(loka::app::scene::Node *, loka::app::scene::NodeDirtyFlags, bool) {}
    virtual void synchronize() {}
    virtual bool hasPendingSync() const
    {
      return false;
    }
    virtual void destroy() {}
  };

  struct NativeBindingProbePlatformController : public DetachProbePlatformController
  {
    explicit NativeBindingProbePlatformController(bool *unboundWhileStateAlive,
                                                  const bool *nodeDestroyed = 0)
        : unboundWhileStateAlive_(unboundWhileStateAlive),
          nodeDestroyed_(nodeDestroyed)
    {
    }

    virtual void onChange(loka::app::scene::Node *, loka::app::scene::NodeDirtyFlags, bool)
    {
      if (g_nativeBindingStateBoundary && !g_nativeBindingStateBoundary->getContext())
      {
        g_nativeBindingStateBoundary->setContext(
            new NativeBindingStateContext(g_nativeBindingStateBoundary->nativeValueState(),
                                          this->unboundWhileStateAlive_,
                                          this->nodeDestroyed_));
      }
    }

  private:
    bool *unboundWhileStateAlive_;
    const bool *nodeDestroyed_;
  };
} // namespace

void testSceneUnmountNotifiesPlainNodeContextDetached()
{
  typedef loka::app::scene::NodeDefinition<PlainDetachProbeProps, PlainDetachProbeNode> PlainDetachProbeDefinition;

  DetachHookCounts counts;
  PlainDetachProbeDefinition definition((PlainDetachProbeProps(&counts)));
  loka::app::scene::NodeDefinitionBase *rootDefinition = definition.clone();
  LOKA_VERIFY(rootDefinition != 0);
  loka::app::scene::Scene scene(rootDefinition);
  DetachProbePlatformController platform;

  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  assert(counts.detachCalls == 0);

  loka::dsl::testing::SceneTestAccess::unmount(scene);

  assert(counts.detachCalls == 1);
}

void testSceneUpdateAttachedFalseNotifiesPlainNodeContextDetachedOnce()
{
  typedef loka::app::scene::NodeDefinition<PlainDetachProbeProps, PlainDetachProbeNode> PlainDetachProbeDefinition;

  DetachHookCounts counts;
  PlainDetachProbeDefinition definition((PlainDetachProbeProps(&counts)));
  loka::app::scene::NodeDefinitionBase *rootDefinition = definition.clone();
  LOKA_VERIFY(rootDefinition != 0);
  loka::app::scene::Scene scene(rootDefinition);
  DetachProbePlatformController platform;

  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  assert(counts.attachCalls == 1);
  assert(counts.detachCalls == 0);

  loka::dsl::testing::SceneTestAccess::updateAttached(scene, false);

  assert(counts.detachCalls == 1);
}

void testSceneTeardownNotifiesBoundaryInternalNodeContextDetachedOnce()
{
  using namespace loka::app::scene;

  DetachHookCounts counts;
  g_boundaryInternalCounts = &counts;
  Scene scene((Boundary<BoundaryInternalProbeNode>()));
  DetachProbePlatformController platform;

  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  assert(counts.attachCalls == 1);
  assert(counts.detachCalls == 0);

  loka::dsl::testing::SceneTestAccess::updateAttached(scene, false);

  assert(counts.detachCalls == 1);
  g_boundaryInternalCounts = 0;
}

void testRootDetachChildWalkRetainsBoundaryStateOwner()
{
  using namespace loka::app::scene;

  DetachProbePlatformController platform;
  DetachStateOwnerObservation rootObservation;
  g_detachStateOwnerObservation = &rootObservation;
  {
    Scene scene((Boundary<RootDetachStateOwnerBoundaryNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  assert(rootObservation.detachCalls == 1);
  assert(rootObservation.currentBoundaryValid &&
         "root child detach must retain the root boundary as current state owner");
  assert(rootObservation.parentStateReadable &&
         "root child detach must read state declared by the root boundary");
  assert(rootObservation.parentStateValue == 37);

  DetachStateOwnerObservation nestedObservation;
  g_detachStateOwnerObservation = &nestedObservation;
  {
    Scene scene((Boundary<NestedDetachStateOwnerHarnessNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  assert(nestedObservation.detachCalls == 1);
  assert(nestedObservation.currentBoundaryValid);
  assert(nestedObservation.parentStateReadable);
  assert(nestedObservation.parentStateValue == 73);

  g_detachStateOwnerObservation = 0;
  g_detachParentState = 0;
}

void testConditionalBranchSwapDestroysRetiredArenaNodeOnNextTrackerRun()
{
  using namespace loka::app::scene;

  int destructorCalls = 0;
  g_conditionalArenaRetireDestructorCalls = &destructorCalls;

  {
    DetachProbePlatformController platform;
    Scene scene((Boundary<ConditionalArenaRetireProbeNode>()));

    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_conditionalArenaRetireProbe != 0);
    Node *retiringBranch = g_conditionalArenaRetireProbe->activeBranchNode();
    (void)retiringBranch;
    assert(retiringBranch != 0);
    assert(retiringBranch->isArenaAllocated() &&
           "conditional retire test must exercise an arena-allocated branch");
    assert(destructorCalls == 0);

    g_conditionalArenaRetireProbe->showAlternate();
    LOKA_VERIFY(scene.flushInvalidation());

    assert(g_conditionalArenaRetireProbe->activeBranchNode() != retiringBranch &&
           "conditional tag swap must replace the retiring branch");
    assert(scene.hasPendingInvalidation() &&
           "arena retirement must schedule a later tracker run");
    assert(destructorCalls == 0 &&
           "retired arena node must remain alive through the retiring apply");

    LOKA_VERIFY(!scene.flushInvalidation() &&
           "a drain-only tracker run must not report a refresh snapshot");

    assert(destructorCalls == 1 &&
           "retired arena node destructor must run at the next tracker run");
  }

  assert(destructorCalls == 1 &&
         "Scene teardown must not destroy an already drained arena node again");

  g_conditionalArenaRetireDestructorCalls = 0;
  g_conditionalArenaActiveDestructorCalls = 0;
  g_conditionalArenaRetireProbe = 0;
}

void testRetiredGenerationSubsumesQueuedArenaSubtreeExactlyOnce()
{
  using namespace loka::app::scene;

  int queuedDestructorCalls = 0;
  int ledgerDestructorCalls = 0;

  {
    RetiredGenerationOverlapBoundary boundary;
    boundary.reserveProbeNodes(2);
    Node *queued = boundary.createProbeNode(&queuedDestructorCalls);
    Node *ledgerOnly = boundary.createProbeNode(&ledgerDestructorCalls);
    assert(queued->isArenaAllocated());
    (void)ledgerOnly;
    assert(ledgerOnly->isArenaAllocated());

    boundary.queueSubtreeThenRetireGeneration(queued);

    assert(!boundary.nodeArena()->hasCapacity() &&
           "generation retirement must detach the arena after queue subsumption");
    assert(queuedDestructorCalls == 0);
    assert(ledgerDestructorCalls == 0);

    boundary.drainRetiredSubtreesAtNextTrackerRun();
    assert(queuedDestructorCalls == 1 &&
           "the subtree-queued node must be destroyed once by its retired generation");
    assert(ledgerDestructorCalls == 1 &&
           "the remaining generation ledger node must be destroyed once");

    boundary.drainRetiredSubtreesAtNextTrackerRun();
    assert(queuedDestructorCalls == 1);
    assert(ledgerDestructorCalls == 1);
  }

  assert(queuedDestructorCalls == 1 &&
         "Boundary teardown must not revisit the subsumed queue entry");
  assert(ledgerDestructorCalls == 1 &&
         "Boundary teardown must not revisit the drained generation");
}

void testRootReplacementDestroysRetiredArenaNodeOnNextTrackerRun()
{
  using namespace loka::app::scene;

  int retiredDestructorCalls = 0;
  g_rootReplacementRetireDestructorCalls = &retiredDestructorCalls;

  {
    DetachProbePlatformController platform;
    Scene scene((Boundary<RootReplacementArenaRetireBoundaryNode>()));

    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_rootReplacementArenaRetireBoundary != 0);
    Node *retiringRoot = g_rootReplacementArenaRetireBoundary->activeRootNode();
    (void)retiringRoot;
    assert(retiringRoot != 0);
    assert(retiringRoot->asNestable() == 0 &&
           "root replacement retire test must exercise a non-nestable root");
    assert(retiringRoot->isArenaAllocated() &&
           "root replacement retire test must exercise an arena-allocated root");
    assert(retiredDestructorCalls == 0);

    g_rootReplacementArenaRetireBoundary->showReplacement();
    LOKA_VERIFY(scene.flushInvalidation());

    assert(g_rootReplacementArenaRetireBoundary->activeRootNode() != retiringRoot &&
           "incompatible root definition type must replace the retiring root");
    assert(retiredDestructorCalls == 0 &&
           "retired arena root must remain alive through the retiring apply");

    LOKA_VERIFY(!scene.flushInvalidation() &&
           "a drain-only tracker run must not report a refresh snapshot");

    assert(retiredDestructorCalls == 1 &&
           "retired arena root destructor must run at the next tracker run");
  }

  assert(retiredDestructorCalls == 1 &&
         "Scene teardown must not destroy an already drained arena root again");

  g_rootReplacementRetireDestructorCalls = 0;
  g_rootReplacementArenaRetireBoundary = 0;
}

void testRetiringNativeContextUnbindsBeforeNodeOwnedStateReclaim()
{
  using namespace loka::app::scene;

  bool unboundWhileStateAlive = false;
  g_includeNativeBindingRetireBranch = true;

  {
    NativeBindingProbePlatformController platform(&unboundWhileStateAlive);
    Scene scene((Boundary<ConditionalArenaRetireProbeNode>()));

    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_conditionalArenaRetireProbe != 0);
    assert(g_nativeBindingStateBoundary != 0);
    LOKA_VERIFY(g_nativeBindingStateBoundary->getContext() != 0);

    g_conditionalArenaRetireProbe->showAlternate();
    LOKA_VERIFY(scene.flushInvalidation());
    assert(unboundWhileStateAlive &&
           "retiring native context must unbind before its node-owned state is reclaimed");
  }

  g_includeNativeBindingRetireBranch = false;
  g_nativeBindingStateBoundary = 0;
  g_conditionalArenaRetireProbe = 0;
}

void testSceneDestructionUnbindsNativeContextBeforeNodeOwnedStateReclaim()
{
  using namespace loka::app::scene;

  bool unboundWhileStateAlive = false;
  NativeBindingProbePlatformController platform(&unboundWhileStateAlive);

  {
    Scene scene((Boundary<NativeBindingStateBoundaryNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_nativeBindingStateBoundary != 0);
    LOKA_VERIFY(g_nativeBindingStateBoundary->getContext() != 0);
    assert(!unboundWhileStateAlive);
  }

  assert(unboundWhileStateAlive &&
         "Scene destruction must unbind native context before its node-owned state is reclaimed");
  g_nativeBindingStateBoundary = 0;
}

void testSceneTeardownDrainsNonEmptyRetiredArenaSubtreeExactlyOnce()
{
  using namespace loka::app::scene;

  std::vector<int> destroyOrder;
  g_conditionalArenaRetireOrder = &destroyOrder;
  g_conditionalArenaActiveOrder = &destroyOrder;

  {
    DetachProbePlatformController platform;
    Scene scene((Boundary<ConditionalArenaRetireProbeNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_conditionalArenaRetireProbe != 0);
    Node *retiringBranch = g_conditionalArenaRetireProbe->activeBranchNode();
    (void)retiringBranch;
    assert(retiringBranch != 0 && retiringBranch->isArenaAllocated());

    g_conditionalArenaRetireProbe->showAlternate();
    LOKA_VERIFY(scene.flushInvalidation());
    assert(scene.hasPendingInvalidation() &&
           "retirement must leave a later drain pending at Scene teardown");
    assert(destroyOrder.empty() &&
           "the retiring flush must not reclaim the queued arena subtree");
  }

  assert(destroyOrder.size() == 4);
  assert(destroyOrder[0] == 1 && destroyOrder[1] == 2 &&
         "Scene teardown must drain the retired subtree before clearing live arena storage");
  assert(destroyOrder[2] == 3 && destroyOrder[3] == 4 && "live owner tree also reclaims children before parents");

  g_conditionalArenaRetireOrder = 0;
  g_conditionalArenaActiveOrder = 0;
  g_conditionalArenaRetireProbe = 0;
}

void testBoundaryTeardownDrainsPendingRetiredGenerationExactlyOnce()
{
  int destructorCalls = 0;
  {
    RetiredGenerationOverlapBoundary boundary;
    boundary.reserveProbeNodes(1);
    loka::app::scene::Node *retiring = boundary.createProbeNode(&destructorCalls);
    assert(retiring->isArenaAllocated());
    boundary.queueSubtreeThenRetireGeneration(retiring);
    assert(destructorCalls == 0);
  }
  assert(destructorCalls == 1 &&
         "owner teardown must drain a pending retired generation exactly once");
}

void testConditionalBranchSwapDestroysRetiredArenaSubtreeChildrenFirst()
{
  using namespace loka::app::scene;

  std::vector<int> destroyOrder;
  g_conditionalArenaRetireOrder = &destroyOrder;

  {
    DetachProbePlatformController platform;
    Scene scene((Boundary<ConditionalArenaRetireProbeNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_conditionalArenaRetireProbe != 0);
    Node *retiringBranch = g_conditionalArenaRetireProbe->activeBranchNode();
    (void)retiringBranch;
    assert(retiringBranch != 0 && retiringBranch->isArenaAllocated());

    g_conditionalArenaRetireProbe->showAlternate();
    LOKA_VERIFY(scene.flushInvalidation());
    assert(scene.hasPendingInvalidation() &&
           "arena subtree retirement must schedule a later tracker run");
    assert(destroyOrder.empty() &&
           "retired arena subtree must remain alive through the retiring apply");

    LOKA_VERIFY(!scene.flushInvalidation() &&
           "a drain-only tracker run must not report a refresh snapshot");
    assert(destroyOrder.size() == 2);
    assert(destroyOrder[0] == 1 &&
           "retired arena subtree must destroy children before parents");
    assert(destroyOrder[1] == 2);
  }

  assert(destroyOrder.size() == 2 &&
         "Scene teardown must not append duplicate subtree destructor records");
  assert(destroyOrder[0] == 1);
  assert(destroyOrder[1] == 2);

  g_conditionalArenaRetireOrder = 0;
  g_conditionalArenaActiveOrder = 0;
  g_conditionalArenaRetireProbe = 0;
}

void testRetiredArenaParentDestroysHeapChildExactlyOnceAtDrain()
{
  using namespace loka::app::scene;

  int parentDestructorCalls = 0;
  int heapChildDestructorCalls = 0;
  g_heapRetireParentDestructorCalls = &parentDestructorCalls;
  g_heapRetireChildDestructorCalls = &heapChildDestructorCalls;

  {
    DetachProbePlatformController platform;
    Scene scene((Boundary<ConditionalArenaRetireProbeNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_conditionalArenaRetireProbe != 0);
    Node *retiringBranch = g_conditionalArenaRetireProbe->activeBranchNode();
    INestable *retiringParent = retiringBranch ? retiringBranch->asNestable() : 0;
    Node *heapChild = retiringParent ? retiringParent->childrenHead() : 0;
    assert(retiringBranch != 0 && retiringBranch->isArenaAllocated());
    (void)heapChild;
    assert(heapChild != 0 && !heapChild->isArenaAllocated() &&
           "drain test must place a heap child under the retired arena parent");

    g_conditionalArenaRetireProbe->showAlternate();
    LOKA_VERIFY(scene.flushInvalidation());
    assert(scene.hasPendingInvalidation());
    assert(parentDestructorCalls == 0);
    assert(heapChildDestructorCalls == 0);

    LOKA_VERIFY(!scene.flushInvalidation() &&
           "heap-child reclamation alone must not report a refresh snapshot");
    assert(parentDestructorCalls == 1);
    assert(heapChildDestructorCalls == 1);
  }

  assert(parentDestructorCalls == 1);
  assert(heapChildDestructorCalls == 1 &&
         "Scene teardown must not delete an already drained heap child again");

  g_heapRetireParentDestructorCalls = 0;
  g_heapRetireChildDestructorCalls = 0;
  g_conditionalArenaRetireProbe = 0;
}

void testConditionalBranchSwapBackDestroysRetiredHeapNodeOnNextTrackerRun()
{
  using namespace loka::app::scene;

  int retireCalls = 0;
  int activeCalls = 0;
  g_conditionalArenaRetireDestructorCalls = &retireCalls;
  g_conditionalArenaActiveDestructorCalls = &activeCalls;

  {
    DetachProbePlatformController platform;
    Scene scene((Boundary<ConditionalArenaRetireProbeNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_conditionalArenaRetireProbe != 0);
    Node *initialBranch = g_conditionalArenaRetireProbe->activeBranchNode();
    (void)initialBranch;
    assert(initialBranch != 0 && initialBranch->isArenaAllocated());

    g_conditionalArenaRetireProbe->showAlternate();
    LOKA_VERIFY(scene.flushInvalidation());
    Node *heapBranch = g_conditionalArenaRetireProbe->activeBranchNode();
    (void)heapBranch;
    assert(heapBranch != 0 && heapBranch != initialBranch);
    assert(!heapBranch->isArenaAllocated() &&
           "the swapped-in branch must land on the heap once the slab is consumed");
    LOKA_VERIFY(!scene.flushInvalidation());
    assert(retireCalls == 1 && activeCalls == 0);

    g_conditionalArenaRetireProbe->setAlternate(false);
    LOKA_VERIFY(scene.flushInvalidation());
    assert(activeCalls == 0 &&
           "the retired heap branch must remain alive through the retiring apply");

    LOKA_VERIFY(!scene.flushInvalidation() &&
           "a drain-only tracker run must not report a refresh snapshot");
    assert(activeCalls == 1 &&
           "the retired heap branch destructor must run at the next tracker run");
    assert(retireCalls == 1);
  }

  assert(activeCalls == 1 &&
         "Scene teardown must not delete an already drained heap branch again");
  assert(retireCalls == 2 &&
         "Scene teardown must reclaim the swapped-back heap branch exactly once");

  g_conditionalArenaRetireDestructorCalls = 0;
  g_conditionalArenaActiveDestructorCalls = 0;
  g_conditionalArenaRetireProbe = 0;
}

void testPendingChildBoundaryUpdateSurvivesHeapSubtreeReplacement()
{
  using namespace loka::app::scene;

  int outerDestructorCalls = 0;
  int nestedBoundaryDestructorCalls = 0;
  g_heapReplacementNestedBranch = true;
  g_nestedArenaRetireOuterDestructorCalls = &outerDestructorCalls;
  g_nestedArenaRetireBoundaryDestructorCalls = &nestedBoundaryDestructorCalls;

  {
    DetachProbePlatformController platform;
    Scene scene((Boundary<ConditionalArenaRetireProbeNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_conditionalArenaRetireProbe != 0);
    g_conditionalArenaRetireProbe->showAlternate();
    LOKA_VERIFY(scene.flushInvalidation());
    LOKA_VERIFY(!scene.flushInvalidation());

    Node *outer = g_conditionalArenaRetireProbe->activeBranchNode();
    assert(outer != 0 && !outer->isArenaAllocated() &&
           "the replacement subtree must land on the heap once the slab is consumed");
    INestable *outerNestable = outer->asNestable();
    Node *nestedNode = outerNestable ? outerNestable->childrenHead() : 0;
    BoundaryNode *nested = nestedNode ? nestedNode->asBoundary() : 0;
    assert(nested != 0 && !nestedNode->isArenaAllocated());

    nested->markViewDirty(NODE_DIRTY_PROPS);
    assert(scene.director().hasPendingBoundary(nested) &&
           "the nested boundary must be a pending update target before the swap");

    g_conditionalArenaRetireProbe->setAlternate(false);
    LOKA_VERIFY(scene.flushInvalidation() &&
           "the flush that replaces the pending target's subtree must complete");
    assert(nestedBoundaryDestructorCalls == 0 && outerDestructorCalls == 0 &&
           "the replaced heap subtree must remain alive through the flush that walks its pending entry");

    LOKA_VERIFY(!scene.flushInvalidation());
    assert(nestedBoundaryDestructorCalls == 1 && outerDestructorCalls == 1 &&
           "the replaced heap subtree must be reclaimed at the next tracker run");
  }

  assert(nestedBoundaryDestructorCalls == 1);
  assert(outerDestructorCalls == 1);

  g_heapReplacementNestedBranch = false;
  g_nestedArenaRetireOuterDestructorCalls = 0;
  g_nestedArenaRetireBoundaryDestructorCalls = 0;
  g_conditionalArenaRetireProbe = 0;
}

void testRetiredHeapParentDestroysArenaChildExactlyOnceAtDrain()
{
  using namespace loka::app::scene;

  int heapParentCalls = 0;
  int heapChildCalls = 0;
  int arenaChildCalls = 0;

  {
    RetiredGenerationOverlapBoundary boundary;
    boundary.reserveProbeNodes(1);
    Node *arenaChild = boundary.createProbeNode(&arenaChildCalls);
    ArenaParentWithHeapChildNode *heapRoot = new ArenaParentWithHeapChildNode(
        (ArenaParentWithHeapChildProps(&heapParentCalls, &heapChildCalls)));
    heapRoot->addChild(arenaChild);
    assert(!heapRoot->isArenaAllocated());
    assert(arenaChild->isArenaAllocated());

    boundary.queueSubtree(heapRoot);
    assert(heapParentCalls == 0 && heapChildCalls == 0 && arenaChildCalls == 0 &&
           "the queued heap subtree must remain alive through the retiring apply");

    boundary.drainRetiredSubtreesAtNextTrackerRun();
    assert(heapParentCalls == 1 && heapChildCalls == 1 && arenaChildCalls == 1 &&
           "the drain must reclaim the heap root, its heap child, and its arena child exactly once");

    boundary.drainRetiredSubtreesAtNextTrackerRun();
    assert(heapParentCalls == 1 && heapChildCalls == 1 && arenaChildCalls == 1);
  }

  assert(heapParentCalls == 1 && heapChildCalls == 1);
  assert(arenaChildCalls == 1 &&
         "Boundary teardown must not revisit the tombstoned arena child");
}

void testRetiredGenerationSubsumesQueuedHeapSubtreeExactlyOnce()
{
  using namespace loka::app::scene;

  int heapParentCalls = 0;
  int heapChildCalls = 0;
  int arenaChildCalls = 0;

  {
    RetiredGenerationOverlapBoundary boundary;
    boundary.reserveProbeNodes(1);
    Node *arenaChild = boundary.createProbeNode(&arenaChildCalls);
    ArenaParentWithHeapChildNode *heapRoot = new ArenaParentWithHeapChildNode(
        (ArenaParentWithHeapChildProps(&heapParentCalls, &heapChildCalls)));
    heapRoot->addChild(arenaChild);
    assert(!heapRoot->isArenaAllocated());

    boundary.queueSubtreeThenRetireGeneration(heapRoot);
    assert(!boundary.nodeArena()->hasCapacity() &&
           "generation retirement must detach the arena after subsuming the queue");
    assert(heapParentCalls == 0 && heapChildCalls == 0 && arenaChildCalls == 0);

    boundary.drainRetiredSubtreesAtNextTrackerRun();
    assert(heapParentCalls == 1 &&
           "the subsumed heap root must be destroyed once by its retired generation");
    assert(heapChildCalls == 1 && arenaChildCalls == 1);

    boundary.drainRetiredSubtreesAtNextTrackerRun();
    assert(heapParentCalls == 1 && heapChildCalls == 1 && arenaChildCalls == 1);
  }

  assert(heapParentCalls == 1 && heapChildCalls == 1);
  assert(arenaChildCalls == 1 &&
         "Boundary teardown must not revisit the drained generation");
}

void testRetiredSubtreeDestroysNestedBoundaryArenaExactlyOnce()
{
  using namespace loka::app::scene;

  int outerDestructorCalls = 0;
  int nestedBoundaryDestructorCalls = 0;
  int innerArenaChildDestructorCalls = 0;
  g_nestedArenaRetireOuterDestructorCalls = &outerDestructorCalls;
  g_nestedArenaRetireBoundaryDestructorCalls = &nestedBoundaryDestructorCalls;
  g_nestedArenaRetireLeafDestructorCalls = &innerArenaChildDestructorCalls;

  {
    DetachProbePlatformController platform;
    Scene scene((Boundary<ConditionalArenaRetireProbeNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_conditionalArenaRetireProbe != 0);
    Node *retiringBranch = g_conditionalArenaRetireProbe->activeBranchNode();
    INestable *outer = retiringBranch ? retiringBranch->asNestable() : 0;
    Node *nestedNode = outer ? outer->childrenHead() : 0;
    NestedArenaRetireBoundaryNode *nested =
        nestedNode ? static_cast<NestedArenaRetireBoundaryNode *>(nestedNode->asBoundary()) : 0;
    Node *innerArenaChild = nested ? nested->innerArenaChild() : 0;
    assert(retiringBranch != 0 && retiringBranch->isArenaAllocated());
    assert(nested != 0 && nested->isArenaAllocated());
    (void)innerArenaChild;
    assert(innerArenaChild != 0 && innerArenaChild->isArenaAllocated() &&
           "nested boundary test must exercise the nested Boundary's own node arena");

    g_conditionalArenaRetireProbe->showAlternate();
    LOKA_VERIFY(scene.flushInvalidation());
    assert(scene.hasPendingInvalidation());
    assert(outerDestructorCalls == 0);
    assert(nestedBoundaryDestructorCalls == 0);
    assert(innerArenaChildDestructorCalls == 0);

    LOKA_VERIFY(!scene.flushInvalidation() &&
           "nested arena reclamation alone must not report a refresh snapshot");
    assert(outerDestructorCalls == 1);
    assert(nestedBoundaryDestructorCalls == 1);
    assert(innerArenaChildDestructorCalls == 1);
  }

  assert(outerDestructorCalls == 1);
  assert(nestedBoundaryDestructorCalls == 1);
  assert(innerArenaChildDestructorCalls == 1);

  g_nestedArenaRetireOuterDestructorCalls = 0;
  g_nestedArenaRetireBoundaryDestructorCalls = 0;
  g_nestedArenaRetireLeafDestructorCalls = 0;
  g_conditionalArenaRetireProbe = 0;
}

void testRetiredBoundaryOwnedStateMutationIsQuiescent()
{
  using namespace loka::app::scene;

  int destructorCalls = 0;
  g_ownedStateCorpseDestructorCalls = &destructorCalls;

  {
    DetachProbePlatformController platform;
    Scene scene((Boundary<ConditionalArenaRetireProbeNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_conditionalArenaRetireProbe != 0);
    OwnedStateCorpseBoundaryNode *retired = g_ownedStateCorpseBoundary;
    assert(retired != 0);
    assert(retired == g_conditionalArenaRetireProbe->activeBranchNode()->asBoundary());
    assert(retired->isArenaAllocated());
    assert(retired->ownsDeclaredState() &&
           "corpse quiescence test requires state owned by the retired Boundary");

    g_conditionalArenaRetireProbe->showAlternate();
    LOKA_VERIFY(scene.flushInvalidation());
    assert(scene.hasPendingInvalidation());
    assert(destructorCalls == 0);

    retired->mutateOwnedStateDuringCorpse();
    LOKA_VERIFY(!scene.flushInvalidation() &&
           "owned state mutation on a detached Boundary must not produce a refresh snapshot");
    assert(destructorCalls == 1);
  }

  assert(destructorCalls == 1);

  g_ownedStateCorpseDestructorCalls = 0;
  g_ownedStateCorpseBoundary = 0;
  g_conditionalArenaRetireProbe = 0;
}

void testRetiredBoundaryIsQuiescentBeforeNextTrackerRun()
{
  using namespace loka::app::scene;

  int destructorCalls = 0;
  loka::core::MutableState<int> externalState(0);
  g_observedCorpseExternalState = &externalState;
  g_observedCorpseDestructorCalls = &destructorCalls;

  {
    DetachProbePlatformController platform;
    Scene scene((Boundary<CorpseRetireHarnessBoundaryNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_corpseRetireHarness != 0);
    ObservedCorpseBoundaryNode *retired = g_corpseRetireHarness->observedBranch();
    (void)retired;
    assert(retired != 0);
    assert(retired->isArenaAllocated());
    LOKA_VERIFY(g_corpseRetireHarness->retireObservedBranch(scene, platform) == retired);
    assert(destructorCalls == 0);

    externalState.set(1);
    assert(!scene.director().hasPendingBoundary(retired) &&
           "detached observed boundary must not enqueue projection work while awaiting reclaim");

    LOKA_VERIFY(!scene.flushInvalidation() &&
           "quiescent corpse reclamation alone must not report a refresh snapshot");
  }

  g_observedCorpseExternalState = 0;
  g_observedCorpseDestructorCalls = 0;
  g_corpseRetireHarness = 0;
}

// #127: a direct root boundary's ordinary observed state must mark the root
// dirty after a write, both on first attach and after a detach/re-attach cycle.
// At HEAD the direct-root compose path never runs declareDirtySources for the
// root boundary itself, so the binding is never created and the write is
// silently dropped (no pending projection work).
void testDirectRootBoundaryReRegistersObservedStateAcrossReattach()
{
  using namespace loka::app::scene;

  loka::core::MutableState<int> externalState(0);
  g_directRootObservedState = &externalState;

  {
    DetachProbePlatformController platform;
    Scene scene((Boundary<DirectRootObservedBoundaryNode>()));
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_directRootObserved != 0);

    // First attach: a write to the observed external state must schedule
    // projection work for the direct root boundary.
    {
      loka::core::StateTrackerGuard guard(g_directRootObserved->tracker());
      externalState.set(1);
    }
    assert(scene.director().hasPendingBoundary(g_directRootObserved) &&
           "direct-root boundary observed-state write must mark the root dirty on first attach");
    scene.flushInvalidation();

    // Re-attach (the stated #127 acceptance scenario): detach tears the root
    // node down, re-attach builds a fresh instance that must re-register its
    // observed state so a later write is still heard.
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, false);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    assert(g_directRootObserved != 0);

    {
      loka::core::StateTrackerGuard guard(g_directRootObserved->tracker());
      externalState.set(2);
    }
    assert(scene.director().hasPendingBoundary(g_directRootObserved) &&
           "direct-root boundary must re-register observed state after ROOT re-attach");
  }

  g_directRootObservedState = 0;
  g_directRootObserved = 0;
}

void testSceneTeardownReleasesBothConditionalBranchContextsOnce()
{
  using namespace loka::app::scene;

  DetachHookCounts trueCounts;
  DetachHookCounts falseCounts;
  loka::core::MutableState<bool> condition(false);
  g_conditionalSceneTrueCounts = &trueCounts;
  g_conditionalSceneFalseCounts = &falseCounts;
  g_conditionalSceneCondition = &condition;

  {
    Scene scene((Boundary<ConditionalSceneProbeNode>()));
    DetachProbePlatformController platform;

    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    assert(falseCounts.attachCalls == 1);
    assert(trueCounts.attachCalls == 0);

    condition.set(true);
    scene.requestInvalidate(NODE_DIRTY_CHILD);
    LOKA_VERIFY(scene.flushInvalidation());
    assert(trueCounts.attachCalls == 1);
    assert(falseCounts.detachCalls == 1 &&
           "a retained detach must deliver the detach fact to the branch's contexts");

    loka::dsl::testing::SceneTestAccess::unmount(scene);

    // Teardown releases each branch context exactly once more.
    assert(trueCounts.detachCalls == 1);
    assert(falseCounts.detachCalls == 2);
    assert(trueCounts.attachCalls == 1);
    assert(falseCounts.attachCalls == 1);
  }

  g_conditionalSceneTrueCounts = 0;
  g_conditionalSceneFalseCounts = 0;
  g_conditionalSceneCondition = 0;
}

void testConditionalConditionWriteDuringDetachDoesNotMaterializeBranch()
{
  using namespace loka::app::scene;

  DetachHookCounts trueCounts;
  DetachHookCounts falseCounts;
  loka::core::MutableState<bool> condition(false);
  g_detachWindowTrueCounts = &trueCounts;
  g_detachWindowFalseCounts = &falseCounts;
  g_detachWindowCondition = &condition;

  BoundaryDefinition<DetachWindowConditionalBoundaryProps, DetachWindowConditionalBoundaryNode>
      conditionalRoot = Boundary<DetachWindowConditionalBoundaryNode>();

  {
    DetachProbePlatformController platform;
    Scene scene(conditionalRoot);
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(falseCounts.attachCalls == 1);
    assert(trueCounts.attachCalls == 0);

    loka::dsl::testing::SceneTestAccess::updateAttached(scene, false);

    assert(condition.get());
    assert(trueCounts.attachCalls == 0 &&
           "a condition write during DETACH must wait for a later scheduled apply");
    assert(falseCounts.detachCalls == 1);
    if (scene.hasPendingInvalidation())
    {
      LOKA_VERIFY(!scene.flushInvalidation());
    }
  }

  g_detachWindowCondition = 0;
  g_detachWindowFalseCounts = 0;
  g_detachWindowTrueCounts = 0;
}

// Retained props refusal keeps later nodes owned or retired.
using LocalRebuildRefusalSupport::RebuildRefusalObservation;
using LocalRebuildRefusalSupport::RefusingRetainedFragment;

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;

  struct StrandObservation
  {
    Node *tail;
    bool destroyed;
    bool retiredBeforeDestructor;
    bool contextReleasedBeforeDestructor;
    unsigned heapCandidatesCreated, heapCandidatesDestroyed;
    StrandObservation() : tail(0), destroyed(false), retiredBeforeDestructor(false), contextReleasedBeforeDestructor(false),
        heapCandidatesCreated(0), heapCandidatesDestroyed(0) {}
  };
  StrandObservation *strandObservation = 0;
  loka::core::MutableState<bool> *strandVisible = 0;

  class StrandTextNode : public TextNode
  {
  public:
    explicit StrandTextNode(const TextProps &props) : TextNode(props) {}
    virtual ~StrandTextNode()
    {
      if (strandObservation && strandObservation->tail == this)
      {
        strandObservation->destroyed = true;
        strandObservation->retiredBeforeDestructor = this->lifecycleFact() == NODE_FACT_RETIRED;
        strandObservation->contextReleasedBeforeDestructor = this->getContext() == 0;
      }
    }
  };

  class StrandCandidateNode : public TextNode
  {
  public:
    explicit StrandCandidateNode(const TextProps &props) : TextNode(props) {}
    virtual ~StrandCandidateNode()
    {
      if (strandObservation)
        ++strandObservation->heapCandidatesDestroyed;
    }
  };

  /** Force a real REPLACE candidate during the parked branch's re-entry. */
  struct StrandCandidateDefinition : NodeDefinition<TextProps, TextNode>
  {
    typedef StrandCandidateDefinition CloneType;
    virtual NodeDefinitionBase *clone() const { return new StrandCandidateDefinition(*this); }
    virtual bool isCompatibleWithNode(const Node *) const { return false; }
    virtual Node *create() const
    {
      // Only heap candidates use the counting type: storage provenance has
      // already been lowered by the destruction door when a destructor runs.
      NodeDefinition<TextProps, StrandCandidateNode> heapDefinition(this->props);
      heapDefinition.setNodeTag(this->nodeTag());
      heapDefinition.setNativeLifetimeHint(this->nativeLifetimeHint());
      Node *node = heapDefinition.create();
      if (node && strandObservation)
        ++strandObservation->heapCandidatesCreated;
      return node;
    }
  };

  class StrandBoundary;
  typedef BoundaryPropsFor<StrandBoundary> StrandProps;
  class StrandBoundary : public BoundaryNodeFor<StrandBoundary>
  {
  public:
    explicit StrandBoundary(const StrandProps &props) : BoundaryNodeFor<StrandBoundary>(props) {}
    virtual void composeNode(NodeComposition &composition)
    {
      FragmentDefinition prefix;
      prefix.tag(7101);
      RefusingRetainedFragment refusal;
      refusal.tag(7102);
      NodeDefinition<TextProps, StrandTextNode> tail((TextProps("tail")));
      tail.tag(7103);
      StrandCandidateDefinition candidate;
      candidate.tag(7104);
      composition.declare(Show(*strandVisible) << prefix << candidate << refusal << tail);
    }
  };

  Node *findStrandTag(Node *node, NodeTag tag)
  {
    if (!node || node->nodeTag() == tag)
      return node;
    INestable *children = node->asNestable();
    for (Node *child = children ? children->childrenHead() : 0;
         child; child = child->nextInComposition)
    {
      Node *found = findStrandTag(child, tag);
      if (found) return found;
    }
    return 0;
  }
}

void testLocalRebuildRefusalKeepsLaterNodesOwnedOrRetired()
{
  using loka::dsl::testing::SceneTestAccess;
  StrandObservation observation;
  loka::core::MutableState<bool> visible(true);
  strandObservation = &observation;
  strandVisible = &visible;
  bool ownedOrRetired = false;
  {
    NullScenePlatformController platform;
    {
      Scene scene((Boundary<StrandBoundary>()));
      scene.mount(&platform);
      SceneTestAccess::updateAttached(scene, true);
      observation.tail = findStrandTag(SceneTestAccess::rootNode(scene), 7103);
      LOKA_VERIFY(observation.tail && observation.tail->getContext());
      {
        loka::core::StateTrackerGuard transaction(visible.trackerOwner());
        visible.set(false);
      }
      scene.flushInvalidation();
      LOKA_VERIFY(!observation.destroyed);
      // Fail-count injection lives in TestingHooks.cpp. Keep refusal armed
      // through the complete run so internal retries cannot hide the defect.
      loka::app::testing::failLocalRebuildProbeProps(
          100);
      {
        loka::core::StateTrackerGuard transaction(visible.trackerOwner());
        visible.set(true);
      }
      scene.flushInvalidation();
      loka::app::testing::failLocalRebuildProbeProps(0);
      const bool reachable = !observation.destroyed &&
          findStrandTag(SceneTestAccess::rootNode(scene), 7103) == observation.tail;
      const bool retired = observation.destroyed ? observation.retiredBeforeDestructor
          : observation.tail->lifecycleFact() == NODE_FACT_RETIRED;
      const bool contextReleased = observation.destroyed ? observation.contextReleasedBeforeDestructor
          : observation.tail->getContext() == 0;
      ownedOrRetired = reachable || (retired && contextReleased);
      Node *candidate = findStrandTag(SceneTestAccess::rootNode(scene), 7104);
      const unsigned reachableHeapCandidates = candidate && !candidate->isArenaAllocated() ? 1 : 0;
      LOKA_VERIFY(observation.heapCandidatesDestroyed > 0);
      LOKA_VERIFY(observation.heapCandidatesCreated - observation.heapCandidatesDestroyed
                  == reachableHeapCandidates);
      SceneTestAccess::unmount(scene);
    }
  }
  LOKA_VERIFY(observation.heapCandidatesCreated == observation.heapCandidatesDestroyed);
  strandObservation = 0;
  strandVisible = 0;
  LOKA_VERIFY(ownedOrRetired && "every old node must remain reachable or be retired after refusal");
}

namespace
{
  struct RebuildCandidateCounts
  {
    unsigned alive, destroyed, attached;
    RebuildCandidateCounts() : alive(0), destroyed(0), attached(0) {}
  };
  class RebuildCandidateNode;
  struct RebuildCandidateTag {};
  struct RebuildCandidateProps : NodePropsBase<RebuildCandidateProps>
  {
    typedef RebuildCandidateTag TypeTag;
    typedef RebuildCandidateNode NodeType;
    RebuildCandidateCounts *counts;
    explicit RebuildCandidateProps(RebuildCandidateCounts *value = 0) : counts(value) {}
    bool operator<(const PropsBase &rhs) const
    {
      if (this->propsTypeId() != rhs.propsTypeId())
        return this->propsTypeId() < rhs.propsTypeId();
      return this->counts < static_cast<const RebuildCandidateProps &>(rhs).counts;
    }
  };
  class RebuildCandidateNode : public ComponentNodeWithProps<RebuildCandidateProps>
  {
  public:
    explicit RebuildCandidateNode(const RebuildCandidateProps &p)
        : ComponentNodeWithProps<RebuildCandidateProps>(p) { ++this->props.counts->alive; }
    virtual ~RebuildCandidateNode()
    {
      --this->props.counts->alive;
      ++this->props.counts->destroyed;
    }
    virtual void attachNode(NodeComposition &) { ++this->props.counts->attached; }
    virtual void composeChildren(NodeComposition &) {}
  };
  typedef NodeDefinition<RebuildCandidateProps, RebuildCandidateNode> RebuildCandidate;

  /** Existing protected plan doors, exposed only by this test Boundary. */
  class RebuildRefusalHarness : public BoundaryNodeFor<RebuildRefusalHarness>
  {
  public:
    explicit RebuildRefusalHarness(const BoundaryPropsFor<RebuildRefusalHarness> &p)
        : BoundaryNodeFor<RebuildRefusalHarness>(p) {}
    virtual void composeNode(NodeComposition &) {}
    Node *materialize(ComponentContext &context, NodeDefinitionBase &definition)
    { return this->materializeLocalRebuildNode(context, &definition); }
    bool rebuild(ComponentContext &context, Node &root, FragmentDefinition &desired)
    {
      BoundaryLocalRebuildPlan plan;
      if (!this->buildParkedBranchReentryPlan(context, &root, *root.asNestable(),
              desired, plan))
        return false;
      std::vector<Node *> retained;
      return this->applyLocalRebuildPlan(context, *root.asNestable(), plan, retained);
    }
  };

  void verifyRebuildOrder(Node &root, const NodeTag *tags, size_t count)
  {
    INestable *children = root.asNestable();
    LOKA_VERIFY(children && children->childrenCount() == count);
    Node *child = children->childrenHead();
    for (size_t i = 0; i < count; ++i)
    {
      LOKA_VERIFY(child && child->nodeTag() == tags[i]);
      child = child->nextInComposition;
    }
    LOKA_VERIFY(!child);
  }

  void runRebuildCandidateRefusal(bool before)
  {
    RebuildCandidateCounts counts;
    loka::core::MutableState<bool> borrowed(false);
    RebuildRefusalHarness owner((BoundaryPropsFor<RebuildRefusalHarness>()));
    ComponentContext context;
    context.setBoundary(&owner);
    context.setStateOwner(&owner);
    FragmentDefinition previous;
    previous << Fragment().tag(7201) << Fragment().tag(7202) << Fragment().tag(7203);
    Node *root = owner.materialize(context, previous);
    LOKA_VERIFY(root);
    Node *oldFirst = root->asNestable()->childrenHead();
    LOKA_VERIFY(owner.registerObservedState(&borrowed, NODE_DIRTY_PROPS, oldFirst));
    const unsigned originalUses = ObservedUseTestSupport::activeUses(root);
    LOKA_VERIFY(originalUses == 1);
    RebuildRefusalObservation observation(*root->asNestable());
    RefusingRetainedFragment refusal(&observation);
    refusal.tag(7202);
    RebuildCandidate candidate((RebuildCandidateProps(&counts)));
    candidate.tag(before ? 7201 : 7204);
    FragmentDefinition desired;
    if (before)
      desired << candidate << refusal << Fragment().tag(7203);
    else
      desired << Fragment().tag(7201) << refusal << candidate;
    loka::app::testing::failLocalRebuildProbeProps(1);
    const bool applied = owner.rebuild(context, *root, desired);
    loka::app::testing::failLocalRebuildProbeProps(0);
    LOKA_VERIFY(!applied);
    LOKA_VERIFY(observation.linkedChildren == 0 &&
                "plan entries must remain unlinked until all fallible work succeeds");
    LOKA_VERIFY(counts.destroyed == 1 && counts.alive == 0);
    LOKA_VERIFY(counts.attached == 0);
    const NodeTag tags[] = {7201, 7202, 7203};
    verifyRebuildOrder(*root, tags, 3);
    Node *restoredFirst = root->asNestable()->childrenHead();
    LOKA_VERIFY(restoredFirst == oldFirst);
    LOKA_VERIFY(ObservedUseTestSupport::activeUses(root) == originalUses);
    LOKA_VERIFY(!findStrandTag(root, 7204));
    DestroyHeapNode(root);
  }
}

void testLocalRebuildRefusalDestroysCandidateBefore()
{
  runRebuildCandidateRefusal(true);
}
void testLocalRebuildRefusalDestroysCandidateAfter()
{
  runRebuildCandidateRefusal(false);
}
void testLocalRebuildNestedRefusalRestoresBothOrders()
{
  RebuildRefusalHarness owner((BoundaryPropsFor<RebuildRefusalHarness>()));
  ComponentContext context;
  context.setBoundary(&owner);
  context.setStateOwner(&owner);
  FragmentDefinition nested;
  nested.tag(7302);
  nested << Fragment().tag(7311) << Fragment().tag(7312) << Fragment().tag(7313);
  FragmentDefinition previous;
  previous << Fragment().tag(7301) << nested << Fragment().tag(7303);
  Node *root = owner.materialize(context, previous);
  LOKA_VERIFY(root);
  Node *oldNested = findStrandTag(root, 7302);
  PolicyScopeDefinition scope;
  FragmentDefinition *scoped = static_cast<FragmentDefinition *>(scope.scopedBranchDefinition());
  scoped->tag(7302);
  RefusingRetainedFragment refusal;
  refusal.tag(7312);
  *scoped << Fragment().tag(7311) << refusal << Fragment().tag(7313);
  FragmentDefinition desired;
  desired << Fragment().tag(7301) << scope << Fragment().tag(7303);
  loka::app::testing::failLocalRebuildProbeProps(1);
  const bool applied = owner.rebuild(context, *root, desired);
  loka::app::testing::failLocalRebuildProbeProps(0);
  LOKA_VERIFY(!applied);
  const NodeTag outerTags[] = {7301, 7302, 7303};
  const NodeTag innerTags[] = {7311, 7312, 7313};
  verifyRebuildOrder(*root, outerTags, 3);
  verifyRebuildOrder(*oldNested, innerTags, 3);
  DestroyHeapNode(root);
}

namespace
{
  void recordFocusTeardownClear(void *data) { *static_cast<bool *>(data) = true; }
}
void testLocalRebuildLiveRefusalKeepsFocusReachable()
{
  using namespace loka::core;
  using loka::dsl::testing::SceneTestAccess;
  loka::app::Focus<unsigned> focus;
  NullScenePlatformController platform;
  NullPlatformContext platformContext;
  WindowProps props;
  props.scene(new Scene(Boundary<RebuildRefusalHarness>()));
  NullWindow window(&platformContext, props, &platform);
  WindowAdmissionTestApp app(window);
  app.flush();
  Scene &scene = *window.scene();
  RebuildRefusalHarness &owner =
      *static_cast<RebuildRefusalHarness *>(SceneTestAccess::rootBoundary(scene));
  StateBatchBase::CreateImmediateState(&owner, focus);
  ComponentContext context;
  context.setBoundary(&owner);
  context.setStateOwner(&owner);
  context.setScene(&scene);
  context.setPlatformController(&platform);
  FragmentDefinition nested;
  nested.tag(7402);
  nested << Fragment().tag(7411) << Fragment().tag(7412) << Fragment().tag(7413);
  FragmentDefinition previous;
  previous << Fragment().tag(7401) << nested
           << EditText(EditTextProps().focusedAs(focus, 7u)).tag(7403);
  Node *root = owner.materialize(context, previous);
  LOKA_VERIFY(root);
  owner.addChild(root);
  BoundaryNode::composeSubtree(root, context, COMPOSE_EVENT_ATTACH, &owner);
  LayoutState bounds;
  bounds.width = 240;
  bounds.height = 320;
  platform.projectLayoutForTesting(root, bounds);
  Node *editor = findStrandTag(root, 7403);
  LOKA_VERIFY(editor && editor->getContext());
  platform.simulateNativeFocus(editor->getContext());
  app.reconcileFocus();
  LOKA_VERIFY(focus.state()->get().is(7u));
  LOKA_VERIFY(SceneFocusTestAccess::published(*editor->asFocusParticipant()));

  PolicyScopeDefinition scope;
  FragmentDefinition *scoped = static_cast<FragmentDefinition *>(scope.scopedBranchDefinition());
  scope.setNodeTag(7402);
  scoped->tag(7402);
  RefusingRetainedFragment refusal;
  refusal.tag(7412);
  *scoped << Fragment().tag(7411) << refusal << Fragment().tag(7413);
  FragmentDefinition desired;
  desired << Fragment().tag(7401) << scope
          << EditText(EditTextProps().focusedAs(focus, 7u)).tag(7403)
          << Fragment().tag(7404);
  loka::app::testing::failLocalRebuildProbeProps(1);
  const bool applied = owner.rebuild(context, *root, desired);
  loka::app::testing::failLocalRebuildProbeProps(0);
  LOKA_VERIFY(!applied);
  const bool reachable = findStrandTag(SceneTestAccess::rootNode(scene), 7403) == editor;
  LOKA_VERIFY(reachable);
  LOKA_VERIFY(editor->lifecycleFact() == NODE_FACT_ATTACHED);
  LOKA_VERIFY(SceneFocusTestAccess::published(*editor->asFocusParticipant()));
  LOKA_VERIFY(focus.state()->get().is(7u));
  const NodeTag tags[] = {7401, 7402, 7403};
  verifyRebuildOrder(*root, tags, 3);
  bool cleared = false;
  focus.state()->bind(&recordFocusTeardownClear, &cleared, false, true);
  SceneTestAccess::unmount(scene);
  LOKA_VERIFY(cleared);
}

void testLocalRebuildSuccessCommitsCandidates()
{
  RebuildCandidateCounts counts;
  RebuildRefusalHarness owner((BoundaryPropsFor<RebuildRefusalHarness>()));
  ComponentContext context;
  context.setBoundary(&owner);
  context.setStateOwner(&owner);
  FragmentDefinition previous;
  previous << Fragment().tag(7501);
  Node *root = owner.materialize(context, previous);
  LOKA_VERIFY(root);
  RebuildCandidate candidate((RebuildCandidateProps(&counts)));
  candidate.tag(7502);
  FragmentDefinition desired;
  desired << Fragment().tag(7501) << candidate;
  const bool applied = owner.rebuild(context, *root, desired);
  LOKA_VERIFY(applied);
  LOKA_VERIFY(counts.alive == 1 && counts.attached == 1 && counts.destroyed == 0);
  const NodeTag tags[] = {7501, 7502};
  verifyRebuildOrder(*root, tags, 2);
  DestroyHeapNode(root);
  LOKA_VERIFY(counts.alive == 0 && counts.destroyed == 1);
}

namespace
{
  class SeatPlanReattachRoot : public BoundaryNodeFor<SeatPlanReattachRoot>
  {
  public:
    explicit SeatPlanReattachRoot(const BoundaryPropsFor<SeatPlanReattachRoot> &props)
        : BoundaryNodeFor<SeatPlanReattachRoot>(props), declarations(0)
    {
      this->state(this->selected, true);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      ++this->declarations;
      FragmentDefinition arm;
      arm.tag(static_cast<NodeTag>(7600 + this->declarations));
      composition.declare(ConditionalDefinition(ConditionalProps(this->selected.state(), &arm, 0)));
    }
    int declarations;
    NodeState<bool> selected;
  };
}

void testStdBoundaryDetachReattachDeclaresFreshSeatPlans()
{
  using loka::dsl::testing::SceneTestAccess;
  NullScenePlatformController platform;
  Scene scene((Boundary<SeatPlanReattachRoot>()));
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  SeatPlanReattachRoot *root = static_cast<SeatPlanReattachRoot *>(SceneTestAccess::rootBoundary(scene));
  LOKA_VERIFY(root->declarations == 1);
  LOKA_VERIFY(findStrandTag(root, 7601));

  // Send the real direct-root DETACH without Scene's terminal root reclamation.
  SceneTestAccess::notifyComposeEvent(scene, COMPOSE_EVENT_DETACH);
  SceneTestAccess::notifyComposeEvent(scene, COMPOSE_EVENT_ATTACH);
  LOKA_VERIFY(SceneTestAccess::rootBoundary(scene) == root);
  LOKA_VERIFY(root->declarations == 2);
  LOKA_VERIFY(!findStrandTag(root, 7601));
  LOKA_VERIFY(findStrandTag(root, 7602));
  LOKA_VERIFY(!loka::dsl::testing::OwnershipDump::seatRuntimeRowAddresses(*root).empty());
}

namespace DetachRunWindow
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using loka::dsl::testing::SceneTestAccess;

  enum Path { UNMOUNT_PLAIN, UNMOUNT_SEAT, DETACH, SWITCH_SEAT, REPLACE_PARKED };

  struct Observation
  {
    loka::core::MutableState<bool> visible;
    loka::core::MutableState<int> pulse;
    Scene *scene;
    Path path;
    bool inside;
    bool armed;
    bool running;
    int detaches;
    int seen;
    int nestedComposes;
    int updatePass;
    int detachPass;
    int seenPass;

    explicit Observation(Path p)
        : visible(true), pulse(0), scene(0), path(p), inside(false),
          armed(true), running(false), detaches(0), seen(0), nestedComposes(0),
          updatePass(0), detachPass(0), seenPass(0) {}
  };

  // The stack fixture outlives the Scene and all its callbacks.
  Observation *observation = 0;

  class Writer;
  struct WriterProps : NodePropsBase<WriterProps>
  {
    struct TypeTag {};
    typedef Writer NodeType;
    bool operator<(const PropsBase &rhs) const { return propsTypeId() < rhs.propsTypeId(); }
  };

  class Writer : public ComponentNodeWithProps<WriterProps>
  {
  public:
    explicit Writer(const WriterProps &props) : ComponentNodeWithProps<WriterProps>(props) {}
    virtual void composeChildren(NodeComposition &) {}
    virtual void detachNode(NodeComposition &)
    {
      if (!observation->armed)
        return;
      ++observation->detaches;
      observation->detachPass = observation->updatePass;
      observation->inside = true;
      observation->running = observation->scene->isRunInProgress();
      {
        loka::core::StateTrackerGuard transaction(observation->pulse.trackerOwner());
        observation->pulse.set(observation->pulse.get() + 1);
      }
      observation->inside = false;
      LOKA_VERIFY(observation->nestedComposes == 0);
      LOKA_VERIFY(observation->running);
    }
  };

  // Force the public compatibility door's REPLACE path on parked re-entry.
  struct ReplacingWriter : NodeDefinition<WriterProps, Writer>
  {
    typedef ReplacingWriter CloneType;
    virtual NodeDefinitionBase *clone() const { return new ReplacingWriter(*this); }
    virtual bool isCompatibleWithNode(const Node *) const { return false; }
  };

  class Sink : public BoundaryNodeFor<Sink>
  {
  public:
    explicit Sink(const BoundaryPropsFor<Sink> &props) : BoundaryNodeFor<Sink>(props) {}
    virtual void declareDirtySources(DirtySourceRegistrar &registrar)
    {
      registrar.markDirtyOnChange(&observation->pulse, NODE_DIRTY_PROPS);
    }
    virtual void composeWithContext(ComponentContext &context, ComposeEvent event)
    {
      // A nested refresh can replay ATTACH after the outer DETACH reset.
      // Count both composing events so that replay cannot evade this pin.
      if (observation->inside && event != COMPOSE_EVENT_DETACH)
        ++observation->nestedComposes;
      if (event == COMPOSE_EVENT_UPDATE)
      {
        observation->seen = observation->pulse.get();
        observation->seenPass = observation->updatePass;
      }
      BoundaryNodeFor<Sink>::composeWithContext(context, event);
    }
  };

  class Root : public BoundaryNodeFor<Root>
  {
  public:
    explicit Root(const BoundaryPropsFor<Root> &props) : BoundaryNodeFor<Root>(props) {}
    virtual void composeWithContext(ComponentContext &context, ComposeEvent event)
    {
      if (event == COMPOSE_EVENT_UPDATE)
        ++observation->updatePass;
      BoundaryNodeFor<Root>::composeWithContext(context, event);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      if (observation->path == UNMOUNT_PLAIN)
        composition.declare(Fragment() << NodeDefinition<WriterProps, Writer>() << Boundary<Sink>());
      else if (observation->path == REPLACE_PARKED)
        composition.declare(Fragment() << (Show(observation->visible) << ReplacingWriter()) << Boundary<Sink>());
      else
        composition.declare(Fragment() << (Show(observation->visible).destroyOnDetach() << NodeDefinition<WriterProps, Writer>()) << Boundary<Sink>());
    }
  };

  void changeVisibility(bool visible)
  {
    loka::core::StateTrackerGuard transaction(observation->visible.trackerOwner());
    observation->visible.set(visible);
  }

  void verify(Path path)
  {
    Observation facts(path);
    observation = &facts;
    {
      NullScenePlatformController platform;
      Scene scene((Boundary<Root>()));
      facts.scene = &scene;
      const bool mounted = scene.mount(&platform);
      LOKA_VERIFY(mounted);
      SceneTestAccess::updateAttached(scene, true);
      if (path == UNMOUNT_PLAIN || path == UNMOUNT_SEAT)
        SceneTestAccess::unmount(scene);
      else if (path == DETACH)
        SceneTestAccess::updateAttached(scene, false);
      else
      {
        changeVisibility(false);
        LOKA_VERIFY(facts.detaches == (path == REPLACE_PARKED ? 0 : 1));
        scene.flushInvalidation();
        LOKA_VERIFY(facts.seen == (path == REPLACE_PARKED ? 0 : 1));
        if (path == REPLACE_PARKED)
        {
          changeVisibility(true);
          LOKA_VERIFY(facts.detaches == 1);
          scene.flushInvalidation();
          LOKA_VERIFY(facts.seen == 1);
        }
      }
      if (path == SWITCH_SEAT || path == REPLACE_PARKED)
      {
        // State completion can flush again before set() returns. Compare
        // actual root UPDATE passes instead of assuming a setter boundary.
        LOKA_VERIFY(facts.detachPass > 0);
        LOKA_VERIFY(facts.seenPass > facts.detachPass);
      }
      LOKA_VERIFY(facts.detaches == 1);
      LOKA_VERIFY(facts.nestedComposes == 0);
      LOKA_VERIFY(facts.running);
      facts.armed = false;
      if (path != UNMOUNT_PLAIN && path != UNMOUNT_SEAT)
        SceneTestAccess::unmount(scene);
      scene.flushInvalidation();
      LOKA_VERIFY(facts.nestedComposes == 0);
    }
    observation = 0;
  }
}

void testUnmountDetachWriteRunsWithoutNestedUpdate() { DetachRunWindow::verify(DetachRunWindow::UNMOUNT_PLAIN); }
void testUnmountDestroyingSeatWriteRunsWithoutNestedUpdate() { DetachRunWindow::verify(DetachRunWindow::UNMOUNT_SEAT); }
void testAttachedDetachWriteOwnsRunWindow() { DetachRunWindow::verify(DetachRunWindow::DETACH); }
void testDestroyingSeatDetachWriteAppliesLater() { DetachRunWindow::verify(DetachRunWindow::SWITCH_SEAT); }
void testParkedReplacementDetachWriteAppliesLater() { DetachRunWindow::verify(DetachRunWindow::REPLACE_PARKED); }
