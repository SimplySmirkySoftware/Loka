#include "support/BlobAllocationProbe.hpp"
#include "support/OrdinaryFlowPin.hpp"
#include "FlowDslTests.hpp"
#include "support/TestVerify.hpp"

#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/scene/composition/NodeComposition.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "app/scene/Scene.hpp"
#include "app/scene/state/FlowSlot.hpp"
#include "testing/flow/FlowSlotTestAccess.hpp"
#include "app/scene/state/WriteSeat.hpp"
#include "support/Headless.hpp"
#include "dsl/flow/Expr.hpp"
#include "dsl/stream/StateStream.hpp"
#include "../example/HelloWorld/src/MainNode.hpp"
#include "../example/MineSweeper/src/MainNode.hpp"
#include "../example/SimpleViewer/src/MainNode.hpp"

class SimpleViewerTestAccess
{
public:
  static void verifyAllocationFailure(const simpleviewer::MainNode &node)
  {
    LOKA_VERIFY(!node.imageLoad_.flow_.isValid());
    LOKA_VERIFY(!node.image_.get().isValid());
    LOKA_VERIFY(node.chooserMessage_.get().equals(loka::core::String::Literal("Not enough contiguous memory to load image.")));
  }
};

#include "../example/SimpleViewer/src/SimpleViewerFlowAdapters.hpp"
#include "core/State.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace
{
  static void incrementNotificationCount(void *user)
  {
    ++(*static_cast<int *>(user));
  }

  struct FlowResultTrackerObservation
  {
    FlowResultTrackerObservation(loka::core::StateTracker *tracker)
        : tracker(tracker),
          phase(loka::core::TRACKER_IDLE),
          calls(0)
    {
    }

    loka::core::StateTracker *tracker;
    loka::core::TrackerPhase phase;
    int calls;
  };

  static void captureFlowResultTrackerPhase(void *user)
  {
    FlowResultTrackerObservation *observation = static_cast<FlowResultTrackerObservation *>(user);
    observation->phase = observation->tracker->phase();
    ++observation->calls;
  }

  static loka::app::HStack buildTypedHStack()
  {
    return loka::app::HStack().alignVertical(loka::app::VERTICAL_ALIGNMENT_BOTTOM)
           << loka::app::Text("Left") << loka::app::Text("Right");
  }

  static loka::app::VStack buildTypedVStack()
  {
    return loka::app::VStack().alignHorizontal(loka::app::HORIZONTAL_ALIGNMENT_CENTER)
           << loka::app::Text("Top") << loka::app::Text("Bottom");
  }

  static loka::app::Box buildTypedBox()
  {
    return loka::app::Box().padding(8).testId("TypedBox") << loka::app::Text("Box child");
  }

  static loka::app::Fragment buildTypedFragment()
  {
    return loka::app::Fragment() << loka::app::Text("Fragment A") << loka::app::Text("Fragment B");
  }

  static loka::app::Grid buildTypedGrid()
  {
    return loka::app::Grid().rows(1).cols(2) << loka::app::Text("Grid A") << loka::app::Text("Grid B");
  }

  static loka::app::ZStack buildTypedZStack()
  {
    return loka::app::ZStack().testId("TypedZStack") << loka::app::Text("Back") << loka::app::Text("Front");
  }

  static loka::app::Fragment buildTypedInlineFragment()
  {
    return loka::app::Fragment() << loka::app::Text("Light child").testId("TypedDslLightChild");
  }

  static void assertEquivalentDefinitionStructure(loka::app::scene::NodeDefinitionBase *expected,
                                                  loka::app::scene::NodeDefinitionBase *actual)
  {
    assert(expected);
    assert(actual);
    assert(expected->nodeKind() == actual->nodeKind());
    LOKA_VERIFY(expected->propsBase());
    LOKA_VERIFY(actual->propsBase());
    LOKA_VERIFY(expected->propsBase()->propsTypeId() == actual->propsBase()->propsTypeId());
    LOKA_VERIFY(!(*expected->propsBase() < *actual->propsBase()));
    LOKA_VERIFY(!(*actual->propsBase() < *expected->propsBase()));

    loka::app::scene::IBranchSeatDefinition *expectedSeat = expected->asBranchSeatDefinition();
    loka::app::scene::IBranchSeatDefinition *actualSeat = actual->asBranchSeatDefinition();
    assert((expectedSeat != 0) == (actualSeat != 0));
    if (expectedSeat)
    {
      assert(expectedSeat->branchCondition() == actualSeat->branchCondition());
      LOKA_VERIFY(expectedSeat->armPolicies(1).destroyOnDetach ==
                  actualSeat->armPolicies(1).destroyOnDetach);
      assertEquivalentDefinitionStructure(expectedSeat->branchDefinition(false), actualSeat->branchDefinition(false));
      assertEquivalentDefinitionStructure(expectedSeat->branchDefinition(true), actualSeat->branchDefinition(true));
    }

    loka::app::scene::INestableDefinition *expectedNestable = expected->asNestableDefinition();
    loka::app::scene::INestableDefinition *actualNestable = actual->asNestableDefinition();
    assert((expectedNestable != 0) == (actualNestable != 0));
    if (!expectedNestable)
    {
      return;
    }

    LOKA_VERIFY(expectedNestable->childrenCount() == actualNestable->childrenCount());
    loka::app::scene::NodeDefinitionBase *expectedChild = expectedNestable->childrenHead();
    loka::app::scene::NodeDefinitionBase *actualChild = actualNestable->childrenHead();
    while (expectedChild && actualChild)
    {
      assertEquivalentDefinitionStructure(expectedChild, actualChild);
      expectedChild = expectedChild->nextInComposition;
      actualChild = actualChild->nextInComposition;
    }
    assert(!expectedChild);
    assert(!actualChild);
  }

  class TypedDslLightHostNode;
  typedef loka::app::scene::BoundaryPropsFor<TypedDslLightHostNode> TypedDslLightHostProps;

  class TypedDslLightHostNode : public loka::app::scene::BoundaryNodeFor<TypedDslLightHostNode>
  {
  public:
    TypedDslLightHostNode(const TypedDslLightHostProps &p)
        : loka::app::scene::BoundaryNodeFor<TypedDslLightHostNode>(TypedDslLightHostProps(p))
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      c.declare(loka::app::HStack().testId("TypedDslLightRow") << buildTypedInlineFragment());
    }
  };

  struct FlowTestMarkerContext
  {
    std::vector<int> *order;
    int marker;
  };

  struct FlowErrorCapture
  {
    int kind;
    int code;
    int calls;
  };

  struct SetTextStyleStateAction
  {
    typedef loka::app::scene::Scene *In;
    typedef loka::app::scene::Scene *Out;

    SetTextStyleStateAction(loka::core::MutableState<loka::app::TextStyle> *state,
                            const loka::app::TextStyle &value,
                            bool flush)
        : state_(state), value_(value), flush_(flush)
    {
    }

    loka::dsl::StepRunStatus
    run(loka::app::scene::Scene *const &in, loka::app::scene::Scene *&out, loka::dsl::FlowError &error) const
    {
      out = in;
      if (!in || !this->state_)
      {
        error.kind = loka::dsl::testing::FLOW_ERROR_KIND_SCENE_SCENARIO;
        error.code = loka::dsl::testing::FLOW_ERROR_SCENE_TEST_NULL_SCENE;
        return loka::dsl::FLOW_STEP_FAILED;
      }
      loka::app::scene::BoundaryNode *boundary = loka::dsl::testing::SceneTestAccess::rootBoundary(*in);
      if (!boundary || !boundary->tracker())
      {
        error.kind = loka::dsl::testing::FLOW_ERROR_KIND_SCENE_SCENARIO;
        error.code = loka::dsl::testing::FLOW_ERROR_SCENE_TEST_ROOT_UNAVAILABLE;
        return loka::dsl::FLOW_STEP_FAILED;
      }
      {
        loka::core::StateTrackerGuard guard(boundary->tracker());
        this->state_->set(this->value_);
      }
      return this->flush_ ? loka::dsl::testing::FlushSceneInvalidation().run(out, out, error)
                          : loka::dsl::FLOW_STEP_SUCCEEDED;
    }

    loka::core::MutableState<loka::app::TextStyle> *state_;
    loka::app::TextStyle value_;
    bool flush_;
  };

  struct CountSceneAction
  {
    typedef loka::app::scene::Scene *In;
    typedef loka::app::scene::Scene *Out;

    CountSceneAction(int *calls, int expectedPreviousCalls)
        : calls_(calls),
          expectedPreviousCalls_(expectedPreviousCalls)
    {
    }

    loka::dsl::StepRunStatus
    run(loka::app::scene::Scene *const &in, loka::app::scene::Scene *&out, loka::dsl::FlowError &error) const
    {
      out = in;
      if (!this->calls_ || *this->calls_ != this->expectedPreviousCalls_)
      {
        error.kind = loka::dsl::FLOW_ERROR_KIND_FLOW;
        error.code = loka::dsl::FLOW_ERROR_CODE_ASSERT_PREDICATE_FAILED;
        return loka::dsl::FLOW_STEP_FAILED;
      }
      ++(*this->calls_);
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }

    int *calls_;
    int expectedPreviousCalls_;
  };

  struct QueueSceneApplyAction
  {
    typedef loka::app::scene::Scene *In;
    typedef loka::app::scene::Scene *Out;

    explicit QueueSceneApplyAction(int *calls)
        : calls_(calls)
    {
    }

    loka::dsl::StepRunStatus
    run(loka::app::scene::Scene *const &in, loka::app::scene::Scene *&out, loka::dsl::FlowError &error) const
    {
      out = in;
      if (!in || !this->calls_)
      {
        error.kind = loka::dsl::testing::FLOW_ERROR_KIND_SCENE_SCENARIO;
        error.code = loka::dsl::testing::FLOW_ERROR_SCENE_TEST_NULL_SCENE;
        return loka::dsl::FLOW_STEP_FAILED;
      }
      ++(*this->calls_);
      in->requestInvalidate(loka::app::scene::NODE_DIRTY_PROPS);
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }

    int *calls_;
  };

  struct PendingSceneApplyAction
  {
    typedef loka::app::scene::Scene *In;
    typedef loka::app::scene::Scene *Out;

    explicit PendingSceneApplyAction(int *calls)
        : calls_(calls)
    {
    }

    loka::dsl::StepRunStatus
    run(loka::app::scene::Scene *const &in, loka::app::scene::Scene *&out, loka::dsl::FlowError &error) const
    {
      out = in;
      if (!in || !this->calls_)
      {
        error.kind = loka::dsl::testing::FLOW_ERROR_KIND_SCENE_SCENARIO;
        error.code = loka::dsl::testing::FLOW_ERROR_SCENE_TEST_NULL_SCENE;
        return loka::dsl::FLOW_STEP_FAILED;
      }
      ++(*this->calls_);
      if (*this->calls_ == 1)
      {
        in->requestInvalidate(loka::app::scene::NODE_DIRTY_PROPS);
        return loka::dsl::FLOW_STEP_PENDING;
      }
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }

    int *calls_;
  };

  class ConditionalProjectedProbeNode;
  struct ConditionalProjectedProbeTypeTag
  {
  };

  struct ConditionalProjectedProbeProps : public loka::app::scene::NodePropsBase<ConditionalProjectedProbeProps>
  {
    typedef ConditionalProjectedProbeTypeTag TypeTag;
    typedef ConditionalProjectedProbeNode NodeType;
    ConditionalProjectedProbeProps() {}
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId() ? false : this->propsTypeId() < rhs.propsTypeId();
    }
  };

  static int g_conditionalProjectedProbeLayoutCalls = 0;
  static int g_conditionalProjectedProbePrepareCalls = 0;

  class ConditionalProjectedProbeNode : public loka::app::scene::Node, public loka::app::scene::IProjectedLayoutNode
  {
  public:
    typedef ConditionalProjectedProbeTypeTag TypeTag;
    ConditionalProjectedProbeProps props;
    ConditionalProjectedProbeNode(const ConditionalProjectedProbeProps &p)
        : props(p)
    {
    }
    virtual loka::app::scene::IProjectedLayoutNode *asProjectedLayoutNode()
    {
      return this;
    }
    virtual short layoutProjected(loka::app::scene::IPlatformController *controller,
                                  loka::app::scene::LayoutState &state)
    {
      ++g_conditionalProjectedProbeLayoutCalls;
      const bool prepared = loka::app::scene::PrepareProjectedLayout(controller, this, state);
      return static_cast<short>(prepared ? state.y + 1 : state.y);
    }
  };

  struct ConditionalProjectedProbeDefinition
      : public loka::app::scene::NodeDefinition<ConditionalProjectedProbeProps, ConditionalProjectedProbeNode>
  {
    ConditionalProjectedProbeDefinition()
        : loka::app::scene::NodeDefinition<ConditionalProjectedProbeProps, ConditionalProjectedProbeNode>()
    {
    }
    ConditionalProjectedProbeDefinition(const ConditionalProjectedProbeProps &p)
        : loka::app::scene::NodeDefinition<ConditionalProjectedProbeProps, ConditionalProjectedProbeNode>(p)
    {
    }
  };

  class ConditionalProjectedProbeController : public loka::app::scene::IPlatformController
  {
  public:
    ConditionalProjectedProbeController()
        : lastRoot_(0),
          lastFlags_(loka::app::scene::NODE_DIRTY_NONE),
          lastFullRebuild_(false),
          changeCalls_(0)
    {
    }

    virtual void onChange(loka::app::scene::Node *rootNode, loka::app::scene::NodeDirtyFlags flags, bool fullRebuild)
    {
      lastRoot_ = rootNode;
      lastFlags_ = flags;
      lastFullRebuild_ = fullRebuild;
      ++changeCalls_;
      if (!rootNode)
      {
        return;
      }
      loka::app::scene::LayoutState state;
      state.x = 0;
      state.y = 0;
      state.width = 100;
      state.height = 100;
      state.lineHeight = 0;
      state.spacing = 0;
      walkTree(this, rootNode, state);
    }

    virtual bool prepareProjectedLayout(loka::app::scene::Node *node, loka::app::scene::LayoutState &state)
    {
      (void)state;
      ++g_conditionalProjectedProbePrepareCalls;
      if (node && !node->getContext())
      {
        node->setContext(new loka::app::scene::NodeContext(node));
      }
      return true;
    }

    virtual void synchronize() {}
    virtual bool hasPendingSync() const
    {
      return false;
    }
    virtual void destroy() {}

    static void walkTree(loka::app::scene::IPlatformController *controller,
                         loka::app::scene::Node *node,
                         const loka::app::scene::LayoutState &state)
    {
      if (!node)
      {
        return;
      }
      if (loka::app::scene::IProjectedLayoutNode *projected = node->asProjectedLayoutNode())
      {
        loka::app::scene::LayoutState projectedState = state;
        projected->layoutProjected(controller, projectedState);
      }
      if (loka::app::scene::INestable *nestable = node->asNestable())
      {
        loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
        for (loka::app::scene::Node *child = it.next(); child; child = it.next())
        {
          walkTree(controller, child, state);
        }
      }
    }

    loka::app::scene::Node *lastRoot_;
    loka::app::scene::NodeDirtyFlags lastFlags_;
    bool lastFullRebuild_;
    int changeCalls_;
  };

  class PendingLayoutBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<PendingLayoutBoundaryNode> PendingLayoutBoundaryProps;
  static loka::core::MutableState<int> *g_pendingLayoutWidthState = 0;
  struct RecordingDirtySourceRegistrar : public loka::app::scene::DirtySourceRegistrar
  {
    RecordingDirtySourceRegistrar()
        : calls(0),
          lastState(0),
          lastFlags(loka::app::scene::NODE_DIRTY_NONE)
    {
    }

    virtual void markDirtyOnChange(loka::core::StateBase *state, loka::app::scene::NodeDirtyFlags flags)
    {
      ++calls;
      lastState = state;
      lastFlags = flags;
    }

    int calls;
    loka::core::StateBase *lastState;
    loka::app::scene::NodeDirtyFlags lastFlags;
  };
  class BoundaryLookupTestApi
  {
  public:
    static const char *kInterfaceName()
    {
      return "BoundaryLookupTestApi";
    }
    static BoundaryLookupTestApi *fromNode(loka::app::scene::Node *node)
    {
      if (!node)
      {
        return 0;
      }
      return static_cast<BoundaryLookupTestApi *>(node->queryInterface(kInterfaceName()));
    }
    virtual ~BoundaryLookupTestApi() {}
    virtual int id() const = 0;
  };

  class BoundaryLookupTestNode : public loka::app::scene::Node, public BoundaryLookupTestApi
  {
  public:
    explicit BoundaryLookupTestNode(int value)
        : value_(value)
    {
    }

    virtual void *queryInterface(const char *name)
    {
      return name && std::strcmp(name, kInterfaceName()) == 0 ? static_cast<BoundaryLookupTestApi *>(this) : 0;
    }

    virtual int id() const
    {
      return value_;
    }

  private:
    int value_;
  };

  class BoundaryLookupStateApi
  {
  public:
    static const char *kInterfaceName()
    {
      return "BoundaryLookupStateApi";
    }
    static BoundaryLookupStateApi *fromNode(loka::app::scene::Node *node)
    {
      if (!node)
      {
        return 0;
      }
      return static_cast<BoundaryLookupStateApi *>(node->queryInterface(kInterfaceName()));
    }
    virtual ~BoundaryLookupStateApi() {}
    virtual loka::app::scene::BorrowedState<int> countState() const = 0;
  };

  class BoundaryLookupStateNode : public loka::app::scene::Node, public BoundaryLookupStateApi
  {
  public:
    explicit BoundaryLookupStateNode(int value)
        : count_(value)
    {
    }

    virtual void *queryInterface(const char *name)
    {
      return name && std::strcmp(name, kInterfaceName()) == 0 ? static_cast<BoundaryLookupStateApi *>(this) : 0;
    }

    virtual loka::app::scene::BorrowedState<int> countState() const
    {
      return loka::app::scene::BorrowedState<int>(const_cast<loka::core::MutableState<int> *>(&count_));
    }

  private:
    loka::core::MutableState<int> count_;
  };

  // Owning test double: adopted states belong to the owner, releaseState
  // deletes, and anything still adopted is freed on teardown so ASan runs
  // stay leak-clean. Unlike the real BoundaryNode owner it does not register
  // states with a StateTracker and never deals with arena-allocated states,
  // so do not reuse it where tracker integration matters.
  class DummyStateOwner : public loka::app::scene::IStateOwner
  {
  public:
    DummyStateOwner()
        : tracker_(0),
          holdLedger_(this)
    {
    }
    virtual ~DummyStateOwner()
    {
      for (size_t i = owned_.size(); i > 0; --i)
      {
        delete owned_[i - 1];
      }
    }

    virtual void adoptState(loka::core::StateBase *state)
    {
      adoptStateUnchecked(state);
    }
    virtual void adoptStateUnchecked(loka::core::StateBase *state)
    {
      if (!state)
      {
        return;
      }
      for (size_t i = 0; i < owned_.size(); ++i)
      {
        if (owned_[i] == state)
        {
          return;
        }
      }
      owned_.push_back(state);
    }
    virtual void releaseState(loka::core::StateBase *state)
    {
      if (!state)
      {
        return;
      }
      for (size_t i = 0; i < owned_.size();)
      {
        if (owned_[i] == state)
        {
          owned_.erase(owned_.begin() + i);
        }
        else
        {
          ++i;
        }
      }
      delete state;
    }
    virtual void reserveStates(size_t) {}
    virtual void reserveStateArena(size_t) {}
    virtual void noteStateAllocationFailure()
    {
      assert(false && "unexpected state OOM in this test fixture");
    }
    virtual void *allocateStateMemory(size_t, size_t)
    {
      return 0;
    }
    virtual void registerStateMemory(loka::core::StateBase *, void (*)(loka::core::StateBase *)) {}
    virtual loka::core::HoldLedger *holdLedger()
    {
      return &this->holdLedger_;
    }
    virtual void reserveHeldArena(size_t) {}
    virtual void *allocateHeldMemory(size_t, size_t)
    {
      return 0;
    }
    virtual void registerHeldMemory(loka::core::detail::HeldBlockBase *) {}
    virtual void retireHeldBlock(loka::core::detail::HeldBlockBase *)
    {
      assert(false && "DummyStateOwner has no retire pool");
    }
    virtual loka::core::StateTracker *tracker()
    {
      return tracker_;
    }

  private:
    loka::core::StateTracker *tracker_;
    std::vector<loka::core::StateBase *> owned_;
    loka::core::HoldLedger holdLedger_;
  };

  class PendingApplyProbeBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<PendingApplyProbeBoundaryNode> PendingApplyProbeBoundaryProps;
  static int g_pendingApplyCallCount = 0;
  static int g_pendingApplyWhileApplyingCount = 0;
  static loka::app::scene::BoundaryNode *g_pendingApplyLastLayoutRoot = 0;
  static loka::app::scene::BoundaryNode *g_pendingApplyLastPaintRoot = 0;
  static int g_defaultApplyLocalPaintCalls = 0;
  static int g_defaultApplyOpaquePaintCalls = 0;
  static int g_defaultApplyCompositedPaintCalls = 0;
  static int g_defaultApplyLayoutCalls = 0;
  static int g_defaultApplyStructureCalls = 0;
  static int g_defaultApplyBoundsWidth = 0;
  static int g_defaultApplyBoundsHeight = 0;
  static int g_defaultApplyOpaqueHintSeen = 0;
  class PendingCompositedProbeBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<PendingCompositedProbeBoundaryNode> PendingCompositedProbeBoundaryProps;
  class PendingApplySiblingABoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<PendingApplySiblingABoundaryNode> PendingApplySiblingABoundaryProps;
  class PendingApplySiblingBBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<PendingApplySiblingBBoundaryNode> PendingApplySiblingBBoundaryProps;
  class PendingApplySiblingsRootNode;
  typedef loka::app::scene::BoundaryPropsFor<PendingApplySiblingsRootNode> PendingApplySiblingsRootProps;
  static int g_pendingApplySiblingACalls = 0;
  static int g_pendingApplySiblingBCalls = 0;
  static loka::app::scene::BoundaryNode *g_pendingApplySiblingALayoutRoot = 0;
  static loka::app::scene::BoundaryNode *g_pendingApplySiblingAPaintRoot = 0;
  static loka::app::scene::BoundaryNode *g_pendingApplySiblingBLayoutRoot = 0;
  static loka::app::scene::BoundaryNode *g_pendingApplySiblingBPaintRoot = 0;
  class PendingApplyDefersSiblingABoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<PendingApplyDefersSiblingABoundaryNode>
      PendingApplyDefersSiblingABoundaryProps;
  class PendingApplyDefersSiblingBBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<PendingApplyDefersSiblingBBoundaryNode>
      PendingApplyDefersSiblingBBoundaryProps;
  class PendingApplyDefersSiblingsRootNode;
  typedef loka::app::scene::BoundaryPropsFor<PendingApplyDefersSiblingsRootNode> PendingApplyDefersSiblingsRootProps;
  static int g_pendingApplyDefersSiblingACalls = 0;
  static int g_pendingApplyDefersSiblingBCalls = 0;
  static bool g_pendingApplyDefersSiblingAQueuedSiblingB = false;
  static loka::app::scene::BoundaryNode *g_pendingApplyDeferredSiblingB = 0;
  class HeadlessScopeProbeBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<HeadlessScopeProbeBoundaryNode> HeadlessScopeProbeProps;
  class HeadlessScopeHostBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<HeadlessScopeHostBoundaryNode> HeadlessScopeHostProps;
  static HeadlessScopeProbeBoundaryNode *g_headlessScopeProbe = 0;
  static HeadlessScopeHostBoundaryNode *g_headlessScopeHost = 0;
  static int g_headlessScopeAttachCount = 0;
  static int g_headlessScopeDetachCount = 0;
  class HeadlessOwnedProbeNode;
  typedef loka::app::scene::HeadlessPropsFor<HeadlessOwnedProbeNode> HeadlessOwnedProbeProps;
  class HeadlessOwnedHostBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<HeadlessOwnedHostBoundaryNode> HeadlessOwnedHostProps;
  static HeadlessOwnedProbeNode *g_headlessOwnedProbe = 0;
  static HeadlessOwnedHostBoundaryNode *g_headlessOwnedHost = 0;
  static int g_headlessOwnedAttachCount = 0;
  static int g_headlessOwnedDestroyCount = 0;
  class HeadlessOwnedMultiProbeANode;
  typedef loka::app::scene::HeadlessPropsFor<HeadlessOwnedMultiProbeANode> HeadlessOwnedMultiProbeAProps;
  class HeadlessOwnedMultiProbeBNode;
  typedef loka::app::scene::HeadlessPropsFor<HeadlessOwnedMultiProbeBNode> HeadlessOwnedMultiProbeBProps;
  class HeadlessOwnedMultiHostBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<HeadlessOwnedMultiHostBoundaryNode> HeadlessOwnedMultiHostProps;
  static HeadlessOwnedMultiProbeANode *g_headlessOwnedMultiProbeA = 0;
  static HeadlessOwnedMultiProbeBNode *g_headlessOwnedMultiProbeB = 0;
  static HeadlessOwnedMultiHostBoundaryNode *g_headlessOwnedMultiHost = 0;
  static int g_headlessOwnedMultiAttachCountA = 0;
  static int g_headlessOwnedMultiAttachCountB = 0;
  static int g_headlessOwnedMultiDestroyCountA = 0;
  static int g_headlessOwnedMultiDestroyCountB = 0;
  class HeadlessOwnedPersistentProbeNode;
  typedef loka::app::scene::HeadlessPropsFor<HeadlessOwnedPersistentProbeNode> HeadlessOwnedPersistentProbeProps;
  class HeadlessOwnedMixedHostBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<HeadlessOwnedMixedHostBoundaryNode> HeadlessOwnedMixedHostProps;
  static HeadlessOwnedPersistentProbeNode *g_headlessOwnedPersistentProbe = 0;
  static int g_headlessOwnedPersistentAttachCount = 0;
  static int g_headlessOwnedPersistentDestroyCount = 0;

  class PendingLayoutBoundaryNode : public loka::app::scene::BoundaryNodeFor<PendingLayoutBoundaryNode>
  {
  public:
    PendingLayoutBoundaryNode(const PendingLayoutBoundaryProps &p)
        : loka::app::scene::BoundaryNodeFor<PendingLayoutBoundaryNode>(PendingLayoutBoundaryProps(p))
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      assert(g_pendingLayoutWidthState);
      this->setLayoutBounds(0, 0, g_pendingLayoutWidthState->get(), 12);
      c.declare(loka::app::Text("Sized").testId("PendingLayoutText"));
    }

    virtual void declareDirtySources(loka::app::scene::DirtySourceRegistrar &registrar)
    {
      assert(g_pendingLayoutWidthState);
      registrar.markDirtyOnChange(g_pendingLayoutWidthState, loka::app::scene::NODE_DIRTY_PROPS);
    }
  };

  class PendingApplyProbeBoundaryNode : public loka::app::scene::BoundaryNodeFor<PendingApplyProbeBoundaryNode>
  {
  public:
    PendingApplyProbeBoundaryNode(const PendingApplyProbeBoundaryProps &p)
        : loka::app::scene::BoundaryNodeFor<PendingApplyProbeBoundaryNode>(PendingApplyProbeBoundaryProps(p))
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      c.declare(loka::app::Text("Apply").testId("PendingApplyText"));
    }

    virtual void applyPendingUpdate(const loka::app::scene::PlatformApplyPlan &plan)
    {
      const LocalApplyInfo info = this->localApplyInfo(plan);
      ++g_pendingApplyCallCount;
      (void)info;
      assert(info.hasAnyWork());
      assert(info.hasRootedWork());
      assert(plan.hasAnyLocalWork(this));
      if (this->isApplyingPlatform())
      {
        ++g_pendingApplyWhileApplyingCount;
      }
      g_pendingApplyLastLayoutRoot = plan.layoutRoot;
      g_pendingApplyLastPaintRoot = plan.paintRoot;
      assert(info.hasPaintWork());
      assert(info.hasGenericPaintWork() || info.hasOpaquePaintWork());
      assert(plan.paintKind == loka::app::scene::PlatformApplyPlan::PAINT_LOCAL
             || plan.paintKind == loka::app::scene::PlatformApplyPlan::PAINT_LOCAL_OPAQUE);
    }
  };

  class PendingCompositedProbeBoundaryNode
      : public loka::app::scene::BoundaryNodeFor<PendingCompositedProbeBoundaryNode>
  {
  public:
    PendingCompositedProbeBoundaryNode(const PendingCompositedProbeBoundaryProps &p)
        : loka::app::scene::BoundaryNodeFor<PendingCompositedProbeBoundaryNode>(PendingCompositedProbeBoundaryProps(p))
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      this->noteCompositedPaint();
      c.declare(loka::app::Text("Composited").testId("PendingCompositedText"));
    }
  };

  class PendingDefaultApplyProbeBoundaryNode;
  typedef loka::app::scene::BoundaryPropsFor<PendingDefaultApplyProbeBoundaryNode>
      PendingDefaultApplyProbeBoundaryProps;

  class PendingDefaultApplyProbeBoundaryNode
      : public loka::app::scene::BoundaryNodeFor<PendingDefaultApplyProbeBoundaryNode>
  {
  public:
    PendingDefaultApplyProbeBoundaryNode(const PendingDefaultApplyProbeBoundaryProps &p)
        : loka::app::scene::BoundaryNodeFor<PendingDefaultApplyProbeBoundaryNode>(
              PendingDefaultApplyProbeBoundaryProps(p))
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      this->setLayoutBounds(0, 0, 40, 12);
      this->noteCompositedPaint();
      this->noteOpaquePaintCoverage(true);
      c.declare(loka::app::Text("DefaultApply").testId("PendingDefaultApplyText"));
    }

  protected:
    virtual void applyPendingStructureInfo(const LocalApplyInfo &info, const loka::app::scene::PlatformApplyPlan &plan)
    {
      (void)plan;
      (void)info;
      assert(info.hasStructureWork);
      assert(info.isLocalStructureRoot);
      ++g_defaultApplyStructureCalls;
    }

    virtual void applyPendingLayoutInfo(const LocalApplyInfo &info, const loka::app::scene::PlatformApplyPlan &plan)
    {
      (void)plan;
      assert(info.hasLayoutWork);
      assert(info.hasBoundsHint());
      g_defaultApplyBoundsWidth = info.bounds->width;
      g_defaultApplyBoundsHeight = info.bounds->height;
      ++g_defaultApplyLayoutCalls;
    }

    virtual void applyPendingLocalPaintInfo(const LocalApplyInfo &info, const loka::app::scene::PlatformApplyPlan &plan)
    {
      (void)plan;
      (void)info;
      assert(info.hasPaintWork());
      assert(info.hasBoundsHint());
      assert(info.hasPaintSpecificBoundsHint);
      assert(info.usesPaintBoundsHint);
      assert(info.hasPaintBoundsHint());
      assert(info.paintKind == loka::app::scene::BoundaryNode::LOCAL_APPLY_PAINT_GENERIC);
      assert(!plan.isOpaqueLocalPaint());
      ++g_defaultApplyLocalPaintCalls;
    }

    virtual void applyPendingOpaquePaintInfo(const LocalApplyInfo &info,
                                             const loka::app::scene::PlatformApplyPlan &plan)
    {
      (void)plan;
      (void)info;
      assert(info.hasPaintWork());
      assert(info.hasBoundsHint());
      assert(info.hasPaintSpecificBoundsHint);
      assert(info.usesPaintBoundsHint);
      assert(info.hasPaintBoundsHint());
      assert(info.hasOpaqueCoverageHint);
      assert(info.paintKind == loka::app::scene::BoundaryNode::LOCAL_APPLY_PAINT_OPAQUE
             || info.paintKind == loka::app::scene::BoundaryNode::LOCAL_APPLY_PAINT_COMPOSITED);
      assert(info.paintIsOpaque);
      ++g_defaultApplyOpaqueHintSeen;
      ++g_defaultApplyOpaquePaintCalls;
    }

    virtual void applyPendingCompositedPaintInfo(const LocalApplyInfo &info,
                                                 const loka::app::scene::PlatformApplyPlan &plan)
    {
      (void)plan;
      assert(info.hasCompositedPaintWork());
      assert(info.paintKind == loka::app::scene::BoundaryNode::LOCAL_APPLY_PAINT_COMPOSITED);
      ++g_defaultApplyCompositedPaintCalls;
      this->applyPendingOpaquePaintInfo(info, plan);
    }
  };

  class SameBoundaryConditionalProbeNode;
  typedef loka::app::scene::BoundaryPropsFor<SameBoundaryConditionalProbeNode> SameBoundaryConditionalProbeProps;
  static SameBoundaryConditionalProbeNode *g_sameBoundaryConditionalProbe = 0;

  class SameBoundaryConditionalProbeNode : public loka::app::scene::BoundaryNodeFor<SameBoundaryConditionalProbeNode>
  {
  public:
    SameBoundaryConditionalProbeNode(const SameBoundaryConditionalProbeProps &p)
        : loka::app::scene::BoundaryNodeFor<SameBoundaryConditionalProbeNode>(SameBoundaryConditionalProbeProps(p)),
          show_(),
          toggle_(),
          initialized_(false)
    {
    }

    virtual void declareBindings(loka::app::scene::BindingToken &t)
    {
      t.action(this->toggle_, this, &SameBoundaryConditionalProbeNode::toggle);
    }

    virtual void attachNode(loka::app::scene::NodeComposition &c)
    {
      if (this->initialized_)
      {
        return;
      }
      c.declareStates().state(this->show_, false);
      g_sameBoundaryConditionalProbe = this;
      this->initialized_ = true;
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      TextDefinition falseText = Text("Off").testId("SameBoundaryOffText");
      TextDefinition trueText = Text("On").testId("SameBoundaryOnText");
      c.declare(Box().testId("SameBoundaryRoot") << c.conditional(*this->show_.state(), trueText, falseText));
    }

    void emitToggle()
    {
      this->toggle_.emit();
    }

    void setShown(bool value)
    {
      this->show_.set(value, true);
    }

  private:
    void toggle()
    {
      this->show_.set(!this->show_.get(), true);
    }

    loka::app::scene::NodeState<bool> show_;
    loka::core::EmitterState toggle_;
    bool initialized_;
  };

  class PendingApplySiblingABoundaryNode : public loka::app::scene::BoundaryNodeFor<PendingApplySiblingABoundaryNode>
  {
  public:
    PendingApplySiblingABoundaryNode(const PendingApplySiblingABoundaryProps &p)
        : loka::app::scene::BoundaryNodeFor<PendingApplySiblingABoundaryNode>(PendingApplySiblingABoundaryProps(p))
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      c.declare(loka::app::Text("SiblingA").testId("PendingSiblingAText"));
    }

    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }

    virtual void applyPendingUpdate(const loka::app::scene::PlatformApplyPlan &plan)
    {
      const LocalApplyInfo info = this->localApplyInfo(plan);
      ++g_pendingApplySiblingACalls;
      g_pendingApplySiblingALayoutRoot = plan.layoutRoot;
      g_pendingApplySiblingAPaintRoot = plan.paintRoot;
      (void)info;
      assert(info.isLocalLayoutRoot || !plan.hasLayoutWork());
      assert(info.isLocalPaintRoot);
      assert(plan.layoutRoot == this);
      assert(plan.paintRoot == this);
    }
  };

  class HeadlessScopeProbeBoundaryNode : public loka::app::scene::BoundaryNodeFor<HeadlessScopeProbeBoundaryNode>
  {
  public:
    HeadlessScopeProbeBoundaryNode(const HeadlessScopeProbeProps &p)
        : loka::app::scene::BoundaryNodeFor<HeadlessScopeProbeBoundaryNode>(HeadlessScopeProbeProps(p)),
          count_(),
          summary_(),
          summaryFlow_(*this),
          initialized_(false)
    {
    }

    virtual void attachNode(loka::app::scene::NodeComposition &c)
    {
      if (this->initialized_)
      {
        return;
      }
      c.declareStates().state(this->count_, 0).state(this->summary_, loka::core::String::Literal("Headless 0"));
      {
        loka::dsl::StateStream<int> countStream = this->count_.stream();
        this->summaryFlow_.set(countStream.map(loka::dsl::Const("Headless ") + countStream.slot.value()))
            .bindTo(this->summary_);
      }
      ++g_headlessScopeAttachCount;
      g_headlessScopeProbe = this;
      this->initialized_ = true;
    }

    virtual void detachNode(loka::app::scene::NodeComposition &c)
    {
      (void)c;
      ++g_headlessScopeDetachCount;
      if (g_headlessScopeProbe == this)
      {
        g_headlessScopeProbe = 0;
      }
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      c.declare(loka::app::Text(this->summary_.state()).testId("HeadlessSummaryText"));
    }

    void increment()
    {
      this->count_.set(this->count_.get() + 1);
    }

  private:
    loka::app::scene::NodeState<int> count_;
    loka::app::scene::NodeState<loka::core::String> summary_;
    loka::app::scene::FlowSlot<loka::dsl::StateStream<loka::core::String> > summaryFlow_;
    bool initialized_;
  };

  class HeadlessScopeHostBoundaryNode : public loka::app::scene::BoundaryNodeFor<HeadlessScopeHostBoundaryNode>
  {
  public:
    HeadlessScopeHostBoundaryNode(const HeadlessScopeHostProps &p)
        : loka::app::scene::BoundaryNodeFor<HeadlessScopeHostBoundaryNode>(HeadlessScopeHostProps(p)),
          show_(),
          toggle_(),
          initialized_(false)
    {
    }

    virtual void declareBindings(loka::app::scene::BindingToken &t)
    {
      t.action(this->toggle_, this, &HeadlessScopeHostBoundaryNode::toggle);
    }

    virtual void attachNode(loka::app::scene::NodeComposition &c)
    {
      if (this->initialized_)
      {
        return;
      }
      c.declareStates().state(this->show_, true);
      g_headlessScopeHost = this;
      this->initialized_ = true;
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      c.declare(Box().testId("HeadlessScopeHostRoot")
                << (Show(*this->show_.state()) << loka::app::scene::Boundary<HeadlessScopeProbeBoundaryNode>()));
    }

    void emitToggle()
    {
      this->toggle_.emit();
    }

    void setShown(bool value)
    {
      this->show_.set(value, true);
    }

  private:
    void toggle()
    {
      this->show_.set(!this->show_.get(), true);
    }

    loka::app::scene::NodeState<bool> show_;
    loka::core::EmitterState toggle_;
    bool initialized_;
  };

  class HeadlessOwnedProbeNode : public loka::app::scene::HeadlessNodeBase<HeadlessOwnedProbeProps>
  {
  public:
    HeadlessOwnedProbeNode(const HeadlessOwnedProbeProps &p)
        : loka::app::scene::HeadlessNodeBase<HeadlessOwnedProbeProps>(p),
          count_(),
          summary_(),
          summaryFlow_(*this),
          initialized_(false)
    {
    }

    virtual ~HeadlessOwnedProbeNode()
    {
      if (g_headlessOwnedProbe == this)
      {
        g_headlessOwnedProbe = 0;
      }
      ++g_headlessOwnedDestroyCount;
    }

    virtual void attachNode(loka::app::scene::NodeComposition &c)
    {
      if (this->initialized_)
      {
        return;
      }
      c.declareStates().state(this->count_, 0).state(this->summary_, loka::core::String::Literal("Owned 0"));
      {
        loka::dsl::StateStream<int> countStream = this->count_.stream();
        this->summaryFlow_.set(countStream.map(loka::dsl::Const("Owned ") + countStream.slot.value()))
            .bindTo(this->summary_);
      }
      ++g_headlessOwnedAttachCount;
      g_headlessOwnedProbe = this;
      this->initialized_ = true;
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      c.declare(loka::app::F().testId("HeadlessOwnedSentinel"));
    }

    void increment()
    {
      this->count_.set(this->count_.get() + 1);
    }

    loka::core::String summaryText() const
    {
      return this->summary_.get();
    }

  private:
    loka::app::scene::NodeState<int> count_;
    loka::app::scene::NodeState<loka::core::String> summary_;
    loka::app::scene::FlowSlot<loka::dsl::StateStream<loka::core::String> > summaryFlow_;
    bool initialized_;
  };

  inline loka::app::scene::NodeDefinition<HeadlessOwnedProbeProps, HeadlessOwnedProbeNode> HeadlessOwnedProbe()
  {
    return loka::app::scene::Headless<HeadlessOwnedProbeNode>();
  }

  class HeadlessOwnedHostBoundaryNode : public loka::app::scene::BoundaryNodeFor<HeadlessOwnedHostBoundaryNode>
  {
  public:
    HeadlessOwnedHostBoundaryNode(const HeadlessOwnedHostProps &p)
        : loka::app::scene::BoundaryNodeFor<HeadlessOwnedHostBoundaryNode>(HeadlessOwnedHostProps(p)),
          show_(),
          initialized_(false)
    {
    }

    virtual void attachNode(loka::app::scene::NodeComposition &c)
    {
      if (this->initialized_)
      {
        return;
      }
      c.declareStates().state(this->show_, true);
      g_headlessOwnedHost = this;
      this->initialized_ = true;
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      c.declare(Box().testId("HeadlessOwnedHostRoot") << (Show(*this->show_.state()) << HeadlessOwnedProbe()));
    }

    void setShown(bool value)
    {
      this->show_.set(value, true);
    }

  private:
    loka::app::scene::NodeState<bool> show_;
    bool initialized_;
  };

  class HeadlessOwnedMultiProbeANode : public loka::app::scene::HeadlessNodeBase<HeadlessOwnedMultiProbeAProps>
  {
  public:
    HeadlessOwnedMultiProbeANode(const HeadlessOwnedMultiProbeAProps &p)
        : loka::app::scene::HeadlessNodeBase<HeadlessOwnedMultiProbeAProps>(p),
          count_(),
          summary_(),
          summaryFlow_(*this),
          initialized_(false)
    {
    }

    virtual ~HeadlessOwnedMultiProbeANode()
    {
      if (g_headlessOwnedMultiProbeA == this)
      {
        g_headlessOwnedMultiProbeA = 0;
      }
      ++g_headlessOwnedMultiDestroyCountA;
    }

    virtual void attachNode(loka::app::scene::NodeComposition &c)
    {
      if (this->initialized_)
      {
        return;
      }
      c.declareStates().state(this->count_, 0).state(this->summary_, loka::core::String::Literal("OwnedA 0"));
      {
        loka::dsl::StateStream<int> countStream = this->count_.stream();
        this->summaryFlow_.set(countStream.map(loka::dsl::Const("OwnedA ") + countStream.slot.value()))
            .bindTo(this->summary_);
      }
      ++g_headlessOwnedMultiAttachCountA;
      g_headlessOwnedMultiProbeA = this;
      this->initialized_ = true;
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      c.declare(loka::app::F().testId("HeadlessOwnedMultiSentinelA"));
    }

    loka::core::String summaryText() const
    {
      return this->summary_.get();
    }

  private:
    loka::app::scene::NodeState<int> count_;
    loka::app::scene::NodeState<loka::core::String> summary_;
    loka::app::scene::FlowSlot<loka::dsl::StateStream<loka::core::String> > summaryFlow_;
    bool initialized_;
  };

  class HeadlessOwnedMultiProbeBNode : public loka::app::scene::HeadlessNodeBase<HeadlessOwnedMultiProbeBProps>
  {
  public:
    HeadlessOwnedMultiProbeBNode(const HeadlessOwnedMultiProbeBProps &p)
        : loka::app::scene::HeadlessNodeBase<HeadlessOwnedMultiProbeBProps>(p),
          count_(),
          summary_(),
          summaryFlow_(*this),
          initialized_(false)
    {
    }

    virtual ~HeadlessOwnedMultiProbeBNode()
    {
      if (g_headlessOwnedMultiProbeB == this)
      {
        g_headlessOwnedMultiProbeB = 0;
      }
      ++g_headlessOwnedMultiDestroyCountB;
    }

    virtual void attachNode(loka::app::scene::NodeComposition &c)
    {
      if (this->initialized_)
      {
        return;
      }
      c.declareStates().state(this->count_, 0).state(this->summary_, loka::core::String::Literal("OwnedB 0"));
      {
        loka::dsl::StateStream<int> countStream = this->count_.stream();
        this->summaryFlow_.set(countStream.map(loka::dsl::Const("OwnedB ") + countStream.slot.value()))
            .bindTo(this->summary_);
      }
      ++g_headlessOwnedMultiAttachCountB;
      g_headlessOwnedMultiProbeB = this;
      this->initialized_ = true;
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      c.declare(loka::app::F().testId("HeadlessOwnedMultiSentinelB"));
    }

    loka::core::String summaryText() const
    {
      return this->summary_.get();
    }

  private:
    loka::app::scene::NodeState<int> count_;
    loka::app::scene::NodeState<loka::core::String> summary_;
    loka::app::scene::FlowSlot<loka::dsl::StateStream<loka::core::String> > summaryFlow_;
    bool initialized_;
  };

  inline loka::app::scene::NodeDefinition<HeadlessOwnedMultiProbeAProps, HeadlessOwnedMultiProbeANode>
  HeadlessOwnedMultiProbeA()
  {
    return loka::app::scene::Headless<HeadlessOwnedMultiProbeANode>();
  }

  inline loka::app::scene::NodeDefinition<HeadlessOwnedMultiProbeBProps, HeadlessOwnedMultiProbeBNode>
  HeadlessOwnedMultiProbeB()
  {
    return loka::app::scene::Headless<HeadlessOwnedMultiProbeBNode>();
  }

  class HeadlessOwnedMultiHostBoundaryNode
      : public loka::app::scene::BoundaryNodeFor<HeadlessOwnedMultiHostBoundaryNode>
  {
  public:
    HeadlessOwnedMultiHostBoundaryNode(const HeadlessOwnedMultiHostProps &p)
        : loka::app::scene::BoundaryNodeFor<HeadlessOwnedMultiHostBoundaryNode>(HeadlessOwnedMultiHostProps(p)),
          show_(),
          initialized_(false)
    {
    }

    virtual void attachNode(loka::app::scene::NodeComposition &c)
    {
      if (this->initialized_)
      {
        return;
      }
      c.declareStates().state(this->show_, true);
      g_headlessOwnedMultiHost = this;
      this->initialized_ = true;
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      c.declare(Box().testId("HeadlessOwnedMultiHostRoot")
                << (Show(*this->show_.state()) << HeadlessOwnedMultiProbeA() << HeadlessOwnedMultiProbeB()));
    }

    void setShown(bool value)
    {
      this->show_.set(value, true);
    }

  private:
    loka::app::scene::NodeState<bool> show_;
    bool initialized_;
  };

  class HeadlessOwnedPersistentProbeNode : public loka::app::scene::HeadlessNodeBase<HeadlessOwnedPersistentProbeProps>
  {
  public:
    HeadlessOwnedPersistentProbeNode(const HeadlessOwnedPersistentProbeProps &p)
        : loka::app::scene::HeadlessNodeBase<HeadlessOwnedPersistentProbeProps>(p),
          initialized_(false)
    {
    }

    virtual ~HeadlessOwnedPersistentProbeNode()
    {
      if (g_headlessOwnedPersistentProbe == this)
      {
        g_headlessOwnedPersistentProbe = 0;
      }
      ++g_headlessOwnedPersistentDestroyCount;
    }

    virtual void attachNode(loka::app::scene::NodeComposition &c)
    {
      (void)c;
      if (this->initialized_)
      {
        return;
      }
      ++g_headlessOwnedPersistentAttachCount;
      g_headlessOwnedPersistentProbe = this;
      this->initialized_ = true;
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      c.declare(loka::app::F().testId("HeadlessOwnedPersistentSentinel"));
    }

  private:
    bool initialized_;
  };

  inline loka::app::scene::NodeDefinition<HeadlessOwnedPersistentProbeProps, HeadlessOwnedPersistentProbeNode>
  HeadlessOwnedPersistentProbe()
  {
    return loka::app::scene::Headless<HeadlessOwnedPersistentProbeNode>();
  }

  class HeadlessOwnedMixedHostBoundaryNode
      : public loka::app::scene::BoundaryNodeFor<HeadlessOwnedMixedHostBoundaryNode>
  {
  public:
    HeadlessOwnedMixedHostBoundaryNode(const HeadlessOwnedMixedHostProps &p)
        : loka::app::scene::BoundaryNodeFor<HeadlessOwnedMixedHostBoundaryNode>(HeadlessOwnedMixedHostProps(p)),
          show_(),
          initialized_(false)
    {
    }

    virtual void attachNode(loka::app::scene::NodeComposition &c)
    {
      if (this->initialized_)
      {
        return;
      }
      c.declareStates().state(this->show_, true);
      this->initialized_ = true;
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      c.declare(Box().testId("HeadlessOwnedMixedHostRoot")
                << HeadlessOwnedPersistentProbe() << (Show(*this->show_.state()) << HeadlessOwnedProbe()));
    }

    void setShown(bool value)
    {
      this->show_.set(value, true);
    }

  private:
    loka::app::scene::NodeState<bool> show_;
    bool initialized_;
  };

  class PendingApplySiblingBBoundaryNode : public loka::app::scene::BoundaryNodeFor<PendingApplySiblingBBoundaryNode>
  {
  public:
    PendingApplySiblingBBoundaryNode(const PendingApplySiblingBBoundaryProps &p)
        : loka::app::scene::BoundaryNodeFor<PendingApplySiblingBBoundaryNode>(PendingApplySiblingBBoundaryProps(p))
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      c.declare(loka::app::Text("SiblingB").testId("PendingSiblingBText"));
    }

    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }

    virtual void applyPendingUpdate(const loka::app::scene::PlatformApplyPlan &plan)
    {
      const LocalApplyInfo info = this->localApplyInfo(plan);
      ++g_pendingApplySiblingBCalls;
      g_pendingApplySiblingBLayoutRoot = plan.layoutRoot;
      g_pendingApplySiblingBPaintRoot = plan.paintRoot;
      (void)info;
      assert(info.isLocalLayoutRoot || !plan.hasLayoutWork());
      assert(info.isLocalPaintRoot);
      assert(plan.layoutRoot == this);
      assert(plan.paintRoot == this);
    }
  };

  class PendingApplySiblingsRootNode : public loka::app::scene::BoundaryNodeFor<PendingApplySiblingsRootNode>
  {
  public:
    PendingApplySiblingsRootNode(const PendingApplySiblingsRootProps &p)
        : loka::app::scene::BoundaryNodeFor<PendingApplySiblingsRootNode>(PendingApplySiblingsRootProps(p))
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      c.declare(
          loka::app::VStack()
          << loka::app::scene::BoundaryDefinition<PendingApplySiblingABoundaryProps, PendingApplySiblingABoundaryNode>()
                 .tag(101)
          << loka::app::scene::BoundaryDefinition<PendingApplySiblingBBoundaryProps, PendingApplySiblingBBoundaryNode>()
                 .tag(102));
    }
  };

  class PendingApplyDefersSiblingABoundaryNode
      : public loka::app::scene::BoundaryNodeFor<PendingApplyDefersSiblingABoundaryNode>
  {
  public:
    PendingApplyDefersSiblingABoundaryNode(const PendingApplyDefersSiblingABoundaryProps &p)
        : loka::app::scene::BoundaryNodeFor<PendingApplyDefersSiblingABoundaryNode>(
              PendingApplyDefersSiblingABoundaryProps(p))
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      c.declare(loka::app::Text("DeferA").testId("PendingDeferSiblingAText"));
    }

    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }

    virtual void applyPendingUpdate(const loka::app::scene::PlatformApplyPlan &plan)
    {
      ++g_pendingApplyDefersSiblingACalls;
      (void)plan;
      assert(plan.paintRoot == this);
      if (!g_pendingApplyDefersSiblingAQueuedSiblingB)
      {
        g_pendingApplyDefersSiblingAQueuedSiblingB = true;
        assert(this->isApplyingPlatform());
        assert(g_pendingApplyDeferredSiblingB != 0);
        g_pendingApplyDeferredSiblingB->markViewDirty(loka::app::scene::NODE_DIRTY_PROPS);
      }
    }
  };

  class PendingApplyDefersSiblingBBoundaryNode
      : public loka::app::scene::BoundaryNodeFor<PendingApplyDefersSiblingBBoundaryNode>
  {
  public:
    PendingApplyDefersSiblingBBoundaryNode(const PendingApplyDefersSiblingBBoundaryProps &p)
        : loka::app::scene::BoundaryNodeFor<PendingApplyDefersSiblingBBoundaryNode>(
              PendingApplyDefersSiblingBBoundaryProps(p))
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      c.declare(loka::app::Text("DeferB").testId("PendingDeferSiblingBText"));
    }

    virtual bool flushViewDirtyImmediately(loka::app::scene::NodeDirtyFlags) const
    {
      return false;
    }

    virtual void applyPendingUpdate(const loka::app::scene::PlatformApplyPlan &plan)
    {
      ++g_pendingApplyDefersSiblingBCalls;
      (void)plan;
      assert(plan.paintRoot == this);
    }
  };

  class PendingApplyDefersSiblingsRootNode
      : public loka::app::scene::BoundaryNodeFor<PendingApplyDefersSiblingsRootNode>
  {
  public:
    PendingApplyDefersSiblingsRootNode(const PendingApplyDefersSiblingsRootProps &p)
        : loka::app::scene::BoundaryNodeFor<PendingApplyDefersSiblingsRootNode>(
              PendingApplyDefersSiblingsRootProps(p))
    {
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      c.declare(
          loka::app::VStack()
          << loka::app::scene::BoundaryDefinition<PendingApplyDefersSiblingABoundaryProps,
                                                  PendingApplyDefersSiblingABoundaryNode>()
                 .tag(201)
          << loka::app::scene::BoundaryDefinition<PendingApplyDefersSiblingBBoundaryProps,
                                                  PendingApplyDefersSiblingBBoundaryNode>()
                 .tag(202));
    }
  };

  struct FlowTestMarker
  {
    static void onStepSuccess(const int &, void *user)
    {
      FlowTestMarkerContext *ctx = static_cast<FlowTestMarkerContext *>(user);
      ctx->order->push_back(ctx->marker);
    }

    static void onStepFinally(void *user)
    {
      FlowTestMarkerContext *ctx = static_cast<FlowTestMarkerContext *>(user);
      ctx->order->push_back(ctx->marker);
    }

    static void onFlowSuccess(void *user)
    {
      FlowTestMarkerContext *ctx = static_cast<FlowTestMarkerContext *>(user);
      ctx->order->push_back(ctx->marker);
    }

    static loka::dsl::FlowHandleResult onFlowFailureHandled(const loka::dsl::FlowError &, void *user)
    {
      FlowTestMarkerContext *ctx = static_cast<FlowTestMarkerContext *>(user);
      ctx->order->push_back(ctx->marker);
      return loka::dsl::FLOW_ERROR_HANDLED;
    }

    static loka::dsl::FlowHandleResult onStepFailureUnhandled(const loka::dsl::FlowError &, void *user)
    {
      FlowTestMarkerContext *ctx = static_cast<FlowTestMarkerContext *>(user);
      ctx->order->push_back(ctx->marker);
      return loka::dsl::FLOW_ERROR_UNHANDLED;
    }

    static loka::dsl::FlowHandleResult onStepFailureHandled(const loka::dsl::FlowError &, void *user)
    {
      FlowTestMarkerContext *ctx = static_cast<FlowTestMarkerContext *>(user);
      ctx->order->push_back(ctx->marker);
      return loka::dsl::FLOW_ERROR_HANDLED;
    }

    static bool is500(const loka::dsl::FlowError &error, void *)
    {
      return error.code == 500;
    }

    static loka::dsl::FlowHandleResult captureFailure(const loka::dsl::FlowError &error, void *user)
    {
      FlowErrorCapture *capture = static_cast<FlowErrorCapture *>(user);
      if (capture)
      {
        capture->kind = error.kind;
        capture->code = error.code;
        ++capture->calls;
      }
      return loka::dsl::FLOW_ERROR_HANDLED;
    }
  };

  struct FlowTestMul2Adapter
  {
    typedef int In;
    typedef int Out;
    loka::dsl::StepRunStatus run(const int &in, int &out, loka::dsl::FlowError &) const
    {
      out = in * 2;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }
  };

  struct FlowTestAdd1Adapter
  {
    typedef int In;
    typedef int Out;
    loka::dsl::StepRunStatus run(const int &in, int &out, loka::dsl::FlowError &) const
    {
      out = in + 1;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }
  };

  struct FlowMatchArmProbe
  {
    FlowMatchArmProbe(bool matchValue, int outputDelta)
        : matches(matchValue),
          delta(outputDelta),
          matcherCalls(0),
          handlerCalls(0)
    {
    }

    bool matches;
    int delta;
    int matcherCalls;
    int handlerCalls;
  };

  struct FlowMatchTestHelpers
  {
    static bool probeMatcher(const int &, void *user)
    {
      FlowMatchArmProbe *probe = static_cast<FlowMatchArmProbe *>(user);
      ++probe->matcherCalls;
      return probe->matches;
    }

    static bool alwaysMatch(const int &, void *)
    {
      return true;
    }

    struct PendingValueProbe
    {
      PendingValueProbe()
          : ready(false),
            calls(0),
            lastIn(-1)
      {
      }
      bool ready;
      int calls;
      int lastIn;
    };

    static loka::dsl::StepRunStatus pendingThenValue(const int &in,
                                                      int &out,
                                                      loka::dsl::FlowError &,
                                                      void *user)
    {
      PendingValueProbe *probe = static_cast<PendingValueProbe *>(user);
      ++probe->calls;
      probe->lastIn = in;
      if (!probe->ready)
      {
        return loka::dsl::FLOW_STEP_PENDING;
      }
      out = in + 100;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }

    static void countFinally(void *user)
    {
      ++(*static_cast<int *>(user));
    }

    static loka::dsl::StepRunStatus valueHandler(const int &in,
                                                  int &out,
                                                  loka::dsl::FlowError &,
                                                  void *user)
    {
      FlowMatchArmProbe *probe = static_cast<FlowMatchArmProbe *>(user);
      ++probe->handlerCalls;
      out = in + probe->delta;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }

    static loka::dsl::StepRunStatus countAsValue(const int &in,
                                                  int &out,
                                                  loka::dsl::FlowError &,
                                                  void *user)
    {
      int *calls = static_cast<int *>(user);
      ++(*calls);
      out = in;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }

    static loka::dsl::FlowHandleResult countAsHandledFailure(const loka::dsl::FlowError &, void *user)
    {
      ++(*static_cast<int *>(user));
      return loka::dsl::FLOW_ERROR_HANDLED;
    }
  };

  static std::string ReadFlowMatchAuditBytes(const char *path)
  {
    FILE *input = std::fopen(path, "rb");
    assert(input != 0);
    std::string content;
    char buffer[128];
    std::size_t count = 0;
    while ((count = std::fread(buffer, 1, sizeof(buffer), input)) != 0)
    {
      content.append(buffer, count);
    }
    const int closeResult = std::fclose(input);
    (void)closeResult;
    assert(closeResult == 0);
    return content;
  }

  struct FlowTestFail500Adapter
  {
    typedef int In;
    typedef int Out;
    loka::dsl::StepRunStatus run(const int &, int &out, loka::dsl::FlowError &error) const
    {
      out = 0;
      error.kind = 1;
      error.code = 500;
      return loka::dsl::FLOW_STEP_FAILED;
    }
  };

  struct FlowTestPredicates
  {
    static bool greaterThan10(const int &value, void *)
    {
      return value > 10;
    }

    static bool equalsExpected(const int &value, void *user)
    {
      const int *expected = static_cast<const int *>(user);
      return expected != 0 && value == *expected;
    }
  };

  struct FlowTestCheckLoadingAdapter
  {
    typedef int In;
    typedef int Out;

    explicit FlowTestCheckLoadingAdapter(const bool *loadingState)
        : loadingState_(loadingState)
    {
    }

    loka::dsl::StepRunStatus run(const int &in, int &out, loka::dsl::FlowError &) const
    {
      assert(this->loadingState_ != 0);
      assert(*this->loadingState_ == true);
      out = in;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }

    const bool *loadingState_;
  };

  struct FlowTestPendingThenSuccessAdapter
  {
    typedef int In;
    typedef int Out;

    FlowTestPendingThenSuccessAdapter(bool *ready, int *calls)
        : ready_(ready),
          calls_(calls)
    {
    }

    loka::dsl::StepRunStatus run(const int &in, int &out, loka::dsl::FlowError &) const
    {
      ++(*this->calls_);
      if (!*this->ready_)
      {
        return loka::dsl::FLOW_STEP_PENDING;
      }
      out = in + 100;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }

    bool *ready_;
    int *calls_;
  };

  struct FlowTestFailOnceAdapter
  {
    typedef int In;
    typedef int Out;

    explicit FlowTestFailOnceAdapter(bool *failedOnce)
        : failedOnce_(failedOnce)
    {
    }

    loka::dsl::StepRunStatus run(const int &in, int &out, loka::dsl::FlowError &error) const
    {
      if (!*this->failedOnce_)
      {
        *this->failedOnce_ = true;
        error.kind = 9;
        error.code = 500;
        return loka::dsl::FLOW_STEP_FAILED;
      }
      out = in + 7;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }

    bool *failedOnce_;
  };

  struct FlowTestPassThenFailAdapter
  {
    typedef int In;
    typedef int Out;

    explicit FlowTestPassThenFailAdapter(int *calls)
        : calls_(calls)
    {
    }

    loka::dsl::StepRunStatus run(const int &in, int &out, loka::dsl::FlowError &error) const
    {
      ++(*this->calls_);
      if (*this->calls_ > 1)
      {
        error.kind = 9;
        error.code = 500;
        return loka::dsl::FLOW_STEP_FAILED;
      }
      out = in + 7;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }

    int *calls_;
  };

  struct FlowTestCountedPassAdapter
  {
    typedef int In;
    typedef int Out;

    explicit FlowTestCountedPassAdapter(int *calls)
        : calls_(calls)
    {
    }

    loka::dsl::StepRunStatus run(const int &in, int &out, loka::dsl::FlowError &) const
    {
      ++(*this->calls_);
      out = in + 1;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }

    int *calls_;
  };

  struct FlowTestFieldOutput
  {
    int value;
    int extra;
    FlowTestFieldOutput()
        : value(0),
          extra(0)
    {
    }
  };

  struct FlowTestFieldAdapter
  {
    typedef int In;
    typedef FlowTestFieldOutput Out;
    loka::dsl::StepRunStatus run(const int &in, FlowTestFieldOutput &out, loka::dsl::FlowError &) const
    {
      out.value = in * 10;
      out.extra = in + 1;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }
  };

  struct FlowTestSnapTextValueAdapter
  {
    typedef int In;
    typedef loka::dsl::SnapRecord Out;

    FlowTestSnapTextValueAdapter(const char *stepName, const char *value, long tick)
        : stepName_(stepName ? stepName : ""),
          value_(value ? value : ""),
          tick_(tick)
    {
    }

    loka::dsl::StepRunStatus run(const int &, loka::dsl::SnapRecord &out, loka::dsl::FlowError &) const
    {
      out.set("test", "SceneFlow");
      out.set("step", this->stepName_.c_str());
      out.set("node", "MainText");
      out.setInt("tick", this->tick_);
      out.setInt("scenario_version", 1);
      out.set("status", loka::dsl::SnapStatusOk());
      out.set("text.value", this->value_.c_str());
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }

    std::string stepName_;
    std::string value_;
    long tick_;
  };

  struct FlowTestSnapDirtyMaskAdapter
  {
    typedef int In;
    typedef loka::dsl::SnapRecord Out;

    FlowTestSnapDirtyMaskAdapter(const char *stepName, long dirtyMask, long tick)
        : stepName_(stepName ? stepName : ""),
          dirtyMask_(dirtyMask),
          tick_(tick)
    {
    }

    loka::dsl::StepRunStatus run(const int &, loka::dsl::SnapRecord &out, loka::dsl::FlowError &) const
    {
      out.set("test", "SceneFlow");
      out.set("step", this->stepName_.c_str());
      out.set("node", "MainText");
      out.setInt("tick", this->tick_);
      out.setInt("scenario_version", 1);
      out.set("status", loka::dsl::SnapStatusOk());
      out.setInt("dirty.mask", this->dirtyMask_);
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }

    std::string stepName_;
    long dirtyMask_;
    long tick_;
  };

  struct FlowTestPlatformContext : public PlatformContext
  {
    FlowTestPlatformContext()
        : createImageResult_(false),
          createImageCalls_(0),
          width_(16),
          height_(16)
    {
    }

    virtual App *createApp(AppConfigurable *, NativeModuleHandle, int) const
    {
      return 0;
    }
    virtual Window *createWindow(const WindowProps &)
    {
      return 0;
    }
    virtual loka::app::scene::NodeContext *createNodeContext(loka::app::scene::Node *) const
    {
      return 0;
    }
    virtual bool openFile(const loka::file::File &, loka::platform::file::FileHandle &) const
    {
      return false;
    }
    virtual bool createImageFromBlob(const loka::core::resource::Blob &,
                                     std::size_t,
                                     std::size_t,
                                     loka::core::resource::Image &out)
    {
      ++createImageCalls_;
      if (!createImageResult_)
      {
        return false;
      }
      out = loka::core::resource::Image::FromNative(
          reinterpret_cast<void *>(0x1), width_, height_, &FlowTestPlatformContext::ReleaseNativeNoop, 0);
      return true;
    }

    static void ReleaseNativeNoop(void *, void *) {}

    bool createImageResult_;
    mutable int createImageCalls_;
    int width_;
    int height_;
  };

  struct SimpleViewerRetainingPlatformContext : public FlowTestPlatformContext
  {
    SimpleViewerRetainingPlatformContext()
        : observedBlob_(),
          capacityReleaseWitness_(0),
          capacityQueryCalls_(0)
    {
    }

    virtual bool queryLargestContiguousAllocation(std::size_t &out) const
    {
      if (!this->capacityReleaseWitness_)
      {
        return false;
      }
      ++this->capacityQueryCalls_;
      out = this->capacityReleaseWitness_->useCount() == 1 ? static_cast<std::size_t>(-1) : 0;
      return true;
    }

    virtual bool createImageFromBlob(const loka::core::resource::Blob &blob,
                                     std::size_t,
                                     std::size_t,
                                     loka::core::resource::Image &out)
    {
      ++this->createImageCalls_;
      if (!this->createImageResult_)
      {
        return false;
      }
      this->observedBlob_ = blob.handle();
      loka::core::resource::Blob *retainedBlob = new loka::core::resource::Blob(blob);
      out = loka::core::resource::Image::FromNative(
          reinterpret_cast<void *>(0x1),
          this->width_,
          this->height_,
          &SimpleViewerRetainingPlatformContext::ReleaseRetainedBlob,
          retainedBlob);
      return true;
    }

    static void ReleaseRetainedBlob(void *, void *userData)
    {
      delete static_cast<loka::core::resource::Blob *>(userData);
    }

    mutable loka::core::Managed<loka::core::resource::BlobRecord> observedBlob_;
    const loka::core::Managed<loka::core::resource::BlobRecord> *capacityReleaseWitness_;
    mutable int capacityQueryCalls_;
  };

  struct FlowTestCapturableBitmap : public loka::app::scene::ICapturableBitmap
  {
    virtual bool captureBitmap(loka::core::resource::Image &out) const
    {
      out = loka::core::resource::Image::FromNative(
          reinterpret_cast<void *>(0x2), 80, 40, &FlowTestPlatformContext::ReleaseNativeNoop, 0);
      return out.isValid();
    }
  };

  struct FlowTestCapturableNodeContext : public loka::app::scene::NodeContext
  {
    FlowTestCapturableNodeContext()
        : bitmap_()
    {
    }

    virtual loka::app::scene::ICapturableBitmap *asCapturableBitmap()
    {
      return &bitmap_;
    }
    virtual const loka::app::scene::ICapturableBitmap *asCapturableBitmap() const
    {
      return &bitmap_;
    }

  private:
    FlowTestCapturableBitmap bitmap_;
  };

  class CurrentTargetBitmap : public loka::app::scene::ICapturableBitmap
  {
  public:
    explicit CurrentTargetBitmap(loka::app::scene::Scene &scene) : scene_(scene) {}

    virtual bool captureBitmap(loka::core::resource::Image &out) const
    {
      loka::app::scene::Node *current = 0;
      loka::dsl::FlowError error;
      LOKA_VERIFY(loka::dsl::testing::LookupNodeById<loka::app::scene::Node>(
                      &this->scene_, "Target", current, error) == loka::dsl::FLOW_STEP_SUCCEEDED);
      LOKA_VERIFY(current->getContext() != 0);
      // Compare against a fresh lookup before using any captured context.
      LOKA_VERIFY(current->getContext()->asCapturableBitmap() == this);
      out = loka::core::resource::Image::FromNative(
          reinterpret_cast<void *>(0x2), 80, 40, &FlowTestPlatformContext::ReleaseNativeNoop, 0);
      return out.isValid();
    }

    static bool capture(const loka::dsl::testing::ViewCaptureTarget &target,
                        loka::dsl::testing::ViewBitmapCapture &out,
                        loka::dsl::FlowError &error, void *user)
    {
      loka::app::scene::Scene *scene = static_cast<loka::app::scene::Scene *>(user);
      loka::app::scene::Node *current = 0;
      LOKA_VERIFY(loka::dsl::testing::LookupNodeById<loka::app::scene::Node>(
                      scene, "Target", current, error) == loka::dsl::FLOW_STEP_SUCCEEDED);
      LOKA_VERIFY(target.node == current);
      LOKA_VERIFY(target.capturableBitmap == current->getContext()->asCapturableBitmap());
      if (!target.capturableBitmap->captureBitmap(out.image))
      {
        return false;
      }
      out.captured = out.image.isValid();
      out.width = out.image.width();
      out.height = out.image.height();
      return out.captured;
    }

  private:
    loka::app::scene::Scene &scene_;
  };

  class BorrowedCaptureContext : public loka::app::scene::NodeContext
  {
  public:
    explicit BorrowedCaptureContext(CurrentTargetBitmap &bitmap) : bitmap_(bitmap) {}
    virtual loka::app::scene::ICapturableBitmap *asCapturableBitmap() { return &this->bitmap_; }
    virtual const loka::app::scene::ICapturableBitmap *asCapturableBitmap() const { return &this->bitmap_; }

  private:
    CurrentTargetBitmap &bitmap_;
  };

  struct StateNotifyDeleteCtx
  {
    loka::core::EmitterState *state;
    loka::core::MutableState<int> *valueState;
    int destroyCalls;
    int siblingCalls;
  };

  struct StateNotifyHelpers
  {
    static void destroySelf(void *user)
    {
      StateNotifyDeleteCtx *ctx = static_cast<StateNotifyDeleteCtx *>(user);
      ++ctx->destroyCalls;
      if (ctx->state)
      {
        loka::core::EmitterState *owned = ctx->state;
        ctx->state = 0;
        delete owned;
      }
    }

    static void sibling(void *user)
    {
      StateNotifyDeleteCtx *ctx = static_cast<StateNotifyDeleteCtx *>(user);
      ++ctx->siblingCalls;
    }

    static void destroyValueSelf(void *user)
    {
      StateNotifyDeleteCtx *ctx = static_cast<StateNotifyDeleteCtx *>(user);
      ++ctx->destroyCalls;
      if (ctx->valueState)
      {
        loka::core::MutableState<int> *owned = ctx->valueState;
        ctx->valueState = 0;
        delete owned;
      }
    }

    static void selfUnbind(void *user)
    {
      StateNotifyDeleteCtx *ctx = static_cast<StateNotifyDeleteCtx *>(user);
      ++ctx->destroyCalls;
      if (ctx->state)
      {
        ctx->state->unbind(&StateNotifyHelpers::selfUnbind, user);
      }
    }

    static void unbindSibling(void *user)
    {
      StateNotifyDeleteCtx *ctx = static_cast<StateNotifyDeleteCtx *>(user);
      ++ctx->destroyCalls;
      if (ctx->state)
      {
        ctx->state->unbind(&StateNotifyHelpers::sibling, user);
      }
    }

    static void unbindValueSibling(void *user)
    {
      StateNotifyDeleteCtx *ctx = static_cast<StateNotifyDeleteCtx *>(user);
      ++ctx->destroyCalls;
      if (ctx->valueState)
      {
        ctx->valueState->unbind(&StateNotifyHelpers::sibling, user);
      }
    }
  };

  struct FlowScenePlatformController : public loka::app::scene::IPlatformController
  {
    FlowScenePlatformController()
        : lastMaterialized_(0),
          lastFlags_(loka::app::scene::NODE_DIRTY_NONE),
          lastFullRebuild_(false),
          lastBoundaryApplyRoot_(0),
          lastBoundaryApplyBoundary_(0),
          lastBoundaryApplyInfo_(),
          lastBoundaryApplyPlan_(),
          boundaryApplyCalls_(0),
          calls_(0),
          synchronizeCalls_(0),
          queuePendingSyncOnChange_(false),
          pendingSync_(false),
          skipGlobalChangeForBoundaryLocalPaint_(false),
          destroyed_(false)
    {
    }

    virtual void onChange(loka::app::scene::Node *rootNode, loka::app::scene::NodeDirtyFlags flags, bool fullRebuild)
    {
      lastMaterialized_ = rootNode;
      lastFlags_ = flags;
      lastFullRebuild_ = fullRebuild;
      ++calls_;
      if (this->queuePendingSyncOnChange_)
      {
        this->pendingSync_ = true;
      }
    }

    virtual void onBoundaryApply(loka::app::scene::Node *rootNode,
                                 loka::app::scene::BoundaryNode *boundary,
                                 const loka::app::scene::BoundaryLocalApplyInfo &info,
                                 const loka::app::scene::PlatformApplyPlan &plan)
    {
      assert(plan.isLocalizedFor(boundary));
      assert(plan.hasBoundaryApplyWork(boundary));
      lastBoundaryApplyRoot_ = rootNode;
      lastBoundaryApplyBoundary_ = boundary;
      lastBoundaryApplyInfo_ = info;
      lastBoundaryApplyPlan_ = plan;
      ++boundaryApplyCalls_;
    }

    virtual bool canSkipGlobalChangeForBoundaryLocalPaint() const
    {
      return skipGlobalChangeForBoundaryLocalPaint_;
    }

    virtual void synchronize()
    {
      ++this->synchronizeCalls_;
      this->pendingSync_ = false;
    }

    virtual bool hasPendingSync() const
    {
      return this->pendingSync_;
    }

    virtual void destroy()
    {
      destroyed_ = true;
    }

    loka::app::scene::Node *lastMaterialized_;
    loka::app::scene::NodeDirtyFlags lastFlags_;
    bool lastFullRebuild_;
    loka::app::scene::Node *lastBoundaryApplyRoot_;
    loka::app::scene::BoundaryNode *lastBoundaryApplyBoundary_;
    loka::app::scene::BoundaryLocalApplyInfo lastBoundaryApplyInfo_;
    loka::app::scene::PlatformApplyPlan lastBoundaryApplyPlan_;
    int boundaryApplyCalls_;
    int calls_;
    int synchronizeCalls_;
    bool queuePendingSyncOnChange_;
    bool pendingSync_;
    bool skipGlobalChangeForBoundaryLocalPaint_;
    bool destroyed_;
  };

  struct FlowTestPlatformDirtyMaskAdapter
  {
    typedef loka::app::scene::Scene *In;
    typedef loka::dsl::SnapRecord Out;

    FlowTestPlatformDirtyMaskAdapter(const char *stepName, const FlowScenePlatformController *platform, long tick)
        : stepName_(stepName ? stepName : ""),
          platform_(platform),
          tick_(tick)
    {
    }

    loka::dsl::StepRunStatus
    run(loka::app::scene::Scene *const &, loka::dsl::SnapRecord &out, loka::dsl::FlowError &error) const
    {
      if (!this->platform_)
      {
        error.kind = loka::dsl::testing::FLOW_ERROR_KIND_SCENE_SCENARIO;
        error.code = loka::dsl::testing::FLOW_ERROR_SCENE_TEST_NULL_SCENE;
        return loka::dsl::FLOW_STEP_FAILED;
      }
      out.set("test", "SceneFlow");
      out.set("step", this->stepName_.c_str());
      out.set("node", "ScenePlatform");
      out.setInt("tick", this->tick_);
      out.setInt("scenario_version", 1);
      out.set("status", loka::dsl::SnapStatusOk());
      out.setInt("platform.dirty.mask", static_cast<long>(this->platform_->lastFlags_));
      out.setInt("platform.materialized", this->platform_->lastMaterialized_ ? 1 : 0);
      out.setInt("platform.full_rebuild", this->platform_->lastFullRebuild_ ? 1 : 0);
      out.setInt("platform.calls", this->platform_->calls_);
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }

    std::string stepName_;
    const FlowScenePlatformController *platform_;
    long tick_;
  };

  struct FlowTestViewBitmapCaptureHelpers
  {
    static bool capture(const loka::dsl::testing::ViewCaptureTarget &target,
                        loka::dsl::testing::ViewBitmapCapture &out,
                        loka::dsl::FlowError &error,
                        void *)
    {
      if (!target.node)
      {
        error.kind = loka::dsl::testing::FLOW_ERROR_KIND_SCENE_SCENARIO;
        error.code = loka::dsl::testing::FLOW_ERROR_SCENE_TEST_NODE_NOT_FOUND;
        return false;
      }
      out.image = loka::core::resource::Image::FromNative(
          reinterpret_cast<void *>(0x1), 64, 32, &FlowTestPlatformContext::ReleaseNativeNoop, 0);
      out.captured = out.image.isValid();
      out.width = out.image.width();
      out.height = out.image.height();
      return out.captured;
    }
  };

  loka::app::OpenFileDialogNode *findSimpleViewerOpenFileDialog(loka::app::scene::Scene &scene)
  {
    loka::app::scene::Scene *scenePtr = &scene;
    loka::app::scene::Node *node = 0;
    loka::dsl::FlowError error;
    if (loka::dsl::testing::LookupNodeById<loka::app::scene::Node>(scenePtr, "SimpleViewerOpenFileDialog", node, error)
        != loka::dsl::FLOW_STEP_SUCCEEDED)
    {
      return 0;
    }
    return node ? node->asOpenFileDialogNode() : 0;
  }

  void deliverSimpleViewerOpenFileDialogResult(loka::app::OpenFileDialogNode *dialog,
                                               const loka::app::FileChooserResult &result)
  {
    assert(dialog && dialog->props.result_.isValid());
    loka::app::scene::NodeState<loka::app::FileChooserResult> resultState = dialog->props.result_;
    resultState.set(result, true);
  }
} // namespace

void testLokaFlowDslV1Core()
{
  printf("\n==== [testLokaFlowDslV1Core] start ====\n");

  {
    using namespace loka::app::scene;

    Node node;
    LOKA_VERIFY(node.resolveChildComposeEvent(COMPOSE_EVENT_UPDATE) == COMPOSE_EVENT_UPDATE);
    node.markPendingAttachForCompose();
    LOKA_VERIFY(node.resolveChildComposeEvent(COMPOSE_EVENT_UPDATE) == COMPOSE_EVENT_ATTACH);
    LOKA_VERIFY(node.resolveChildComposeEvent(COMPOSE_EVENT_UPDATE) == COMPOSE_EVENT_UPDATE);
    node.markPendingAttachForCompose();
    LOKA_VERIFY(node.resolveChildComposeEvent(COMPOSE_EVENT_ATTACH) == COMPOSE_EVENT_ATTACH);
    LOKA_VERIFY(node.resolveChildComposeEvent(COMPOSE_EVENT_DETACH) == COMPOSE_EVENT_DETACH);
  }

  {
    loka::app::HStack buttons = buildTypedHStack();
  LOKA_VERIFY(buttons.childrenCount() == 2);
    assert(buttons.props.hasVerticalAlignment_);
    assert(buttons.props.verticalAlignment_ == loka::app::VERTICAL_ALIGNMENT_BOTTOM);
  }

  {
    loka::app::VStack column = buildTypedVStack();
  LOKA_VERIFY(column.childrenCount() == 2);
    assert(column.props.hasHorizontalAlignment_);
    assert(column.props.horizontalAlignment_ == loka::app::HORIZONTAL_ALIGNMENT_CENTER);
  }

  {
    loka::app::Box box = buildTypedBox();
  LOKA_VERIFY(box.childrenCount() == 1);
  assert(box.props.padding == loka::app::layout::LayoutGap::of(8));
  assert(box.hasTestId());
  assert(box.testIdValue() == "TypedBox");
  }

  {
    loka::app::Fragment fragment = buildTypedFragment();
  LOKA_VERIFY(fragment.childrenCount() == 2);
  }

  {
    loka::app::Grid grid = buildTypedGrid();
  LOKA_VERIFY(grid.childrenCount() == 2);
    assert(grid.props.rows == 1);
    assert(grid.props.cols == 2);
  }

  {
    loka::app::ZStack stack = buildTypedZStack();
  LOKA_VERIFY(stack.childrenCount() == 2);
    assert(stack.hasTestId());
    assert(stack.testIdValue() == "TypedZStack");
  }

  {
    using namespace loka::app::scene;

    NodeDefinitionBase *rootDefinition = BoundaryDefinition<TypedDslLightHostProps, TypedDslLightHostNode>().clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    loka::app::TextNode *lightChild = 0;
    loka::dsl::FlowError lookupError;
    LOKA_VERIFY(
        loka::dsl::testing::LookupNodeById<loka::app::TextNode>(&scene, "TypedDslLightChild", lightChild, lookupError)
        != loka::dsl::FLOW_STEP_FAILED);
    assert(lightChild != 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    RecordingDirtySourceRegistrar registrar;
    loka::app::TextNode constantTextNode(loka::app::TextProps("Constant text"));
    constantTextNode.declareDirtySources(registrar);
    assert(registrar.calls == 0);

    loka::core::MutableState<loka::core::String> liveText(loka::core::String::Literal("Live text"));
    loka::app::TextNode liveTextNode((loka::app::TextProps(&liveText)));
    liveTextNode.declareDirtySources(registrar);
    assert(registrar.calls == 1);
    assert(registrar.lastState == &liveText);
    assert(registrar.lastFlags == loka::app::scene::NODE_DIRTY_PROPS);
  }

  {
    RecordingDirtySourceRegistrar registrar;
    loka::app::ButtonProps constantButtonProps;
    constantButtonProps.text("Constant button");
    loka::app::ButtonNode constantButtonNode(constantButtonProps);
    constantButtonNode.declareDirtySources(registrar);
    assert(registrar.calls == 0);

    loka::core::MutableState<loka::core::String> liveButtonText(loka::core::String::Literal("Live button"));
    loka::app::ButtonProps liveButtonProps;
    liveButtonProps.text(&liveButtonText);
    loka::app::ButtonNode liveButtonNode(liveButtonProps);
    liveButtonNode.declareDirtySources(registrar);
    assert(registrar.calls == 1);
    assert(registrar.lastState == &liveButtonText);
    assert(registrar.lastFlags == loka::app::scene::NODE_DIRTY_PROPS);
  }

  {
    RecordingDirtySourceRegistrar registrar;
    loka::app::CellProps constantCellProps;
    constantCellProps.text("Constant cell");
    loka::app::CellNode constantCellNode(constantCellProps);
    constantCellNode.declareDirtySources(registrar);
    assert(registrar.calls == 0);

    loka::core::MutableState<loka::core::String> liveCellText(loka::core::String::Literal("Live cell"));
    loka::app::CellProps liveCellProps;
    liveCellProps.text(&liveCellText);
    loka::app::CellNode liveCellNode(liveCellProps);
    liveCellNode.declareDirtySources(registrar);
    assert(registrar.calls == 1);
    assert(registrar.lastState == &liveCellText);
    assert(registrar.lastFlags == loka::app::scene::NODE_DIRTY_PROPS);
  }

  {
    PendingLayoutBoundaryProps::assertAllowedValueType<int>();
    PendingLayoutBoundaryProps::assertAllowedValueType<loka::core::State<int> *>();

    assert((loka::app::scene::BoundaryPropValueRules<int>::kAllowed));
    assert((loka::app::scene::BoundaryPropValueRules<loka::core::State<int> *>::kAllowed));
    LOKA_STATIC_ASSERT(
        !(loka::app::scene::BoundaryPropValueRules<loka::core::Managed<loka::core::MutableState<int> > >::kAllowed),
        boundary_props_reject_managed_mutable_state);
    assert(!(loka::app::scene::BoundaryPropValueRules<loka::app::scene::NodeState<int> >::kAllowed));
    assert(!(loka::app::scene::BoundaryPropValueRules<loka::core::MutableState<int> *>::kAllowed));
    loka::core::MutableState<int> countState(21);
    loka::core::MutableState<int> sharedCountState(34);
    loka::app::scene::BorrowedState<int> borrowedCount = PendingLayoutBoundaryProps::borrowed<int>(&countState);
    loka::app::scene::BorrowedState<int> sharedCount = PendingLayoutBoundaryProps::borrowed<int>(&sharedCountState);
    (void)borrowedCount;
    assert(borrowedCount.isValid());
    assert(borrowedCount.get() == 21);
    (void)sharedCount;
    assert(sharedCount.isValid());
    assert(sharedCount.get() == 34);
  }

  {
    loka::app::scene::BorrowedState<int> emptyBorrowed;
    assert(!emptyBorrowed.isValid());
  }

  {
    loka::app::scene::NodeComposition composition;
    loka::app::scene::ComponentContext context;

    context.setBoundary(reinterpret_cast<loka::app::scene::BoundaryNode *>(0x6100));
    composition.setContext(&context);

    const loka::app::scene::NodeComposition::CurrentBoundary current = composition.currentBoundary();
    (void)current;
    assert(!current.isValid());
  }

  {
    loka::app::scene::NodeComposition composition;
    loka::app::scene::ComponentContext parentBoundaryContext;
    loka::app::scene::ComponentContext currentBoundaryContext(&parentBoundaryContext);
    loka::app::scene::ComponentContext childContext(&currentBoundaryContext);
    BoundaryLookupTestNode parentBoundaryNode(2);
    loka::app::scene::Node currentBoundaryNode;
    loka::app::scene::Node childNode;

    parentBoundaryContext.setOwner(&parentBoundaryNode);
    parentBoundaryContext.setBoundary(reinterpret_cast<loka::app::scene::BoundaryNode *>(0x2000));
    currentBoundaryContext.setOwner(&currentBoundaryNode);
    currentBoundaryContext.setBoundary(reinterpret_cast<loka::app::scene::BoundaryNode *>(0x3000));
    childContext.setOwner(&childNode);
    childContext.setBoundary(reinterpret_cast<loka::app::scene::BoundaryNode *>(0x3000));
    composition.setContext(&childContext);

    loka::app::scene::NodeComposition::FoundBoundary<BoundaryLookupTestApi> foundParent =
        composition.findBoundary<BoundaryLookupTestApi>();
    (void)foundParent;
    assert(foundParent.isValid());
    assert(foundParent.facade().id() == 2);
  }

  {
    loka::app::scene::NodeComposition composition;
    loka::app::scene::ComponentContext context;
    BoundaryLookupTestNode currentBoundaryNode(4);

    context.setOwner(&currentBoundaryNode);
    context.setBoundary(reinterpret_cast<loka::app::scene::BoundaryNode *>(0x2050));
    composition.setContext(&context);

    const loka::app::scene::NodeComposition::FoundBoundary<BoundaryLookupTestApi> foundParent =
        composition.findBoundary<BoundaryLookupTestApi>();
    (void)foundParent;
    assert(!foundParent.isValid());
  }

  {
    loka::app::scene::NodeComposition composition;
    loka::app::scene::ComponentContext parentBoundaryContext;
    loka::app::scene::ComponentContext currentBoundaryContext(&parentBoundaryContext);
    loka::app::scene::ComponentContext childContext(&currentBoundaryContext);
    BoundaryLookupStateNode parentBoundaryNode(9);
    loka::app::scene::Node currentBoundaryNode;
    loka::app::scene::Node childNode;

    parentBoundaryContext.setOwner(&parentBoundaryNode);
    parentBoundaryContext.setBoundary(reinterpret_cast<loka::app::scene::BoundaryNode *>(0x2100));
    currentBoundaryContext.setOwner(&currentBoundaryNode);
    currentBoundaryContext.setBoundary(reinterpret_cast<loka::app::scene::BoundaryNode *>(0x3100));
    childContext.setOwner(&childNode);
    childContext.setBoundary(reinterpret_cast<loka::app::scene::BoundaryNode *>(0x3100));
    composition.setContext(&childContext);

    const loka::app::scene::NodeComposition::FoundBoundary<BoundaryLookupStateApi> foundParent =
        composition.findBoundary<BoundaryLookupStateApi>();
    (void)foundParent;
    assert(foundParent.isValid());
    assert(foundParent.facade().countState().isValid());
    assert(foundParent.facade().countState().get() == 9);
  }

  {
    loka::app::scene::NodeComposition composition;
    loka::app::scene::ComponentContext grandparentContext;
    loka::app::scene::ComponentContext parentBoundaryContext(&grandparentContext);
    loka::app::scene::ComponentContext currentBoundaryContext(&parentBoundaryContext);
    loka::app::scene::ComponentContext childContext(&currentBoundaryContext);
    BoundaryLookupTestNode grandparentNode(3);
    loka::app::scene::Node parentBoundaryNode;
    loka::app::scene::Node currentBoundaryNode;
    loka::app::scene::Node childNode;

    grandparentContext.setOwner(&grandparentNode);
    grandparentContext.setBoundary(reinterpret_cast<loka::app::scene::BoundaryNode *>(0x3000));
    parentBoundaryContext.setOwner(&parentBoundaryNode);
    parentBoundaryContext.setBoundary(reinterpret_cast<loka::app::scene::BoundaryNode *>(0x4000));
    currentBoundaryContext.setOwner(&currentBoundaryNode);
    currentBoundaryContext.setBoundary(reinterpret_cast<loka::app::scene::BoundaryNode *>(0x5000));
    childContext.setOwner(&childNode);
    childContext.setBoundary(reinterpret_cast<loka::app::scene::BoundaryNode *>(0x5000));
    composition.setContext(&childContext);

    loka::app::scene::NodeComposition::FoundBoundary<BoundaryLookupTestApi> foundGrandparent =
        composition.findBoundary<BoundaryLookupTestApi>();
    (void)foundGrandparent;
    assert(!foundGrandparent.isValid());
  }

  {
    DummyStateOwner owner;
    loka::app::scene::NodeComposition composition;
    loka::app::scene::ComponentContext context;
    loka::core::MutableState<int> ownedValue(3);
    loka::app::scene::NodeState<int> nodeState(&ownedValue, 0, &owner);

    context.setBoundary(reinterpret_cast<loka::app::scene::BoundaryNode *>(0x6000));
    context.setStateOwner(&owner);
    composition.setContext(&context);

    loka::app::scene::NodeComposition::CurrentBoundary current = composition.currentBoundary();
    loka::app::scene::NodeComposition::CurrentBoundary::CurrentState<int> foundState = current.state(nodeState);
    assert(current.isValid());
    assert(foundState.isValid());
    assert(foundState.get() == 3);
    foundState.set(7);
    assert(ownedValue.get() == 7);
  }

  {
    DummyStateOwner currentOwner;
    DummyStateOwner foreignOwner;
    loka::app::scene::NodeComposition composition;
    loka::app::scene::ComponentContext context;
    loka::core::MutableState<int> foreignValue(11);
    loka::app::scene::NodeState<int> foreignNodeState(&foreignValue, 0, &foreignOwner);

    context.setBoundary(reinterpret_cast<loka::app::scene::BoundaryNode *>(0x7000));
    context.setStateOwner(&currentOwner);
    composition.setContext(&context);

    loka::app::scene::NodeComposition::CurrentBoundary::CurrentState<int> foundForeignState =
        composition.currentBoundary().state(foreignNodeState);
    (void)foundForeignState;
    assert(!foundForeignState.isValid());
  }

  {
    int input = 3;
    int captured = 0;
    std::vector<int> order;

    FlowTestMarkerContext s1 = {&order, 10};
    FlowTestMarkerContext f1 = {&order, 11};
    FlowTestMarkerContext fs1 = {&order, 12};
    FlowTestMarkerContext s2 = {&order, 20};
    FlowTestMarkerContext f2 = {&order, 21};
    FlowTestMarkerContext fs2 = {&order, 22};
    FlowTestMarkerContext ff = {&order, 99};

    loka::dsl::FlowChain<int, int> chain = //
        loka::dsl::Flow()
        | loka::dsl::Step(1, FlowTestMul2Adapter())
              .input(&input)
              .onSuccess(&captured)
              .onSuccess(&FlowTestMarker::onStepSuccess, &s1)
              .onFinally(&FlowTestMarker::onStepFinally, &f1)
        | loka::dsl::Step(2, FlowTestAdd1Adapter())
              .onSuccess(&FlowTestMarker::onStepSuccess, &s2)
              .onFinally(&FlowTestMarker::onStepFinally, &f2);
    chain.onSuccess(&FlowTestMarker::onFlowSuccess, &fs1);
    chain.onSuccess(&FlowTestMarker::onFlowSuccess, &fs2);
    chain.onFinally(&FlowTestMarker::onStepFinally, &ff);

    const bool ok = chain.run();
    (void)ok;
    assert(ok);
    assert(captured == 6);
    assert(order.size() == 7);
    assert(order[0] == 10);
    assert(order[1] == 11);
    assert(order[2] == 20);
    assert(order[3] == 21);
    assert(order[4] == 12);
    assert(order[5] == 22);
    assert(order[6] == 99);
  }

  {
    int input = 7;
    std::vector<int> order;

    FlowTestMarkerContext s1 = {&order, 110};
    FlowTestMarkerContext f1 = {&order, 111};
    FlowTestMarkerContext fs = {&order, 112};
    FlowTestMarkerContext failMatchUnhandled = {&order, 130};
    FlowTestMarkerContext failDefaultHandled = {&order, 131};
    FlowTestMarkerContext f2 = {&order, 132};
    FlowTestMarkerContext s3 = {&order, 140};
    FlowTestMarkerContext flowFail = {&order, 150};
    FlowTestMarkerContext flowFinal = {&order, 199};

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, FlowTestMul2Adapter())
              .input(&input)
              .onSuccess(&FlowTestMarker::onStepSuccess, &s1)
              .onFinally(&FlowTestMarker::onStepFinally, &f1)
        | loka::dsl::Step(2, FlowTestFail500Adapter())
              .onFailure(&FlowTestMarker::is500, &FlowTestMarker::onStepFailureUnhandled, &failMatchUnhandled)
              .onFailure(&FlowTestMarker::onStepFailureHandled, &failDefaultHandled)
              .onFinally(&FlowTestMarker::onStepFinally, &f2)
        | loka::dsl::Step(3, FlowTestAdd1Adapter()).onSuccess(&FlowTestMarker::onStepSuccess, &s3);
    chain.onSuccess(&FlowTestMarker::onFlowSuccess, &fs);
    chain.onFailure(&FlowTestMarker::onFlowFailureHandled, &flowFail);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    const bool ok = chain.run();
    (void)ok;
    assert(ok);
    assert(order.size() == 6);
    assert(order[0] == 110);
    assert(order[1] == 111);
    assert(order[2] == 130);
    assert(order[3] == 132);
    assert(order[4] == 150);
    assert(order[5] == 199);

    // Flow failure handler must run, default step failure handler must not run
    // (first-match-wins).
    bool hasFlowFailure = false;
    bool hasDefaultStepFailure = false;
    bool hasStep3Success = false;
    for (std::size_t i = 0; i < order.size(); ++i)
    {
      if (order[i] == 150)
        hasFlowFailure = true;
      if (order[i] == 131)
        hasDefaultStepFailure = true;
      if (order[i] == 140)
        hasStep3Success = true;
    }
    (void)hasFlowFailure;
    assert(hasFlowFailure);
    (void)hasDefaultStepFailure;
    assert(!hasDefaultStepFailure);
    (void)hasStep3Success;
    assert(!hasStep3Success);
  }

  {
    int inputA = 5;
    int inputB = 41;
    int captured = 0;
    std::vector<int> order;

    FlowTestMarkerContext s1 = {&order, 210};
    FlowTestMarkerContext f1 = {&order, 211};
    FlowTestMarkerContext s2 = {&order, 220};
    FlowTestMarkerContext f2 = {&order, 221};
    FlowTestMarkerContext flowFinal = {&order, 299};

    loka::dsl::FlowChain<int, int> chain = //
        loka::dsl::Flow()
        | loka::dsl::Step(1, FlowTestMul2Adapter())
              .input(&inputA)
              .onSuccess(&FlowTestMarker::onStepSuccess, &s1)
              .onFinally(&FlowTestMarker::onStepFinally, &f1)
        | loka::dsl::Step(2, FlowTestAdd1Adapter())
              .input(&inputB)
              .onSuccess(&captured)
              .onSuccess(&FlowTestMarker::onStepSuccess, &s2)
              .onFinally(&FlowTestMarker::onStepFinally, &f2);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    const bool resumedOk = chain.resume(2);
    (void)resumedOk;
    assert(resumedOk);
    assert(captured == 42);
    assert(order.size() == 3);
    assert(order[0] == 220);
    assert(order[1] == 221);
    assert(order[2] == 299);

    const bool missing = chain.resume(9999);
    (void)missing;
    assert(!missing);
  }

  {
    int input = 10;
    bool loading = false;
    std::vector<int> order;
    FlowTestMarkerContext flowFinal = {&order, 399};

    // Keep this chain multi-line for readability of the DSL flow.
    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestCheckLoadingAdapter(&loading)).input(&input);
    chain.trackLoading(&loading);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    assert(loading == false);
    const bool ok = chain.run();
    (void)ok;
    assert(ok);
    assert(loading == false);
    assert(order.size() == 1);
    assert(order[0] == 399);
  }

  {
    int input = 9;
    bool loading = false;
    std::vector<int> order;
    FlowTestMarkerContext flowFinal = {&order, 499};

    loka::dsl::FlowChain<int, int> chain = //
        loka::dsl::Flow()                  //
        | loka::dsl::Step(1, FlowTestFail500Adapter()).input(&input);
    chain.trackLoading(&loading);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    assert(loading == false);
    const bool ok = chain.run();
    (void)ok;
    assert(!ok);
    assert(loading == false);
    assert(order.size() == 1);
    assert(order[0] == 499);
  }

  {
    int input = 23;
    int calls = 0;
    bool ready = false;
    bool loading = false;
    int captured = 0;
    std::vector<int> order;
    FlowTestMarkerContext stepFinal = {&order, 510};
    FlowTestMarkerContext flowFinal = {&order, 599};

    loka::dsl::FlowChain<int, int> chain = loka::dsl::Flow()
                                           | loka::dsl::Step(1, FlowTestPendingThenSuccessAdapter(&ready, &calls))
                                                 .input(&input)
                                                 .onSuccess(&captured)
                                                 .onFinally(&FlowTestMarker::onStepFinally, &stepFinal);
    chain.trackLoading(&loading);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    const loka::dsl::FlowRunResult first = chain.runResult();
    (void)first;
    assert(first == loka::dsl::FLOW_RUN_PENDING);
    assert(calls == 1);
    assert(loading == true);
    assert(order.empty()); // no finally on pending

    ready = true;
    const loka::dsl::FlowRunResult resumed = chain.resumeResult(1);
    (void)resumed;
    assert(resumed == loka::dsl::FLOW_RUN_SUCCEEDED);
    assert(calls == 2);
    assert(captured == 123);
    assert(loading == false);
    assert(order.size() == 2);
    assert(order[0] == 510);
    assert(order[1] == 599);
  }

  {
    int input = 31;
    int calls = 0;
    bool ready = false;
    int captured = 0;
    std::vector<int> order;
    FlowTestMarkerContext flowFinal = {&order, 605};

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, FlowTestPendingThenSuccessAdapter(&ready, &calls)).input(&input).onSuccess(&captured);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    const loka::dsl::FlowRunResult first = chain.runResult();
    (void)first;
    assert(first == loka::dsl::FLOW_RUN_PENDING);
    assert(calls == 1);
    assert(captured == 0);
    assert(order.empty());

    chain.cancel();
    ready = true;
    const loka::dsl::FlowRunResult canceled = chain.resumeResult(1);
    (void)canceled;
    assert(canceled == loka::dsl::FLOW_RUN_CANCELED);
    assert(calls == 1);
    assert(captured == 0);
    assert(order.size() == 1);
    assert(order[0] == 605);
  }

  {
    int input = 37;
    int calls = 0;
    bool ready = false;
    FlowErrorCapture capture = {0, 0, 0};
    std::vector<int> order;
    FlowTestMarkerContext flowFinal = {&order, 607};

    loka::dsl::FlowChain<int, int> chain = loka::dsl::Flow()
                                           | loka::dsl::Step(1, FlowTestPendingThenSuccessAdapter(&ready, &calls))
                                                 .input(&input)
                                                 .timeoutPending(1)
                                                 .onFailure(&FlowTestMarker::captureFailure, &capture);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_PENDING);
    assert(calls == 1);
    assert(capture.calls == 0);

    LOKA_VERIFY(chain.resumeResult(1) == loka::dsl::FLOW_RUN_SUCCEEDED);
    assert(calls == 2);
    assert(capture.calls == 1);
    assert(capture.kind == loka::dsl::FLOW_ERROR_KIND_FLOW);
    assert(capture.code == loka::dsl::FLOW_ERROR_CODE_STEP_PENDING_TIMEOUT);
    assert(order.size() == 1);
    assert(order[0] == 607);
  }

  {
    int input = 41;
    int calls = 0;
    bool ready = false;
    int captured = 0;
    std::vector<int> order;
    FlowTestMarkerContext flowFinal = {&order, 615};

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, FlowTestPendingThenSuccessAdapter(&ready, &calls)).input(&input).onSuccess(&captured);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_PENDING);
    assert(calls == 1);

    chain.cancel();
    chain.clearCancel();
    ready = true;

    const loka::dsl::FlowRunResult resumed = chain.resumeResult(1);
    (void)resumed;
    assert(resumed == loka::dsl::FLOW_RUN_SUCCEEDED);
    assert(calls == 2);
    assert(captured == 141);
    assert(order.size() == 1);
    assert(order[0] == 615);
  }

  {
    int input = 11;
    int out = 0;
    std::vector<int> order;
    FlowTestMarkerContext flowFinal = {&order, 625};

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestAdd1Adapter()).input(&input).onSuccess(&out);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);
    chain.cancel();

    const loka::dsl::FlowRunResult canceled = chain.runResult();
    (void)canceled;
    assert(canceled == loka::dsl::FLOW_RUN_CANCELED);
    assert(out == 0);
    assert(order.size() == 1);
    assert(order[0] == 625);
  }

  {
    int input = 2;
    int out = 0;
    bool failedOnce = false;
    std::vector<int> order;
    FlowTestMarkerContext stepFail = {&order, 610};
    FlowTestMarkerContext stepSuccess = {&order, 611};
    FlowTestMarkerContext flowFinal = {&order, 699};

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, FlowTestFailOnceAdapter(&failedOnce))
              .input(&input)
              .onFailure(&FlowTestMarker::is500, &FlowTestMarker::onStepFailureHandled, &stepFail, 1)
              .onSuccess(&out)
              .onSuccess(&FlowTestMarker::onStepSuccess, &stepSuccess);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    const bool ok = chain.run();
    (void)ok;
    assert(ok);
    assert(out == 9);
    assert(order.size() == 3);
    assert(order[0] == 610);
    assert(order[1] == 611);
    assert(order[2] == 699);
  }

  {
    int input = 3;
    int out = 0;
    bool failedOnce = false;
    std::vector<int> order;
    FlowTestMarkerContext flowFail = {&order, 710};
    FlowTestMarkerContext flowSuccess = {&order, 711};
    FlowTestMarkerContext flowFinal = {&order, 799};

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestFailOnceAdapter(&failedOnce)).input(&input).onSuccess(&out);
    chain.onFailure(&FlowTestMarker::onFlowFailureHandled, &flowFail, 1);
    chain.onSuccess(&FlowTestMarker::onFlowSuccess, &flowSuccess);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    const bool ok = chain.run();
    (void)ok;
    assert(ok);
    assert(out == 10);
    assert(order.size() == 3);
    assert(order[0] == 710);
    assert(order[1] == 711);
    assert(order[2] == 799);
  }

  {
    int input = 7;
    int output = 0;

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestMul2Adapter()).input(&input)
        | loka::dsl::Step(2, loka::dsl::AssertPredicate<int>(&FlowTestPredicates::greaterThan10, 0))
        | loka::dsl::Step(3, FlowTestAdd1Adapter()).onSuccess(&output);

    LOKA_VERIFY(chain.run());
    assert(output == 15);
  }

  {
    int input = 4;
    int expected = 9;
    FlowErrorCapture capture = {0, 0, 0};

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestMul2Adapter()).input(&input)
        | loka::dsl::Step(2, loka::dsl::AssertPredicate<int>(&FlowTestPredicates::equalsExpected, &expected))
              .onFailure(&FlowTestMarker::captureFailure, &capture);

    LOKA_VERIFY(chain.run());
    assert(capture.calls == 1);
    assert(capture.kind == loka::dsl::FLOW_ERROR_KIND_FLOW);
    assert(capture.code == loka::dsl::FLOW_ERROR_CODE_ASSERT_PREDICATE_FAILED);
  }

  {
    int input = 10;
    int out = 0;
    int callsA = 0;
    int callsB = 0;
    std::vector<int> order;
    FlowTestMarkerContext flowFinal = {&order, 899};

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestCountedPassAdapter(&callsA)).input(&input).onSuccess(&out, 2)
        | loka::dsl::Step(2, FlowTestCountedPassAdapter(&callsB)).onSuccess(&out);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    const bool ok = chain.run();
    (void)ok;
    assert(ok);
    assert(callsA == 1);
    assert(callsB == 1);
    assert(out == 12);
    assert(order.size() == 1);
    assert(order[0] == 899);
  }

  {
    int input = 20;
    int out = 0;
    std::vector<int> flowMarks;
    FlowTestMarkerContext flowSuccess = {&flowMarks, 1};
    std::vector<int> order;
    FlowTestMarkerContext flowFinal = {&order, 999};

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestAdd1Adapter()).input(&input).onSuccess(&out);
    chain.onSuccess(&FlowTestMarker::onFlowSuccess, &flowSuccess, 9999);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    const bool ok = chain.run();
    (void)ok;
    assert(!ok);
    assert(flowMarks.size() == 1);
    assert(out == 21);
    assert(order.size() == 1);
    assert(order[0] == 999);
  }

  {
    // Flow-level onSuccess with a VALID resume step id (issue #46 item F2):
    // the flow-level finally must fire exactly once per logical run, at the
    // resumed segment's terminal — not once before the jump and again at the
    // end. The resume target fails on its rerun so the segment terminates.
    int input = 30;
    int out = 0;
    int resumeCalls = 0;
    bool loading = false;
    std::vector<int> flowMarks;
    FlowTestMarkerContext flowSuccess = {&flowMarks, 1};
    std::vector<int> order;
    FlowTestMarkerContext flowFinal = {&order, 1000};

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestAdd1Adapter()).input(&input)
        | loka::dsl::Step(2, FlowTestPassThenFailAdapter(&resumeCalls)).onSuccess(&out);
    chain.onSuccess(&FlowTestMarker::onFlowSuccess, &flowSuccess, 2);
    chain.trackLoading(&loading);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    const loka::dsl::FlowRunResult result = chain.runResult();
    (void)result;
    assert(result == loka::dsl::FLOW_RUN_FAILED); // rerun of step 2 fails unhandled
    assert(flowMarks.size() == 1);
    assert(resumeCalls == 2);
    assert(out == 38);
    assert(loading == false);
    assert(order.size() == 1); // flow-level finally fires once per logical run
    assert(order[0] == 1000);
  }

  {
    int input = 4;
    std::vector<int> order;
    FlowTestMarkerContext step1Final = {&order, 1001};
    FlowTestMarkerContext step2Final = {&order, 1002};
    FlowTestMarkerContext flowFinal = {&order, 1099};

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, FlowTestMul2Adapter()).input(&input).onFinally(&FlowTestMarker::onStepFinally, &step1Final)
        | loka::dsl::Step(2, FlowTestAdd1Adapter()).onFinally(&FlowTestMarker::onStepFinally, &step2Final);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    LOKA_VERIFY(chain.run());
    assert(order.size() == 3);
    assert(order[0] == 1001);
    assert(order[1] == 1002);
    assert(order[2] == 1099);
  }

  {
    int input = 5;
    std::vector<int> order;
    FlowTestMarkerContext step1Final = {&order, 1101};
    FlowTestMarkerContext step2Final = {&order, 1102};
    FlowTestMarkerContext step3Final = {&order, 1103};
    FlowTestMarkerContext flowFinal = {&order, 1199};

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, FlowTestMul2Adapter()).input(&input).onFinally(&FlowTestMarker::onStepFinally, &step1Final)
        | loka::dsl::Step(2, FlowTestFail500Adapter()).onFinally(&FlowTestMarker::onStepFinally, &step2Final)
        | loka::dsl::Step(3, FlowTestAdd1Adapter()).onFinally(&FlowTestMarker::onStepFinally, &step3Final);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    LOKA_VERIFY(!chain.run());
    assert(order.size() == 3);
    assert(order[0] == 1101);
    assert(order[1] == 1102);
    assert(order[2] == 1199);
  }

  {
    int input = 6;
    std::vector<int> order;
    FlowTestMarkerContext stepFirstMatch = {&order, 1201};
    FlowTestMarkerContext stepDefault = {&order, 1202};
    FlowTestMarkerContext flowFinal = {&order, 1299};

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, FlowTestFail500Adapter())
              .input(&input)
              .onFailure(&FlowTestMarker::is500, &FlowTestMarker::onStepFailureUnhandled, &stepFirstMatch)
              .onFailure(&FlowTestMarker::onStepFailureHandled, &stepDefault);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    LOKA_VERIFY(!chain.run());
    assert(order.size() == 2);
    assert(order[0] == 1201);
    assert(order[1] == 1299);
  }

  {
    int input = 8;
    std::vector<int> order;
    FlowTestMarkerContext flowFirstMatch = {&order, 1301};
    FlowTestMarkerContext flowDefault = {&order, 1302};
    FlowTestMarkerContext flowFinal = {&order, 1399};

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestFail500Adapter()).input(&input);
    chain.onFailure(&FlowTestMarker::is500, &FlowTestMarker::onStepFailureUnhandled, &flowFirstMatch);
    chain.onFailure(&FlowTestMarker::onFlowFailureHandled, &flowDefault);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    LOKA_VERIFY(!chain.run());
    assert(order.size() == 2);
    assert(order[0] == 1301);
    assert(order[1] == 1399);
  }

  {
    int input = 9;
    std::vector<int> order;
    FlowTestMarkerContext flowNoMatch = {&order, 1401};
    FlowTestMarkerContext flowDefault = {&order, 1402};
    FlowTestMarkerContext flowFinal = {&order, 1499};

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestFail500Adapter()).input(&input);
    chain.onFailure(&FlowTestMarker::onFlowFailureHandled, &flowDefault);
    chain.onFailure(&FlowTestMarker::is500, &FlowTestMarker::onStepFailureUnhandled, &flowNoMatch);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    LOKA_VERIFY(chain.run());
    assert(order.size() == 2);
    assert(order[0] == 1402);
    assert(order[1] == 1499);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using namespace loka::dsl;
    using namespace loka::dsl::testing;

    FlowScenePlatformController platform;
    loka::core::MutableState<bool> visible(true);
    loka::core::MutableState<bool> replacementVisible(false);
    NodeComposition composition;
    BoxDefinition root = Box();
    root << (Show(visible) << Button("Capture").testId("Target"));
    root << (Show(replacementVisible) << Text("Replacement").testId("Target"));
    composition.declare(root);
    NodeDefinitionBase *definition = composition.root()->clone();
    LOKA_VERIFY(definition != 0);
    Scene scene(definition);
    // Bitmaps outlive both context generations, so the red pin compares identity
    // safely even after the first context has been detached and reclaimed.
    CurrentTargetBitmap originalBitmap(scene);
    CurrentTargetBitmap replacementBitmap(scene);
    scene.mount(&platform);
    SceneTestAccess::updateAttached(scene, true);
    Scene *input = &scene;
    Scene *output = 0;
    Node *original = 0;
    FlowError error;
    LOKA_VERIFY(LookupNodeById<Node>(&scene, "Target", original, error) == FLOW_STEP_SUCCEEDED);
    original->setContext(new BorrowedCaptureContext(originalBitmap));

    ScenarioClock clock;
    ViewBitmapCapture bitmap;
    FlowChain<Scene *, ViewBitmapCapture> chain = Flow()
        | Step(1, AtTick(clock, 2, CaptureViewTarget("Target"))).input(&input)
        | Step(2, AtTick(clock, 32, CaptureViewBitmapFromPlatform())).onSuccess(&bitmap);
    ViewBitmapCapture callbackBitmap;
    FlowChain<Scene *, ViewBitmapCapture> callbackChain = Flow()
        | Step(1, AtTick(clock, 2, CaptureViewTarget("Target"))).input(&input)
        | Step(2, AtTick(clock, 32, CaptureViewBitmap(&CurrentTargetBitmap::capture, &scene)))
              .onSuccess(&callbackBitmap);
    SnapRecord metadata;
    FlowChain<Scene *, SnapRecord> metadataChain = Flow()
        | Step(1, AtTick(clock, 2, CaptureViewTarget("Target"))).input(&input)
        | Step(2, AtTick(clock, 32, CaptureView("Replacement", "capture", 32, 1))).onSuccess(&metadata);
    clock.advanceTo(2);
    LOKA_VERIFY(chain.runResult() == FLOW_RUN_PENDING);
    LOKA_VERIFY(callbackChain.runResult() == FLOW_RUN_PENDING);
    LOKA_VERIFY(metadataChain.runResult() == FLOW_RUN_PENDING);
    LOKA_VERIFY(!bitmap.captured);
    clock.advanceTo(16);
    LOKA_VERIFY(SetBoolStateAndFlush(&visible, false).run(input, output, error) == FLOW_STEP_SUCCEEDED);
    Node *missing = 0;
    LOKA_VERIFY(LookupNodeById<Node>(&scene, "Target", missing, error) == FLOW_STEP_FAILED);
    LOKA_VERIFY(SetBoolStateAndFlush(&replacementVisible, true).run(input, output, error) == FLOW_STEP_SUCCEEDED);
    Node *replacement = 0;
    LOKA_VERIFY(LookupNodeById<Node>(&scene, "Target", replacement, error) == FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(replacement != original);
    replacement->setContext(new BorrowedCaptureContext(replacementBitmap));
    clock.advanceTo(32);
    LOKA_VERIFY(chain.runResult() == FLOW_RUN_SUCCEEDED);
    LOKA_VERIFY(bitmap.captured && bitmap.width == 80 && bitmap.height == 40);
    LOKA_VERIFY(callbackChain.runResult() == FLOW_RUN_SUCCEEDED);
    LOKA_VERIFY(callbackBitmap.captured);
    LOKA_VERIFY(metadataChain.runResult() == FLOW_RUN_SUCCEEDED);
    long kind = 0;
    const bool hasKind = metadata.getInt("view.node.kind", kind);
    LOKA_VERIFY(hasKind);
    LOKA_VERIFY(kind == static_cast<long>(replacement->kind()));

    // Cache the key while the target exists; all consumers must reject removal.
    ViewCaptureKey key;
    LOKA_VERIFY(CaptureViewTarget("Target").run(input, key, error) == FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(SetBoolStateAndFlush(&replacementVisible, false).run(input, output, error) == FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(CaptureViewBitmapFromPlatform().run(key, bitmap, error) == FLOW_STEP_FAILED);
    LOKA_VERIFY(error.code == FLOW_ERROR_SCENE_TEST_NODE_NOT_FOUND);
    LOKA_VERIFY(CaptureViewBitmap(&CurrentTargetBitmap::capture, &scene).run(key, callbackBitmap, error)
                == FLOW_STEP_FAILED);
    LOKA_VERIFY(error.code == FLOW_ERROR_SCENE_TEST_NODE_NOT_FOUND);
    LOKA_VERIFY(CaptureView("Replacement", "absent", 33, 1).run(key, metadata, error) == FLOW_STEP_FAILED);
    LOKA_VERIFY(error.code == FLOW_ERROR_SCENE_TEST_NODE_NOT_FOUND);
    SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << Text("Hello Flow").testId("MainText");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;
    loka::dsl::SnapRecord capture;

    loka::dsl::FlowChain<Scene *, loka::dsl::SnapRecord> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, loka::dsl::testing::FindNodeById<TextNode>("MainText")).input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::CaptureNode<TextNode>("SceneFlow", "capture-text", 1, 1))
              .onSuccess(&capture);

    LOKA_VERIFY(chain.run());
    std::string capturedText;
    LOKA_VERIFY(capture.get("node", capturedText));
    assert(capturedText == "MainText");
    LOKA_VERIFY(capture.get("text.value", capturedText));
    assert(capturedText == "Hello Flow");

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app::scene;

    loka::core::MutableState<int> countState(2);
    loka::core::PushStateTracker tracker;
    DummyStateOwner owner;
    loka::core::MutableState<loka::core::String> labelStorage;
    NodeState<loka::core::String> label(&labelStorage, &tracker, &owner);
    loka::dsl::StateStream<int> countStream(&countState, &tracker, &owner);
    loka::app::scene::FlowSlot<loka::dsl::StateStream<loka::core::String> > labelFlow(loka::dsl::testing::FlowSlotTestAccess::unowned());

    tracker.begin();
    labelFlow.set(countStream.map(loka::dsl::Const("Count: ") + countStream.slot.value())).bindTo(label, true);
    tracker.end();

    assert(label.isValid());
    assert(label.get().equals(loka::core::String::Literal("Count: 2")));

    tracker.begin();
    countState.set(5, true);
    tracker.end();

    assert(label.get().equals(loka::core::String::Literal("Count: 5")));
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    loka::core::MutableState<bool> isVisible(false);

    NodeComposition explicitComposition;
    explicitComposition.declare(Box() << (Show(isVisible) << Text("Visible child")));

    NodeComposition shorthandComposition;
    shorthandComposition.declare(Box() << (isVisible << Text("Visible child")));

    assertEquivalentDefinitionStructure(explicitComposition.root(), shorthandComposition.root());
  }

  {
    using namespace loka::app;

    loka::core::MutableState<bool> isVisible(false);
    ShowDefinition explicitShow = Show(isVisible) << Text("First") << Text("Second");
    ShowDefinition shorthandShow = isVisible << Text("First") << Text("Second");

  LOKA_VERIFY(shorthandShow.childrenCount() == 2);
    assertEquivalentDefinitionStructure(&explicitShow, &shorthandShow);
  }

  {
    // A const State<bool>& reaches both doors: Show() has a const factory, so
    // the shorthand carries the const overload too and the documented
    // "expands to exactly Show(condition) << child" holds for read-only
    // conditions as well.
    using namespace loka::app;

    loka::core::MutableState<bool> owned(false);
    const loka::core::State<bool> &isVisible = owned;
    ShowDefinition explicitShow = Show(isVisible) << Text("First");
    ShowDefinition shorthandShow = isVisible << Text("First");

  LOKA_VERIFY(shorthandShow.childrenCount() == 1);
    assertEquivalentDefinitionStructure(&explicitShow, &shorthandShow);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    loka::core::MutableState<bool> showState(false);

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    TextDefinition trueText = Text("On").testId("ShowOnText");
    ShowDefinition showDef = Show(showState);
    showDef << trueText;
    LOKA_VERIFY(showDef.childrenCount() == 1);
    root << showDef;
    LOKA_VERIFY(root.childrenCount() == 1);
    LOKA_VERIFY(root.childrenHead() != 0);
    LOKA_VERIFY(root.childrenHead()->asNestableDefinition() == 0);

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;

    loka::dsl::FlowChain<Scene *, Scene *> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::SetBoolStateAndFlush(&showState, true)).input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::CheckText("ShowOnText", "On"));

    LOKA_VERIFY(chain.run());
    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_CHILD) != 0);
    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_LAYOUT) != 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    g_conditionalProjectedProbeLayoutCalls = 0;
    g_conditionalProjectedProbePrepareCalls = 0;

    loka::core::MutableState<bool> showState(false);

    NodeComposition composition;
    BoxDefinition root = Box().testId("ConditionalProjectedRoot");
    root << (Show(showState) << ConditionalProjectedProbeDefinition());

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    ConditionalProjectedProbeController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_conditionalProjectedProbeLayoutCalls == 0);
    assert(g_conditionalProjectedProbePrepareCalls == 0);

    loka::app::scene::BoundaryNode *rootBoundary = loka::dsl::testing::SceneTestAccess::rootBoundary(scene);
    assert(rootBoundary);
    {
      loka::core::StateTrackerGuard guard(rootBoundary->tracker());
      showState.set(true);
    }
    scene.invalidate(loka::app::scene::NODE_DIRTY_CHILD);

    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_CHILD) != 0);
    assert(g_conditionalProjectedProbeLayoutCalls > 0);
    assert(g_conditionalProjectedProbePrepareCalls > 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    loka::core::MutableState<bool> showState(false);

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << (Show(showState) << Text("First").testId("ShowFirstText") << Text("Second").testId("ShowSecondText"));

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;

    loka::dsl::FlowChain<Scene *, Scene *> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::SetBoolStateAndFlush(&showState, true)).input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::CheckText("ShowFirstText", "First"))
        | loka::dsl::Step(3, loka::dsl::testing::CheckText("ShowSecondText", "Second"));

    LOKA_VERIFY(chain.run());
    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_CHILD) != 0);
    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_LAYOUT) != 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << Text("Hello SnapText").testId("MainText");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;
    loka::dsl::SnapRecord captured;

    loka::dsl::FlowChain<Scene *, loka::dsl::SnapRecord> okChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::SnapText("MainText", "SceneFlow", "snap-text", 10, 1))
              .input(&scenePtr)
              .onSuccess(&captured);

    LOKA_VERIFY(okChain.run());
    std::string value;
    assert(captured.get("text.value", value));
    assert(value == "Hello SnapText");
    // Scenario snapshots describe logical values, independent of layout visits.
    LOKA_VERIFY(!captured.get("dirty.mask", value));
    LOKA_VERIFY(!captured.get("dirty.flags", value));

    FlowErrorCapture failCapture = {0, 0, 0};
    loka::dsl::FlowChain<Scene *, loka::dsl::SnapRecord> failChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::SnapText("MissingText", "SceneFlow", "snap-text-missing", 11, 1))
              .input(&scenePtr)
              .onFailure(&FlowTestMarker::captureFailure, &failCapture);

    LOKA_VERIFY(failChain.run());
    assert(failCapture.calls == 1);
    assert(failCapture.kind == loka::dsl::testing::FLOW_ERROR_KIND_SCENE_SCENARIO);
    assert(failCapture.code == loka::dsl::testing::FLOW_ERROR_SCENE_TEST_NODE_NOT_FOUND);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using namespace loka::dsl::testing;

    NodeComposition composition;
    BoxDefinition root = Box().testId("SelectorRegion");
    root << Text("First") << (Box() << Text("Nested second"));

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    const NodeSelector<TextNode> secondText = Within("SelectorRegion").descendant<TextNode>(2);
    TextNode *text = 0;
    loka::dsl::FlowError error;
    LOKA_VERIFY(ResolveSelector<TextNode>(&scene, secondText, text, error) == loka::dsl::FLOW_STEP_SUCCEEDED);
    std::string textValue;
    LOKA_VERIFY(text && text->props.text_ && loka::platform::CollectUtf8(text->props.text_->get(), textValue));
    LOKA_VERIFY(textValue == "Nested second");

    Node *firstDescendant = 0;
    LOKA_VERIFY(ResolveSelector<Node>(
                    &scene, Within("SelectorRegion").descendant<Node>(1), firstDescendant, error)
                == loka::dsl::FLOW_STEP_SUCCEEDED);
    Node *anchor = 0;
    LOKA_VERIFY(LookupNodeById<Node>(&scene, "SelectorRegion", anchor, error)
                == loka::dsl::FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(firstDescendant != anchor);

    loka::dsl::SnapRecord captured;
    LOKA_VERIFY(SnapText(secondText, "SceneFlow", "selector-snap", 12, 1).run(&scene, captured, error)
                == loka::dsl::FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(captured.serialize(false).find("node\tSelectorRegion/Text[2]\n") != std::string::npos);

    TextNode *missing = 0;
    LOKA_VERIFY(ResolveSelector<TextNode>(
                    &scene, Within("MissingRegion").descendant<TextNode>(1), missing, error)
                == loka::dsl::FLOW_STEP_FAILED);
    LOKA_VERIFY(error.code == FLOW_ERROR_SCENE_TEST_NODE_NOT_FOUND);

    LOKA_VERIFY(ResolveSelector<TextNode>(
                    &scene, Within("SelectorRegion").descendant<TextNode>(0), missing, error)
                == loka::dsl::FLOW_STEP_FAILED);
    LOKA_VERIFY(error.code == FLOW_ERROR_SCENE_TEST_INVALID_ORDINAL);

    LOKA_VERIFY(ResolveSelector<TextNode>(
                    &scene, Within("SelectorRegion").descendant<TextNode>(3), missing, error)
                == loka::dsl::FLOW_STEP_FAILED);
    LOKA_VERIFY(error.code == FLOW_ERROR_SCENE_TEST_ORDINAL_OUT_OF_RANGE);

    EditTextNode *noEditText = 0;
    LOKA_VERIFY(ResolveSelector<EditTextNode>(
                    &scene, Within("SelectorRegion").descendant<EditTextNode>(1), noEditText, error)
                == loka::dsl::FLOW_STEP_FAILED);
    LOKA_VERIFY(error.code == FLOW_ERROR_SCENE_TEST_ORDINAL_OUT_OF_RANGE);

    LOKA_VERIFY(std::strcmp(SceneNodeCast<Node>::name(), "Node") == 0);
    LOKA_VERIFY(std::strcmp(SceneNodeCast<TextNode>::name(), "Text") == 0);
    LOKA_VERIFY(std::strcmp(SceneNodeCast<ButtonNode>::name(), "Button") == 0);
    LOKA_VERIFY(std::strcmp(SceneNodeCast<CellNode>::name(), "Cell") == 0);
    LOKA_VERIFY(std::strcmp(SceneNodeCast<EditTextNode>::name(), "EditText") == 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    // An omitted anchor must be refused, not silently matched against the one
    // id-less node in the scene (which would turn the selector into a
    // scene-wide ordinal lookup).
    using namespace loka::app;
    using namespace loka::app::scene;
    using namespace loka::dsl::testing;

    NodeComposition composition;
    BoxDefinition root = Box();
    root << Text("Only").testId("EmptyAnchorLeaf");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    TextNode *text = 0;
    loka::dsl::FlowError error;
    LOKA_VERIFY(ResolveSelector<TextNode>(&scene, Within("").descendant<TextNode>(1), text, error)
                == loka::dsl::FLOW_STEP_FAILED);
    LOKA_VERIFY(error.code == FLOW_ERROR_SCENE_TEST_MISSING_TEST_ID);
    LOKA_VERIFY(text == 0);
    LOKA_VERIFY(ResolveSelector<TextNode>(&scene, Within(0).descendant<TextNode>(1), text, error)
                == loka::dsl::FLOW_STEP_FAILED);
    LOKA_VERIFY(error.code == FLOW_ERROR_SCENE_TEST_MISSING_TEST_ID);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using namespace loka::dsl::testing;

    NodeComposition composition;
    BoxDefinition root = Box().testId("DuplicateRoot");
    root << (Box().testId("DuplicatedRegion") << Text("A"));
    root << (Box().testId("DuplicatedRegion") << Text("B"));

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    TextNode *text = 0;
    loka::dsl::FlowError error;
    LOKA_VERIFY(ResolveSelector<TextNode>(
                    &scene, Within("DuplicatedRegion").descendant<TextNode>(1), text, error)
                == loka::dsl::FLOW_STEP_FAILED);
    LOKA_VERIFY(error.code == FLOW_ERROR_SCENE_TEST_DUPLICATE_TEST_ID);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using namespace loka::dsl::testing;

    loka::core::MutableState<bool> visible(false);
    loka::core::MutableState<bool> replacementVisible(false);
    NodeComposition composition;
    BoxDefinition root = Box().testId("ConditionalRegion");
    root << (Show(visible) << Text("First materialized"));
    root << (Show(replacementVisible) << Text("Replacement materialized"));

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    const NodeSelector<TextNode> selector = Within("ConditionalRegion").descendant<TextNode>(1);
    TextNode *first = 0;
    loka::dsl::FlowError error;
    LOKA_VERIFY(ResolveSelector<TextNode>(&scene, selector, first, error) == loka::dsl::FLOW_STEP_FAILED);
    LOKA_VERIFY(error.code == FLOW_ERROR_SCENE_TEST_ORDINAL_OUT_OF_RANGE);

    Scene *sceneInput = &scene;
    Scene *sceneOutput = 0;
    LOKA_VERIFY(SetBoolStateAndFlush(&visible, true).run(sceneInput, sceneOutput, error)
                == loka::dsl::FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(ResolveSelector<TextNode>(&scene, selector, first, error) == loka::dsl::FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(first != 0);
    std::string firstText;
    LOKA_VERIFY(first->props.text_ && loka::platform::CollectUtf8(first->props.text_->get(), firstText));
    LOKA_VERIFY(firstText == "First materialized");

    LOKA_VERIFY(SetBoolStateAndFlush(&visible, false).run(sceneInput, sceneOutput, error)
                == loka::dsl::FLOW_STEP_SUCCEEDED);
    TextNode *parked = 0;
    LOKA_VERIFY(ResolveSelector<TextNode>(&scene, selector, parked, error) == loka::dsl::FLOW_STEP_FAILED);
    LOKA_VERIFY(error.code == FLOW_ERROR_SCENE_TEST_ORDINAL_OUT_OF_RANGE);

    LOKA_VERIFY(SetBoolStateAndFlush(&replacementVisible, true).run(sceneInput, sceneOutput, error)
                == loka::dsl::FLOW_STEP_SUCCEEDED);
    TextNode *replacement = 0;
    LOKA_VERIFY(ResolveSelector<TextNode>(&scene, selector, replacement, error) == loka::dsl::FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(replacement != 0);
    std::string replacementText;
    LOKA_VERIFY(replacement->props.text_
                && loka::platform::CollectUtf8(replacement->props.text_->get(), replacementText));
    LOKA_VERIFY(replacementText == "Replacement materialized");

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using namespace loka::dsl::testing;

    loka::core::MutableState<loka::core::String> input(loka::core::String::Literal("before"));
    loka::core::EmitterState buttonClick;
    loka::core::EmitterState cellClick;
    int buttonClicks = 0;
    int cellClicks = 0;
    buttonClick.bind(&incrementNotificationCount, &buttonClicks, false);
    cellClick.bind(&incrementNotificationCount, &cellClicks, false);

    NodeComposition composition;
    BoxDefinition root = Box().testId("ActionRegion");
    root << EditText(loka::app::scene::WriteSeat<loka::core::String>(&input)) << Button("Press").onClick(&buttonClick) << Cell("Open").onClick(&cellClick);

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *sceneInput = &scene;
    Scene *sceneOutput = 0;
    loka::dsl::FlowError error;
    LOKA_VERIFY(EnterText(Within("ActionRegion").descendant<EditTextNode>(1), "after")
                    .run(sceneInput, sceneOutput, error)
                == loka::dsl::FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(input.get().equals(loka::core::String::Literal("after")));
    LOKA_VERIFY(ClickButton(Within("ActionRegion").descendant<ButtonNode>(1))
                    .run(sceneInput, sceneOutput, error)
                == loka::dsl::FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(ClickCell(Within("ActionRegion").descendant<CellNode>(1)).run(sceneInput, sceneOutput, error)
                == loka::dsl::FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(buttonClicks == 1);
    LOKA_VERIFY(cellClicks == 1);

    buttonClick.unbind(&incrementNotificationCount, &buttonClicks);
    cellClick.unbind(&incrementNotificationCount, &cellClicks);
    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << Button("Press").testId("ActionButton");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    loka::app::scene::Node *actionButton = 0;
    loka::dsl::FlowError lookupError;
    LOKA_VERIFY(loka::dsl::testing::LookupNodeById<loka::app::scene::Node>(
               &scene, std::string("ActionButton"), actionButton, lookupError)
           == loka::dsl::FLOW_STEP_SUCCEEDED);
    assert(actionButton);
    actionButton->setContext(new FlowTestCapturableNodeContext());

    Scene *scenePtr = &scene;
    loka::dsl::testing::ViewBitmapCapture bitmap;

    loka::dsl::FlowChain<Scene *, loka::dsl::testing::ViewBitmapCapture> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, loka::dsl::testing::CaptureViewTarget("ActionButton")).input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::CaptureViewBitmap(&FlowTestViewBitmapCaptureHelpers::capture, 0))
              .onSuccess(&bitmap);

    LOKA_VERIFY(chain.run());
    assert(bitmap.captured);
    assert(bitmap.image.isValid());
    assert(bitmap.width == 64);
    assert(bitmap.height == 32);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << Button("Press").testId("ActionButton");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    loka::app::scene::Node *actionButton = 0;
    loka::dsl::FlowError lookupError;
    LOKA_VERIFY(loka::dsl::testing::LookupNodeById<loka::app::scene::Node>(
               &scene, std::string("ActionButton"), actionButton, lookupError)
           == loka::dsl::FLOW_STEP_SUCCEEDED);
    assert(actionButton);
    actionButton->setContext(new FlowTestCapturableNodeContext());

    Scene *scenePtr = &scene;
    loka::dsl::testing::ViewBitmapCapture bitmap;

    loka::dsl::FlowChain<Scene *, loka::dsl::testing::ViewBitmapCapture> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, loka::dsl::testing::CaptureViewTarget("ActionButton")).input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::CaptureViewBitmapFromPlatform()).onSuccess(&bitmap);

    LOKA_VERIFY(chain.run());
    assert(bitmap.captured);
    assert(bitmap.image.isValid());
    assert(bitmap.width == 80);
    assert(bitmap.height == 40);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << Button("Press").testId("ActionButton");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;
    loka::dsl::SnapRecord captured;

    loka::dsl::FlowChain<Scene *, loka::dsl::SnapRecord> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, loka::dsl::testing::CaptureViewTarget("ActionButton")).input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::CaptureViewBitmap(&FlowTestViewBitmapCaptureHelpers::capture, 0))
        | loka::dsl::Step(3, loka::dsl::testing::CaptureViewBitmapSnap("SceneFlow", "capture-view-bitmap", 50, 1))
              .onSuccess(&captured)
        | loka::dsl::Step(4, loka::dsl::testing::AssertSnapIntEquals("view.bitmap.captured", 1))
        | loka::dsl::Step(5, loka::dsl::testing::AssertSnapIntEquals("view.bitmap.image.valid", 1))
        | loka::dsl::Step(6, loka::dsl::testing::AssertSnapIntEquals("view.bitmap.width", 64))
        | loka::dsl::Step(7, loka::dsl::testing::AssertSnapIntEquals("view.bitmap.height", 32));

    LOKA_VERIFY(chain.run());

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << Button("Press").testId("ActionButton");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;
    loka::dsl::SnapRecord captured;

    loka::dsl::FlowChain<Scene *, loka::dsl::SnapRecord> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, loka::dsl::testing::CaptureViewTarget("ActionButton")).input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::CaptureView("SceneFlow", "capture-view", 48, 1)).onSuccess(&captured)
        | loka::dsl::Step(3, loka::dsl::testing::AssertSnapIntEquals("view.target.present", 1))
        | loka::dsl::Step(4, loka::dsl::testing::AssertSnapStringEquals("view.node.test_id", "ActionButton"));

    LOKA_VERIFY(chain.run());
    long contextPresent = 1;
    (void)contextPresent;
    assert(captured.getInt("view.context.present", contextPresent));
    assert(contextPresent == 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << Text("Ready").testId("StatusText");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;
    loka::dsl::SnapRecord captured;

    loka::dsl::FlowChain<Scene *, loka::dsl::SnapRecord> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::CaptureScene("SceneFlow", "capture-scene", 40, 1))
              .input(&scenePtr)
              .onSuccess(&captured)
        | loka::dsl::Step(2, loka::dsl::testing::AssertSnapIntEquals("scene.root.present", 1))
        | loka::dsl::Step(3, loka::dsl::testing::AssertSnapIntEquals("scene.root_boundary.present", 1))
        | loka::dsl::Step(4, loka::dsl::testing::AssertSnapIntGreaterEqual("scene.root.kind", 0));

    LOKA_VERIFY(chain.run());

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    loka::core::MutableState<loka::core::String> textState(loka::core::String::Literal("Before"));

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << Text(&textState).testId("MainText");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;

    loka::dsl::FlowChain<Scene *, Scene *> okChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::CheckText("MainText", "Before"))
              .input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::SetStringStateAndFlush(&textState, "After"))
        | loka::dsl::Step(3, loka::dsl::testing::CheckText("MainText", "After"));

    LOKA_VERIFY(okChain.run());
    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_PROPS) != 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    loka::core::MutableState<loka::core::String> textState(loka::core::String::Literal("Before"));

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << (Text(&textState) + BlockStyle().wrap(TEXT_WRAP_WORD)).testId("WrapText");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;

    loka::dsl::FlowChain<Scene *, Scene *> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::SetStringStateAndFlush(&textState, "A longer wrapped line"))
              .input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::CheckText("WrapText", "A longer wrapped line"));

    LOKA_VERIFY(chain.run());
    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_PROPS) != 0);
    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_LAYOUT) != 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    loka::core::MutableState<loka::core::String> textState(loka::core::String::Literal("Before"));
    loka::core::MutableState<bool> enabledState(false);

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << (Text(&textState) + BlockStyle().wrap(TEXT_WRAP_WORD)).testId("WrapText");
    root << Button("Run").enabled(&enabledState).testId("MainButton");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;

    loka::dsl::FlowChain<Scene *, Scene *> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::SetBoolStateAndFlush(&enabledState, true)).input(&scenePtr);

    LOKA_VERIFY(chain.run());
    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_PROPS) != 0);
    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_LAYOUT) == 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    loka::core::MutableState<bool> showState(false);
    loka::core::MutableState<bool> enabledState(false);

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    TextDefinition falseText = Text("Off").testId("OffText");
    TextDefinition trueText = Text("On").testId("OnText");
    root << composition.conditional(showState, trueText, falseText);
    root << Button("Run").enabled(&enabledState).testId("MainButton");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;

    loka::dsl::FlowChain<Scene *, Scene *> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::SetBoolStateAndFlush(&enabledState, true)).input(&scenePtr);

    LOKA_VERIFY(chain.run());
    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_PROPS) != 0);
    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_CHILD) == 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    g_sameBoundaryConditionalProbe = 0;

    NodeDefinitionBase *rootDefinition = BoundaryDefinition<SameBoundaryConditionalProbeProps, SameBoundaryConditionalProbeNode>().clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;

    loka::dsl::FlowChain<Scene *, Scene *> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::CheckText("SameBoundaryOffText", "Off")).input(&scenePtr);

    LOKA_VERIFY(chain.run());
    assert(g_sameBoundaryConditionalProbe != 0);

    g_sameBoundaryConditionalProbe->emitToggle();

    loka::dsl::FlowChain<Scene *, Scene *> postToggleChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::CheckText("SameBoundaryOnText", "On")).input(&scenePtr);

    LOKA_VERIFY(postToggleChain.run());
    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_CHILD) != 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
    g_sameBoundaryConditionalProbe = 0;
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using namespace loka::dsl::testing;

    g_headlessScopeProbe = 0;
    g_headlessScopeHost = 0;
    g_headlessScopeAttachCount = 0;
    g_headlessScopeDetachCount = 0;

    NodeDefinitionBase *rootDefinition = BoundaryDefinition<HeadlessScopeHostProps, HeadlessScopeHostBoundaryNode>().clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;

    loka::dsl::FlowChain<Scene *, Scene *> initialChain =
        loka::dsl::Flow() | loka::dsl::Step(1, CheckText("HeadlessSummaryText", "Headless 0")).input(&scenePtr);

    LOKA_VERIFY(initialChain.run());
    assert(g_headlessScopeProbe != 0);
    assert(g_headlessScopeHost != 0);
    assert(g_headlessScopeAttachCount == 1);
    assert(g_headlessScopeDetachCount == 0);
    HeadlessScopeProbeBoundaryNode *firstProbe = g_headlessScopeProbe;

    g_headlessScopeProbe->increment();

    loka::dsl::FlowChain<Scene *, Scene *> incrementedChain =
        loka::dsl::Flow() | loka::dsl::Step(1, CheckText("HeadlessSummaryText", "Headless 1")).input(&scenePtr);

    LOKA_VERIFY(incrementedChain.run());

    g_headlessScopeHost->setShown(false);
    (void)SceneTestAccess::flushInvalidation(scene);

    loka::dsl::FlowError hiddenLookupError;
    hiddenLookupError.kind = 0;
    hiddenLookupError.code = 0;
    loka::app::TextNode *hiddenNode = 0;
    LOKA_VERIFY(LookupNodeById<loka::app::TextNode>(&scene, "HeadlessSummaryText", hiddenNode, hiddenLookupError)
           == loka::dsl::FLOW_STEP_FAILED);
    assert(hiddenLookupError.kind == FLOW_ERROR_KIND_SCENE_SCENARIO);
    assert(hiddenLookupError.code == FLOW_ERROR_SCENE_TEST_NODE_NOT_FOUND);
    assert(g_headlessScopeAttachCount == 1);
    assert(g_headlessScopeDetachCount == 0);

    (void)firstProbe;
    assert(g_headlessScopeProbe == firstProbe);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
    g_headlessScopeProbe = 0;
    g_headlessScopeHost = 0;
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using namespace loka::dsl::testing;

    g_headlessOwnedProbe = 0;
    g_headlessOwnedHost = 0;
    g_headlessOwnedAttachCount = 0;
    g_headlessOwnedDestroyCount = 0;

    NodeDefinitionBase *rootDefinition = BoundaryDefinition<HeadlessOwnedHostProps, HeadlessOwnedHostBoundaryNode>().clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_headlessOwnedProbe != 0);
    assert(g_headlessOwnedHost != 0);
    assert(g_headlessOwnedAttachCount == 1);
    assert(g_headlessOwnedDestroyCount == 0);
    assert(g_headlessOwnedProbe->summaryText().equals(loka::core::String::Literal("Owned 0")));

    g_headlessOwnedProbe->increment();
    assert(g_headlessOwnedProbe->summaryText().equals(loka::core::String::Literal("Owned 1")));

    HeadlessOwnedProbeNode *firstOwnedProbe = g_headlessOwnedProbe;
    g_headlessOwnedHost->setShown(false);
    (void)SceneTestAccess::flushInvalidation(scene);
    assert(g_headlessOwnedDestroyCount == 0);
    (void)firstOwnedProbe;
    assert(g_headlessOwnedProbe == firstOwnedProbe);
    assert(firstOwnedProbe != 0);

    g_headlessOwnedHost->setShown(true);
    (void)SceneTestAccess::flushInvalidation(scene);
    assert(g_headlessOwnedProbe != 0);
    assert(g_headlessOwnedProbe == firstOwnedProbe);
    assert(g_headlessOwnedAttachCount == 1);
    assert(g_headlessOwnedDestroyCount == 0);
    assert(g_headlessOwnedProbe->summaryText().equals(loka::core::String::Literal("Owned 1")));

    loka::dsl::testing::SceneTestAccess::unmount(scene);
    g_headlessOwnedProbe = 0;
    g_headlessOwnedHost = 0;
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using loka::dsl::testing::SceneTestAccess;

    g_headlessOwnedProbe = 0;
    g_headlessOwnedHost = 0;
    g_headlessOwnedAttachCount = 0;
    g_headlessOwnedDestroyCount = 0;

    NodeDefinitionBase *rootDefinition = BoundaryDefinition<HeadlessOwnedHostProps, HeadlessOwnedHostBoundaryNode>().clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    HeadlessOwnedProbeNode *firstOwnedProbe = g_headlessOwnedProbe;
    (void)firstOwnedProbe;
    assert(firstOwnedProbe != 0);
    assert(g_headlessOwnedAttachCount == 1);
    assert(g_headlessOwnedDestroyCount == 0);

    (void)SceneTestAccess::flushInvalidation(scene);

    assert(g_headlessOwnedProbe == firstOwnedProbe);
    assert(g_headlessOwnedAttachCount == 1);
    assert(g_headlessOwnedDestroyCount == 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
    g_headlessOwnedProbe = 0;
    g_headlessOwnedHost = 0;
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using loka::dsl::testing::SceneTestAccess;

    g_headlessOwnedMultiProbeA = 0;
    g_headlessOwnedMultiProbeB = 0;
    g_headlessOwnedMultiHost = 0;
    g_headlessOwnedMultiAttachCountA = 0;
    g_headlessOwnedMultiAttachCountB = 0;
    g_headlessOwnedMultiDestroyCountA = 0;
    g_headlessOwnedMultiDestroyCountB = 0;

    NodeDefinitionBase *rootDefinition = BoundaryDefinition<HeadlessOwnedMultiHostProps, HeadlessOwnedMultiHostBoundaryNode>().clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_headlessOwnedMultiProbeA != 0);
    assert(g_headlessOwnedMultiProbeB != 0);
    assert(g_headlessOwnedMultiHost != 0);
    assert(g_headlessOwnedMultiAttachCountA == 1);
    assert(g_headlessOwnedMultiAttachCountB == 1);
    assert(g_headlessOwnedMultiDestroyCountA == 0);
    assert(g_headlessOwnedMultiDestroyCountB == 0);
    assert(g_headlessOwnedMultiProbeA->summaryText().equals(loka::core::String::Literal("OwnedA 0")));
    assert(g_headlessOwnedMultiProbeB->summaryText().equals(loka::core::String::Literal("OwnedB 0")));

    HeadlessOwnedMultiProbeANode *firstProbeA = g_headlessOwnedMultiProbeA;
    HeadlessOwnedMultiProbeBNode *firstProbeB = g_headlessOwnedMultiProbeB;

    g_headlessOwnedMultiHost->setShown(false);
    (void)SceneTestAccess::flushInvalidation(scene);
    (void)firstProbeA;
    assert(g_headlessOwnedMultiProbeA == firstProbeA);
    (void)firstProbeB;
    assert(g_headlessOwnedMultiProbeB == firstProbeB);
    assert(g_headlessOwnedMultiDestroyCountA == 0);
    assert(g_headlessOwnedMultiDestroyCountB == 0);
    assert(firstProbeA != 0);
    assert(firstProbeB != 0);

    g_headlessOwnedMultiHost->setShown(true);
    (void)SceneTestAccess::flushInvalidation(scene);
    assert(g_headlessOwnedMultiProbeA != 0);
    assert(g_headlessOwnedMultiProbeB != 0);
    assert(g_headlessOwnedMultiProbeA == firstProbeA);
    assert(g_headlessOwnedMultiProbeB == firstProbeB);
    assert(g_headlessOwnedMultiAttachCountA == 1);
    assert(g_headlessOwnedMultiAttachCountB == 1);
    assert(g_headlessOwnedMultiDestroyCountA == 0);
    assert(g_headlessOwnedMultiDestroyCountB == 0);
    assert(g_headlessOwnedMultiProbeA->summaryText().equals(loka::core::String::Literal("OwnedA 0")));
    assert(g_headlessOwnedMultiProbeB->summaryText().equals(loka::core::String::Literal("OwnedB 0")));

    loka::dsl::testing::SceneTestAccess::unmount(scene);
    g_headlessOwnedMultiProbeA = 0;
    g_headlessOwnedMultiProbeB = 0;
    g_headlessOwnedMultiHost = 0;
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using loka::dsl::testing::SceneTestAccess;

    g_headlessOwnedProbe = 0;
    g_headlessOwnedAttachCount = 0;
    g_headlessOwnedDestroyCount = 0;
    g_headlessOwnedPersistentProbe = 0;
    g_headlessOwnedPersistentAttachCount = 0;
    g_headlessOwnedPersistentDestroyCount = 0;

    NodeDefinitionBase *rootDefinition = BoundaryDefinition<HeadlessOwnedMixedHostProps, HeadlessOwnedMixedHostBoundaryNode>().clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    assert(g_headlessOwnedPersistentProbe != 0);
    assert(g_headlessOwnedProbe != 0);
    assert(g_headlessOwnedPersistentAttachCount == 1);
    assert(g_headlessOwnedPersistentDestroyCount == 0);
    assert(g_headlessOwnedAttachCount == 1);
    assert(g_headlessOwnedDestroyCount == 0);

    HeadlessOwnedPersistentProbeNode *persistentProbe = g_headlessOwnedPersistentProbe;
    HeadlessOwnedProbeNode *ownedProbe = g_headlessOwnedProbe;

    static_cast<HeadlessOwnedMixedHostBoundaryNode *>(SceneTestAccess::rootBoundary(scene))->setShown(false);
    (void)SceneTestAccess::flushInvalidation(scene);
    (void)persistentProbe;
    assert(g_headlessOwnedPersistentProbe == persistentProbe);
    assert(g_headlessOwnedPersistentAttachCount == 1);
    assert(g_headlessOwnedPersistentDestroyCount == 0);
    (void)ownedProbe;
    assert(g_headlessOwnedProbe == ownedProbe);
    assert(g_headlessOwnedDestroyCount == 0);

    static_cast<HeadlessOwnedMixedHostBoundaryNode *>(SceneTestAccess::rootBoundary(scene))->setShown(true);
    (void)SceneTestAccess::flushInvalidation(scene);
    assert(g_headlessOwnedPersistentProbe == persistentProbe);
    assert(g_headlessOwnedPersistentAttachCount == 1);
    assert(g_headlessOwnedPersistentDestroyCount == 0);
    assert(g_headlessOwnedProbe == ownedProbe);
    assert(g_headlessOwnedAttachCount == 1);
    assert(g_headlessOwnedDestroyCount == 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
    g_headlessOwnedProbe = 0;
    g_headlessOwnedPersistentProbe = 0;
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    loka::core::MutableState<bool> enabledState(false);

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << Button("Run").enabled(&enabledState).testId("MainButton");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;

    loka::dsl::FlowChain<Scene *, Scene *> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::SetBoolStateAndFlush(&enabledState, true)).input(&scenePtr);

    LOKA_VERIFY(chain.run());
    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_PROPS) != 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    loka::core::MutableState<bool> enabledState(false);

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << Button("Run").enabled(&enabledState).testId("MainButton");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;
    loka::dsl::SnapRecord captured;

    loka::dsl::FlowChain<Scene *, loka::dsl::SnapRecord> okChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::SetBoolStateAndFlush(&enabledState, true)).input(&scenePtr)
        | loka::dsl::Step(2, FlowTestPlatformDirtyMaskAdapter("platform-props-dirty", &platform, 24))
              .onSuccess(&captured)
        | loka::dsl::Step(
            3, loka::dsl::testing::AssertSnapIntMaskHasBits("platform.dirty.mask", loka::app::scene::NODE_DIRTY_PROPS))
        | loka::dsl::Step(4, loka::dsl::testing::AssertSnapIntEquals("platform.materialized", 1));

    LOKA_VERIFY(okChain.run());
    long dirtyMask = 0;
    (void)dirtyMask;
    assert(captured.getInt("platform.dirty.mask", dirtyMask));
    assert((dirtyMask & loka::app::scene::NODE_DIRTY_PROPS) != 0);
    assert((dirtyMask & loka::app::scene::NODE_DIRTY_LAYOUT) == 0);
    assert((dirtyMask & loka::app::scene::NODE_DIRTY_CHILD) == 0);
    long fullRebuild = 1;
    (void)fullRebuild;
    assert(captured.getInt("platform.full_rebuild", fullRebuild));
    assert(fullRebuild == 0);
    long platformCalls = 0;
    (void)platformCalls;
    assert(captured.getInt("platform.calls", platformCalls));
    assert(platformCalls >= 1);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app::scene;
    using loka::dsl::testing::BoundaryObservedStateTestAccess;

    loka::core::MutableState<int> observedValue(1);
    loka::core::PushStateTracker tracker;
    tracker.addState(&observedValue);

    BoundaryObservedState observedState;
    observedState.beginPass();
    BoundaryObservedStateTestAccess::appendObservedEntry(observedState, &observedValue, NODE_DIRTY_PROPS);

    {
      loka::core::StateTrackerGuard guard(&tracker);
      observedValue.set(2, true);
    }
    NodeDirtyFlags committedFlags = NODE_DIRTY_NONE;
    LOKA_VERIFY(observedState.dirtyFlagsForCommittedStates(&tracker, committedFlags));
    assert(committedFlags == NODE_DIRTY_PROPS);

    observedState.beginPass();
    {
      loka::core::StateTrackerGuard guard(&tracker);
      observedValue.set(3, true);
    }
    LOKA_VERIFY(observedState.dirtyFlagsForCommittedStates(&tracker, committedFlags));
    assert(committedFlags == NODE_DIRTY_NONE);

    BoundaryObservedStateTestAccess::updateFirstEntryForCurrentPass(observedState, NODE_DIRTY_LAYOUT);
    LOKA_VERIFY(observedState.dirtyFlagsForCommittedStates(&tracker, committedFlags));
    assert(committedFlags == NODE_DIRTY_LAYOUT);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using loka::dsl::testing::SceneTestAccess;

    Scene scene((BoundaryDefinition<PendingCompositedProbeBoundaryProps, PendingCompositedProbeBoundaryNode>()));
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    BoundaryNode *rootBoundary = SceneTestAccess::rootBoundary(scene);
    (void)rootBoundary;
    assert(rootBoundary != 0);

    scene.requestInvalidate(NODE_DIRTY_PROPS);
    assert(SceneTestAccess::snapshotEffectiveDirtyFlags(SceneTestAccess::updateSnapshot(scene)) == NODE_DIRTY_NONE);
    assert(SceneTestAccess::snapshotFirstPendingRoot(SceneTestAccess::updateSnapshot(scene)) == 0);
    assert(SceneTestAccess::snapshotRootBoundary(SceneTestAccess::updateSnapshot(scene)) == 0);
    assert(SceneTestAccess::snapshotGeneration(SceneTestAccess::updateSnapshot(scene)) == 0);
    assert(SceneTestAccess::snapshotEffectiveDirtyFlags(SceneTestAccess::lastUpdateSnapshot(scene)) == NODE_DIRTY_NONE);
    assert(SceneTestAccess::hasRequestedInput(scene));
    assert(SceneTestAccess::requestedDirtyFlags(scene) == NODE_DIRTY_PROPS);
    assert(SceneTestAccess::effectiveRequestedDirtyFlags(scene) == NODE_DIRTY_PROPS);
    assert(SceneTestAccess::requestedFullRebuild(scene) == false);
    assert(SceneTestAccess::projectionTransactionGeneration(scene) != 0);
    const unsigned long firstGeneration = SceneTestAccess::projectionTransactionGeneration(scene);
    assert(SceneTestAccess::director(scene).firstPendingBoundary() == rootBoundary);

    LOKA_VERIFY(scene.flushInvalidation());
    const SceneDirector::SceneUpdateSnapshot &observationAfterFlush = SceneTestAccess::updateSnapshot(scene);
    (void)observationAfterFlush;
    assert(SceneTestAccess::snapshotGeneration(observationAfterFlush) == 0);
    assert(SceneTestAccess::snapshotRequestedDirtyFlags(observationAfterFlush) == NODE_DIRTY_NONE);
    assert(SceneTestAccess::snapshotTransactionDirtyFlags(observationAfterFlush) == NODE_DIRTY_NONE);
    assert(SceneTestAccess::snapshotEffectiveDirtyFlags(observationAfterFlush) == NODE_DIRTY_NONE);
    assert(SceneTestAccess::snapshotRequestedFullRebuild(observationAfterFlush) == false);
    assert(SceneTestAccess::snapshotEffectiveFullRebuild(observationAfterFlush) == false);
    assert(SceneTestAccess::snapshotFirstPendingRoot(observationAfterFlush) == 0);
    assert(SceneTestAccess::snapshotRootBoundary(observationAfterFlush) == 0);
    assert(SceneTestAccess::snapshotPrimaryRoot(observationAfterFlush) == 0);
    assert(SceneTestAccess::snapshotLayoutRequired(observationAfterFlush) == false);
    assert(SceneTestAccess::snapshotStructureRequired(observationAfterFlush) == false);
    assert(SceneTestAccess::snapshotCompositedPaintRequired(observationAfterFlush) == false);
    assert(SceneTestAccess::snapshotOpaqueLocalPaintRequired(observationAfterFlush) == false);
    const SceneDirector::SceneUpdateSnapshot &lastObservationAfterFlush = SceneTestAccess::lastUpdateSnapshot(scene);
    (void)firstGeneration;
    (void)lastObservationAfterFlush;
    assert(SceneTestAccess::snapshotGeneration(lastObservationAfterFlush) == firstGeneration);
    assert((SceneTestAccess::snapshotRequestedDirtyFlags(lastObservationAfterFlush) & NODE_DIRTY_PROPS) != 0);
    assert((SceneTestAccess::snapshotTransactionDirtyFlags(lastObservationAfterFlush) & NODE_DIRTY_PROPS) != 0);
    assert((SceneTestAccess::snapshotEffectiveDirtyFlags(lastObservationAfterFlush) & NODE_DIRTY_PROPS) != 0);
    assert(SceneTestAccess::snapshotFirstPendingRoot(lastObservationAfterFlush) == rootBoundary);
    assert(SceneTestAccess::snapshotRootBoundary(lastObservationAfterFlush) == rootBoundary);
    assert(SceneTestAccess::snapshotPrimaryRoot(lastObservationAfterFlush) == rootBoundary);
    assert(SceneTestAccess::snapshotRequestedFullRebuild(lastObservationAfterFlush) == false);
    assert(SceneTestAccess::snapshotEffectiveFullRebuild(lastObservationAfterFlush) == false);
    assert(SceneTestAccess::hasRequestedInput(scene) == false);
    assert(SceneTestAccess::requestedDirtyFlags(scene) == NODE_DIRTY_NONE);
    assert(SceneTestAccess::effectiveRequestedDirtyFlags(scene) == NODE_DIRTY_PROPS);
    assert(SceneTestAccess::requestedFullRebuild(scene) == false);
    assert(SceneTestAccess::projectionTransactionGeneration(scene) == 0);
    assert(SceneTestAccess::director(scene).firstPendingBoundary() == 0);

    scene.requestInvalidate(static_cast<NodeDirtyFlags>(NODE_DIRTY_PROPS | NODE_DIRTY_LAYOUT));
    const unsigned long secondGeneration = SceneTestAccess::projectionTransactionGeneration(scene);
    (void)secondGeneration;
    assert(secondGeneration != 0);
    assert(secondGeneration != firstGeneration);
    assert(SceneTestAccess::hasRequestedInput(scene));
    assert((SceneTestAccess::requestedDirtyFlags(scene) & NODE_DIRTY_LAYOUT) != 0);
    assert((SceneTestAccess::effectiveRequestedDirtyFlags(scene) & NODE_DIRTY_LAYOUT) != 0);
    assert(SceneTestAccess::requestedFullRebuild(scene) == false);
    assert(SceneTestAccess::director(scene).firstPendingBoundary() == rootBoundary);
    LOKA_VERIFY(scene.flushInvalidation());
    const PlatformApplyPlan &plan = SceneTestAccess::lastApplyPlan(scene);
    (void)plan;
    assert(plan.layoutRoot == rootBoundary);
    assert(plan.paintRoot == rootBoundary);
    const SceneDirector::SceneUpdateSnapshot &lastLayoutObservation = SceneTestAccess::lastUpdateSnapshot(scene);
    (void)lastLayoutObservation;
    assert(SceneTestAccess::snapshotGeneration(lastLayoutObservation) == secondGeneration);
    assert((SceneTestAccess::snapshotRequestedDirtyFlags(lastLayoutObservation) & NODE_DIRTY_LAYOUT) != 0);
    assert((SceneTestAccess::snapshotEffectiveDirtyFlags(lastLayoutObservation) & NODE_DIRTY_LAYOUT) != 0);
    assert(SceneTestAccess::snapshotRootBoundary(lastLayoutObservation) == rootBoundary);
    assert(SceneTestAccess::snapshotPrimaryRoot(lastLayoutObservation) == rootBoundary);
    assert(SceneTestAccess::hasRequestedInput(scene) == false);
    assert(SceneTestAccess::requestedDirtyFlags(scene) == NODE_DIRTY_NONE);
    assert(SceneTestAccess::effectiveRequestedDirtyFlags(scene) == NODE_DIRTY_PROPS);
    assert(SceneTestAccess::projectionTransactionGeneration(scene) == 0);
    assert(SceneTestAccess::director(scene).firstPendingBoundary() == 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
    const SceneDirector::SceneUpdateSnapshot &observationAfterUnmount = SceneTestAccess::updateSnapshot(scene);
    (void)observationAfterUnmount;
    assert(SceneTestAccess::snapshotGeneration(observationAfterUnmount) == 0);
    assert(SceneTestAccess::snapshotEffectiveDirtyFlags(observationAfterUnmount) == NODE_DIRTY_NONE);
    assert(SceneTestAccess::snapshotFirstPendingRoot(observationAfterUnmount) == 0);
    assert(SceneTestAccess::snapshotRootBoundary(observationAfterUnmount) == 0);
    assert(SceneTestAccess::snapshotPrimaryRoot(observationAfterUnmount) == 0);
    const SceneDirector::SceneUpdateSnapshot &lastObservationAfterUnmount = SceneTestAccess::lastUpdateSnapshot(scene);
    (void)lastObservationAfterUnmount;
    assert(SceneTestAccess::snapshotGeneration(lastObservationAfterUnmount) == 0);
    assert(SceneTestAccess::snapshotEffectiveDirtyFlags(lastObservationAfterUnmount) == NODE_DIRTY_NONE);
    assert(SceneTestAccess::snapshotFirstPendingRoot(lastObservationAfterUnmount) == 0);
    assert(SceneTestAccess::snapshotRootBoundary(lastObservationAfterUnmount) == 0);
    assert(SceneTestAccess::snapshotPrimaryRoot(lastObservationAfterUnmount) == 0);
    assert(SceneTestAccess::lastApplyPlan(scene).hasAnyWork() == false);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using loka::dsl::testing::SceneTestAccess;

    Scene scene((BoundaryDefinition<PendingCompositedProbeBoundaryProps, PendingCompositedProbeBoundaryNode>()));
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    BoundaryNode *rootBoundary = SceneTestAccess::rootBoundary(scene);
    (void)rootBoundary;
    assert(rootBoundary != 0);

    scene.requestInvalidate(NODE_DIRTY_PROPS);
    assert(SceneTestAccess::projectionTransactionHasPending(scene));
    assert(SceneTestAccess::projectionTransactionTargetCount(scene) == 1);
    assert(SceneTestAccess::projectionTransactionAggregateDirtyFlags(scene) == NODE_DIRTY_PROPS);
    assert(SceneTestAccess::projectionTransactionGeneration(scene) != 0);
    assert(SceneTestAccess::projectionTransactionFirstTargetNode(scene) == rootBoundary);
    assert(SceneTestAccess::projectionTransactionFirstTargetDirtyFlags(scene) == NODE_DIRTY_PROPS);

    scene.requestInvalidate(NODE_DIRTY_LAYOUT);
    assert(SceneTestAccess::projectionTransactionHasPending(scene));
    assert(SceneTestAccess::projectionTransactionTargetCount(scene) == 1);
    assert((SceneTestAccess::projectionTransactionAggregateDirtyFlags(scene) & NODE_DIRTY_PROPS) != 0);
    assert((SceneTestAccess::projectionTransactionAggregateDirtyFlags(scene) & NODE_DIRTY_LAYOUT) != 0);
    assert(SceneTestAccess::projectionTransactionFirstTargetNode(scene) == rootBoundary);
    assert((SceneTestAccess::projectionTransactionFirstTargetDirtyFlags(scene) & NODE_DIRTY_PROPS) != 0);
    assert((SceneTestAccess::projectionTransactionFirstTargetDirtyFlags(scene) & NODE_DIRTY_LAYOUT) != 0);

    LOKA_VERIFY(scene.flushInvalidation());
    assert(!SceneTestAccess::projectionTransactionHasPending(scene));
    assert(SceneTestAccess::projectionTransactionTargetCount(scene) == 0);
    assert(SceneTestAccess::projectionTransactionAggregateDirtyFlags(scene) == NODE_DIRTY_NONE);
    assert(SceneTestAccess::projectionTransactionGeneration(scene) == 0);
    assert(SceneTestAccess::projectionTransactionFirstTargetNode(scene) == 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using loka::dsl::testing::SceneTestAccess;

    Scene scene((BoundaryDefinition<PendingCompositedProbeBoundaryProps, PendingCompositedProbeBoundaryNode>()));
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    BoundaryNode *rootBoundary = SceneTestAccess::rootBoundary(scene);
    (void)rootBoundary;
    assert(rootBoundary != 0);
    scene.requestInvalidate(NODE_DIRTY_PROPS);
    assert(SceneTestAccess::director(scene).firstPendingBoundary() == rootBoundary);
    LOKA_VERIFY(scene.flushInvalidation());
    const PlatformApplyPlan &plan = SceneTestAccess::lastApplyPlan(scene);
    (void)plan;
    assert(plan.paintKind == PlatformApplyPlan::PAINT_COMPOSITED);
    assert(plan.paintRoot == rootBoundary);
    assert(SceneTestAccess::director(scene).firstPendingBoundary() == 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using loka::dsl::testing::SceneTestAccess;

    g_defaultApplyLocalPaintCalls = 0;
    g_defaultApplyOpaquePaintCalls = 0;
    g_defaultApplyCompositedPaintCalls = 0;
    g_defaultApplyLayoutCalls = 0;
    g_defaultApplyStructureCalls = 0;
    g_defaultApplyBoundsWidth = 0;
    g_defaultApplyBoundsHeight = 0;
    g_defaultApplyOpaqueHintSeen = 0;

    Scene scene((BoundaryDefinition<PendingDefaultApplyProbeBoundaryProps, PendingDefaultApplyProbeBoundaryNode>()));
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    scene.requestInvalidate(static_cast<NodeDirtyFlags>(NODE_DIRTY_PROPS | NODE_DIRTY_LAYOUT));
    BoundaryNode *rootBoundary = SceneTestAccess::rootBoundary(scene);
    (void)rootBoundary;
    assert(rootBoundary != 0);
    assert(SceneTestAccess::director(scene).firstPendingBoundary() == rootBoundary);
    LOKA_VERIFY(scene.flushInvalidation());
    assert(g_defaultApplyLayoutCalls == 1);
    assert(g_defaultApplyCompositedPaintCalls == 1);
    assert(g_defaultApplyLocalPaintCalls == 0);
    assert(g_defaultApplyOpaquePaintCalls == 1);
    assert(g_defaultApplyOpaqueHintSeen == 1);
    assert(g_defaultApplyBoundsWidth == 40);
    assert(g_defaultApplyBoundsHeight == 12);
    assert(SceneTestAccess::director(scene).firstPendingBoundary() == 0);
    assert(SceneTestAccess::lastApplyPlan(scene).paintKind == PlatformApplyPlan::PAINT_COMPOSITED);
    assert(SceneTestAccess::lastApplyPlan(scene).structureRoot == rootBoundary);

    g_defaultApplyLocalPaintCalls = 0;
    g_defaultApplyOpaquePaintCalls = 0;
    g_defaultApplyCompositedPaintCalls = 0;
    g_defaultApplyLayoutCalls = 0;
    g_defaultApplyStructureCalls = 0;

    scene.requestInvalidate(NODE_DIRTY_CHILD);
    assert(SceneTestAccess::director(scene).firstPendingBoundary() == rootBoundary);
    LOKA_VERIFY(scene.flushInvalidation());
    assert(g_defaultApplyStructureCalls == 0);
    // #695: A CHILD invalidation that does not change actual bounds must
    // not trigger layout.  Before the fix, stale actualBoundsChanged from
    // the first cycle's bounds publication leaked into this cycle and
    // promoted every later request to LAYOUT.
    assert(g_defaultApplyLayoutCalls == 0);
    assert(g_defaultApplyCompositedPaintCalls == 1);
    assert(SceneTestAccess::lastApplyPlan(scene).structureChanged == false);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using loka::dsl::testing::SceneTestAccess;

    Scene scene((BoundaryDefinition<PendingCompositedProbeBoundaryProps, PendingCompositedProbeBoundaryNode>()));
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    BoundaryNode *rootBoundary = SceneTestAccess::rootBoundary(scene);
    assert(rootBoundary != 0);
    const int baselineCalls = platform.calls_;

    scene.requestBoundaryUpdate(rootBoundary, NODE_DIRTY_NONE, true);

    (void)baselineCalls;
    assert(platform.calls_ > baselineCalls);
    assert((SceneTestAccess::lastApplyPlan(scene).paintKind == PlatformApplyPlan::PAINT_COMPOSITED)
           || (SceneTestAccess::lastApplyPlan(scene).paintKind == PlatformApplyPlan::PAINT_LOCAL_OPAQUE)
           || (SceneTestAccess::lastApplyPlan(scene).paintKind == PlatformApplyPlan::PAINT_LOCAL));
    assert((SceneTestAccess::snapshotRequestedDirtyFlags(SceneTestAccess::lastUpdateSnapshot(scene)) & NODE_DIRTY_PROPS)
           != 0);
    assert((SceneTestAccess::snapshotEffectiveDirtyFlags(SceneTestAccess::lastUpdateSnapshot(scene)) & NODE_DIRTY_PROPS)
           != 0);
    assert(SceneTestAccess::director(scene).firstPendingBoundary() == 0);
    assert(SceneTestAccess::projectionTransactionGeneration(scene) == 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using loka::dsl::testing::SceneTestAccess;

    loka::core::MutableState<int> pendingLayoutWidthState(32);
    g_pendingLayoutWidthState = &pendingLayoutWidthState;

    Scene scene((BoundaryDefinition<PendingLayoutBoundaryProps, PendingLayoutBoundaryNode>()));
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;
    loka::dsl::SnapRecord captured;
    FlowErrorCapture failCapture = {0, 0, 0};

    loka::dsl::FlowChain<Scene *, loka::dsl::SnapRecord> okChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::SetIntStateAndFlush(g_pendingLayoutWidthState, 64)).input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::CheckText("PendingLayoutText", "Sized"))
        | loka::dsl::Step(3, FlowTestPlatformDirtyMaskAdapter("pending-layout-upgrade", &platform, 20))
              .onSuccess(&captured)
        | loka::dsl::Step(
            4, loka::dsl::testing::AssertSnapIntMaskHasBits("platform.dirty.mask", loka::app::scene::NODE_DIRTY_PROPS))
        | loka::dsl::Step(
            5,
            loka::dsl::testing::AssertSnapIntMaskHasBits("platform.dirty.mask", loka::app::scene::NODE_DIRTY_LAYOUT));

    okChain.onFailure(&FlowTestMarker::captureFailure, &failCapture);
    const bool ok = okChain.run();
    if (!ok)
    {
      std::printf("[pending-layout-upgrade] fail kind=%d code=%d calls=%d full=%d flags=%ld\n",
                  failCapture.kind,
                  failCapture.code,
                  failCapture.calls,
                  platform.lastFullRebuild_ ? 1 : 0,
                  static_cast<long>(platform.lastFlags_));
      std::fflush(stdout);
    }
    assert(ok);
    assert(SceneTestAccess::director(scene).firstPendingBoundary() == 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
    g_pendingLayoutWidthState = 0;
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using loka::dsl::testing::SceneTestAccess;

    g_pendingApplyCallCount = 0;
    g_pendingApplyWhileApplyingCount = 0;

    Scene scene((BoundaryDefinition<PendingApplyProbeBoundaryProps, PendingApplyProbeBoundaryNode>()));
    FlowScenePlatformController platform;
    platform.skipGlobalChangeForBoundaryLocalPaint_ = true;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    platform.calls_ = 0;
    platform.boundaryApplyCalls_ = 0;
    platform.lastFlags_ = loka::app::scene::NODE_DIRTY_NONE;

    scene.requestInvalidate(NODE_DIRTY_PROPS);
    BoundaryNode *rootBoundary = SceneTestAccess::rootBoundary(scene);
    (void)rootBoundary;
    assert(rootBoundary != 0);
    assert(SceneTestAccess::director(scene).firstPendingBoundary() == rootBoundary);
    LOKA_VERIFY(scene.flushInvalidation());
    assert(g_pendingApplyCallCount == 1);
    assert(g_pendingApplyWhileApplyingCount == 1);
    assert(platform.boundaryApplyCalls_ == 1);
    assert(platform.lastBoundaryApplyRoot_ == SceneTestAccess::rootNode(scene)); // loka-assert-ok: rootNode is a pure test reader
    assert(platform.lastBoundaryApplyBoundary_ == rootBoundary);
    const PlatformApplyPlan &plan = SceneTestAccess::lastApplyPlan(scene);
    (void)plan;
    assert(plan.paintKind == PlatformApplyPlan::PAINT_LOCAL);
    assert(plan.layoutRoot == rootBoundary);
    assert(plan.paintRoot == rootBoundary);
    assert(platform.lastBoundaryApplyInfo_.hasAnyWork());
    assert(platform.lastBoundaryApplyInfo_.isLocalPaintRoot);
    assert(platform.lastBoundaryApplyPlan_.paintRoot == rootBoundary);
    assert(platform.lastBoundaryApplyPlan_.isLocalizedFor(rootBoundary));
    assert(platform.lastBoundaryApplyPlan_.hasBoundaryApplyWork(rootBoundary));
    assert(SceneTestAccess::director(scene).firstPendingBoundary() == 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using loka::dsl::testing::SceneTestAccess;

    Scene scene((BoundaryDefinition<PendingApplyProbeBoundaryProps, PendingApplyProbeBoundaryNode>()));
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    BoundaryNode *rootBoundary = SceneTestAccess::rootBoundary(scene);
    assert(rootBoundary != 0);
    rootBoundary->noteOpaquePaintCoverage(true);
    scene.requestInvalidate(NODE_DIRTY_PROPS);
    LOKA_VERIFY(scene.flushInvalidation());
    assert(SceneTestAccess::lastApplyPlan(scene).paintKind == PlatformApplyPlan::PAINT_LOCAL_OPAQUE);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using loka::dsl::testing::SceneTestAccess;

    g_pendingApplySiblingACalls = 0;
    g_pendingApplySiblingBCalls = 0;
    g_pendingApplySiblingALayoutRoot = 0;
    g_pendingApplySiblingAPaintRoot = 0;
    g_pendingApplySiblingBLayoutRoot = 0;
    g_pendingApplySiblingBPaintRoot = 0;

    Scene scene((BoundaryDefinition<PendingApplySiblingsRootProps, PendingApplySiblingsRootNode>()));
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    BoundaryNode *rootBoundary = SceneTestAccess::rootBoundary(scene);
    assert(rootBoundary != 0);
    INestable *rootNestable = rootBoundary->asNestable();
    assert(rootNestable != 0);
    Node *stackNode = rootNestable->childrenHead();
    assert(stackNode != 0);
    INestable *stackNestable = stackNode->asNestable();
    assert(stackNestable != 0);
    loka::dsl::CompositionCursor<Node> it(stackNestable->childrenHead(), stackNestable->childrenCount());
    Node *siblingANode = it.next();
    Node *siblingBNode = it.next();
    BoundaryNode *siblingA = siblingANode ? siblingANode->asBoundary() : 0;
    BoundaryNode *siblingB = siblingBNode ? siblingBNode->asBoundary() : 0;
    assert(siblingA != 0);
    assert(siblingB != 0);

    siblingA->markViewDirty(NODE_DIRTY_PROPS);
    siblingB->markViewDirty(NODE_DIRTY_PROPS);

    const SceneDirector &director = SceneTestAccess::director(scene);
    (void)director;
    assert(director.firstPendingBoundary() != 0);
    LOKA_VERIFY(scene.flushInvalidation());
    assert(g_pendingApplySiblingACalls == 1);
    assert(g_pendingApplySiblingBCalls == 1);
    assert(platform.boundaryApplyCalls_ == 2);
    assert(g_pendingApplySiblingALayoutRoot != 0);
    assert(g_pendingApplySiblingAPaintRoot != 0);
    assert(g_pendingApplySiblingBLayoutRoot != 0);
    assert(g_pendingApplySiblingBPaintRoot != 0);
    assert(platform.lastBoundaryApplyBoundary_ == siblingB || platform.lastBoundaryApplyBoundary_ == siblingA);
    assert(SceneTestAccess::director(scene).firstPendingBoundary() == 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using loka::dsl::testing::SceneTestAccess;

    g_pendingApplyDefersSiblingACalls = 0;
    g_pendingApplyDefersSiblingBCalls = 0;
    g_pendingApplyDefersSiblingAQueuedSiblingB = false;
    g_pendingApplyDeferredSiblingB = 0;

    Scene scene((BoundaryDefinition<PendingApplyDefersSiblingsRootProps, PendingApplyDefersSiblingsRootNode>()));
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    BoundaryNode *rootBoundary = SceneTestAccess::rootBoundary(scene);
    assert(rootBoundary != 0);
    INestable *rootNestable = rootBoundary->asNestable();
    assert(rootNestable != 0);
    Node *stackNode = rootNestable->childrenHead();
    assert(stackNode != 0);
    INestable *stackNestable = stackNode->asNestable();
    assert(stackNestable != 0);
    loka::dsl::CompositionCursor<Node> it(stackNestable->childrenHead(), stackNestable->childrenCount());
    Node *siblingANode = it.next();
    Node *siblingBNode = it.next();
    assert(siblingANode != 0);
    assert(siblingBNode != 0);
    BoundaryNode *siblingA = siblingANode->asBoundary();
    BoundaryNode *siblingB = siblingBNode->asBoundary();
    assert(siblingA != 0);
    assert(siblingB != 0);
    g_pendingApplyDeferredSiblingB = siblingB;

    siblingA->markViewDirty(NODE_DIRTY_PROPS);

    LOKA_VERIFY(scene.flushInvalidation());
    assert(g_pendingApplyDefersSiblingACalls == 1);
    assert(g_pendingApplyDefersSiblingBCalls == 0);
    assert(SceneTestAccess::director(scene).firstPendingBoundary() == siblingB);
    assert(scene.hasPendingInvalidation());

    LOKA_VERIFY(scene.flushInvalidation());
    assert(g_pendingApplyDefersSiblingACalls == 1);
    assert(g_pendingApplyDefersSiblingBCalls == 1);
    assert(SceneTestAccess::director(scene).firstPendingBoundary() == 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    loka::core::MutableState<bool> showState(false);

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    TextDefinition falseText = Text("Off").testId("OffText");
    TextDefinition trueText = Text("On").testId("OnText");
    root << composition.conditional(showState, trueText, falseText);

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;

    loka::dsl::FlowChain<Scene *, Scene *> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, loka::dsl::testing::CheckText("OffText", "Off")).input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::SetBoolStateAndFlush(&showState, true))
        | loka::dsl::Step(3, loka::dsl::testing::CheckText("OnText", "On"));

    LOKA_VERIFY(chain.run());
    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_CHILD) != 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    loka::core::MutableState<bool> showState(false);

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    TextDefinition falseText = Text("Off").testId("OffText");
    TextDefinition trueText = Text("On").testId("OnText");
    root << composition.conditional(showState, trueText, falseText);

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;
    loka::dsl::SnapRecord captured;

    loka::dsl::FlowChain<Scene *, loka::dsl::SnapRecord> okChain =
        loka::dsl::Flow() | loka::dsl::Step(1, loka::dsl::testing::CheckText("OffText", "Off")).input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::SetBoolStateAndFlush(&showState, true))
        | loka::dsl::Step(3, loka::dsl::testing::CheckText("OnText", "On"))
        | loka::dsl::Step(4, FlowTestPlatformDirtyMaskAdapter("platform-child-dirty", &platform, 16))
              .onSuccess(&captured)
        | loka::dsl::Step(5,
                          loka::dsl::testing::AssertSnapIntMaskHasBits(
                              "platform.dirty.mask",
                              static_cast<loka::app::scene::NodeDirtyFlags>(loka::app::scene::NODE_DIRTY_CHILD
                                                                            | loka::app::scene::NODE_DIRTY_LAYOUT)))
        | loka::dsl::Step(6, loka::dsl::testing::AssertSnapIntEquals("platform.materialized", 1));

    LOKA_VERIFY(okChain.run());
    long dirtyMask = 0;
    (void)dirtyMask;
    assert(captured.getInt("platform.dirty.mask", dirtyMask));
    assert((dirtyMask & loka::app::scene::NODE_DIRTY_CHILD) != 0);
    assert((dirtyMask & loka::app::scene::NODE_DIRTY_LAYOUT) != 0);
    long fullRebuild = 0;
    (void)fullRebuild;
    assert(captured.getInt("platform.full_rebuild", fullRebuild));
    long platformCalls = 0;
    (void)platformCalls;
    assert(captured.getInt("platform.calls", platformCalls));
    assert(platformCalls >= 1);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    loka::core::MutableState<TextStyle> fontSizeState((FontSize<12>()));

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << (Text("Sized") + &fontSizeState).testId("SizedText");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;
    loka::dsl::SnapRecord captured;

    loka::dsl::FlowChain<Scene *, loka::dsl::SnapRecord> okChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, SetTextStyleStateAction(&fontSizeState, FontSize<24>(), true))
              .input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::CheckText("SizedText", "Sized"))
        | loka::dsl::Step(3, FlowTestPlatformDirtyMaskAdapter("platform-layout-dirty", &platform, 32))
              .onSuccess(&captured)
        | loka::dsl::Step(
            4, loka::dsl::testing::AssertSnapIntMaskHasBits("platform.dirty.mask", loka::app::scene::NODE_DIRTY_LAYOUT))
        | loka::dsl::Step(5, loka::dsl::testing::AssertSnapIntEquals("platform.materialized", 1));

    LOKA_VERIFY(okChain.run());
    long dirtyMask = 0;
    (void)dirtyMask;
    assert(captured.getInt("platform.dirty.mask", dirtyMask));
    assert((dirtyMask & loka::app::scene::NODE_DIRTY_LAYOUT) != 0);
    long fullRebuild = 1;
    (void)fullRebuild;
    assert(captured.getInt("platform.full_rebuild", fullRebuild));
    assert(fullRebuild == 0);
    long platformCalls = 0;
    (void)platformCalls;
    assert(captured.getInt("platform.calls", platformCalls));
    assert(platformCalls >= 1);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    loka::core::MutableState<TextStyle> fontSizeState((FontSize<12>()));

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << (Text("Sized") + &fontSizeState).testId("SizedText");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;

    loka::dsl::FlowChain<Scene *, Scene *> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, SetTextStyleStateAction(&fontSizeState, FontSize<24>(), true))
              .input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::CheckText("SizedText", "Sized"));

    LOKA_VERIFY(chain.run());
    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_LAYOUT) != 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    loka::core::MutableState<loka::core::String> textState(loka::core::String::Literal("Before"));
    loka::core::MutableState<TextStyle> fontSizeState((FontSize<12>()));

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << (Text(&textState) + &fontSizeState).testId("MixedText");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;

    loka::dsl::FlowChain<Scene *, Scene *> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, loka::dsl::testing::SetStringState(&textState, "After")).input(&scenePtr)
        | loka::dsl::Step(2, SetTextStyleStateAction(&fontSizeState, FontSize<18>(), false))
        | loka::dsl::Step(3, loka::dsl::testing::FlushSceneInvalidation())
        | loka::dsl::Step(4, loka::dsl::testing::CheckText("MixedText", "After"));

    LOKA_VERIFY(chain.run());
    assert((platform.lastFlags_ & loka::app::scene::NODE_DIRTY_LAYOUT) != 0);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    const int input = 0;

    loka::dsl::FlowChain<int, loka::dsl::SnapRecord> okChain =
        loka::dsl::Flow()
        | loka::dsl::Step(
              1,
              FlowTestSnapDirtyMaskAdapter(
                  "dirty-mask-ok", loka::app::scene::NODE_DIRTY_LAYOUT | loka::app::scene::NODE_DIRTY_PROPS, 12))
              .input(&input)
        | loka::dsl::Step(2, loka::dsl::testing::CheckDirtyHasBits(loka::app::scene::NODE_DIRTY_LAYOUT));

    LOKA_VERIFY(okChain.run());

    FlowErrorCapture failCapture = {0, 0, 0};
    loka::dsl::FlowChain<int, loka::dsl::SnapRecord> failChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, FlowTestSnapDirtyMaskAdapter("dirty-mask-fail", loka::app::scene::NODE_DIRTY_PROPS, 13))
              .input(&input)
        | loka::dsl::Step(2, loka::dsl::testing::CheckDirtyHasBits(loka::app::scene::NODE_DIRTY_LAYOUT))
              .onFailure(&FlowTestMarker::captureFailure, &failCapture);

    LOKA_VERIFY(failChain.run());
    assert(failCapture.calls == 1);
    assert(failCapture.kind == loka::dsl::testing::FLOW_ERROR_KIND_SCENE_TEST_ASSERT);
    assert(failCapture.code == loka::dsl::testing::FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED);
  }

  {
    const int input = 0;

    loka::dsl::FlowChain<int, loka::dsl::SnapRecord> okChain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestSnapDirtyMaskAdapter("dirty-equals-ok", 0, 14)).input(&input)
        | loka::dsl::Step(2, loka::dsl::testing::CheckDirtyEquals(0));

    LOKA_VERIFY(okChain.run());

    FlowErrorCapture failCapture = {0, 0, 0};
    loka::dsl::FlowChain<int, loka::dsl::SnapRecord> failChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, FlowTestSnapDirtyMaskAdapter("dirty-equals-fail", loka::app::scene::NODE_DIRTY_PROPS, 15))
              .input(&input)
        | loka::dsl::Step(2, loka::dsl::testing::CheckDirtyEquals(0))
              .onFailure(&FlowTestMarker::captureFailure, &failCapture);

    LOKA_VERIFY(failChain.run());
    assert(failCapture.calls == 1);
    assert(failCapture.kind == loka::dsl::testing::FLOW_ERROR_KIND_SCENE_TEST_ASSERT);
    assert(failCapture.code == loka::dsl::testing::FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED);
  }

  {
    const int input = 0;

    loka::dsl::FlowChain<int, loka::dsl::SnapRecord> okChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, FlowTestSnapTextValueAdapter("snap-check-ok", "Hello Flow", 2)).input(&input)
        | loka::dsl::Step(2, loka::dsl::testing::CheckSnapStringEquals("text.value", "Hello Flow"));

    LOKA_VERIFY(okChain.run());

    FlowErrorCapture failCapture = {0, 0, 0};
    loka::dsl::FlowChain<int, loka::dsl::SnapRecord> failChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, FlowTestSnapTextValueAdapter("snap-check-fail", "Hello Flow", 3)).input(&input)
        | loka::dsl::Step(2, loka::dsl::testing::CheckSnapStringEquals("text.value", "Mismatch"))
              .onFailure(&FlowTestMarker::captureFailure, &failCapture);

    LOKA_VERIFY(failChain.run());
    assert(failCapture.calls == 1);
    assert(failCapture.kind == loka::dsl::testing::FLOW_ERROR_KIND_SCENE_TEST_ASSERT);
    assert(failCapture.code == loka::dsl::testing::FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED);
  }

  {
    const int input = 0;
    loka::dsl::SnapRecord captured;

    loka::dsl::FlowChain<int, loka::dsl::SnapRecord> okChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::SnapV1("SceneFlow", "timing-assert-ok", "TimingNode", 4, 1).timingFlushMs(4))
              .input(&input)
        | loka::dsl::Step(2, loka::dsl::testing::AssertSnapIntLessEqual("timing.flush_ms", 5)).onSuccess(&captured);

    LOKA_VERIFY(okChain.run());
    long flushMs = 0;
    (void)flushMs;
    assert(captured.getInt("timing.flush_ms", flushMs));
    assert(flushMs == 4);

    FlowErrorCapture failCapture = {0, 0, 0};
    loka::dsl::FlowChain<int, loka::dsl::SnapRecord> failChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::SnapV1("SceneFlow", "timing-assert-fail", "TimingNode", 5, 1).timingFlushMs(7))
              .input(&input)
        | loka::dsl::Step(2, loka::dsl::testing::AssertSnapIntLessEqual("timing.flush_ms", 5))
              .onFailure(&FlowTestMarker::captureFailure, &failCapture);

    LOKA_VERIFY(failChain.run());
    assert(failCapture.calls == 1);
    assert(failCapture.kind == loka::dsl::testing::FLOW_ERROR_KIND_SCENE_TEST_ASSERT);
    assert(failCapture.code == loka::dsl::testing::FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED);
  }

  {
    const int input = 0;

    loka::dsl::FlowChain<int, loka::dsl::SnapRecord> okChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::SnapV1("SceneFlow", "int-gte-ok", "LayoutNode", 8, 1).timingLayoutMs(12))
              .input(&input)
        | loka::dsl::Step(2, loka::dsl::testing::CheckSnapIntGreaterEqual("timing.layout_ms", 10));

    LOKA_VERIFY(okChain.run());

    FlowErrorCapture failCapture = {0, 0, 0};
    loka::dsl::FlowChain<int, loka::dsl::SnapRecord> failChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::SnapV1("SceneFlow", "int-gte-fail", "LayoutNode", 9, 1).timingLayoutMs(7))
              .input(&input)
        | loka::dsl::Step(2, loka::dsl::testing::CheckSnapIntGreaterEqual("timing.layout_ms", 10))
              .onFailure(&FlowTestMarker::captureFailure, &failCapture);

    LOKA_VERIFY(failChain.run());
    assert(failCapture.calls == 1);
    assert(failCapture.kind == loka::dsl::testing::FLOW_ERROR_KIND_SCENE_TEST_ASSERT);
    assert(failCapture.code == loka::dsl::testing::FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED);
  }

  {
    const int input = 0;

    loka::dsl::FlowChain<int, loka::dsl::SnapRecord> okChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::SnapV1("SceneFlow", "timing-check-ok", "TimingNode", 6, 1).timingFlushMs(4))
              .input(&input)
        | loka::dsl::Step(2, loka::dsl::testing::CheckTimingLessEqual("timing.flush_ms", 5));

    LOKA_VERIFY(okChain.run());

    FlowErrorCapture failCapture = {0, 0, 0};
    loka::dsl::FlowChain<int, loka::dsl::SnapRecord> failChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::SnapV1("SceneFlow", "timing-check-fail", "TimingNode", 7, 1).timingFlushMs(8))
              .input(&input)
        | loka::dsl::Step(2, loka::dsl::testing::CheckTimingLessEqual("timing.flush_ms", 5))
              .onFailure(&FlowTestMarker::captureFailure, &failCapture);

    LOKA_VERIFY(failChain.run());
    assert(failCapture.calls == 1);
    assert(failCapture.kind == loka::dsl::testing::FLOW_ERROR_KIND_SCENE_TEST_ASSERT);
    assert(failCapture.code == loka::dsl::testing::FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << Text("Hello Flow").testId("MainText");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;
    loka::dsl::SnapRecord captured;

    loka::dsl::FlowChain<Scene *, loka::dsl::SnapRecord> okChain =
        loka::dsl::Flow() | loka::dsl::Step(1, loka::dsl::testing::FindNodeById<TextNode>("MainText")).input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::CaptureNode<TextNode>("SceneFlow", "capture-assert", 2, 1))
        | loka::dsl::Step(3, loka::dsl::testing::AssertSnapStringEquals("text.value", "Hello Flow"))
              .onSuccess(&captured);

    LOKA_VERIFY(okChain.run());
    std::string value;
    assert(captured.get("text.value", value));
    assert(value == "Hello Flow");

    FlowErrorCapture failCapture = {0, 0, 0};
    loka::dsl::FlowChain<Scene *, loka::dsl::SnapRecord> failChain =
        loka::dsl::Flow() | loka::dsl::Step(1, loka::dsl::testing::FindNodeById<TextNode>("MainText")).input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::CaptureNode<TextNode>("SceneFlow", "capture-assert-fail", 3, 1))
        | loka::dsl::Step(3, loka::dsl::testing::AssertSnapStringEquals("text.value", "Mismatch"))
              .onFailure(&FlowTestMarker::captureFailure, &failCapture);

    LOKA_VERIFY(failChain.run());
    assert(failCapture.calls == 1);
    assert(failCapture.kind == loka::dsl::testing::FLOW_ERROR_KIND_SCENE_TEST_ASSERT);
    assert(failCapture.code == loka::dsl::testing::FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << Text("Ready").testId("StatusText");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;
    Scene *sameScene = 0;

    loka::dsl::FlowChain<Scene *, Scene *> okChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::CheckText("StatusText", "Ready"))
              .input(&scenePtr)
              .onSuccess(&sameScene);

    LOKA_VERIFY(okChain.run());
    assert(sameScene == scenePtr);

    FlowErrorCapture failCapture = {0, 0, 0};
    loka::dsl::FlowChain<Scene *, Scene *> failChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::CheckText("StatusText", "Busy"))
              .input(&scenePtr)
              .onFailure(&FlowTestMarker::captureFailure, &failCapture);

    LOKA_VERIFY(failChain.run());
    assert(failCapture.calls == 1);
    assert(failCapture.kind == loka::dsl::testing::FLOW_ERROR_KIND_SCENE_TEST_ASSERT);
    assert(failCapture.code == loka::dsl::testing::FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << Text("Ready").testId("StatusText");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;

    loka::dsl::FlowChain<Scene *, TextNode *> okChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::FindNodeById<TextNode>("StatusText")).input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::AssertTextEquals("Ready"));

    LOKA_VERIFY(okChain.run());

    FlowErrorCapture capture = {0, 0, 0};
    loka::dsl::FlowChain<Scene *, TextNode *> failChain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::FindNodeById<TextNode>("StatusText")).input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::AssertTextEquals("Busy"))
              .onFailure(&FlowTestMarker::captureFailure, &capture);

    LOKA_VERIFY(failChain.run());
    assert(capture.calls == 1);
    assert(capture.kind == loka::dsl::testing::FLOW_ERROR_KIND_SCENE_TEST_ASSERT);
    assert(capture.code == loka::dsl::testing::FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << Button("Press").testId("ActionButton");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;
    FlowErrorCapture capture = {0, 0, 0};

    loka::dsl::FlowChain<Scene *, TextNode *> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::FindNodeById<TextNode>("ActionButton"))
              .input(&scenePtr)
              .onFailure(&FlowTestMarker::captureFailure, &capture);

    LOKA_VERIFY(chain.run());
    assert(capture.calls == 1);
    assert(capture.kind == loka::dsl::testing::FLOW_ERROR_KIND_SCENE_SCENARIO);
    assert(capture.code == loka::dsl::testing::FLOW_ERROR_SCENE_TEST_NODE_TYPE_MISMATCH);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << Text("A").testId("DupText");
    root << Text("B").testId("DupText");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;
    FlowErrorCapture capture = {0, 0, 0};

    loka::dsl::FlowChain<Scene *, TextNode *> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::FindNodeById<TextNode>("DupText"))
              .input(&scenePtr)
              .onFailure(&FlowTestMarker::captureFailure, &capture);

    LOKA_VERIFY(chain.run());
    assert(capture.calls == 1);
    assert(capture.kind == loka::dsl::testing::FLOW_ERROR_KIND_SCENE_SCENARIO);
    assert(capture.code == loka::dsl::testing::FLOW_ERROR_SCENE_TEST_DUPLICATE_TEST_ID);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    loka::core::MutableState<loka::core::String> text(loka::core::String::Literal("before"));
    int textNotificationCount = 0;
    text.bind(&incrementNotificationCount, &textNotificationCount, false);
    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << EditText(loka::app::scene::WriteSeat<loka::core::String>(&text)).testId("ScenarioEditText");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;
    loka::dsl::FlowChain<Scene *, Scene *> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::EnterText("ScenarioEditText", "after")).input(&scenePtr)
        | loka::dsl::Step(2, loka::dsl::testing::EnterText("ScenarioEditText", "after"));

    LOKA_VERIFY(chain.run());
    assert(text.get().equals(loka::core::String::Literal("after")));
    assert(textNotificationCount == 2);

    FlowErrorCapture capture = {0, 0, 0};
    loka::dsl::FlowChain<Scene *, Scene *> wrongType =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::EnterText("RootBox", "ignored"))
              .input(&scenePtr)
              .onFailure(&FlowTestMarker::captureFailure, &capture);

    LOKA_VERIFY(wrongType.run());
    assert(capture.calls == 1);
    assert(capture.kind == loka::dsl::testing::FLOW_ERROR_KIND_SCENE_SCENARIO);
    assert(capture.code == loka::dsl::testing::FLOW_ERROR_SCENE_TEST_NODE_TYPE_MISMATCH);
    assert(text.get().equals(loka::core::String::Literal("after")));
    assert(textNotificationCount == 2);

    text.unbind(&incrementNotificationCount, &textNotificationCount);
    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    using namespace loka::app;
    using namespace loka::app::scene;

    loka::core::EmitterState click;
    int clickCount = 0;
    click.bind(&incrementNotificationCount, &clickCount, false);
    NodeComposition composition;
    BoxDefinition root = Box().testId("RootBox");
    root << Cell("Covered").onClick(&click).testId("ScenarioCell");

    composition.declare(root);
    NodeDefinitionBase *rootDefinition = composition.root()->clone();
    LOKA_VERIFY(rootDefinition != 0);
    Scene scene(rootDefinition);
    FlowScenePlatformController platform;
    scene.mount(&platform);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

    Scene *scenePtr = &scene;
    loka::dsl::FlowChain<Scene *, Scene *> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::ClickCell("ScenarioCell")).input(&scenePtr);

    LOKA_VERIFY(chain.run());
    assert(clickCount == 1);

    FlowErrorCapture capture = {0, 0, 0};
    loka::dsl::FlowChain<Scene *, Scene *> wrongType =
        loka::dsl::Flow()
        | loka::dsl::Step(1, loka::dsl::testing::ClickCell("RootBox"))
              .input(&scenePtr)
              .onFailure(&FlowTestMarker::captureFailure, &capture);

    LOKA_VERIFY(wrongType.run());
    assert(capture.calls == 1);
    assert(capture.kind == loka::dsl::testing::FLOW_ERROR_KIND_SCENE_SCENARIO);
    assert(capture.code == loka::dsl::testing::FLOW_ERROR_SCENE_TEST_NODE_TYPE_MISMATCH);
    assert(clickCount == 1);

    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  {
    loka::app::FileChooserResult fileResult;
    fileResult.kind = loka::app::FileChooserResult::RESULT_FILE;
    fileResult.item = loka::file::File::FromPath(loka::core::String::Literal("C:/tmp/a.png"));
    simpleviewer::ChooserContext context;
    simpleviewer::ChooserProjection projection;

    loka::dsl::FlowChain<loka::app::FileChooserResult, simpleviewer::ChooserProjection> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, simpleviewer::ChooserToContextAdapter()).input(&fileResult).onSuccess(&context)
        | loka::dsl::Step(2, simpleviewer::ContextToProjectionAdapter()).onSuccess(&projection);

    LOKA_VERIFY(chain.run());
    assert(projection.request.source == loka::core::resource::BLOB_SOURCE_FILE);
    assert(!projection.request.filePath.empty());
    assert(projection.request.tag.equals(loka::core::String::Literal("image-file")));
    assert(projection.message.equals(loka::core::String::Literal("Loka file: C:/tmp/a.png")));
  }

  {
    loka::app::FileChooserResult canceled;
    canceled.kind = loka::app::FileChooserResult::RESULT_CANCELED;
    simpleviewer::ChooserContext context;
    simpleviewer::ChooserProjection projection;

    loka::dsl::FlowChain<loka::app::FileChooserResult, simpleviewer::ChooserProjection> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, simpleviewer::ChooserToContextAdapter()).input(&canceled).onSuccess(&context)
        | loka::dsl::Step(2, simpleviewer::ContextToProjectionAdapter()).onSuccess(&projection);

    LOKA_VERIFY(chain.run());
    assert(projection.request.source == loka::core::resource::BLOB_SOURCE_NONE);
    assert(projection.message.equals(loka::core::String::Literal("Canceled")));
  }

  {
    loka::app::FileChooserResult folder =
        loka::app::FileChooserResult::Folder(loka::file::File::FromPath(loka::core::String::Literal("C:/tmp/images")));
    simpleviewer::ChooserContext context;
    simpleviewer::ChooserProjection projection;

    loka::dsl::FlowChain<loka::app::FileChooserResult, simpleviewer::ChooserProjection> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, simpleviewer::ChooserToContextAdapter()).input(&folder).onSuccess(&context)
        | loka::dsl::Step(2, simpleviewer::ContextToProjectionAdapter()).onSuccess(&projection);

    LOKA_VERIFY(chain.run());
    assert(projection.request.source == loka::core::resource::BLOB_SOURCE_NONE);
    assert(projection.request.filePath.empty());
    assert(projection.message.equals(loka::core::String::Literal("Loka folder: C:/tmp/images")));
  }

  {
    loka::app::FileChooserResult errorResult = loka::app::FileChooserResult::Error(42);
    simpleviewer::ChooserContext context;
    simpleviewer::ChooserProjection projection;

    loka::dsl::FlowChain<loka::app::FileChooserResult, simpleviewer::ChooserProjection> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, simpleviewer::ChooserToContextAdapter()).input(&errorResult).onSuccess(&context)
        | loka::dsl::Step(2, simpleviewer::ContextToProjectionAdapter()).onSuccess(&projection);

    LOKA_VERIFY(chain.run());
    assert(projection.request.source == loka::core::resource::BLOB_SOURCE_NONE);
    assert(projection.request.filePath.empty());
    assert(projection.message.equals(loka::core::String::Literal("Error 42")));
  }

  {
    loka::app::FileChooserResult fileEmptyPath;
    fileEmptyPath.kind = loka::app::FileChooserResult::RESULT_FILE;
    simpleviewer::ChooserContext context;
    simpleviewer::ChooserProjection projection;

    loka::dsl::FlowChain<loka::app::FileChooserResult, simpleviewer::ChooserProjection> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, simpleviewer::ChooserToContextAdapter()).input(&fileEmptyPath).onSuccess(&context)
        | loka::dsl::Step(2, simpleviewer::ContextToProjectionAdapter()).onSuccess(&projection);

    LOKA_VERIFY(chain.run());
    assert(projection.request.source == loka::core::resource::BLOB_SOURCE_NONE);
    assert(projection.request.filePath.empty());
    assert(projection.request.tag.empty());
    assert(projection.message.equals(loka::core::String::Literal("Loka file: (unknown)")));
  }

  {
    loka::core::resource::Blob blob = loka::core::resource::Blob::Create();
    simpleviewer::BlobDecodeAttempt attempt;
    loka::core::resource::Image image;
    FlowErrorCapture capture = {0, 0, 0};

    loka::dsl::FlowChain<loka::core::resource::Blob, loka::core::resource::Image> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, simpleviewer::BlobToDecodeAttemptAdapter(0))
              .input(&blob)
              .onSuccess(&attempt)
              .onFailure(&FlowTestMarker::captureFailure, &capture)
        | loka::dsl::Step(2, simpleviewer::DecodeAttemptToImageAdapter()).onSuccess(&image);

    LOKA_VERIFY(chain.run());
    assert(!image.isValid());
    assert(capture.calls == 1);
    assert(capture.kind == simpleviewer::SIMPLE_VIEWER_FLOW_ERROR_DECODE);
    assert(capture.code == simpleviewer::SIMPLE_VIEWER_FLOW_ERROR_CODE_PLATFORM_CONTEXT_MISSING);
  }

  {
    FlowTestPlatformContext ctx;
    ctx.createImageResult_ = true;
    loka::core::resource::Blob blob = loka::core::resource::Blob::Create();
    simpleviewer::BlobDecodeAttempt attempt;
    loka::core::resource::Image image;
    FlowErrorCapture capture = {0, 0, 0};

    loka::dsl::FlowChain<loka::core::resource::Blob, loka::core::resource::Image> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, simpleviewer::BlobToDecodeAttemptAdapter(&ctx))
              .input(&blob)
              .onSuccess(&attempt)
              .onFailure(&FlowTestMarker::captureFailure, &capture)
        | loka::dsl::Step(2, simpleviewer::DecodeAttemptToImageAdapter()).onSuccess(&image);

    LOKA_VERIFY(chain.run());
    assert(ctx.createImageCalls_ == 1);
    assert(image.isValid());
    LOKA_VERIFY(image.width() == 16);
    assert(image.height() == 16);
    assert(capture.calls == 0);
  }

  {
    FlowTestPlatformContext ctx;
    ctx.createImageResult_ = false;
    loka::core::resource::Blob blob = loka::core::resource::Blob::Create();
    simpleviewer::BlobDecodeAttempt attempt;
    loka::core::resource::Image image;
    FlowErrorCapture capture = {0, 0, 0};

    loka::dsl::FlowChain<loka::core::resource::Blob, loka::core::resource::Image> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, simpleviewer::BlobToDecodeAttemptAdapter(&ctx))
              .input(&blob)
              .onSuccess(&attempt)
              .onFailure(&FlowTestMarker::captureFailure, &capture)
        | loka::dsl::Step(2, simpleviewer::DecodeAttemptToImageAdapter()).onSuccess(&image);

    LOKA_VERIFY(chain.run());
    assert(ctx.createImageCalls_ == 1);
    assert(!image.isValid());
    assert(capture.calls == 1);
    assert(capture.kind == simpleviewer::SIMPLE_VIEWER_FLOW_ERROR_DECODE);
    assert(capture.code == simpleviewer::SIMPLE_VIEWER_FLOW_ERROR_CODE_IMAGE_DECODE_FAILED);
  }

  {
    int input = 10;
    int calls = 0;
    bool ready = false;
    int captured = 0;

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestMul2Adapter()).input(&input)
        | loka::dsl::Step(2, FlowTestPendingThenSuccessAdapter(&ready, &calls)).onSuccess(&captured);

    const loka::dsl::FlowRunResult first = chain.runResult();
    (void)first;
    assert(first == loka::dsl::FLOW_RUN_PENDING);
    assert(calls == 1);

    ready = true;
    const loka::dsl::FlowRunResult resumed = chain.resumeResult(2);
    (void)resumed;
    assert(resumed == loka::dsl::FLOW_RUN_SUCCEEDED);
    assert(calls == 2);
    assert(captured == 120);
  }

  {
    loka::core::MutableState<int> trigger;
    int calls = 0;
    bool ready = false;
    int captured = 0;

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestPendingThenSuccessAdapter(&ready, &calls)).onSuccess(&captured);
    chain.bindTrigger(&trigger);

    trigger.set(9, true);
    assert(calls == 1);
    assert(captured == 0);

    chain.cancel();
    ready = true;
    const loka::dsl::FlowRunResult canceled = chain.resumeResult(1);
    (void)canceled;
    assert(canceled == loka::dsl::FLOW_RUN_CANCELED);
    assert(calls == 1);
    assert(captured == 0);

    // Cancel terminal must not consume pending step output.
    assert(calls == 1);
    assert(captured == 0);
  }

  {
    int input = 5;
    int resultNotifications = 0;
    loka::core::MutableState<int> result;
    loka::core::PushStateTracker tracker;
    tracker.addState(&result);
    result.bind(&incrementNotificationCount, &resultNotifications, false);

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestMul2Adapter()).input(&input).onSuccess(&result, &tracker);

    LOKA_VERIFY(chain.run());
    assert(result.get() == 10);
    assert(resultNotifications == 1);
  }

  {
    int input = 7;
    loka::core::MutableState<int> result;
    loka::core::PushStateTracker tracker;
    loka::core::StateTracker *port = &tracker;
    FlowResultTrackerObservation observation(port);
    tracker.addState(&result);
    result.bind(&captureFlowResultTrackerPhase, &observation, false);

    loka::dsl::FlowChain<int, int> chain =
        ordinary_flow_pin::make(&input, &result, port);

    chain.withTracker(port);
    LOKA_VERIFY(chain.run());
    assert(result.get() == 14);
    assert(observation.calls == 1);
    assert(observation.phase == loka::core::TRACKER_PRECOMMIT);
    assert(tracker.transactionDirty());
  }

  {
    int input = 1;
    int callsA = 0;
    std::vector<int> order;
    FlowTestMarkerContext flowFinal = {&order, 1599};

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestCountedPassAdapter(&callsA)).input(&input).onSuccess(&input, 1);
    chain.onFinally(&FlowTestMarker::onStepFinally, &flowFinal);

    const loka::dsl::FlowRunResult result = chain.runResult();
    (void)result;
    assert(result == loka::dsl::FLOW_RUN_FAILED);
    assert(callsA <= 1024);
    assert(order.size() == 1);
    assert(order[0] == 1599);
  }

  // --- ProjectionToBlobAdapter: source=NONE (cancel) → empty Blob, success ---
  {
    simpleviewer::ChooserProjection projection;
    // default: source == BLOB_SOURCE_NONE
    loka::core::resource::Blob blob;
    FlowErrorCapture capture = {0, 0, 0};

    loka::dsl::FlowChain<simpleviewer::ChooserProjection, loka::core::resource::Blob> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, simpleviewer::ProjectionToBlobAdapter())
              .input(&projection)
              .onFailure(&FlowTestMarker::captureFailure, &capture)
              .onSuccess(&blob);

    LOKA_VERIFY(chain.run());
    assert(capture.calls == 1);
    assert(capture.kind == simpleviewer::SIMPLE_VIEWER_FLOW_ERROR_BLOB_LOAD);
    assert(capture.code == simpleviewer::SIMPLE_VIEWER_FLOW_ERROR_CODE_NO_FILE_SELECTED);
  }

  // --- ProjectionToBlobAdapter: source=FILE → reads bytes from file ---
  {
    const char *tmpPath = "_loka_test_blob_adapter.bin";
    {
      const unsigned char data[] = {0xDE, 0xAD, 0xBE, 0xEF};
      FILE *out = std::fopen(tmpPath, "wb");
      assert(out != 0);
      const std::size_t written = std::fwrite(data, 1, sizeof(data), out);
      (void)written;
      assert(written == sizeof(data));
      LOKA_VERIFY(std::fclose(out) == 0);
    }

    simpleviewer::ChooserProjection projection;
    projection.request.setFilePath(loka::core::String::Literal(tmpPath));

    loka::core::resource::Blob blob;
    FlowErrorCapture capture = {0, 0, 0};

    loka::dsl::FlowChain<simpleviewer::ChooserProjection, loka::core::resource::Blob> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, simpleviewer::ProjectionToBlobAdapter())
              .input(&projection)
              .onSuccess(&blob)
              .onFailure(&FlowTestMarker::captureFailure, &capture);

    LOKA_VERIFY(chain.run());
    assert(capture.calls == 0);
    assert(blob.isValid());
    assert(blob.size() == 4);
    assert(blob.data()[0] == 0xDE);
    assert(blob.data()[1] == 0xAD);
    assert(blob.data()[2] == 0xBE);
    assert(blob.data()[3] == 0xEF);
    assert(blob.isCompleted());

    std::remove(tmpPath);
  }

  // --- ProjectionToBlobAdapter: source=FILE, missing file → failure ---
  {
    simpleviewer::ChooserProjection projection;
    projection.request.setFilePath(loka::core::String::Literal("_loka_nonexistent_file.bin"));

    loka::core::resource::Blob blob;
    FlowErrorCapture capture = {0, 0, 0};

    loka::dsl::FlowChain<simpleviewer::ChooserProjection, loka::core::resource::Blob> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, simpleviewer::ProjectionToBlobAdapter())
              .input(&projection)
              .onFailure(&FlowTestMarker::captureFailure, &capture)
              .onSuccess(&blob);

    LOKA_VERIFY(chain.run());
    assert(capture.calls == 1);
    assert(capture.kind == simpleviewer::SIMPLE_VIEWER_FLOW_ERROR_BLOB_LOAD);
    assert(capture.code == simpleviewer::SIMPLE_VIEWER_FLOW_ERROR_CODE_STDIO_OPEN_FAILED);
  }

  // --- Full 6-step integrated chain: FileChooserResult → Image ---
  {
    const char *tmpPath = "_loka_test_full_chain.bin";
    {
      const unsigned char data[] = {0x89, 0x50, 0x4E, 0x47}; // fake PNG header
      FILE *out = std::fopen(tmpPath, "wb");
      assert(out != 0);
      const std::size_t written = std::fwrite(data, 1, sizeof(data), out);
      (void)written;
      assert(written == sizeof(data));
      LOKA_VERIFY(std::fclose(out) == 0);
    }

    FlowTestPlatformContext ctx;
    ctx.createImageResult_ = true;

    loka::app::FileChooserResult fileResult;
    fileResult.kind = loka::app::FileChooserResult::RESULT_FILE;
    fileResult.item = loka::file::File::FromPath(loka::core::String::Literal(tmpPath));

    loka::core::resource::Image image;
    FlowErrorCapture capture = {0, 0, 0};

    loka::dsl::FlowChain<loka::app::FileChooserResult, loka::core::resource::Image> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, simpleviewer::ChooserToContextAdapter()).input(&fileResult)
        | loka::dsl::Step(2, simpleviewer::ContextToProjectionAdapter())
        | loka::dsl::Step(3, simpleviewer::ProjectionToBlobAdapter())
        | loka::dsl::Step(4, simpleviewer::BlobToDecodeAttemptAdapter(&ctx))
              .onFailure(&FlowTestMarker::captureFailure, &capture)
        | loka::dsl::Step(5, simpleviewer::DecodeAttemptToImageAdapter()).onSuccess(&image);

    LOKA_VERIFY(chain.run());
    assert(capture.calls == 0);
    assert(ctx.createImageCalls_ == 1);
    assert(image.isValid());
    LOKA_VERIFY(image.width() == 16);
    assert(image.height() == 16);

    std::remove(tmpPath);
  }

  // --- Full 6-step chain: canceled → no-file-selected at blob projection ---
  {
    FlowTestPlatformContext ctx;
    ctx.createImageResult_ = false;

    loka::app::FileChooserResult canceled;
    canceled.kind = loka::app::FileChooserResult::RESULT_CANCELED;

    loka::core::resource::Image image;
    FlowErrorCapture capture = {0, 0, 0};

    loka::dsl::FlowChain<loka::app::FileChooserResult, loka::core::resource::Image> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, simpleviewer::ChooserToContextAdapter()).input(&canceled)
        | loka::dsl::Step(2, simpleviewer::ContextToProjectionAdapter())
        | loka::dsl::Step(3, simpleviewer::ProjectionToBlobAdapter())
        | loka::dsl::Step(4, simpleviewer::BlobToDecodeAttemptAdapter(&ctx))
              .onFailure(&FlowTestMarker::captureFailure, &capture)
        | loka::dsl::Step(5, simpleviewer::DecodeAttemptToImageAdapter()).onSuccess(&image);
    chain.onFailure(&FlowTestMarker::captureFailure, &capture);

    LOKA_VERIFY(chain.run());
    assert(!image.isValid());
    assert(capture.calls == 1);
    assert(capture.kind == simpleviewer::SIMPLE_VIEWER_FLOW_ERROR_BLOB_LOAD);
    assert(capture.code == simpleviewer::SIMPLE_VIEWER_FLOW_ERROR_CODE_NO_FILE_SELECTED);
  }

  // --- onSuccess field extraction: extract single field into MutableState ---
  {
    int input = 5;
    loka::core::MutableState<int> valueState;
    loka::core::MutableState<int> extraState;
    loka::core::PushStateTracker tracker;
    loka::core::StateTracker *port = &tracker;
    FlowResultTrackerObservation observation(port);
    tracker.addState(&valueState);
    tracker.addState(&extraState);
    valueState.bind(&captureFlowResultTrackerPhase, &observation, false);

    loka::dsl::FlowChain<int, FlowTestFieldOutput> chain = loka::dsl::Flow()
                                                           | loka::dsl::Step(1, FlowTestFieldAdapter())
                                                                 .input(&input)
                                                                 .onSuccess(&valueState,
                                                                            port,
                                                                            &FlowTestFieldOutput::value)
                                                                 .onSuccess(&extraState,
                                                                            port,
                                                                            &FlowTestFieldOutput::extra);

    LOKA_VERIFY(chain.run());
    assert(valueState.get() == 50); // 5 * 10
    assert(extraState.get() == 6);  // 5 + 1
    assert(observation.calls == 1);
    assert(observation.phase == loka::core::TRACKER_PRECOMMIT);
  }

  // --- bindTrigger: auto-execute flow on state change ---
  {
    loka::core::MutableState<int> trigger;
    loka::core::MutableState<int> result;
    loka::core::PushStateTracker tracker;
    tracker.addState(&result);

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestMul2Adapter()).onSuccess(&result, &tracker);
    chain.bindTrigger(&trigger);

    trigger.set(7, true);
    assert(result.get() == 14); // 7 * 2

    trigger.set(3, true);
    assert(result.get() == 6); // 3 * 2
  }

  // --- FlowSlot: long-lived owner forwards run and trigger binding ---
  {
    typedef loka::dsl::FlowChain<int, int> SlotFlowChain;

    int input = 8;
    int directResult = 0;
    loka::app::scene::FlowSlot<SlotFlowChain> slot(loka::dsl::testing::FlowSlotTestAccess::unowned());
    SlotFlowChain directChain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestMul2Adapter()).input(&input).onSuccess(&directResult);

    assert(!slot.isValid());
    slot.set(directChain);
    assert(slot.isValid());
    LOKA_VERIFY(slot.run());
    assert(directResult == 16);

    loka::core::MutableState<int> trigger;
    loka::core::MutableState<int> triggerResult;
    loka::core::PushStateTracker tracker;
    tracker.addState(&triggerResult);
    SlotFlowChain triggerChain =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestAdd1Adapter()).onSuccess(&triggerResult, &tracker);

    slot.set(triggerChain).bindTrigger(&trigger);
    trigger.set(41, true);
    assert(triggerResult.get() == 42);

    slot.clear();
    assert(!slot.isValid());
  }

  // --- FlowSlot: cancel and resume are available through the owner slot ---
  {
    typedef loka::dsl::FlowChain<int, int> SlotFlowChain;

    int input = 12;
    int calls = 0;
    bool ready = false;
    int captured = 0;
    loka::app::scene::FlowSlot<SlotFlowChain> slot(loka::dsl::testing::FlowSlotTestAccess::unowned());
    SlotFlowChain chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, FlowTestPendingThenSuccessAdapter(&ready, &calls)).input(&input).onSuccess(&captured);

    slot.set(chain);
    LOKA_VERIFY(slot.runResult() == loka::dsl::FLOW_RUN_PENDING);
    assert(calls == 1);
    slot.cancel();
    ready = true;
    LOKA_VERIFY(slot.resumeResult(1) == loka::dsl::FLOW_RUN_CANCELED);
    assert(captured == 0);

    slot.clear();
  }

  // --- FlowSlot: set during onSuccess defers replacing the running flow ---
  {
    typedef loka::dsl::FlowChain<int, int> SlotFlowChain;

    struct SetDuringRunHelper
    {
      struct Context
      {
        loka::app::scene::FlowSlot<SlotFlowChain> *slot;
        SlotFlowChain *replacement;
        int *calls;
      };

      static void replaceFlow(const int &, void *user)
      {
        Context *ctx = static_cast<Context *>(user);
        ++(*ctx->calls);
        ctx->slot->set(*ctx->replacement);
      }
    };

    int input = 4;
    int oldResult = 0;
    int replacementInput = 10;
    int replacementResult = 0;
    int replaceCalls = 0;
    loka::app::scene::FlowSlot<SlotFlowChain> slot(loka::dsl::testing::FlowSlotTestAccess::unowned());
    SlotFlowChain replacement =
        loka::dsl::Flow() | loka::dsl::Step(1, FlowTestAdd1Adapter()).input(&replacementInput).onSuccess(&replacementResult);
    SetDuringRunHelper::Context ctx = {&slot, &replacement, &replaceCalls};
    slot.set(loka::dsl::Flow()
             | loka::dsl::Step(1, FlowTestMul2Adapter())
                   .input(&input)
                   .onSuccess(&SetDuringRunHelper::replaceFlow, &ctx)
                   .onSuccess(&oldResult));
    LOKA_VERIFY(slot.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
    assert(replaceCalls == 1);
    assert(oldResult == 8);
    assert(slot.isValid());
    LOKA_VERIFY(slot.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
    assert(replacementResult == 11);

    slot.clear();
  }

  // --- FlowSlot: clear during onSuccess defers deleting the running flow ---
  {
    typedef loka::dsl::FlowChain<int, int> SlotFlowChain;

    struct ClearDuringRunHelper
    {
      struct Context
      {
        loka::app::scene::FlowSlot<SlotFlowChain> *slot;
        int *calls;
      };

      static void clearFlow(const int &, void *user)
      {
        Context *ctx = static_cast<Context *>(user);
        ++(*ctx->calls);
        ctx->slot->clear();
      }
    };

    int input = 5;
    int oldResult = 0;
    int clearCalls = 0;
    loka::app::scene::FlowSlot<SlotFlowChain> slot(loka::dsl::testing::FlowSlotTestAccess::unowned());
    ClearDuringRunHelper::Context ctx = {&slot, &clearCalls};
    slot.set(loka::dsl::Flow()
             | loka::dsl::Step(1, FlowTestMul2Adapter())
                   .input(&input)
                   .onSuccess(&ClearDuringRunHelper::clearFlow, &ctx)
                   .onSuccess(&oldResult));
    LOKA_VERIFY(slot.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
    assert(clearCalls == 1);
    assert(oldResult == 10);
    assert(!slot.isValid());
  }

  // --- FlowSlot: a chain copied out of the slot outlives the slot safely ---
  // FlowChain copies share the impl, so a copy taken via dangerouslyUnwrap()
  // keeps the hooked impl alive after the slot is gone. Disowning must clear
  // the hooks on the shared impl, or running the escaped copy would fire
  // them into the slot's freed run state.
  {
    typedef loka::dsl::FlowChain<int, int> SlotFlowChain;

    int input = 6;
    int result = 0;
    SlotFlowChain *escaped = 0;
    {
      loka::app::scene::FlowSlot<SlotFlowChain> slot(loka::dsl::testing::FlowSlotTestAccess::unowned());
      slot.set(loka::dsl::Flow()
               | loka::dsl::Step(1, FlowTestMul2Adapter()).input(&input).onSuccess(&result));
      LOKA_VERIFY(slot.run());
      assert(result == 12);
      escaped = new SlotFlowChain(slot.dangerouslyUnwrap()); // shares the hooked impl
    } // slot destroyed: its run state is freed with the slot

    result = 0;
    LOKA_VERIFY(escaped->run()); // pre-fix: hooks fire into the freed run state (ASan UAF)
    assert(result == 12);
    delete escaped;
  }

  // --- FlowSlot: extending an escaped shared chain drops slot-owned hooks ---
  {
    typedef loka::dsl::FlowChain<int, int> SlotFlowChain;

    int input = 6;
    int result = 0;
    SlotFlowChain *escaped = 0;
    {
      loka::app::scene::FlowSlot<SlotFlowChain> slot(loka::dsl::testing::FlowSlotTestAccess::unowned());
      slot.set(loka::dsl::Flow() | loka::dsl::Step(1, FlowTestMul2Adapter()).input(&input));
      escaped = new SlotFlowChain(slot.dangerouslyUnwrap()
                                  | loka::dsl::Step(2, FlowTestAdd1Adapter()).onSuccess(&result));
    }

    LOKA_VERIFY(escaped->run());
    assert(result == 13);
    delete escaped;
  }

  // --- FlowChain: run pinning must not make cancel() detach from the active impl ---
  {
    typedef loka::dsl::FlowChain<int, int> SlotFlowChain;

    struct CancelDuringFlowSuccess
    {
      struct Context
      {
        SlotFlowChain *chain;
        int *calls;
      };

      static void cancelSelf(void *user)
      {
        Context *ctx = static_cast<Context *>(user);
        ++(*ctx->calls);
        ctx->chain->cancel();
      }
    };

    int input = 3;
    int stepCalls = 0;
    int cancelCalls = 0;
    SlotFlowChain chain = loka::dsl::Flow() | loka::dsl::Step(1, FlowTestCountedPassAdapter(&stepCalls)).input(&input);
    CancelDuringFlowSuccess::Context ctx = {&chain, &cancelCalls};
    chain.onSuccess(&CancelDuringFlowSuccess::cancelSelf, &ctx, 1);

    LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_CANCELED);
    assert(stepCalls == 1);
    assert(cancelCalls == 1);
  }

  // --- bindTrigger: reentry guard prevents double execution ---
  {
    loka::core::MutableState<int> trigger;
    int calls = 0;

    // Callback that re-sets the trigger during step onSuccess processing.
    // This fires while the flow is still running, so reentry guard must block it.
    struct ReentryHelper
    {
      static void reSetTrigger(const int &, void *user)
      {
        loka::core::MutableState<int> *t = static_cast<loka::core::MutableState<int> *>(user);
        t->set(99); // value changes → notifyStateChanged → OnTriggerChanged
      }
    };

    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, FlowTestCountedPassAdapter(&calls)).onSuccess(&ReentryHelper::reSetTrigger, &trigger);
    chain.bindTrigger(&trigger);

    trigger.set(10);
    assert(calls == 1);          // reentry blocked: flow ran only once
    assert(trigger.get() == 99); // but the trigger value was updated
  }

  // --- bindTrigger: trigger-driven cancel must hit the active impl ---
  {
    struct CancelDuringTriggeredStep
    {
      struct Context
      {
        loka::dsl::FlowChain<int, int> *chain;
        int *cancelCalls;
      };

      static void cancelSelf(const int &, void *user)
      {
        Context *ctx = static_cast<Context *>(user);
        ++(*ctx->cancelCalls);
        ctx->chain->cancel();
      }
    };

    loka::core::MutableState<int> trigger;
    int callsA = 0;
    int callsB = 0;
    int cancelCalls = 0;
    CancelDuringTriggeredStep::Context ctx = {0, &cancelCalls};
    loka::dsl::FlowChain<int, int> chain =
        loka::dsl::Flow()
        | loka::dsl::Step(1, FlowTestCountedPassAdapter(&callsA))
              .onSuccess(&CancelDuringTriggeredStep::cancelSelf, &ctx)
        | loka::dsl::Step(2, FlowTestCountedPassAdapter(&callsB));
    ctx.chain = &chain;
    chain.bindTrigger(&trigger);

    trigger.set(10);
    assert(callsA == 1);
    assert(callsB == 0);
    assert(cancelCalls == 1);
  }

  // --- State<void>: callback may delete emitter safely ---
  {
    StateNotifyDeleteCtx ctx;
    ctx.state = new loka::core::EmitterState();
    ctx.valueState = 0;
    ctx.destroyCalls = 0;
    ctx.siblingCalls = 0;

    ctx.state->bind(&StateNotifyHelpers::destroySelf, &ctx, false);
    ctx.state->bind(&StateNotifyHelpers::sibling, &ctx, false);
    ctx.state->emit();

    assert(ctx.state == 0);
    assert(ctx.destroyCalls == 1);
    assert(ctx.siblingCalls == 0);
  }

  // --- State<void>: self-unbind runs once across multiple emits ---
  {
    StateNotifyDeleteCtx ctx;
    loka::core::EmitterState state;
    ctx.state = &state;
    ctx.valueState = 0;
    ctx.destroyCalls = 0;
    ctx.siblingCalls = 0;

    state.bind(&StateNotifyHelpers::selfUnbind, &ctx, false);
    state.bind(&StateNotifyHelpers::sibling, &ctx, false);

    state.emit();
    state.emit();

    assert(ctx.destroyCalls == 1);
    assert(ctx.siblingCalls == 2);
  }

  // --- State<void>: unbound sibling must not run in same emit ---
  {
    StateNotifyDeleteCtx ctx;
    loka::core::EmitterState state;
    ctx.state = &state;
    ctx.valueState = 0;
    ctx.destroyCalls = 0;
    ctx.siblingCalls = 0;

    state.bind(&StateNotifyHelpers::unbindSibling, &ctx, false);
    state.bind(&StateNotifyHelpers::sibling, &ctx, false);
    state.emit();

    assert(ctx.destroyCalls == 1);
    assert(ctx.siblingCalls == 0);
  }

  // --- State<T>: callback may delete mutable state safely ---
  {
    StateNotifyDeleteCtx ctx;
    ctx.state = 0;
    ctx.valueState = new loka::core::MutableState<int>(0);
    ctx.destroyCalls = 0;
    ctx.siblingCalls = 0;

    ctx.valueState->bind(&StateNotifyHelpers::destroyValueSelf, &ctx, false);
    ctx.valueState->bind(&StateNotifyHelpers::sibling, &ctx, false);
    ctx.valueState->set(1);

    assert(ctx.valueState == 0);
    assert(ctx.destroyCalls == 1);
    assert(ctx.siblingCalls == 0);
  }

  // --- State<T>: unbound sibling must not run in same notify ---
  {
    StateNotifyDeleteCtx ctx;
    loka::core::MutableState<int> valueState(0);
    ctx.state = 0;
    ctx.valueState = &valueState;
    ctx.destroyCalls = 0;
    ctx.siblingCalls = 0;

    valueState.bind(&StateNotifyHelpers::unbindValueSibling, &ctx, false);
    valueState.bind(&StateNotifyHelpers::sibling, &ctx, false);
    valueState.set(1);

    assert(ctx.destroyCalls == 1);
    assert(ctx.siblingCalls == 0);
  }

  printf("==== [testLokaFlowDslV1Core] end ====\n");
}

void testSimpleViewerClosesDialogFromChooserCompletion()
{
  FlowTestPlatformContext platformContext;
  loka::core::EmitterState openDialogEvent;
  simpleviewer::MainProps props;
  props.platformContext(&platformContext)
      .openDialogEvent(&openDialogEvent);
  loka::app::scene::Scene scene(
      new loka::app::scene::BoundaryDefinition<simpleviewer::MainProps, simpleviewer::MainNode>(props));
  FlowScenePlatformController platform;
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  loka::app::OpenFileDialogNode *dialog = findSimpleViewerOpenFileDialog(scene);
  assert(!dialog);

  openDialogEvent.emit();
  if (scene.hasPendingInvalidation())
  {
    LOKA_VERIFY(scene.flushInvalidation());
  }
  dialog = findSimpleViewerOpenFileDialog(scene);
  assert(dialog && dialog->props.result_.isValid());

  deliverSimpleViewerOpenFileDialogResult(dialog, loka::app::FileChooserResult());
  if (scene.hasPendingInvalidation())
  {
    LOKA_VERIFY(scene.flushInvalidation());
  }
  dialog = findSimpleViewerOpenFileDialog(scene);
  assert(dialog && "RESULT_NONE is not a delivered completion and must leave the dialog shown");

  loka::app::FileChooserResult delivered[4];
  delivered[0] =
      loka::app::FileChooserResult::File(loka::file::File::FromPath(loka::core::String::Literal("missing-file.png")));
  delivered[1] =
      loka::app::FileChooserResult::Folder(loka::file::File::FromPath(loka::core::String::Literal("missing-folder")));
  delivered[2] = loka::app::FileChooserResult::Canceled();
  delivered[3] = loka::app::FileChooserResult::Error(7);

  for (int i = 0; i < 4; ++i)
  {
    if (i != 0)
    {
      openDialogEvent.emit();
      if (scene.hasPendingInvalidation())
      {
        LOKA_VERIFY(scene.flushInvalidation());
      }
      dialog = findSimpleViewerOpenFileDialog(scene);
      assert(dialog);
    }

    deliverSimpleViewerOpenFileDialogResult(dialog, delivered[i]);
    if (scene.hasPendingInvalidation())
    {
      // Closing the Show may apply synchronously. A remaining request can be
      // the silent retire-drain tick rather than another visible update.
      (void)scene.flushInvalidation();
    }
    dialog = findSimpleViewerOpenFileDialog(scene);
    assert(!dialog && "every delivered chooser result closes SimpleViewer's owning Show");
  }

  loka::dsl::testing::SceneTestAccess::unmount(scene);
}

void testSimpleViewerImageLoadSessionPreservesAndReleasesCurrentImage()
{
  BlobAllocationProbe allocation;
  const char *path = "_loka_test_simpleviewer_reopen.bin";
  {
    const unsigned char bytes[] = {0x00, 0x11, 0x02, 0xff};
    FILE *file = std::fopen(path, "wb");
    assert(file != 0);
    LOKA_VERIFY(std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
    LOKA_VERIFY(std::fclose(file) == 0);
  }

  SimpleViewerRetainingPlatformContext platformContext;
  platformContext.createImageResult_ = true;
  loka::core::EmitterState openDialogEvent;
  simpleviewer::MainProps props;
  props.platformContext(&platformContext)
      .openDialogEvent(&openDialogEvent);
  loka::app::scene::Scene scene(
      new loka::app::scene::BoundaryDefinition<simpleviewer::MainProps, simpleviewer::MainNode>(props));
  FlowScenePlatformController platform;
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  openDialogEvent.emit();
  if (scene.hasPendingInvalidation())
  {
    LOKA_VERIFY(scene.flushInvalidation());
  }
  loka::app::OpenFileDialogNode *dialog = findSimpleViewerOpenFileDialog(scene);
  assert(dialog != 0);
  deliverSimpleViewerOpenFileDialogResult(
      dialog,
      loka::app::FileChooserResult::File(loka::file::File::FromPath(loka::core::String::Literal(path))));
  assert(platformContext.createImageCalls_ == 1);

  loka::core::Managed<loka::core::resource::BlobRecord> previousBlob = platformContext.observedBlob_;
  platformContext.observedBlob_.reset();
  assert(previousBlob.useCount() == 2
         && "a completed image load must leave its Blob only with the displayed Image and this witness");

  openDialogEvent.emit();
  assert(previousBlob.useCount() == 2 && "opening a chooser must preserve the displayed image");

  dialog = findSimpleViewerOpenFileDialog(scene);
  assert(dialog != 0 && "reopening must publish a fresh dialog node before its result is delivered");
  deliverSimpleViewerOpenFileDialogResult(dialog, loka::app::FileChooserResult::Canceled());
  assert(previousBlob.useCount() == 2 && "canceling an image load must preserve the displayed image");

  openDialogEvent.emit();
  dialog = findSimpleViewerOpenFileDialog(scene);
  assert(dialog != 0);
  allocation.refuse("Blob", "Bytes");
  deliverSimpleViewerOpenFileDialogResult(
      dialog,
      loka::app::FileChooserResult::File(loka::file::File::FromPath(loka::core::String::Literal(path))));
  assert(allocation.refusals == 1
         && "capacity refusal must release the current image and retry the Blob staging step once");
  assert(previousBlob.useCount() == 1
         && "the retry must release the previous payload before allocating the replacement");
  assert(platformContext.createImageCalls_ == 2 && "the fresh flow must remain bound to chooser completion");
  assert(platformContext.observedBlob_.isValid());
  assert(platformContext.observedBlob_ != previousBlob && "the second read must publish a fresh blob");

  loka::dsl::testing::SceneTestAccess::unmount(scene);
  LOKA_VERIFY(std::remove(path) == 0);
}

void testSimpleViewerImageLoadStopsWhenCapacityRemainsUnavailable()
{
  BlobAllocationProbe allocation;
  const char *path = "_loka_test_simpleviewer_capacity_refusal.bin";
  {
    const unsigned char bytes[] = {0x00, 0x11, 0x02, 0xff};
    FILE *file = std::fopen(path, "wb");
    assert(file != 0);
    LOKA_VERIFY(std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
    LOKA_VERIFY(std::fclose(file) == 0);
  }

  SimpleViewerRetainingPlatformContext platformContext;
  platformContext.createImageResult_ = true;
  loka::core::Managed<loka::core::resource::BlobRecord> unavailableCapacity;
  loka::core::EmitterState openDialogEvent;
  simpleviewer::MainProps props;
  props.platformContext(&platformContext)
      .openDialogEvent(&openDialogEvent);
  loka::app::scene::Scene scene(
      new loka::app::scene::BoundaryDefinition<simpleviewer::MainProps, simpleviewer::MainNode>(props));
  FlowScenePlatformController platform;
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  openDialogEvent.emit();
  loka::app::OpenFileDialogNode *dialog = findSimpleViewerOpenFileDialog(scene);
  assert(dialog != 0);
  deliverSimpleViewerOpenFileDialogResult(
      dialog,
      loka::app::FileChooserResult::File(loka::file::File::FromPath(loka::core::String::Literal(path))));
  LOKA_VERIFY(platformContext.createImageCalls_ == 1);
  platformContext.observedBlob_.reset();
  const int freed = allocation.byteFrees;
  openDialogEvent.emit();
  dialog = findSimpleViewerOpenFileDialog(scene);
  LOKA_VERIFY(dialog != 0);
  allocation.refuse("Blob", "Bytes", 2);
  deliverSimpleViewerOpenFileDialogResult(
      dialog, loka::app::FileChooserResult::File(loka::file::File::FromPath(path)));
  LOKA_VERIFY(allocation.refusals == 2 && allocation.byteFrees > freed);
  SimpleViewerTestAccess::verifyAllocationFailure(
      *static_cast<simpleviewer::MainNode *>(loka::dsl::testing::SceneTestAccess::rootNode(scene)));
  LOKA_VERIFY(platformContext.createImageCalls_ == 1);
  // A new chooser proves finalization cleared the previous session's running gate.
  openDialogEvent.emit();
  dialog = findSimpleViewerOpenFileDialog(scene);
  LOKA_VERIFY(dialog != 0);
  deliverSimpleViewerOpenFileDialogResult(dialog, loka::app::FileChooserResult::Canceled());

  loka::dsl::testing::SceneTestAccess::unmount(scene);
  LOKA_VERIFY(std::remove(path) == 0);
}

void testSimpleViewerBlobAdapterClearsPreviousOutputBeforeFailure()
{
  const char *path = "_loka_test_simpleviewer_blob_adapter.bin";
  {
    const unsigned char bytes[] = {0x10, 0x20, 0x30, 0x40};
    FILE *file = std::fopen(path, "wb");
    assert(file != 0);
    LOKA_VERIFY(std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
    LOKA_VERIFY(std::fclose(file) == 0);
  }

  simpleviewer::ProjectionToBlobAdapter adapter;
  simpleviewer::ChooserProjection projection;
  projection.request.setFilePath(loka::core::String::Literal(path));
  loka::core::resource::Blob output;
  loka::dsl::FlowError error;
  LOKA_VERIFY(adapter.run(projection, output, error) == loka::dsl::FLOW_STEP_SUCCEEDED);
  assert(output.isCompleted());
  assert(output.isCompleted());
  assert(output.size() == 4);
  assert(output.size() == 4);

  loka::core::Managed<loka::core::resource::BlobRecord> previousBlob = output.handle();
  assert(previousBlob.useCount() == 2);
  const simpleviewer::ChooserProjection canceled;
  LOKA_VERIFY(adapter.run(canceled, output, error) == loka::dsl::FLOW_STEP_FAILED);
  assert(previousBlob.useCount() == 1
         && "a failed read attempt must not leave the adapter's previous heavy output installed");
  assert(output == loka::core::resource::Blob::Empty());

  LOKA_VERIFY(std::remove(path) == 0);
}

namespace
{
  typedef loka::dsl::FlowChain<int, int> CharacterizationIntFlow;
  typedef loka::app::scene::FlowSlot<CharacterizationIntFlow> CharacterizationIntFlowSlot;

  struct FlowImplementationLifetimeProbe
  {
    FlowImplementationLifetimeProbe()
        : liveAdapters(0),
          runCalls(0)
    {
    }

    int liveAdapters;
    int runCalls;
  };

  struct CharacterizationFlowAdapter
  {
    typedef int In;
    typedef int Out;
    typedef void (*DuringRunFn)(void *);

    CharacterizationFlowAdapter(FlowImplementationLifetimeProbe *probe,
                                DuringRunFn duringRun = 0,
                                void *userData = 0,
                                bool pending = false)
        : probe_(probe),
          duringRun_(duringRun),
          userData_(userData),
          pending_(pending)
    {
      assert(this->probe_ != 0);
      ++this->probe_->liveAdapters;
    }

    CharacterizationFlowAdapter(const CharacterizationFlowAdapter &other)
        : probe_(other.probe_),
          duringRun_(other.duringRun_),
          userData_(other.userData_),
          pending_(other.pending_)
    {
      assert(this->probe_ != 0);
      ++this->probe_->liveAdapters;
    }

    ~CharacterizationFlowAdapter()
    {
      assert(this->probe_ != 0);
      assert(this->probe_->liveAdapters > 0);
      --this->probe_->liveAdapters;
    }

    loka::dsl::StepRunStatus run(const int &in, int &out, loka::dsl::FlowError &) const
    {
      ++this->probe_->runCalls;
      if (this->duringRun_)
      {
        this->duringRun_(this->userData_);
      }
      if (this->pending_)
      {
        return loka::dsl::FLOW_STEP_PENDING;
      }
      out = in + 1;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }

    FlowImplementationLifetimeProbe *probe_;
    DuringRunFn duringRun_;
    void *userData_;
    bool pending_;
  };

  static CharacterizationIntFlow makeCharacterizationFlow(
      int *input,
      FlowImplementationLifetimeProbe *probe,
      CharacterizationFlowAdapter::DuringRunFn duringRun = 0,
      void *userData = 0,
      bool pending = false)
  {
    return loka::dsl::Flow()
           | loka::dsl::Step(1, CharacterizationFlowAdapter(probe, duringRun, userData, pending)).input(input);
  }

  struct DropAllFlowHandlesContext
  {
    CharacterizationIntFlow **first;
    CharacterizationIntFlow **second;
    FlowImplementationLifetimeProbe *probe;
  };

  static void dropAllFlowHandlesDuringRun(void *userData)
  {
    DropAllFlowHandlesContext *context = static_cast<DropAllFlowHandlesContext *>(userData);
    assert(context != 0);
    assert(context->probe->liveAdapters == 1);

    delete *context->first;
    *context->first = 0;
    assert(context->probe->liveAdapters == 1);

    delete *context->second;
    *context->second = 0;
    assert(context->probe->liveAdapters == 1);
  }

  struct ClearRunningFlowContext
  {
    CharacterizationIntFlowSlot *slot;
    FlowImplementationLifetimeProbe *runningProbe;
  };

  static void clearFlowSlotDuringRun(void *userData)
  {
    ClearRunningFlowContext *context = static_cast<ClearRunningFlowContext *>(userData);
    assert(context != 0);
    context->slot->clear();
    assert(!context->slot->isValid());
    assert(context->runningProbe->liveAdapters == 1);
  }

  struct ReplaceRunningFlowContext
  {
    CharacterizationIntFlowSlot *slot;
    FlowImplementationLifetimeProbe *runningProbe;
    FlowImplementationLifetimeProbe *replacementProbe;
    int *replacementInput;
  };

  static void replaceFlowSlotDuringRun(void *userData)
  {
    ReplaceRunningFlowContext *context = static_cast<ReplaceRunningFlowContext *>(userData);
    assert(context != 0);
    CharacterizationIntFlow replacement =
        makeCharacterizationFlow(context->replacementInput, context->replacementProbe);
    context->slot->set(replacement);
    assert(context->slot->isValid());
    assert(context->runningProbe->liveAdapters == 1);
  }

  class CharacterizationBoundaryBase : public loka::app::scene::BoundaryNode
  {
  public:
    virtual void composeWithContext(loka::app::scene::ComponentContext &,
                                    loka::app::scene::ComposeEvent)
    {
    }
  };

  class CharacterizationFlowOwner : public CharacterizationBoundaryBase
  {
  public:
    CharacterizationFlowOwner() : flow_(*this) {}
    virtual ~CharacterizationFlowOwner() {}

    void setFlow(const CharacterizationIntFlow &flow)
    {
      this->flow_.set(flow);
    }

    loka::dsl::FlowRunResult runFlow()
    {
      return this->flow_.runResult();
    }

  private:
    CharacterizationIntFlowSlot flow_;
  };

  struct DestroyRunningFlowOwnerContext
  {
    CharacterizationFlowOwner **owner;
    FlowImplementationLifetimeProbe *runningProbe;
  };

  static void destroyFlowOwnerDuringRun(void *userData)
  {
    DestroyRunningFlowOwnerContext *context = static_cast<DestroyRunningFlowOwnerContext *>(userData);
    assert(context != 0);
    delete *context->owner;
    *context->owner = 0;
    assert(context->runningProbe->liveAdapters == 1);
  }

  class CharacterizationStateOwner : public loka::app::scene::IStateOwner
  {
  public:
    CharacterizationStateOwner()
        : tracker_(),
          owned_(),
          borrowed_(),
          releaseCalls_(0),
          holdLedger_(this)
    {
    }

    virtual ~CharacterizationStateOwner()
    {
      for (size_t i = 0; i < this->borrowed_.size(); ++i)
      {
        this->tracker_.removeState(this->borrowed_[i]);
      }
      for (size_t i = this->owned_.size(); i > 0; --i)
      {
        this->tracker_.removeState(this->owned_[i - 1]);
        delete this->owned_[i - 1];
      }
    }

    virtual void adoptState(loka::core::StateBase *state)
    {
      this->adoptStateUnchecked(state);
    }

    virtual void adoptStateUnchecked(loka::core::StateBase *state)
    {
      if (!state)
      {
        return;
      }
      for (size_t i = 0; i < this->owned_.size(); ++i)
      {
        if (this->owned_[i] == state)
        {
          return;
        }
      }
      this->owned_.push_back(state);
      this->tracker_.addStateUnchecked(state);
    }

    virtual void releaseState(loka::core::StateBase *state)
    {
      if (!state)
      {
        return;
      }
      for (size_t i = 0; i < this->owned_.size(); ++i)
      {
        if (this->owned_[i] == state)
        {
          this->owned_.erase(this->owned_.begin() + i);
          this->tracker_.removeState(state);
          ++this->releaseCalls_;
          delete state;
          return;
        }
      }
      assert(false && "CharacterizationStateOwner::releaseState requires an owned state");
    }

    virtual void reserveStates(size_t count)
    {
      this->owned_.reserve(this->owned_.size() + count);
      this->tracker_.reserveStates(count);
    }

    virtual void reserveStateArena(size_t) {}

    virtual void noteStateAllocationFailure()
    {
      assert(false && "unexpected state OOM in this test fixture");
    }

    virtual void *allocateStateMemory(size_t, size_t)
    {
      return 0;
    }

    virtual void registerStateMemory(loka::core::StateBase *, void (*)(loka::core::StateBase *)) {}

    virtual loka::core::HoldLedger *holdLedger()
    {
      return &this->holdLedger_;
    }
    virtual void reserveHeldArena(size_t) {}
    virtual void *allocateHeldMemory(size_t, size_t)
    {
      return 0;
    }
    virtual void registerHeldMemory(loka::core::detail::HeldBlockBase *) {}
    virtual void retireHeldBlock(loka::core::detail::HeldBlockBase *)
    {
      assert(false && "CharacterizationStateOwner has no retire pool");
    }

    virtual loka::core::StateTracker *tracker()
    {
      return &this->tracker_;
    }

    void trackBorrowed(loka::core::StateBase *state)
    {
      this->borrowed_.push_back(state);
      this->tracker_.addState(state);
    }

    loka::core::PushStateTracker *pushTracker()
    {
      return &this->tracker_;
    }

    int releaseCalls() const
    {
      return this->releaseCalls_;
    }

  private:
    loka::core::PushStateTracker tracker_;
    std::vector<loka::core::StateBase *> owned_;
    std::vector<loka::core::StateBase *> borrowed_;
    int releaseCalls_;
    loka::core::HoldLedger holdLedger_;
  };

  struct CharacterizationAddOneMapper
  {
    typedef int Result;

    int operator()(const int &value) const
    {
      return value + 1;
    }
  };

  class BorrowDirectionApi
  {
  public:
    static const char *kInterfaceName()
    {
      return "BorrowDirectionApi";
    }

    static BorrowDirectionApi *fromNode(loka::app::scene::Node *node)
    {
      return node ? static_cast<BorrowDirectionApi *>(node->queryInterface(kInterfaceName())) : 0;
    }

    virtual ~BorrowDirectionApi() {}
  };

  class BorrowDirectionBoundary : public CharacterizationBoundaryBase, public BorrowDirectionApi
  {
  public:
    virtual void *queryInterface(const char *name)
    {
      return name && std::strcmp(name, kInterfaceName()) == 0 ? static_cast<BorrowDirectionApi *>(this) : 0;
    }
  };
} // namespace

namespace
{
  /** Minimal conforming StateTracker: counts the transaction bracket and the
      dirt reported into it, and is deliberately not a PushStateTracker. */
  struct FlowCountingStateTracker : public loka::core::StateTracker
  {
    FlowCountingStateTracker()
        : begins(0),
          ends(0),
          dirtyMarks(0),
          currentPhase(loka::core::TRACKER_IDLE)
    {
    }
    void begin()
    {
      ++begins;
      currentPhase = loka::core::TRACKER_PRECOMMIT;
    }
    void defer(loka::core::TrackerDeferKey, void (*)(void *), void *)
    {
    }
    void markDirty(loka::core::StateBase *)
    {
      ++dirtyMarks;
    }
    bool end()
    {
      ++ends;
      currentPhase = loka::core::TRACKER_IDLE;
      return true;
    }
    void registerDependency(loka::core::StateBase *, loka::core::StateBase *)
    {
    }
    loka::core::TrackerPhase phase() const
    {
      return currentPhase;
    }
    int begins;
    int ends;
    int dirtyMarks;
    loka::core::TrackerPhase currentPhase;
  };
} // namespace

void testFlowOnSuccessBracketsGenericTrackerTransaction()
{
  int input = 5;
  loka::core::MutableState<int> result;
  FlowCountingStateTracker tracker;
  loka::core::StateTracker *port = &tracker;
  assert(port->asPushTracker() == 0); // pure query: the port is deliberately not a push tracker

  loka::dsl::FlowChain<int, int> chain =
      ordinary_flow_pin::make(&input, &result, port);

  LOKA_VERIFY(chain.run());
  LOKA_VERIFY(result.get() == 10);
  // The write must land inside one begin()/end() bracket on the abstract port,
  // not bypass the transaction because the port is not a PushStateTracker.
  LOKA_VERIFY(tracker.begins == 1);
  LOKA_VERIFY(tracker.ends == 1);
  assert(tracker.phase() == loka::core::TRACKER_IDLE);
}

void testFlowChainHandleCopiesShareImplementationLifetime()
{
  FlowImplementationLifetimeProbe sharedProbe;
  FlowImplementationLifetimeProbe replacedProbe;
  int sharedInput = 1;
  int replacedInput = 2;

  {
    CharacterizationIntFlow original = makeCharacterizationFlow(&sharedInput, &sharedProbe);
    assert(sharedProbe.liveAdapters == 1);

    {
      CharacterizationIntFlow copied(original);
      assert(sharedProbe.liveAdapters == 1);

      CharacterizationIntFlow assigned = makeCharacterizationFlow(&replacedInput, &replacedProbe);
      assert(replacedProbe.liveAdapters == 1);
      assigned = copied;
      assert(replacedProbe.liveAdapters == 0);
      assert(sharedProbe.liveAdapters == 1);
    }

    assert(sharedProbe.liveAdapters == 1);
  }

  assert(sharedProbe.liveAdapters == 0);
  assert(replacedProbe.liveAdapters == 0);
}

void testFlowChainRunPinDefersImplementationDeletion()
{
  FlowImplementationLifetimeProbe probe;
  int input = 5;
  CharacterizationIntFlow *first = 0;
  CharacterizationIntFlow *second = 0;
  DropAllFlowHandlesContext context = {&first, &second, &probe};

  first = new CharacterizationIntFlow(
      makeCharacterizationFlow(&input, &probe, &dropAllFlowHandlesDuringRun, &context));
  second = new CharacterizationIntFlow(*first);
  assert(probe.liveAdapters == 1);

  const loka::dsl::FlowRunResult result = first->runResult();
  (void)result;
  assert(result == loka::dsl::FLOW_RUN_SUCCEEDED);
  assert(first == 0);
  assert(second == 0);
  assert(probe.runCalls == 1);
  assert(probe.liveAdapters == 0);
}

void testFlowSlotClearDefersRunningFlowDeletion()
{
  FlowImplementationLifetimeProbe probe;
  int input = 8;
  CharacterizationIntFlowSlot slot(loka::dsl::testing::FlowSlotTestAccess::unowned());
  ClearRunningFlowContext context = {&slot, &probe};
  {
    CharacterizationIntFlow flow =
        makeCharacterizationFlow(&input, &probe, &clearFlowSlotDuringRun, &context);
    slot.set(flow);
  }
  assert(probe.liveAdapters == 1);

  const loka::dsl::FlowRunResult result = slot.runResult();
  (void)result;
  assert(result == loka::dsl::FLOW_RUN_SUCCEEDED);
  assert(!slot.isValid());
  assert(probe.runCalls == 1);
  assert(probe.liveAdapters == 0);
}

void testFlowSlotSetDefersRunningFlowDeletion()
{
  FlowImplementationLifetimeProbe runningProbe;
  FlowImplementationLifetimeProbe replacementProbe;
  int runningInput = 13;
  int replacementInput = 21;
  CharacterizationIntFlowSlot slot(loka::dsl::testing::FlowSlotTestAccess::unowned());
  ReplaceRunningFlowContext context = {&slot, &runningProbe, &replacementProbe, &replacementInput};
  {
    CharacterizationIntFlow flow =
        makeCharacterizationFlow(&runningInput, &runningProbe, &replaceFlowSlotDuringRun, &context);
    slot.set(flow);
  }
  assert(runningProbe.liveAdapters == 1);

  const loka::dsl::FlowRunResult firstResult = slot.runResult();
  (void)firstResult;
  assert(firstResult == loka::dsl::FLOW_RUN_SUCCEEDED);
  assert(slot.isValid());
  assert(runningProbe.runCalls == 1);
  assert(runningProbe.liveAdapters == 0);
  assert(replacementProbe.liveAdapters == 1);

  const loka::dsl::FlowRunResult replacementResult = slot.runResult();
  (void)replacementResult;
  assert(replacementResult == loka::dsl::FLOW_RUN_SUCCEEDED);
  assert(replacementProbe.runCalls == 1);
  slot.clear();
  assert(replacementProbe.liveAdapters == 0);
}

void testFlowSlotOwnerDestructionDefersRunningFlowDeletion()
{
  FlowImplementationLifetimeProbe probe;
  int input = 34;
  CharacterizationFlowOwner *owner = new CharacterizationFlowOwner();
  DestroyRunningFlowOwnerContext context = {&owner, &probe};
  {
    CharacterizationIntFlow flow =
        makeCharacterizationFlow(&input, &probe, &destroyFlowOwnerDuringRun, &context);
    owner->setFlow(flow);
  }
  assert(probe.liveAdapters == 1);

  const loka::dsl::FlowRunResult result = owner->runFlow();
  (void)result;
  assert(result == loka::dsl::FLOW_RUN_SUCCEEDED);
  assert(owner == 0);
  assert(probe.runCalls == 1);
  assert(probe.liveAdapters == 0);
}

void testFlowOwnerDestructionReleasesPendingFlow()
{
  FlowImplementationLifetimeProbe neverRunProbe;
  int neverRunInput = 55;
  {
    CharacterizationFlowOwner *owner = new CharacterizationFlowOwner();
    {
      CharacterizationIntFlow flow = makeCharacterizationFlow(&neverRunInput, &neverRunProbe);
      owner->setFlow(flow);
    }
    assert(neverRunProbe.liveAdapters == 1);
    delete owner;
  }
  assert(neverRunProbe.runCalls == 0);
  assert(neverRunProbe.liveAdapters == 0);

  FlowImplementationLifetimeProbe pendingProbe;
  int pendingInput = 89;
  {
    CharacterizationFlowOwner *owner = new CharacterizationFlowOwner();
    {
      CharacterizationIntFlow flow = makeCharacterizationFlow(&pendingInput, &pendingProbe, 0, 0, true);
      owner->setFlow(flow);
    }
    assert(pendingProbe.liveAdapters == 1);
    LOKA_VERIFY(owner->runFlow() == loka::dsl::FLOW_RUN_PENDING);
    assert(pendingProbe.runCalls == 1);
    delete owner;
  }
  assert(pendingProbe.liveAdapters == 0);
}

void testStateStreamCopyTransfersOwnedState()
{
  CharacterizationStateOwner owner;
  loka::core::MutableState<int> *state = new loka::core::MutableState<int>(7);
  owner.adoptState(state);

  {
    loka::dsl::StateStream<int> source(state, owner.tracker(), &owner, true);
    loka::dsl::StateStream<int> destination(source);
    source.releaseOwnedState();
    assert(owner.releaseCalls() == 0);
    destination.releaseOwnedState();
    assert(owner.releaseCalls() == 1);
  }

  assert(owner.releaseCalls() == 1);
}

void testStateStreamAssignmentTransfersOwnedState()
{
  CharacterizationStateOwner owner;
  loka::core::MutableState<int> *sourceState = new loka::core::MutableState<int>(3);
  loka::core::MutableState<int> *destinationState = new loka::core::MutableState<int>(9);
  owner.adoptState(sourceState);
  owner.adoptState(destinationState);

  {
    loka::dsl::StateStream<int> source(sourceState, owner.tracker(), &owner, true);
    loka::dsl::StateStream<int> destination(destinationState, owner.tracker(), &owner, true);
    destination = source;
    assert(owner.releaseCalls() == 1);
    source.releaseOwnedState();
    assert(owner.releaseCalls() == 1);
  }

  assert(owner.releaseCalls() == 2);
}

void testStateStreamDestructionReleasesOwnedState()
{
  CharacterizationStateOwner owner;
  loka::core::MutableState<int> *ownedState = new loka::core::MutableState<int>(12);
  owner.adoptState(ownedState);
  {
    loka::dsl::StateStream<int> stream(ownedState, owner.tracker(), &owner, true);
    assert(owner.releaseCalls() == 0);
  }
  assert(owner.releaseCalls() == 1);

  loka::core::MutableState<int> *borrowedState = new loka::core::MutableState<int>(18);
  owner.adoptState(borrowedState);
  {
    loka::dsl::StateStream<int> stream(borrowedState, owner.tracker(), &owner, false);
  }
  assert(owner.releaseCalls() == 1);
  owner.releaseState(borrowedState);
  assert(owner.releaseCalls() == 2);
}

void testStateStreamDestructionUnbindsSources()
{
  loka::core::MutableState<int> source(4);
  loka::core::MutableState<int> targetStorage(0);
  CharacterizationStateOwner owner;
  owner.trackBorrowed(&source);
  owner.trackBorrowed(&targetStorage);
  loka::app::scene::NodeState<int> target(&targetStorage, owner.tracker(), &owner);

  {
    loka::dsl::StateStream<int> sourceStream(&source, owner.tracker(), &owner);
    loka::dsl::StateStream<int> mapped = sourceStream.map(CharacterizationAddOneMapper());
    mapped.set(target, true);
    assert(target.get() == 5);
  }
  assert(owner.releaseCalls() == 1);

  owner.pushTracker()->begin();
  source.set(40, true);
  owner.pushTracker()->end();
  assert(target.get() == 5);
}

namespace
{
  struct ChainAddOneMapper
  {
    typedef int Result;
    int *calls;
    explicit ChainAddOneMapper(int *count) : calls(count) {}
    int operator()(const int &value) const
    {
      ++*this->calls;
      return value + 1;
    }
  };

  struct ChainSum
  {
    typedef int Result;
    int operator()(const int &left, const int &right) const { return left + right; }
  };

  loka::dsl::StateStream<int> makeOwnedChainLink(loka::dsl::StateStream<int> &source, int *calls)
  {
    return source.map(ChainAddOneMapper(calls));
  }

  class ChainReleaseOrderOwner : public CharacterizationStateOwner
  {
  public:
    std::vector<loka::core::StateBase *> adopted;
    std::vector<loka::core::StateBase *> released;

    virtual void adoptStateUnchecked(loka::core::StateBase *state)
    {
      this->adopted.push_back(state);
      CharacterizationStateOwner::adoptStateUnchecked(state);
    }

    virtual void releaseState(loka::core::StateBase *state)
    {
      this->released.push_back(state);
      CharacterizationStateOwner::releaseState(state);
    }
  };

  enum StreamChainShape
  {
    CHAIN_MAP,
    CHAIN_COMBINE_LEFT,
    CHAIN_COMBINE_RIGHT,
    CHAIN_RETURNED,
    CHAIN_EXPR,
    CHAIN_NAMED_ESCAPE,
    CHAIN_FLOW_SLOT
  };

  void verifyStreamChainOwnership(StreamChainShape shape)
  {
    // Borrowed storage outlives the owner's tracker deregistration.
    loka::core::MutableState<int> input(1), otherInput(10), output(0);
    CharacterizationStateOwner owner;
    owner.trackBorrowed(&input);
    owner.trackBorrowed(&otherInput);
    owner.trackBorrowed(&output);
    loka::app::scene::NodeState<int> target(&output, owner.tracker(), &owner);
    int calls = 0;
    {
      loka::dsl::StateStream<int> source(&input, owner.tracker(), &owner);
      loka::dsl::StateStream<int> other(&otherInput, owner.tracker(), &owner);
      loka::dsl::StateStream<int> result;
      loka::app::scene::FlowSlot<loka::dsl::StateStream<int> > slot(loka::dsl::testing::FlowSlotTestAccess::unowned());
      switch (shape)
      {
      case CHAIN_MAP:
        result = source.map(ChainAddOneMapper(&calls)).map(ChainAddOneMapper(&calls));
        break;
      case CHAIN_COMBINE_LEFT:
        result = source.map(ChainAddOneMapper(&calls)).combine(other, ChainSum());
        break;
      case CHAIN_COMBINE_RIGHT:
        result = other.combine(source.map(ChainAddOneMapper(&calls)), ChainSum());
        break;
      case CHAIN_RETURNED:
        result = makeOwnedChainLink(source, &calls).map(ChainAddOneMapper(&calls));
        break;
      case CHAIN_EXPR:
        result = source.map(ChainAddOneMapper(&calls)).map(source.slot.value() + loka::dsl::Const(1));
        break;
      case CHAIN_NAMED_ESCAPE:
        {
          loka::dsl::StateStream<int> a = source.map(ChainAddOneMapper(&calls));
          result = a.map(ChainAddOneMapper(&calls));
          a.releaseOwnedState();
          assert(owner.releaseCalls() == 0);
        }
        break;
      case CHAIN_FLOW_SLOT:
        slot.set(source.map(ChainAddOneMapper(&calls)).map(ChainAddOneMapper(&calls)));
        slot.bindTo(target);
        break;
      }
      if (shape != CHAIN_FLOW_SLOT)
        result.set(target);
      const int initialCalls = calls;
      (void)initialCalls;
      {
        loka::core::StateTrackerGuard guard(owner.tracker());
        input.set(5);
      }
      assert(calls > initialCalls);
      assert(output.get() == ((shape == CHAIN_COMBINE_LEFT || shape == CHAIN_COMBINE_RIGHT) ? 16 : 7));
    }
    assert(owner.releaseCalls() == 2);
    const int releasedCalls = calls;
    (void)releasedCalls;
    {
      loka::core::StateTrackerGuard guard(owner.tracker());
      input.set(9);
      otherInput.set(20);
    }
    assert(calls == releasedCalls);
    assert(output.get() == ((shape == CHAIN_COMBINE_LEFT || shape == CHAIN_COMBINE_RIGHT) ? 16 : 7));
  }
}

void testStateStreamChainMapOwnsLinks() { verifyStreamChainOwnership(CHAIN_MAP); }
void testStateStreamChainCombineLeftOwnsLinks() { verifyStreamChainOwnership(CHAIN_COMBINE_LEFT); }
void testStateStreamChainCombineRightOwnsLinks() { verifyStreamChainOwnership(CHAIN_COMBINE_RIGHT); }
void testStateStreamChainReturnedOwnsLinks() { verifyStreamChainOwnership(CHAIN_RETURNED); }
void testStateStreamChainExprOwnsLinks() { verifyStreamChainOwnership(CHAIN_EXPR); }
void testStateStreamChainNamedConsumptionEscapes() { verifyStreamChainOwnership(CHAIN_NAMED_ESCAPE); }
void testStateStreamChainFlowSlotOwnsLinks() { verifyStreamChainOwnership(CHAIN_FLOW_SLOT); }

void testStateStreamChainReleasesIntermediatesInReverseOrder()
{
  loka::core::MutableState<int> leftInput(1), rightInput(10), output(0);
  ChainReleaseOrderOwner owner;
  owner.trackBorrowed(&leftInput);
  owner.trackBorrowed(&rightInput);
  owner.trackBorrowed(&output);
  loka::app::scene::NodeState<int> target(&output, owner.tracker(), &owner);
  int calls = 0;
  {
    loka::dsl::StateStream<int> left(&leftInput, owner.tracker(), &owner);
    loka::dsl::StateStream<int> right(&rightInput, owner.tracker(), &owner);
    // Named construction fixes adoption order independently of argument evaluation order.
    loka::dsl::StateStream<int> a = left.map(ChainAddOneMapper(&calls)).map(ChainAddOneMapper(&calls));
    loka::dsl::StateStream<int> b = right.map(ChainAddOneMapper(&calls));
    loka::dsl::StateStream<int> result = a.combine(b, ChainSum()).map(ChainAddOneMapper(&calls));
    a.releaseOwnedState();
    b.releaseOwnedState();
    assert(owner.releaseCalls() == 0);
    result.set(target);
    {
      loka::core::StateTrackerGuard guard(owner.tracker());
      leftInput.set(5);
      rightInput.set(20);
    }
    assert(output.get() == 29);
  }
  assert(owner.releaseCalls() == 5);
  assert(owner.adopted.size() == 5 && owner.released.size() == 5);
  // Four intermediate records unwind before the final State, which was adopted last.
  for (size_t i = 0; i < 4; ++i)
    assert(owner.released[i] == owner.adopted[3 - i]);
  assert(owner.released[4] == owner.adopted[4]);
}

void testStateStreamChainCopyAssignmentAndRepeatedRelease()
{
  loka::core::MutableState<int> input(1);
  CharacterizationStateOwner owner;
  owner.trackBorrowed(&input);
  int calls = 0;
  {
    loka::dsl::StateStream<int> source(&input, owner.tracker(), &owner);
    loka::dsl::StateStream<int> first = source.map(ChainAddOneMapper(&calls)).map(ChainAddOneMapper(&calls));
    loka::dsl::StateStream<int> copied(first);
    first.releaseOwnedState();
    assert(owner.releaseCalls() == 0);
    loka::dsl::StateStream<int> assigned = source.map(ChainAddOneMapper(&calls)).map(ChainAddOneMapper(&calls));
    assigned = copied;
    assert(owner.releaseCalls() == 2);
    copied.releaseOwnedState();
    assert(owner.releaseCalls() == 2);
    loka::app::scene::FlowSlot<loka::dsl::StateStream<int> > slot(loka::dsl::testing::FlowSlotTestAccess::unowned());
    slot.set(assigned);
    assigned.releaseOwnedState();
    assert(owner.releaseCalls() == 2);
    slot.set(source.map(ChainAddOneMapper(&calls)).map(ChainAddOneMapper(&calls)));
    assert(owner.releaseCalls() == 4);
    slot.clear();
    slot.clear();
    assert(owner.releaseCalls() == 6);
    const int releasedCalls = calls;
    (void)releasedCalls;
    {
      loka::core::StateTrackerGuard guard(owner.tracker());
      input.set(5);
    }
    assert(calls == releasedCalls);
  }
  assert(owner.releaseCalls() == 6);
}

void testBoundaryBorrowDirectionsRejectSiblingAndDescendant()
{
  using loka::app::scene::ComponentContext;
  using loka::app::scene::Node;
  using loka::app::scene::NodeComposition;
  using loka::app::scene::NodeState;

  CharacterizationBoundaryBase parent;
  BorrowDirectionBoundary siblingA;
  BorrowDirectionBoundary siblingB;
  BorrowDirectionBoundary descendant;
  siblingA.setParentBoundary(&parent);
  siblingB.setParentBoundary(&parent);
  descendant.setParentBoundary(&siblingA);

  loka::core::MutableState<int> siblingValue(1);
  loka::core::MutableState<int> descendantValue(2);
  NodeState<int> siblingState(&siblingValue, siblingB.tracker(), &siblingB);
  NodeState<int> descendantState(&descendantValue, descendant.tracker(), &descendant);

  ComponentContext parentContext;
  ComponentContext siblingContext(&parentContext);
  ComponentContext leafContext(&siblingContext);
  Node leaf;
  parentContext.setOwner(&parent);
  parentContext.setBoundary(&parent);
  parentContext.setStateOwner(&parent);
  siblingContext.setOwner(&siblingA);
  siblingContext.setBoundary(&siblingA);
  siblingContext.setStateOwner(&siblingA);
  leafContext.setOwner(&leaf);
  leafContext.setBoundary(&siblingA);
  leafContext.setStateOwner(&siblingA);

  ComponentContext descendantContext(&leafContext);
  descendantContext.setOwner(&descendant);
  descendantContext.setBoundary(&descendant);
  descendantContext.setStateOwner(&descendant);

  ComponentContext siblingBContext(&parentContext);
  siblingBContext.setOwner(&siblingB);
  siblingBContext.setBoundary(&siblingB);
  siblingBContext.setStateOwner(&siblingB);

  NodeComposition composition;
  composition.setContext(&parentContext);
  assert(composition.currentBoundary().isValid());
  assert(!composition.findBoundary<BorrowDirectionApi>().isValid());
  assert(!composition.currentBoundary().state(descendantState).isValid());

  composition.setContext(&leafContext);
  assert(composition.currentBoundary().isValid());
  assert(!composition.findBoundary<BorrowDirectionApi>().isValid());
  assert(!composition.currentBoundary().state(siblingState).isValid());
}

void testSceneFlowAtTickRunsEachActionOnceInOrder()
{
  loka::dsl::testing::ScenarioClock clock;
  loka::app::scene::Scene *scene = 0;
  int calls = 0;
  loka::dsl::FlowChain<loka::app::scene::Scene *, loka::app::scene::Scene *> chain =
      (loka::dsl::testing::ScenarioFlow(clock, &scene)
       | loka::dsl::testing::AtTick(2, CountSceneAction(&calls, 0))
       | loka::dsl::testing::AtTick(4, CountSceneAction(&calls, 1)))
          .flow();

  clock.advanceTo(1);
  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_PENDING);
  LOKA_VERIFY(calls == 0);
  clock.advanceTo(2);
  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_PENDING);
  LOKA_VERIFY(calls == 1);
  clock.advanceTo(3);
  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_PENDING);
  LOKA_VERIFY(calls == 1);
  clock.advanceTo(4);
  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(calls == 2);
  clock.advanceTo(5);
  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(calls == 2);
}

void testSceneFlowThenRunsEachActionOnceInOrder()
{
  loka::dsl::testing::ScenarioClock clock;
  loka::app::scene::Scene *scene = 0;
  int calls = 0;
  loka::dsl::FlowChain<loka::app::scene::Scene *, loka::app::scene::Scene *> chain =
      (loka::dsl::testing::ScenarioFlow(clock, &scene) | loka::dsl::testing::Then(CountSceneAction(&calls, 0))
       | loka::dsl::testing::Then(CountSceneAction(&calls, 1)))
          .flow();

  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(calls == 2);
  clock.advanceTo(5);
  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(calls == 2);
}

void testSceneFlowWaitUntilAppliedWaitsForProjection()
{
  using namespace loka::app::scene;

  Scene scene((BoundaryDefinition<PendingCompositedProbeBoundaryProps, PendingCompositedProbeBoundaryNode>()));
  FlowScenePlatformController platform;
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  const int callsBeforeAction = platform.calls_;
  int actionCalls = 0;
  Scene *scenePtr = &scene;
  Scene *completedScene = 0;
  loka::dsl::testing::ScenarioClock clock;
  loka::dsl::FlowChain<Scene *, Scene *> chain = (loka::dsl::testing::ScenarioFlow(clock, &scenePtr)
                                                  | loka::dsl::testing::Then(QueueSceneApplyAction(&actionCalls))
                                                        .onSuccess(&completedScene)
                                                        .waitUntil(loka::dsl::testing::ProjectionEvent::APPLIED))
                                                     .flow();

  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_PENDING);
  LOKA_VERIFY(actionCalls == 1);
  const bool applyPending = scene.hasPendingInvalidation();
  LOKA_VERIFY(applyPending);
  LOKA_VERIFY(platform.calls_ == callsBeforeAction);
  LOKA_VERIFY(completedScene == 0);

  LOKA_VERIFY(scene.flushInvalidation());
  LOKA_VERIFY(platform.calls_ > callsBeforeAction);
  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(actionCalls == 1);
  LOKA_VERIFY(completedScene == &scene);
  const int callsAfterApply = platform.calls_;
  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(actionCalls == 1);
  LOKA_VERIFY(platform.calls_ == callsAfterApply);

  loka::dsl::testing::SceneTestAccess::unmount(scene);
}

void testSceneFlowWaitUntilAppliedRetainsBaselineWhileActionPending()
{
  using namespace loka::app::scene;

  Scene scene((BoundaryDefinition<PendingCompositedProbeBoundaryProps, PendingCompositedProbeBoundaryNode>()));
  FlowScenePlatformController platform;
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  const int callsBeforeAction = platform.calls_;
  int actionCalls = 0;
  Scene *scenePtr = &scene;
  loka::dsl::testing::ScenarioClock clock;
  loka::dsl::FlowChain<Scene *, Scene *> chain = (loka::dsl::testing::ScenarioFlow(clock, &scenePtr)
                                                  | loka::dsl::testing::Then(PendingSceneApplyAction(&actionCalls))
                                                        .waitUntil(loka::dsl::testing::ProjectionEvent::APPLIED))
                                                     .flow();

  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_PENDING);
  LOKA_VERIFY(actionCalls == 1);
  const bool applyPending = scene.hasPendingInvalidation();
  LOKA_VERIFY(applyPending);
  LOKA_VERIFY(scene.flushInvalidation());
  LOKA_VERIFY(platform.calls_ > callsBeforeAction);

  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(actionCalls == 2);
  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(actionCalls == 2);

  loka::dsl::testing::SceneTestAccess::unmount(scene);
}

void testSceneFlowWaitUntilPresentedWaitsForPlatformSynchronization()
{
  using namespace loka::app::scene;

  Scene scene((BoundaryDefinition<PendingCompositedProbeBoundaryProps, PendingCompositedProbeBoundaryNode>()));
  FlowScenePlatformController platform;
  platform.queuePendingSyncOnChange_ = true;
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  platform.synchronize();

  int actionCalls = 0;
  Scene *scenePtr = &scene;
  loka::dsl::testing::ScenarioClock clock;
  loka::dsl::FlowChain<Scene *, Scene *> chain = (loka::dsl::testing::ScenarioFlow(clock, &scenePtr)
                                                  | loka::dsl::testing::Then(QueueSceneApplyAction(&actionCalls))
                                                        .waitUntil(loka::dsl::testing::ProjectionEvent::PRESENTED))
                                                     .flow();

  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_PENDING);
  LOKA_VERIFY(actionCalls == 1);
  LOKA_VERIFY(scene.flushInvalidation());
  const bool presentationPending = platform.hasPendingSync();
  LOKA_VERIFY(presentationPending);
  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_PENDING);
  LOKA_VERIFY(actionCalls == 1);

  const int synchronizeCallsBeforePresentation = platform.synchronizeCalls_;
  platform.synchronize();
  LOKA_VERIFY(platform.synchronizeCalls_ == synchronizeCallsBeforePresentation + 1);
  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(actionCalls == 1);

  loka::dsl::testing::SceneTestAccess::unmount(scene);
}

void testSceneFlowWaitUntilAppliedDoesNotWaitForPlatformSynchronization()
{
  using namespace loka::app::scene;

  Scene scene((BoundaryDefinition<PendingCompositedProbeBoundaryProps, PendingCompositedProbeBoundaryNode>()));
  FlowScenePlatformController platform;
  platform.queuePendingSyncOnChange_ = true;
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  platform.synchronize();

  int actionCalls = 0;
  Scene *scenePtr = &scene;
  loka::dsl::testing::ScenarioClock clock;
  loka::dsl::FlowChain<Scene *, Scene *> chain = (loka::dsl::testing::ScenarioFlow(clock, &scenePtr)
                                                  | loka::dsl::testing::Then(QueueSceneApplyAction(&actionCalls))
                                                        .waitUntil(loka::dsl::testing::ProjectionEvent::APPLIED))
                                                     .flow();

  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_PENDING);
  LOKA_VERIFY(scene.flushInvalidation());
  const bool presentationPending = platform.hasPendingSync();
  LOKA_VERIFY(presentationPending);
  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(actionCalls == 1);

  platform.synchronize();
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}

void testSceneFlowWaitUntilAppliedRefusesMissingProjection()
{
  using namespace loka::app::scene;

  Scene scene((BoundaryDefinition<PendingCompositedProbeBoundaryProps, PendingCompositedProbeBoundaryNode>()));
  FlowScenePlatformController platform;
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);

  Scene *scenePtr = &scene;
  loka::dsl::testing::ScenarioClock clock;
  int calls = 0;
  FlowErrorCapture failure = {0, 0, 0};
  loka::dsl::FlowChain<Scene *, Scene *> chain =
      (loka::dsl::testing::ScenarioFlow(clock, &scenePtr)
       | loka::dsl::testing::Then(CountSceneAction(&calls, 0)).waitUntil(loka::dsl::testing::ProjectionEvent::APPLIED))
          .flow();
  chain.onFailure(&FlowTestMarker::captureFailure, &failure);

  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(calls == 1);
  LOKA_VERIFY(failure.calls == 1);
  LOKA_VERIFY(failure.kind == loka::dsl::testing::FLOW_ERROR_KIND_SCENE_SCENARIO);
  LOKA_VERIFY(failure.code == loka::dsl::testing::FLOW_ERROR_SCENE_TEST_EXPECTED_APPLY);

  loka::dsl::testing::SceneTestAccess::unmount(scene);
}

void testFlowMatchRunsOnlyFirstMatchingArm()
{
  int input = 10;
  int captured = 0;
  FlowMatchArmProbe first(true, 5);
  FlowMatchArmProbe later(true, 100);

  loka::dsl::FlowChain<int, int> chain =
      loka::dsl::Flow() | loka::dsl::Step(1, FlowTestAdd1Adapter()).input(&input)
      | loka::dsl::Match<int, int>(2)
            .arm(&FlowMatchTestHelpers::probeMatcher, &first, &FlowMatchTestHelpers::valueHandler)
            .arm(&FlowMatchTestHelpers::probeMatcher, &later, &FlowMatchTestHelpers::valueHandler)
      | loka::dsl::Step(3, FlowTestAdd1Adapter()).onSuccess(&captured);

  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(first.matcherCalls == 1);
  LOKA_VERIFY(first.handlerCalls == 1);
  LOKA_VERIFY(later.matcherCalls == 0);
  LOKA_VERIFY(later.handlerCalls == 0);
  LOKA_VERIFY(captured == 17);
}

void testFlowMatchChildOutputFlowsToNextStep()
{
  int input = 6;
  int captured = 0;
  loka::dsl::FlowChain<int, int> child =
      loka::dsl::Flow() | loka::dsl::Step(101, FlowTestMul2Adapter());
  loka::dsl::MatchSpec<int, int> match = loka::dsl::Match<int, int>(2);
  match.arm(&FlowMatchTestHelpers::alwaysMatch, 0, child);

  loka::dsl::FlowChain<int, int> chain =
      loka::dsl::Flow() | loka::dsl::Step(1, FlowTestAdd1Adapter()).input(&input)
      | match
      | loka::dsl::Step(3, FlowTestAdd1Adapter()).onSuccess(&captured);

  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(captured == 15);

  FlowErrorCapture failure = {0, 0, 0};
  loka::dsl::FlowChain<int, int> failingChild =
      loka::dsl::Flow() | loka::dsl::Step(102, FlowTestFail500Adapter());
  loka::dsl::MatchSpec<int, int> failingMatch = loka::dsl::Match<int, int>(4);
  failingMatch.arm(&FlowMatchTestHelpers::alwaysMatch, 0, failingChild);
  loka::dsl::FlowChain<int, int> failingParent =
      loka::dsl::Flow() | loka::dsl::Step(1, FlowTestAdd1Adapter()).input(&input) | failingMatch;
  failingParent.onFailure(&FlowTestMarker::is500, &FlowTestMarker::captureFailure, &failure);
  LOKA_VERIFY(failingParent.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(failure.calls == 1);
  LOKA_VERIFY(failure.kind == 1);
  LOKA_VERIFY(failure.code == 500);
}

void testFlowMatchPendingChildKeepsParentTriggerReentryDropped()
{
  loka::core::MutableState<int> trigger;
  bool ready = false;
  int childCalls = 0;
  int captured = 0;
  loka::dsl::FlowChain<int, int> child =
      loka::dsl::Flow() | loka::dsl::Step(101, FlowTestPendingThenSuccessAdapter(&ready, &childCalls));
  loka::dsl::MatchSpec<int, int> match = loka::dsl::Match<int, int>(20);
  match.arm(&FlowMatchTestHelpers::alwaysMatch, 0, child);
  loka::dsl::FlowChain<int, int> parent =
      loka::dsl::Flow() | match | loka::dsl::Step(21, FlowTestAdd1Adapter()).onSuccess(&captured);
  parent.bindTrigger(&trigger);

  trigger.set(5, true);
  LOKA_VERIFY(childCalls == 1);
  LOKA_VERIFY(captured == 0);

  trigger.set(8, true);
  LOKA_VERIFY(childCalls == 1);
  LOKA_VERIFY(captured == 0);

  ready = true;
  LOKA_VERIFY(parent.resumeResult(20) == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(childCalls == 2);
  LOKA_VERIFY(captured == 106);
}

void testFlowMatchForwardsParentCancelToPendingChild()
{
  int input = 9;
  bool ready = false;
  int pendingCalls = 0;
  int laterChildCalls = 0;
  loka::dsl::FlowChain<int, int> child =
      loka::dsl::Flow() | loka::dsl::Step(101, FlowTestPendingThenSuccessAdapter(&ready, &pendingCalls))
      | loka::dsl::Step(102, FlowTestCountedPassAdapter(&laterChildCalls));
  loka::dsl::MatchSpec<int, int> match = loka::dsl::Match<int, int>(20);
  match.arm(&FlowMatchTestHelpers::alwaysMatch, 0, child);
  loka::dsl::FlowChain<int, int> parent = loka::dsl::Flow() | loka::dsl::Step(1, FlowTestAdd1Adapter()).input(&input)
                                               | match;

  LOKA_VERIFY(parent.runResult() == loka::dsl::FLOW_RUN_PENDING);
  LOKA_VERIFY(pendingCalls == 1);
  LOKA_VERIFY(laterChildCalls == 0);

  parent.cancel();
  ready = true;
  LOKA_VERIFY(parent.resumeResult(20) == loka::dsl::FLOW_RUN_CANCELED);
  LOKA_VERIFY(pendingCalls == 1);
  LOKA_VERIFY(laterChildCalls == 0);

  LOKA_VERIFY(parent.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(pendingCalls == 2);
  LOKA_VERIFY(laterChildCalls == 1);
}

void testFlowMatchFreshRunAfterCancelSurvivesCopyOnWriteClone()
{
  int input = 9;
  bool ready = false;
  int childCalls = 0;
  loka::dsl::FlowChain<int, int> child =
      loka::dsl::Flow() | loka::dsl::Step(101, FlowTestPendingThenSuccessAdapter(&ready, &childCalls));
  loka::dsl::MatchSpec<int, int> match = loka::dsl::Match<int, int>(20);
  match.arm(&FlowMatchTestHelpers::alwaysMatch, 0, child);
  loka::dsl::FlowChain<int, int> parent = loka::dsl::Flow() | loka::dsl::Step(1, FlowTestAdd1Adapter()).input(&input)
                                               | match;

  LOKA_VERIFY(parent.runResult() == loka::dsl::FLOW_RUN_PENDING);
  LOKA_VERIFY(childCalls == 1);
  parent.cancel();

  // A shared wrapper that mutates after the cancel detaches onto a
  // copy-on-write clone. The clone's child must not carry the stale cancel
  // request into its next fresh arm start.
  loka::dsl::FlowChain<int, int> copy = parent;
  copy.clearCancel();
  ready = true;
  LOKA_VERIFY(copy.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(childCalls == 2);
}

void testFlowMatchChildKeepsMatchedInputAcrossTriggerRebind()
{
  loka::core::MutableState<int> trigger;
  loka::core::MutableState<int> replacement;
  bool ready = false;
  int childCalls = 0;
  int captured = 0;
  loka::dsl::FlowChain<int, int> child =
      loka::dsl::Flow() | loka::dsl::Step(101, FlowTestPendingThenSuccessAdapter(&ready, &childCalls));
  loka::dsl::MatchSpec<int, int> match = loka::dsl::Match<int, int>(20);
  match.arm(&FlowMatchTestHelpers::alwaysMatch, 0, child);
  loka::dsl::FlowChain<int, int> parent =
      loka::dsl::Flow() | match | loka::dsl::Step(21, FlowTestAdd1Adapter()).onSuccess(&captured);
  parent.bindTrigger(&trigger);

  trigger.set(5, true);
  LOKA_VERIFY(childCalls == 1);

  // Rebinding frees the old trigger input buffer while the child is still
  // pending on the matched value; the resume must read a value Match owns,
  // not the freed buffer.
  parent.bindTrigger(&replacement);
  ready = true;
  LOKA_VERIFY(parent.resumeResult(20) == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(childCalls == 2);
  LOKA_VERIFY(captured == 106);
}

void testFlowMatchParentCancelTerminallyCleansPendingChild()
{
  int input = 9;
  bool ready = false;
  int childCalls = 0;
  int childFinally = 0;
  bool childLoading = false;
  loka::dsl::FlowChain<int, int> child =
      loka::dsl::Flow() | loka::dsl::Step(101, FlowTestPendingThenSuccessAdapter(&ready, &childCalls));
  child.onFinally(&FlowMatchTestHelpers::countFinally, &childFinally).trackLoading(&childLoading);
  loka::dsl::MatchSpec<int, int> match = loka::dsl::Match<int, int>(20);
  match.arm(&FlowMatchTestHelpers::alwaysMatch, 0, child);
  loka::dsl::FlowChain<int, int> parent = loka::dsl::Flow() | loka::dsl::Step(1, FlowTestAdd1Adapter()).input(&input)
                                               | match;

  LOKA_VERIFY(parent.runResult() == loka::dsl::FLOW_RUN_PENDING);
  LOKA_VERIFY(childLoading);
  LOKA_VERIFY(childFinally == 0);

  // Consuming the parent's cancellation must also settle the pending child:
  // its loading state drops and its onFinally fires exactly once.
  parent.cancel();
  LOKA_VERIFY(parent.resumeResult(20) == loka::dsl::FLOW_RUN_CANCELED);
  LOKA_VERIFY(!childLoading);
  LOKA_VERIFY(childFinally == 1);
  LOKA_VERIFY(childCalls == 1);
}

void testFlowMatchChildHandledFailureWithoutOutputFailsMatch()
{
  int input = 500;
  int handledCalls = 0;
  int downstreamCalls = 0;
  loka::dsl::FlowChain<int, int> child =
      loka::dsl::Flow() | loka::dsl::Step(101, FlowTestFail500Adapter())
      | loka::dsl::Step(102, FlowTestAdd1Adapter());
  child.onFailure(&FlowMatchTestHelpers::countAsHandledFailure, &handledCalls);
  loka::dsl::MatchSpec<int, int> match = loka::dsl::Match<int, int>(20);
  match.arm(&FlowMatchTestHelpers::alwaysMatch, 0, child);
  loka::dsl::FlowChain<int, int> parent = loka::dsl::Flow() | loka::dsl::Step(1, FlowTestAdd1Adapter()).input(&input)
                                               | match | loka::dsl::Step(30, FlowTestCountedPassAdapter(&downstreamCalls));

  // The child's own onFailure handles the error, so the child run "succeeds"
  // without ever producing its final output. Match must not fabricate one.
  LOKA_VERIFY(parent.runResult() == loka::dsl::FLOW_RUN_FAILED);
  LOKA_VERIFY(handledCalls == 1);
  LOKA_VERIFY(downstreamCalls == 0);
}

void testFlowMatchPendingValueArmKeepsMatchedInputAcrossTriggerRebind()
{
  loka::core::MutableState<int> trigger;
  loka::core::MutableState<int> replacement;
  FlowMatchTestHelpers::PendingValueProbe probe;
  int captured = 0;
  loka::dsl::MatchSpec<int, int> valueMatch = loka::dsl::Match<int, int>(20);
  valueMatch.arm(&FlowMatchTestHelpers::alwaysMatch, &probe, &FlowMatchTestHelpers::pendingThenValue);
  loka::dsl::FlowChain<int, int> parent =
      loka::dsl::Flow() | valueMatch | loka::dsl::Step(21, FlowTestAdd1Adapter()).onSuccess(&captured);
  parent.bindTrigger(&trigger);

  trigger.set(5, true);
  LOKA_VERIFY(probe.calls == 1);
  LOKA_VERIFY(probe.lastIn == 5);

  // The value that selected the arm must be the value the resumed handler
  // sees, even after the trigger buffer it came from has been replaced.
  parent.bindTrigger(&replacement);
  probe.ready = true;
  LOKA_VERIFY(parent.resumeResult(20) == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(probe.calls == 2);
  LOKA_VERIFY(probe.lastIn == 5);
  LOKA_VERIFY(captured == 106);
}

void testFlowMatchWithoutMatchingArmPropagatesFailure()
{
  int input = 4;
  FlowMatchArmProbe arm(false, 1);
  loka::dsl::MatchSpec<int, int> match = loka::dsl::Match<int, int>(10);
  match.arm(&FlowMatchTestHelpers::probeMatcher, &arm, &FlowMatchTestHelpers::valueHandler);
  loka::dsl::FlowChain<int, int> chain =
      loka::dsl::Flow() | loka::dsl::Step(9, FlowTestAdd1Adapter()).input(&input) | match;

  LOKA_VERIFY(chain.runResult() == loka::dsl::FLOW_RUN_FAILED);
  LOKA_VERIFY(arm.matcherCalls == 1);
  LOKA_VERIFY(arm.handlerCalls == 0);

  int otherwiseCalls = 0;
  loka::dsl::MatchSpec<int, int> withOtherwise = loka::dsl::Match<int, int>(11);
  withOtherwise.otherwise(&FlowMatchTestHelpers::countAsValue, &otherwiseCalls);
  loka::dsl::FlowChain<int, int> handled =
      loka::dsl::Flow() | loka::dsl::Step(8, FlowTestAdd1Adapter()).input(&input) | withOtherwise;
  LOKA_VERIFY(handled.runResult() == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(otherwiseCalls == 1);
}

void testFlowMatchDegenerateRoutingMatchesOnFailureResult()
{
  int input = 500;
  int failureCalls = 0;
  int matchCalls = 0;
  int failureActionCalls = 0;
  int matchActionCalls = 0;
  loka::dsl::FlowChain<int, int> failureChain =
      loka::dsl::Flow() | loka::dsl::Step(1, FlowTestFail500Adapter()).input(&input)
      | loka::dsl::Step(2, FlowTestCountedPassAdapter(&failureActionCalls)).input(&input);
  failureChain.onFailure(&FlowMatchTestHelpers::countAsHandledFailure, &failureCalls, 2);

  loka::dsl::MatchSpec<int, int> match = loka::dsl::Match<int, int>(1);
  match.arm(&FlowMatchTestHelpers::alwaysMatch, &matchCalls, &FlowMatchTestHelpers::countAsValue);
  loka::dsl::FlowChain<int, int> matchChain =
      loka::dsl::Flow() | loka::dsl::Step(0, FlowTestAdd1Adapter()).input(&input) | match
      | loka::dsl::Step(2, FlowTestCountedPassAdapter(&matchActionCalls));

  const loka::dsl::FlowRunResult failureResult = failureChain.runResult();
  const loka::dsl::FlowRunResult matchResult = matchChain.runResult();
  LOKA_VERIFY(failureResult == loka::dsl::FLOW_RUN_SUCCEEDED);
  LOKA_VERIFY(matchResult == failureResult);
  LOKA_VERIFY(failureCalls == 1);
  LOKA_VERIFY(matchCalls == 1);
  LOKA_VERIFY(failureActionCalls == 1);
  LOKA_VERIFY(matchActionCalls == 1);
}

void testFlowMatchAuditWritesExactMatchAndSubstepLinesOnce()
{
  const char *path = "_loka_flow_match_audit.audit";
  std::remove(path);
  {
    loka::platform::file::FileHandle destination;
    destination.displayPath = loka::core::String::Literal(path);
    loka::dsl::testing::ScenarioAuditFile audit(destination, "flow-match");
    loka::dsl::FlowError auditError;
    loka::dsl::testing::scenario_audit_detail::MatchSelectionEmitter selected(&audit, 12, 1);
    const bool selectedFirst = selected.emit(auditError);
    const bool selectedAgain = selected.emit(auditError);
    LOKA_VERIFY(selectedFirst);
    LOKA_VERIFY(selectedAgain);

    loka::dsl::FlowError childError;
    childError.kind = 7;
    childError.code = 42;
    loka::dsl::testing::scenario_audit_detail::SubstepTerminalEmitter substep(
        &audit, 12, 1, 101, "child step\tA", 20);
    const bool substepFirst = substep.emit(21, loka::dsl::FLOW_STEP_FAILED, childError);
    const bool substepAgain = substep.emit(21, loka::dsl::FLOW_STEP_FAILED, childError);
    LOKA_VERIFY(substepFirst);
    LOKA_VERIFY(substepAgain);

    loka::dsl::testing::scenario_audit_detail::MatchSelectionEmitter unmatched(&audit, 13);
    const bool unmatchedFirst = unmatched.emit(auditError);
    const bool unmatchedAgain = unmatched.emit(auditError);
    LOKA_VERIFY(unmatchedFirst);
    LOKA_VERIFY(unmatchedAgain);
  }

  const std::string content = ReadFlowMatchAuditBytes(path);
  std::remove(path);
  LOKA_VERIFY(content == "loka_scenario_audit version=1 scenario=flow-match\n"
                         "match id=12 arm=1\n"
                         "substep match=12 arm=1 id=101 due_tick=20 tick=21 status=failed error_kind=7 error_code=42 name=child%20step%09A\n"
                         "match id=13 arm=none\n");
}

#include "RetiredFlowParticipationTests.hpp"
#include "support/LifecycleFactTestAccess.hpp"

namespace
{
  typedef loka::dsl::FlowChain<int, int> ParticipantFlow;
  typedef loka::app::scene::LifecycleFactTestAccess RetirementAccess;
  class ParticipantFlowNode : public CharacterizationBoundaryBase
  {
  public:
    ParticipantFlowNode() : flow(*this), stream(*this) {}
    loka::app::scene::FlowSlot<ParticipantFlow> flow;
    loka::app::scene::FlowSlot<loka::dsl::StateStream<int> > stream;
  };
  struct ParticipantAdapter
  {
    typedef int In;
    typedef int Out;
    int *calls;
    explicit ParticipantAdapter(int &count) : calls(&count) {}
    loka::dsl::StepRunStatus run(const int &in, int &out, loka::dsl::FlowError &) const
    { ++*this->calls; out = in + 1; return loka::dsl::FLOW_STEP_SUCCEEDED; }
  };
  ParticipantFlow participantFlow(int &calls, int &output)
  { return loka::dsl::Flow() | loka::dsl::Step(1, ParticipantAdapter(calls)).onSuccess(&output); }

  struct ReplaceAndRetire
  {
    typedef int In;
    typedef int Out;
    ParticipantFlowNode *node;
    loka::core::MutableState<int> *source;
    int *calls;
    int *output;
    ReplaceAndRetire(ParticipantFlowNode &n, loka::core::MutableState<int> &s, int &c, int &o)
        : node(&n), source(&s), calls(&c), output(&o) {}
    loka::dsl::StepRunStatus run(const int &in, int &out, loka::dsl::FlowError &) const
    {
      ++*this->calls;
      this->node->flow.set(participantFlow(*this->calls, *this->output)).bindTrigger(this->source);
      RetirementAccess::MarkSubtreeRetired(this->node);
      // The step may finish and read its pinned trigger input after withdrawal.
      out = in + 1;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }
  };
  class ParticipantStreamOwner : public CharacterizationStateOwner
  {
  public:
    ParticipantStreamOwner() : withdrawals(0) {}
    int withdrawals;
    virtual void withdrawState(loka::core::StateBase *state)
    { ++this->withdrawals; this->pushTracker()->removeState(state); }
  };
}

void testRetiredFlowWithdrawsSharedImpl()
{
  loka::core::MutableState<int> source(0);
  loka::core::PushStateTracker tracker;
  ParticipantFlowNode node;
  int calls = 0, output = 0;
  node.flow.set(participantFlow(calls, output)).bindTrigger(&source);
  ParticipantFlow shared(node.flow.dangerouslyUnwrap());
  { loka::core::StateTrackerGuard guard(&tracker); source.set(1); }
  LOKA_VERIFY(calls == 1 && output == 2);
  RetirementAccess::MarkSubtreeRetired(&node);
  { loka::core::StateTrackerGuard guard(&tracker); source.set(2); }
  LOKA_VERIFY(calls == 1 && output == 2);
}

void testRetiredFlowWithdrawsReplacedRunningResident()
{
  loka::core::MutableState<int> source(0);
  loka::core::PushStateTracker tracker;
  ParticipantFlowNode node;
  int calls = 0, output = 0;
  node.flow.set(loka::dsl::Flow() | loka::dsl::Step(1, ReplaceAndRetire(node, source, calls, output))
                .onSuccess(&output)).bindTrigger(&source);
  // Keep the old implementation alive beyond run end, so destruction cannot
  // conceal a missed withdrawal of the slot's deferred resident.
  ParticipantFlow oldResident(node.flow.dangerouslyUnwrap());
  { loka::core::StateTrackerGuard guard(&tracker); source.set(3); }
  LOKA_VERIFY(calls == 1 && output == 4);
  { loka::core::StateTrackerGuard guard(&tracker); source.set(5); }
  LOKA_VERIFY(calls == 1 && output == 4);
}

void testRetiredStreamChainDisconnectsAndReleasesOnce()
{
  loka::core::MutableState<int> *source = new loka::core::MutableState<int>(0);
  ParticipantStreamOwner owner;
  int calls = 0;
  {
    ParticipantFlowNode node;
    node.stream.set(loka::dsl::StateStream<int>(source, owner.tracker(), &owner)
      .map(ChainAddOneMapper(&calls)).map(ChainAddOneMapper(&calls)).map(ChainAddOneMapper(&calls)));
    LOKA_VERIFY(calls == 3);
    RetirementAccess::MarkSubtreeRetired(&node);
    const int releasesBeforeDestruction = owner.releaseCalls();
    LOKA_VERIFY(owner.withdrawals == 3 && releasesBeforeDestruction == 0);
    { loka::core::StateTrackerGuard guard(owner.tracker()); source->set(3); }
    LOKA_VERIFY(calls == 3);
    // The source dies before deferred stream destruction: stale source fields
    // would make releaseOwnedState unbind through freed memory (ASan pin).
    delete source;
  }
  const int releasesAfterDestruction = owner.releaseCalls();
  LOKA_VERIFY(releasesAfterDestruction == 3);
}

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define LOKA_FLOW_SLOT_PIN_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define LOKA_FLOW_SLOT_PIN_ASAN 1
#endif
#if defined(__linux__) && defined(LOKA_LIFECYCLE_AUDIT) && !defined(NDEBUG) && !defined(LOKA_FLOW_SLOT_PIN_ASAN)
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif

void testFlowParticipantOwnerDiesBeforeHolder()
{
#if defined(NDEBUG) || (defined(__linux__) && defined(LOKA_LIFECYCLE_AUDIT) && !defined(LOKA_FLOW_SLOT_PIN_ASAN))
#if !defined(NDEBUG)
  const pid_t child = fork();
  LOKA_VERIFY(child >= 0);
  if (child == 0)
#endif
  {
    CharacterizationBoundaryBase *node = new CharacterizationBoundaryBase();
    loka::app::scene::FlowSlot<ParticipantFlow> *holder =
        new loka::app::scene::FlowSlot<ParticipantFlow>(*node);
    const loka::app::scene::ComposableNode *before =
        loka::dsl::testing::FlowSlotTestAccess::owner(*holder);
    LOKA_VERIFY(before == node);
    delete node;
    const loka::app::scene::ComposableNode *after =
        loka::dsl::testing::FlowSlotTestAccess::owner(*holder);
    LOKA_VERIFY(after == 0);
    delete holder;
#if !defined(NDEBUG)
    _exit(0);
#endif
  }
#if !defined(NDEBUG)
  int status = 0;
  const pid_t waited = waitpid(child, &status, 0);
  LOKA_VERIFY(waited == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#endif
#else
  std::fprintf(stderr, "[skip] node-first FlowSlot destruction needs NDEBUG or Linux audit Debug without ASan\n");
#endif
}
#undef LOKA_FLOW_SLOT_PIN_ASAN

void testFlowParticipantMemberUnlinksBeforeBaseDestruction()
{
  loka::core::MutableState<int> source(0);
  loka::core::PushStateTracker tracker;
  int calls = 0, output = 0;
  {
    ParticipantFlowNode node;
    node.flow.set(participantFlow(calls, output)).bindTrigger(&source);
    { loka::core::StateTrackerGuard guard(&tracker); source.set(1); }
    LOKA_VERIFY(calls == 1);
  }
  { loka::core::StateTrackerGuard guard(&tracker); source.set(2); }
  LOKA_VERIFY(calls == 1);
}

namespace
{
  void attemptRetiredFlowRearm(int door)
  {
    loka::core::MutableState<int> source(0);
    loka::core::PushStateTracker tracker;
    ParticipantFlowNode node;
    int calls = 0, output = 0;
    node.flow.set(participantFlow(calls, output)).bindTrigger(&source);
    RetirementAccess::MarkSubtreeRetired(&node);
    const ParticipantFlow *installed = node.flow.get();
    if (door == 0)
    {
      node.flow.set(participantFlow(calls, output));
      LOKA_VERIFY(node.flow.get() == installed);
    }
    else if (door == 1) node.flow.bindTrigger(&source);
    else { const bool ran = node.flow.run(); LOKA_VERIFY(!ran); }
    { loka::core::StateTrackerGuard guard(&tracker); source.set(1); }
    LOKA_VERIFY(calls == 0 && output == 0);
  }
}
void testRetiredFlowRefusesRearm()
{
  for (int door = 0; door < 3; ++door)
    attemptRetiredFlowRearm(door);
}

void testRetiredStreamBroadOwnerStopsEvaluation()
{
  loka::core::MutableState<int> source(0);
  CharacterizationBoundaryBase owner;
  owner.tracker()->asPushTracker()->addState(&source);
  int calls = 0;
  {
    ParticipantFlowNode node;
    node.stream.set(loka::dsl::StateStream<int>(&source, owner.tracker(), &owner)
                    .map(ChainAddOneMapper(&calls)));
    const int initial = calls;
    { loka::core::StateTrackerGuard guard(owner.tracker()); source.set(1); }
    LOKA_VERIFY(calls > initial);
    RetirementAccess::MarkSubtreeRetired(&node);
    const int before = calls;
    // #948 leaves no tracker dependency edges on stream-derived States.
    // This broad owner deliberately declines withdrawState: only the slot's
    // binding withdrawal can stop the stream's remaining evaluation route.
    { loka::core::StateTrackerGuard guard(owner.tracker()); source.set(2); }
    std::fprintf(stderr, "932 broad owner: calls before=%d after=%d\n", before, calls);
    LOKA_VERIFY(calls == before);
  }
  owner.tracker()->asPushTracker()->removeState(&source);
}

namespace
{
  struct StreamCountExpr
  {
    int *calls;
    explicit StreamCountExpr(int *count) : calls(count) {}
    int eval(const loka::dsl::EvalContext &context) const
    {
      ++*this->calls;
      return *static_cast<int *>(context.slots[1]) + 1;
    }
  };

  struct StreamCountSum
  {
    typedef int Result;
    int *calls;
    explicit StreamCountSum(int *count) : calls(count) {}
    int operator()(int left, int right) const
    {
      ++*this->calls;
      return left + right;
    }
  };

  void verifyStreamSingleEvaluator(bool chain, bool crossTracker, bool expression)
  {
    loka::core::MutableState<int> input(1), output(0);
    CharacterizationStateOwner owner;
    loka::core::PushStateTracker sourceTracker;
    if (crossTracker)
      sourceTracker.addState(&input);
    else
      owner.trackBorrowed(&input);
    owner.trackBorrowed(&output);
    loka::app::scene::NodeState<int> target(&output, owner.tracker(), &owner);
    int firstCalls = 0, secondCalls = 0;
    {
      loka::dsl::StateStream<int> source(&input, owner.tracker(), &owner);
      loka::dsl::StateStream<int> result;
      if (expression)
        result = source.map(loka::dsl::Expr<int, StreamCountExpr>(StreamCountExpr(&firstCalls)));
      else
        result = source.map(ChainAddOneMapper(&firstCalls));
      if (chain)
        result = result.map(ChainAddOneMapper(&secondCalls));
      result.set(target);
      assert(firstCalls == 1);
      assert(secondCalls == (chain ? 1 : 0));
      for (int write = 1; write <= 2; ++write)
      {
        {
          loka::core::StateTrackerGuard ownerGuard(owner.tracker());
          loka::core::StateTrackerGuard sourceGuard(crossTracker ? &sourceTracker : owner.tracker());
          input.set(write + 10);
          assert(output.get() == write + 11 + (chain ? 1 : 0));
        }
        assert(firstCalls == write + 1);
        assert(secondCalls == (chain ? write + 1 : 0));
      }
    }
    {
      loka::core::StateTrackerGuard ownerGuard(owner.tracker());
      loka::core::StateTrackerGuard sourceGuard(crossTracker ? &sourceTracker : owner.tracker());
      input.set(99);
    }
    assert(firstCalls == 3);
    assert(secondCalls == (chain ? 3 : 0));
    if (crossTracker)
      sourceTracker.removeState(&input);
  }

  struct StreamProjectedEval : loka::core::DerivedState<int>::EvalFn
  {
    loka::core::State<int> *source;
    explicit StreamProjectedEval(loka::core::State<int> *value) : source(value) {}
    int operator()() { return this->source->get() * 2; }
  };
}

void testStateStreamSingleEvaluatorMap() { verifyStreamSingleEvaluator(false, false, false); }
void testStateStreamSingleEvaluatorChain() { verifyStreamSingleEvaluator(true, false, false); }
void testStateStreamSingleEvaluatorCrossTracker() { verifyStreamSingleEvaluator(false, true, false); }
void testStateStreamSingleEvaluatorExpr() { verifyStreamSingleEvaluator(false, false, true); }

void testStateStreamSingleEvaluatorCombine()
{
  loka::core::MutableState<int> left(1), right(10), output(0);
  CharacterizationStateOwner owner;
  owner.trackBorrowed(&left);
  owner.trackBorrowed(&right);
  owner.trackBorrowed(&output);
  loka::app::scene::NodeState<int> target(&output, owner.tracker(), &owner);
  int calls = 0;
  loka::dsl::StateStream<int> a(&left, owner.tracker(), &owner);
  loka::dsl::StateStream<int> b(&right, owner.tracker(), &owner);
  loka::dsl::StateStream<int> combined = a.combine(b, StreamCountSum(&calls));
  combined.set(target);
  assert(calls == 1);
  {
    loka::core::StateTrackerGuard guard(owner.tracker());
    left.set(2);
    assert(output.get() == 12);
  }
  assert(calls == 2);
  {
    loka::core::StateTrackerGuard guard(owner.tracker());
    right.set(20);
    assert(output.get() == 22);
  }
  assert(calls == 3);
}

void testStateStreamSingleEvaluatorProjectedReader()
{
  // The public output door is a mutable NodeState, not the private stream State.
  loka::core::MutableState<int> input(1), output(0);
  CharacterizationStateOwner owner;
  owner.trackBorrowed(&input);
  owner.trackBorrowed(&output);
  loka::app::scene::NodeState<int> target(&output, owner.tracker(), &owner);
  int calls = 0;
  loka::dsl::StateStream<int> source(&input, owner.tracker(), &owner);
  loka::dsl::StateStream<int> mapped = source.map(ChainAddOneMapper(&calls));
  mapped.set(target);
  loka::core::DerivedState<int> *reader =
      new loka::core::DerivedState<int>(&output, new StreamProjectedEval(&output));
  owner.adoptState(reader);
  {
    loka::core::StateTrackerGuard guard(owner.tracker());
    input.set(5);
    assert(target.get() == 6);
  }
  assert(reader->get() == 12);
  owner.releaseState(reader);
}

namespace
{
  struct OnceCountAdapter
  {
    typedef int In;
    typedef int Out;
    int *calls;
    loka::dsl::StepRunStatus terminal;
    int pendingPolls;

    OnceCountAdapter(int *count, loka::dsl::StepRunStatus result, int polls = 0)
        : calls(count),
          terminal(result),
          pendingPolls(polls)
    {
    }

    loka::dsl::StepRunStatus run(const int &in, int &out, loka::dsl::FlowError &error) const
    {
      ++*this->calls;
      if (*this->calls <= this->pendingPolls)
        return loka::dsl::FLOW_STEP_PENDING;
      out = in + *this->calls;
      error.kind = 71;
      error.code = 93;
      return this->terminal;
    }
  };

  struct OnceDiagnosticAdapter
  {
    typedef loka::app::scene::Scene *In;
    typedef loka::app::scene::Scene *Out;
    int *calls;
    const std::string *message;
    OnceDiagnosticAdapter(int *count, const std::string *text)
        : calls(count),
          message(text)
    {
    }
    const char *diagnostic() const
    {
      return this->message->c_str();
    }
    loka::dsl::StepRunStatus run(const In &in, Out &out, loka::dsl::FlowError &error) const
    {
      ++*this->calls;
      out = in;
      error.kind = 71;
      error.code = 93;
      return loka::dsl::FLOW_STEP_FAILED;
    }
  };

  struct OnceInheritedDiagnosticAdapter : OnceDiagnosticAdapter
  {
    OnceInheritedDiagnosticAdapter(int *count, const std::string *text) : OnceDiagnosticAdapter(count, text) {}
  };

  struct OnceRecursiveAdapter
  {
    typedef int In;
    typedef int Out;
    typedef loka::dsl::testing::RunOnceAdapter<OnceRecursiveAdapter> Wrapper;
    Wrapper **wrapper;
    int *calls;
    OnceRecursiveAdapter(Wrapper **value, int *count)
        : wrapper(value),
          calls(count)
    {
    }
    loka::dsl::StepRunStatus run(const int &in, int &out, loka::dsl::FlowError &error) const
    {
      ++*this->calls;
      // Bound the negative control so removing the entered guard fails, rather than overflowing.
      if (*this->calls == 1)
      {
        LOKA_VERIFY((*this->wrapper)->run(in, out, error) == loka::dsl::FLOW_STEP_PENDING);
      }
      out = in;
      return loka::dsl::FLOW_STEP_SUCCEEDED;
    }
  };
} // namespace

void testSceneRunOnceCachesSuccessAndFailure()
{
  using namespace loka::dsl;
  using namespace loka::dsl::testing;
  int input = 10;
  int output = 0;
  int successCalls = 0;
  FlowChain<int, int> success =
      Flow() | Step(1, RunOnce(OnceCountAdapter(&successCalls, FLOW_STEP_SUCCEEDED))).input(&input).onSuccess(&output);
  for (int i = 0; i < 5; ++i)
  {
    LOKA_VERIFY(success.runResult() == FLOW_RUN_SUCCEEDED);
    LOKA_VERIFY(output == 11);
    ++input;
  }
  LOKA_VERIFY(successCalls == 1);

  int failureCalls = 0;
  FlowChain<int, int> failure =
      Flow() | Step(1, RunOnce(OnceCountAdapter(&failureCalls, FLOW_STEP_FAILED))).input(&input);
  for (int i = 0; i < 5; ++i)
    LOKA_VERIFY(failure.runResult() == FLOW_RUN_FAILED);
  LOKA_VERIFY(failureCalls == 1);

  int directCalls = 0;
  RunOnceAdapter<OnceCountAdapter> failed = RunOnce(OnceCountAdapter(&directCalls, FLOW_STEP_FAILED));
  for (int i = 0; i < 5; ++i)
  {
    FlowError error;
    LOKA_VERIFY(failed.run(input, output, error) == FLOW_STEP_FAILED);
    LOKA_VERIFY(error.kind == 71 && error.code == 93);
  }
  LOKA_VERIFY(directCalls == 1);
  RunOnceAdapter<OnceCountAdapter> copied(failed);
  FlowError error;
  LOKA_VERIFY(copied.run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(directCalls == 1);
  LOKA_VERIFY(RunOnce(OnceCountAdapter(&directCalls, FLOW_STEP_FAILED)).run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(directCalls == 2);
}

void testSceneRunOncePollsPendingUntilTerminal()
{
  using namespace loka::dsl;
  using namespace loka::dsl::testing;
  for (int fails = 0; fails < 2; ++fails)
  {
    int input = 10;
    int calls = 0;
    int output = 0;
    FlowChain<int, int> chain =
        Flow()
        | Step(1, RunOnce(OnceCountAdapter(&calls, fails ? FLOW_STEP_FAILED : FLOW_STEP_SUCCEEDED, 2)))
              .input(&input)
              .onSuccess(&output);
    LOKA_VERIFY(chain.runResult() == FLOW_RUN_PENDING);
    LOKA_VERIFY(calls == 1);
    LOKA_VERIFY(chain.runResult() == FLOW_RUN_PENDING);
    LOKA_VERIFY(calls == 2);
    for (int i = 0; i < 5; ++i)
      LOKA_VERIFY(chain.runResult() == (fails ? FLOW_RUN_FAILED : FLOW_RUN_SUCCEEDED));
    LOKA_VERIFY(calls == 3);
    LOKA_VERIFY(output == (fails ? 0 : 13));
  }
}

void testSceneRunOnceMarksEntryBeforeCallingAdapter()
{
  using namespace loka::dsl;
  int calls = 0;
  OnceRecursiveAdapter::Wrapper *borrow = 0;
  OnceRecursiveAdapter::Wrapper wrapper = testing::RunOnce(OnceRecursiveAdapter(&borrow, &calls));
  borrow = &wrapper;
  int output = 0;
  FlowError error;
  LOKA_VERIFY(wrapper.run(42, output, error) == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(calls == 1 && output == 42);
}

void testSceneSettlePollsBothQueuesAndRejectsMissingSceneOrController()
{
  using namespace loka::app::scene;
  using namespace loka::dsl;
  using namespace loka::dsl::testing;
  FlowScenePlatformController platform;
  Scene scene((BoundaryDefinition<PendingCompositedProbeBoundaryProps, PendingCompositedProbeBoundaryNode>()));
  Scene *input = 0;
  Scene *output = 0;
  FlowError error;
  LOKA_VERIFY(Settle().run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(error.kind == FLOW_ERROR_KIND_SCENE_SCENARIO && error.code == FLOW_ERROR_SCENE_TEST_NULL_SCENE);
  input = &scene;
  LOKA_VERIFY(Settle().run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(error.kind == FLOW_ERROR_KIND_SCENE_SCENARIO
              && error.code == FLOW_ERROR_SCENE_TEST_CONTROLLER_UNAVAILABLE);
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  LOKA_VERIFY(Settle().run(input, output, error) == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(output == input);
  scene.requestInvalidate(NODE_DIRTY_PROPS);
  const int applies = platform.calls_;
  const int syncs = platform.synchronizeCalls_;
  for (int i = 0; i < 5; ++i)
    LOKA_VERIFY(Settle().run(input, output, error) == FLOW_STEP_PENDING);
  LOKA_VERIFY(platform.calls_ == applies && platform.synchronizeCalls_ == syncs);
  LOKA_VERIFY(scene.flushInvalidation());
  LOKA_VERIFY(Settle().run(input, output, error) == FLOW_STEP_SUCCEEDED);
  platform.pendingSync_ = true;
  LOKA_VERIFY(Settle().run(input, output, error) == FLOW_STEP_PENDING);
  LOKA_VERIFY(platform.pendingSync_);
  platform.synchronize();
  LOKA_VERIFY(Settle().run(input, output, error) == FLOW_STEP_SUCCEEDED);
  SceneTestAccess::unmount(scene);
  LOKA_VERIFY(Settle().run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(error.code == FLOW_ERROR_SCENE_TEST_CONTROLLER_UNAVAILABLE);
}

void testSceneDefaultClickRejectsDisabledButtonAndExplicitNoopSucceeds()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::dsl;
  using namespace loka::dsl::testing;
  FlowScenePlatformController platform;
  loka::core::MutableState<bool> enabled(false);
  loka::core::EmitterState click;
  int calls = 0;
  click.bind(&incrementNotificationCount, &calls, false);
  NodeComposition composition;
  BoxDefinition root = Box().testId("ClickRegion");
  root << Button("Run").enabled(&enabled).onClick(&click).testId("StrictButton");
  root << Cell("Cell").onClick(&click).testId("StrictCell");
  composition.declare(root);
  NodeDefinitionBase *definition = composition.root()->clone();
  LOKA_VERIFY(definition != 0);
  Scene scene(definition);
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  Scene *input = &scene;
  Scene *output = 0;
  FlowError error;
  const NodeSelector<ButtonNode> selector = Within("ClickRegion").descendant<ButtonNode>(1);
  LOKA_VERIFY(EmitNodeClick<ButtonNode>(input, scene_test_detail::NodeTarget<ButtonNode>(selector), error)
              == FLOW_STEP_FAILED);
  LOKA_VERIFY(error.kind == FLOW_ERROR_KIND_SCENE_SCENARIO && error.code == FLOW_ERROR_SCENE_TEST_BUTTON_DISABLED);
  LOKA_VERIFY(ClickButtonByIdAdapter("StrictButton").run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(error.kind == FLOW_ERROR_KIND_SCENE_SCENARIO && error.code == FLOW_ERROR_SCENE_TEST_BUTTON_DISABLED);
  LOKA_VERIFY(ClickButtonByIdAdapter(selector).run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(error.kind == FLOW_ERROR_KIND_SCENE_SCENARIO && error.code == FLOW_ERROR_SCENE_TEST_BUTTON_DISABLED);
  LOKA_VERIFY(ClickButtonById("StrictButton").run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(error.kind == FLOW_ERROR_KIND_SCENE_SCENARIO && error.code == FLOW_ERROR_SCENE_TEST_BUTTON_DISABLED);
  LOKA_VERIFY(ClickButtonById(selector).run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(error.kind == FLOW_ERROR_KIND_SCENE_SCENARIO && error.code == FLOW_ERROR_SCENE_TEST_BUTTON_DISABLED);
  LOKA_VERIFY(ClickButtonByIdAndFlushAdapter("StrictButton").run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(error.kind == FLOW_ERROR_KIND_SCENE_SCENARIO && error.code == FLOW_ERROR_SCENE_TEST_BUTTON_DISABLED);
  LOKA_VERIFY(ClickButtonByIdAndFlushAdapter(selector).run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(error.kind == FLOW_ERROR_KIND_SCENE_SCENARIO && error.code == FLOW_ERROR_SCENE_TEST_BUTTON_DISABLED);
  LOKA_VERIFY(ClickButtonByIdAndFlush("StrictButton").run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(error.kind == FLOW_ERROR_KIND_SCENE_SCENARIO && error.code == FLOW_ERROR_SCENE_TEST_BUTTON_DISABLED);
  LOKA_VERIFY(ClickButtonByIdAndFlush(selector).run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(error.kind == FLOW_ERROR_KIND_SCENE_SCENARIO && error.code == FLOW_ERROR_SCENE_TEST_BUTTON_DISABLED);
  LOKA_VERIFY(ClickButton("StrictButton").run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(error.kind == FLOW_ERROR_KIND_SCENE_SCENARIO && error.code == FLOW_ERROR_SCENE_TEST_BUTTON_DISABLED);
  LOKA_VERIFY(ClickButton(selector).run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(error.kind == FLOW_ERROR_KIND_SCENE_SCENARIO && error.code == FLOW_ERROR_SCENE_TEST_BUTTON_DISABLED);
  LOKA_VERIFY(calls == 0);
  LOKA_VERIFY(EmitNodeClick<ButtonNode>(
                  input, scene_test_detail::NodeTarget<ButtonNode>(selector), error, CLICK_DISABLED_IS_NOOP)
              == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(ClickButtonByIdAdapter("StrictButton", CLICK_DISABLED_IS_NOOP).run(input, output, error)
              == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(ClickButtonByIdAdapter(selector, CLICK_DISABLED_IS_NOOP).run(input, output, error)
              == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(ClickButtonById("StrictButton", CLICK_DISABLED_IS_NOOP).run(input, output, error) == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(ClickButtonById(selector, CLICK_DISABLED_IS_NOOP).run(input, output, error) == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(ClickButtonByIdAndFlushAdapter("StrictButton", CLICK_DISABLED_IS_NOOP).run(input, output, error)
              == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(ClickButtonByIdAndFlushAdapter(selector, CLICK_DISABLED_IS_NOOP).run(input, output, error)
              == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(ClickButtonByIdAndFlush("StrictButton", CLICK_DISABLED_IS_NOOP).run(input, output, error)
              == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(ClickButtonByIdAndFlush(selector, CLICK_DISABLED_IS_NOOP).run(input, output, error)
              == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(ClickButton("StrictButton", CLICK_DISABLED_IS_NOOP).run(input, output, error) == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(ClickButton(selector, CLICK_DISABLED_IS_NOOP).run(input, output, error) == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(calls == 0);
  {
    loka::core::StateTrackerGuard guard(SceneTestAccess::rootBoundary(scene)->tracker());
    enabled.set(true);
  }
  LOKA_VERIFY(ClickButton("StrictButton", CLICK_DISABLED_FAILS).run(input, output, error) == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(calls == 1);
  LOKA_VERIFY(ClickCell("StrictCell").run(input, output, error) == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(calls == 2);
  SceneTestAccess::unmount(scene);
  click.unbind(&incrementNotificationCount, &calls);
}

void testSceneRunOnceAuditOwnsEscapedBoundedMessage()
{
  using namespace loka::dsl;
  using namespace loka::dsl::testing;
  const char *path = "_loka_once_message.audit";
  std::remove(path);
  int calls = 0;
  std::string text = "bad \t\n%= " + std::string(300, 'x');
  const std::string bounded = text.substr(0, 253) + "...";
  loka::app::scene::Scene *input = 0;
  loka::app::scene::Scene *output = 0;
  FlowError error;
  RunOnceAdapter<OnceDiagnosticAdapter> once = RunOnce(OnceDiagnosticAdapter(&calls, &text));
  LOKA_VERIFY(once.run(input, output, error) == FLOW_STEP_FAILED);
  int inheritedCalls = 0;
  RunOnceAdapter<OnceInheritedDiagnosticAdapter> inherited =
      RunOnce(OnceInheritedDiagnosticAdapter(&inheritedCalls, &text));
  LOKA_VERIFY(inherited.run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(std::string(inherited.diagnostic()) == bounded);
  text = "replaced after failure";
  LOKA_VERIFY(once.run(input, output, error) == FLOW_STEP_FAILED);
  LOKA_VERIFY(std::string(once.diagnostic()) == bounded);
  // The detector answers by name lookup, not by a sizeof of the call: that form said
  // every adapter had diagnostic() on GCC 4.2 and Apple clang up to Xcode 10 (#1248).
  typedef char HasDiagnosticSeesDeclared[scenario_step_detail::HasDiagnostic<OnceDiagnosticAdapter>::value ? 1 : -1];
  typedef char
      HasDiagnosticSeesInherited[scenario_step_detail::HasDiagnostic<OnceInheritedDiagnosticAdapter>::value ? 1 : -1];
  typedef char HasDiagnosticRefusesPlain[scenario_step_detail::HasDiagnostic<OnceRecursiveAdapter>::value ? -1 : 1];
  (void)sizeof(HasDiagnosticSeesDeclared);
  (void)sizeof(HasDiagnosticSeesInherited);
  (void)sizeof(HasDiagnosticRefusesPlain);
  // waitUntil wraps the action; the wrapper must forward its diagnostic (#1038 review).
  ProjectionWaitAdapter<RunOnceAdapter<OnceDiagnosticAdapter> > wait(once, ProjectionEvent::APPLIED);
  const char *forwarded =
      scenario_step_detail::Diagnostic<ProjectionWaitAdapter<RunOnceAdapter<OnceDiagnosticAdapter> > >::get(wait);
  LOKA_VERIFY(forwarded && std::string(forwarded) == bounded);
  {
    loka::platform::file::FileHandle destination;
    destination.displayPath = loka::core::String::Literal(path);
    ScenarioAuditFile audit(destination, "once-message");
    ScenarioClock clock;
    FlowChain<loka::app::scene::Scene *, loka::app::scene::Scene *> chain =
        (ScenarioFlow(clock, &input).auditTo(&audit) | Then(once).named("fail one")).flow();
    for (int i = 0; i < 5; ++i)
    {
      clock.advanceTo(i);
      LOKA_VERIFY(chain.runResult() == FLOW_RUN_FAILED);
    }
    LOKA_VERIFY(calls == 1);
    std::string shortMessage = "owned";
    ScenarioStepTerminal record(2, "plain", 0, 0, FLOW_STEP_FAILED, error, shortMessage.c_str());
    shortMessage = "changed";
    LOKA_VERIFY(record.message() == "owned");
    LOKA_VERIFY(audit.recordStep(ScenarioStepTerminal(2, "plain", 0, 0, FLOW_STEP_FAILED, error)));
    LOKA_VERIFY(audit.recordStep(ScenarioStepTerminal(3, "empty", 0, 0, FLOW_STEP_FAILED, error, "")));
    LOKA_VERIFY(ScenarioStepTerminal(4, "limit", 0, 0, FLOW_STEP_FAILED, error, std::string(256, 'a').c_str()).message()
                == std::string(256, 'a'));
    LOKA_VERIFY(ScenarioStepTerminal(4, "limit", 0, 0, FLOW_STEP_FAILED, error, std::string(257, 'a').c_str()).message()
                == std::string(253, 'a') + "...");
  }
  const std::string content = ReadFlowMatchAuditBytes(path);
  std::remove(path);
  const std::string expected = "loka_scenario_audit version=1 scenario=once-message\n"
                               "step id=1 due_tick=0 tick=0 status=failed error_kind=71 error_code=93 name=fail%20one "
                               "message=bad%20%09%0A%25%3D%20"
                               + std::string(244, 'x')
                               + "...\n"
                                 "step id=2 due_tick=0 tick=0 status=failed error_kind=71 error_code=93 name=plain\n"
                                 "step id=3 due_tick=0 tick=0 status=failed error_kind=71 error_code=93 name=empty\n";
  LOKA_VERIFY(content == expected);
}

namespace
{
  struct SimpleViewerReadContext : FlowTestPlatformContext
  {
    SimpleViewerReadContext(const char *path, std::size_t limit, int refuseAfter = 0)
        : path_(path), limit_(limit), refuseAfter_(refuseAfter), queries_(0) {}
    virtual bool openFile(const loka::file::File &, loka::platform::file::FileHandle &out) const
    {
      out.displayPath = loka::core::String::Literal(this->path_);
      return true;
    }
    virtual bool queryLargestContiguousAllocation(std::size_t &out) const
    {
      ++this->queries_;
      out = this->refuseAfter_ && this->queries_ > this->refuseAfter_ ? 0 : this->limit_;
      return true;
    }
    const char *path_;
    std::size_t limit_;
    int refuseAfter_;
    mutable int queries_;
  };
}

void testSimpleViewerReadFailuresAndFallback()
{
  BlobAllocationProbe allocation;
  const char *path = "_loka_read_characterization.bin";
  FILE *file = std::fopen(path, "wb");
  assert(file != 0);
  LOKA_VERIFY(std::fwrite("abcd", 1, 4, file) == 4);
  LOKA_VERIFY(std::fclose(file) == 0);
  simpleviewer::ChooserProjection projection;
  projection.request.setFilePath(loka::core::String::Literal(path));
  projection.hasFileItem = true;
  projection.fileItem = loka::file::File::FromPath(loka::core::String::Literal(path));
  loka::core::resource::Blob output;
  loka::dsl::FlowError error;
  SimpleViewerReadContext missing("_loka_missing_read_file_", 4);
  simpleviewer::ProjectionToBlobAdapter fallback(&missing);
  LOKA_VERIFY(fallback.run(projection, output, error) == loka::dsl::FLOW_STEP_SUCCEEDED);
  assert(output.size() == 4 && output.data()[3] == 'd' && output.isCompleted());
  assert(missing.queries_ == 0);

  allocation.refuse("Blob", "Bytes");
  SimpleViewerReadContext small(path, 3);
  simpleviewer::ProjectionToBlobAdapter limited(&small);
  LOKA_VERIFY(limited.run(projection, output, error) == loka::dsl::FLOW_STEP_FAILED);
  assert(error.code == 1014 && output.size() == 0);
  assert(small.queries_ == 0 && "capacity refusal must not retry through stdio");

  SimpleViewerReadContext exact(path, 4);
  simpleviewer::ProjectionToBlobAdapter accepted(&exact);
  LOKA_VERIFY(accepted.run(projection, output, error) == loka::dsl::FLOW_STEP_SUCCEEDED);
  assert(output.size() == 4 && exact.queries_ == 0);
  projection.request.setFilePath(loka::core::String::Literal("_loka_missing_read_file_"));
  projection.fileItem = loka::file::File::FromPath("_loka_missing_read_file_");
  LOKA_VERIFY(fallback.run(projection, output, error) == loka::dsl::FLOW_STEP_FAILED);
  assert(error.code == 1011 && output.size() == 0);
  LOKA_VERIFY(std::remove(path) == 0);


}

#if defined(LOKA_FILE_READ_FAULT_PINS)
namespace
{
  enum ReadFault { READ_FAULT_NONE, READ_FAULT_SEEK, READ_FAULT_READ,
                   READ_FAULT_CHUNK, READ_FAULT_CHUNK_READ, READ_FAULT_CHUNK_SEEK,
                   READ_FAULT_END_SEEK, READ_FAULT_SHORT, READ_FAULT_OVERFLOW };
  ReadFault readFault = READ_FAULT_NONE;
  unsigned readCloses = 0;
  unsigned readCalls = 0;
  struct ReadFaultScope
  {
    explicit ReadFaultScope(ReadFault fault) { readFault = fault; readCalls = 0; }
    ~ReadFaultScope() { readFault = READ_FAULT_NONE; }
  };
}
extern "C" int __real_fclose(FILE *);
extern "C" int __wrap_fclose(FILE *file)
{
  ++readCloses;
  return __real_fclose(file);
}
extern "C" int __real_fseek(FILE *, long, int);
extern "C" long __real_ftell(FILE *);
extern "C" std::size_t __real_fread(void *, std::size_t, std::size_t, FILE *);
extern "C" int __real_ferror(FILE *);
extern "C" int __wrap_fseek(FILE *file, long offset, int origin)
{
  if ((readFault == READ_FAULT_SEEK || readFault == READ_FAULT_CHUNK_SEEK) && origin == SEEK_SET) return -1;
  if (readFault == READ_FAULT_END_SEEK && origin == SEEK_END) return -1;
  return __real_fseek(file, offset, origin);
}
extern "C" long __wrap_ftell(FILE *file)
{
  if (readFault == READ_FAULT_CHUNK || readFault == READ_FAULT_CHUNK_READ || readFault == READ_FAULT_CHUNK_SEEK || readFault == READ_FAULT_OVERFLOW) return -1;
  return __real_ftell(file);
}
extern "C" std::size_t __wrap_fread(void *out, std::size_t size, std::size_t count, FILE *file)
{
  if (readFault == READ_FAULT_OVERFLOW && ++readCalls == 2) return static_cast<std::size_t>(-1);
  if (readFault == READ_FAULT_READ || readFault == READ_FAULT_CHUNK_READ) return 0;
  if (readFault == READ_FAULT_SHORT) count /= 2;
  return __real_fread(out, size, count, file);
}
extern "C" int __wrap_ferror(FILE *file)
{
  if (readFault == READ_FAULT_READ || readFault == READ_FAULT_CHUNK_READ) return 1;
  return __real_ferror(file);
}
#endif

void testSimpleViewerReadStdioFaults()
{
  BlobAllocationProbe allocation;
#if defined(LOKA_FILE_READ_FAULT_PINS)
  const char *path = "_loka_stdio_faults.bin";
  FILE *file = std::fopen(path, "wb");
  assert(file != 0);
  unsigned char data[8193];
  std::memset(data, 0x5a, sizeof(data));
  LOKA_VERIFY(std::fwrite(data, 1, sizeof(data), file) == sizeof(data));
  LOKA_VERIFY(std::fclose(file) == 0);
  simpleviewer::ChooserProjection projection;
  projection.request.setFilePath(loka::core::String::Literal(path));
  loka::core::resource::Blob output;
  loka::dsl::FlowError error;
  simpleviewer::ProjectionToBlobAdapter adapter;
  {
    ReadFaultScope fault(READ_FAULT_SEEK);
    LOKA_VERIFY(adapter.run(projection, output, error) == loka::dsl::FLOW_STEP_FAILED);
    assert(error.code == 1012 && output.size() == 0);
  }
  {
    ReadFaultScope fault(READ_FAULT_CHUNK_SEEK);
    LOKA_VERIFY(adapter.run(projection, output, error) == loka::dsl::FLOW_STEP_FAILED);
    assert(error.code == 1012 && output.size() == 0);
  }
  {
    ReadFaultScope fault(READ_FAULT_END_SEEK);
    LOKA_VERIFY(adapter.run(projection, output, error) == loka::dsl::FLOW_STEP_SUCCEEDED);
    assert(output.size() == sizeof(data) && output.data()[8192] == 0x5a);
  }
  {
    ReadFaultScope fault(READ_FAULT_OVERFLOW);
    loka::core::resource::Blob scratch = loka::core::resource::Blob::Create();
    const unsigned closes = readCloses;
    LOKA_VERIFY(loka::platform::file::ReadBytes(loka::core::String::Literal(path), scratch)
                == loka::platform::file::READ_SIZE_OVERFLOW);
    LOKA_VERIFY(scratch.size() == 0 && readCloses == closes + 1);
  }
  {
    ReadFaultScope fault(READ_FAULT_SHORT);
    LOKA_VERIFY(adapter.run(projection, output, error) == loka::dsl::FLOW_STEP_SUCCEEDED);
    assert(output.size() == sizeof(data) / 2 && output.isCompleted());
  }
  {
    ReadFaultScope fault(READ_FAULT_READ);
    LOKA_VERIFY(adapter.run(projection, output, error) == loka::dsl::FLOW_STEP_FAILED);
    assert(error.code == 1013 && output.size() == 0);
  }
  {
    ReadFaultScope fault(READ_FAULT_CHUNK_READ);
    LOKA_VERIFY(adapter.run(projection, output, error) == loka::dsl::FLOW_STEP_FAILED);
    assert(error.code == 1013 && output.size() == 0);
  }
  {
    ReadFaultScope fault(READ_FAULT_CHUNK);
    LOKA_VERIFY(adapter.run(projection, output, error) == loka::dsl::FLOW_STEP_SUCCEEDED);
    assert(output.size() == sizeof(data) && output.data()[8192] == 0x5a);
    allocation.refuse("Blob", "Bytes", 1, 1);
    SimpleViewerReadContext context(path, static_cast<std::size_t>(-1), 1);
    simpleviewer::ProjectionToBlobAdapter limited(&context);
    LOKA_VERIFY(limited.run(projection, output, error) == loka::dsl::FLOW_STEP_FAILED);
    assert(error.code == 1014 && output.size() == 0 && allocation.refusals == 1);
  }
  {
    struct Ceiling : loka::platform::file::ReadCapacity
    {
      virtual bool allows(std::size_t n) const { return n <= 8447; }
    } ceiling;
    ReadFaultScope fault(READ_FAULT_CHUNK);
    loka::core::resource::Blob scratch = loka::core::resource::Blob::Create();
    LOKA_VERIFY(loka::platform::file::ReadBytes(loka::core::String::Literal(path), scratch, &ceiling)
                == loka::platform::file::READ_OK);
    LOKA_VERIFY(scratch.size() == 8193 && scratch.data()[8192] == 0x5a && !scratch.isCompleted());
    const unsigned closes = readCloses;
    allocation.refuse("Blob", "Bytes", 1, 1);
    LOKA_VERIFY(loka::platform::file::ReadBytes(loka::core::String::Literal(path), scratch, &ceiling)
                == loka::platform::file::READ_ALLOCATION_REFUSED);
    LOKA_VERIFY(scratch.size() == 0 && scratch.data() == 0 && readCloses == closes + 1);
  }
  {
    const char *sites[][2] = {{"Blob", "Record"}, {"Managed", "ControlBlock"}, {"Blob", "Bytes"}};
    output = loka::core::resource::Blob::Create();
    LOKA_VERIFY(output.tryAssign(data, 3));
    const loka::core::resource::Blob before = output;
    const loka::file::File input = loka::file::File::FromPath(path);
    for (unsigned i = 0; i < 3; ++i)
    {
      const unsigned closes = readCloses;
      allocation.refuse(sites[i][0], sites[i][1]);
      LOKA_VERIFY(loka::app::ReadFileImageBlob(0, input, output)
                  == loka::platform::file::READ_ALLOCATION_REFUSED);
      LOKA_VERIFY(output == before && output.size() == 3 && output.data()[2] == 0x5a);
      LOKA_VERIFY(readCloses == closes + (i == 2 ? 1 : 0));
    }
  }
  file = std::fopen(path, "wb");
  assert(file != 0);
  LOKA_VERIFY(std::fclose(file) == 0);
  LOKA_VERIFY(adapter.run(projection, output, error) == loka::dsl::FLOW_STEP_SUCCEEDED);
  assert(output.size() == 0 && output.isCompleted());
  LOKA_VERIFY(std::remove(path) == 0);
#else
  std::puts("[skip] stdio fault injection requires Linux linker wrapping");
#endif
}

#include "app/FileImageSource.hpp"

void testFileImageSourceRefusesUnresolvedApplicationFile()
{
  // An application-relative File that no context resolves has no path to
  // flatten: the read must refuse, not assert in File::toString (#1133).
  loka::core::resource::Blob blob = loka::core::resource::Blob::Create();
  LOKA_VERIFY(blob.tryResize(4));
  std::memset(blob.mutableData(), 'k', 4);
  const loka::core::resource::Blob before = blob;
  const loka::file::File absent = loka::file::File::Application() << loka::file::File("absent.pict");
  LOKA_VERIFY(loka::app::ReadFileImageBlob(0, absent, blob) == loka::platform::file::READ_NO_NATIVE_SPEC);
  assert(blob == before);
  assert(blob.size() == 4);
}
