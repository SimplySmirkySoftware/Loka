#ifndef LOKA_CORE2_SCENE_BOUNDARY_BOUNDARY_HPP
#define LOKA_CORE2_SCENE_BOUNDARY_BOUNDARY_HPP

#include <cstdlib>

#include "PaintBaselineStats.hpp"
#include <cstdarg>
#include <vector>
#include "core/diag/LifecycleAudit.hpp"
#include "app/scene/boundary/detail/BoundaryArena.hpp"
#include "app/scene/boundary/detail/NodeBuildTicket.hpp"
#include "app/scene/Node.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "app/scene/node/ComposableNode.hpp"
#include "app/scene/state/NodeState.hpp"
#include "app/scene/context/ComponentContext.hpp"
#include "app/scene/projection/PlatformApplyPlan.hpp"
#include "app/scene/boundary/BoundaryApplyInfo.hpp"
#include "app/scene/boundary/detail/BoundaryCompositionState.hpp"
#include "app/scene/boundary/detail/BoundaryObservedState.hpp"
#include "app/scene/boundary/detail/BoundaryRuntimeState.hpp"
#include "app/scene/boundary/detail/BoundaryParkedBranchLedger.hpp"
#include "app/scene/boundary/detail/BoundaryBranchSeatState.hpp"
#include "app/scene/boundary/detail/BranchSeatDeclaration.hpp"
#include "app/scene/boundary/PendingSubtree.hpp"
#include "app/scene/boundary/BoundaryStateTypes.hpp"
#include "core/StateTracker.hpp"
#include "core/util/StateUtil.hpp"
#include "core/Profiler.hpp"
#include "platform/debug/DebugLog.hpp"

namespace loka
{
  namespace dsl
  {
    namespace testing
    {
      class OwnershipDump;
      class PartitionReclaimAccess;
      class SeatBuildRequestAccess;
      class SceneTestAccess;
    }
  } // namespace dsl

  namespace app
  {
    namespace scene
    {
      class Scene;

      class RootBoundaryWrapper;

      // BoundaryNode: owns a local tracker for its subtree.
      class BoundaryNode : public ComposableNode, public IStateOwner LOKA_AUDITED_AS(BoundaryNode)
      {
      public:
        typedef BoundaryUpdateResult::BoundsHint LayoutBounds;
        typedef BoundaryLocalApplyPaintKind LocalApplyPaintKind;
        typedef BoundaryLocalApplyInfo LocalApplyInfo;
        static const LocalApplyPaintKind LOCAL_APPLY_PAINT_NONE = scene::LOCAL_APPLY_PAINT_NONE;
        static const LocalApplyPaintKind LOCAL_APPLY_PAINT_GENERIC = scene::LOCAL_APPLY_PAINT_GENERIC;
        static const LocalApplyPaintKind LOCAL_APPLY_PAINT_OPAQUE = scene::LOCAL_APPLY_PAINT_OPAQUE;
        static const LocalApplyPaintKind LOCAL_APPLY_PAINT_COMPOSITED = scene::LOCAL_APPLY_PAINT_COMPOSITED;
        BoundaryNode()
            : ComposableNode(),
              tracker_(),
              runtimeState_(),
              updateState_(),
              compositionState_(),
              observedState_(),
              parkedBranches_(),
              branchSeats_(),
              holdLedger_(this),
              pendingHeldReleasesHead_(0),
              pendingHeldReleasesTail_(0),
              retiredSubtreesHead_(0),
              retiredSubtreesTail_(0),
              retiredGenerations_(),
              drainingRetiredSubtrees_(false)
        {
          this->tracker_.setInvalidateCallback(&BoundaryNode::InvalidateSceneThunk, this);
        }
        virtual ~BoundaryNode()
        {
          this->seatReservations_.cancelRequests();
          this->holdLedger_.auditEmptyBeforeReclaim();
#ifdef LOKA_LIFECYCLE_AUDIT
          assert(!this->pendingHeldReleasesHead_ &&
                 "a Boundary must drain its Held releasers before reclamation");
#endif
          clearObservedStateEntries();
          this->releaseOwnedNodeStorage();
          reclaimParticipants();
          clearOwnedStates();
          stateArena_.clear();
        }

        using ComposableNode::compose;
        virtual void compose(ComponentContext &context, ComposeEvent event)
        {
          if (event == COMPOSE_EVENT_DETACH && this->composition().root())
          {
            this->retireDeclarationScope(context, this->branchSeats_);
            this->forgetBranchSeatDirtySources(this->branchSeats_);
            this->branchSeats_.capture(0);
          }
          ComposableNode::compose(context, event);
        }

        virtual BoundaryNode *asBoundary()
        {
          return this;
        }
        /** Whether this boundary's composition strategy owns child traversal for
            this event, including the decision to skip it. Custom boundaries
            default to the generic composeTree child walk. */
        virtual bool ownsChildTraversal(ComposeEvent) const
        {
          return false;
        }
        virtual IStateOwner *asStateOwner()
        {
          return this;
        }
        virtual Node *retainedLifecycleBranch(unsigned index)
        {
          return this->parkedBranches_.branch(index);
        }
#ifdef TEST_BUILD
        unsigned parkedBranchCountForTesting() const
        {
          return this->parkedBranches_.countForTesting();
        }
        unsigned parkedBranchArmForTesting(unsigned index) const
        {
          const BoundaryParkedBranchLedger::Entry *entry =
              this->parkedBranches_.entry(index);
          assert(entry && "parked branch test index must name a ledger row");
          return entry ? entry->arm : 0;
        }
#endif

        virtual loka::core::StateTracker *tracker()
        {
          return &tracker_;
        }
        virtual bool flushViewDirtyImmediately(NodeDirtyFlags flags) const
        {
          (void)flags;
          return true;
        }
        virtual void applyPendingUpdate(const PlatformApplyPlan &plan)
        {
          const LocalApplyInfo info = this->localApplyInfo(plan);
          if (info.hasStructureWork)
          {
            this->applyPendingStructureInfo(info, plan);
          }
          if (info.hasLayoutWork)
          {
            this->applyPendingLayoutInfo(info, plan);
          }
          switch (info.paintKind)
          {
          case LOCAL_APPLY_PAINT_COMPOSITED:
            this->applyPendingCompositedPaintInfo(info, plan);
            break;
          case LOCAL_APPLY_PAINT_OPAQUE:
            this->applyPendingOpaquePaintInfo(info, plan);
            break;
          case LOCAL_APPLY_PAINT_GENERIC:
            this->applyPendingLocalPaintInfo(info, plan);
            break;
          case LOCAL_APPLY_PAINT_NONE:
          default:
            break;
          }
        }
        bool hasLocalApplyStructureWork(const PlatformApplyPlan &plan) const
        {
          return this->localApplyInfo(plan).hasStructureWork;
        }
        bool hasLocalApplyLayoutWork(const PlatformApplyPlan &plan) const
        {
          return this->localApplyInfo(plan).hasLayoutWork;
        }
        bool hasLocalApplyPaintWork(const PlatformApplyPlan &plan) const
        {
          return this->localApplyInfo(plan).hasPaintWork();
        }
        LocalApplyInfo localApplyInfo(const PlatformApplyPlan &plan) const
        {
          LocalApplyInfo info;
          info.isLocalStructureRoot = plan.hasLocalStructureWork(this);
          info.isLocalLayoutRoot = plan.hasLocalLayoutWork(this);
          info.isLocalPaintRoot = plan.hasLocalPaintWork(this);
          info.hasStructureWork = info.isLocalStructureRoot;
          info.hasLayoutWork = info.isLocalLayoutRoot;
          bool opaqueByHint = false;
          this->updateState_.selectLocalOpaqueCoverageHint(
              info.isLocalPaintRoot, info.hasOpaqueCoverageHint, opaqueByHint);
          if (!info.isLocalPaintRoot)
          {
            info.paintKind = LOCAL_APPLY_PAINT_NONE;
          }
          else if (plan.requiresCompositedPaint())
          {
            info.paintKind = LOCAL_APPLY_PAINT_COMPOSITED;
          }
          else if (plan.isOpaqueLocalPaint() || opaqueByHint)
          {
            info.paintKind = LOCAL_APPLY_PAINT_OPAQUE;
          }
          else
          {
            info.paintKind = LOCAL_APPLY_PAINT_GENERIC;
          }
          info.paintIsOpaque =
              info.paintKind == LOCAL_APPLY_PAINT_OPAQUE || info.paintKind == LOCAL_APPLY_PAINT_COMPOSITED;
          this->updateState_.selectLocalApplyBoundsHint(info.hasPaintWork(),
                                                        info.hasLayoutWork,
                                                        info.bounds,
                                                        info.usesPaintBoundsHint,
                                                        info.hasPaintSpecificBoundsHint);
          if (info.usesPaintBoundsHint)
          {
            info.boundsKind = scene::LOCAL_APPLY_BOUNDS_PAINT;
          }
          else if (info.bounds)
          {
            info.boundsKind = scene::LOCAL_APPLY_BOUNDS_LAYOUT;
          }
          assert(info.hasAnyWork() == plan.hasAnyLocalWork(this));
          return info;
        }
        LocalApplyPaintKind localApplyPaintKind(const PlatformApplyPlan &plan) const
        {
          return this->localApplyInfo(plan).paintKind;
        }
        bool requiresLocalCompositedPaint(const PlatformApplyPlan &plan) const
        {
          return this->localApplyInfo(plan).hasCompositedPaintWork();
        }
        bool hasLocalOpaquePaintWork(const PlatformApplyPlan &plan) const
        {
          return this->localApplyInfo(plan).hasOpaquePaintWork();
        }
        bool hasLocalOpaquePaintHint(const PlatformApplyPlan &plan) const
        {
          return this->localApplyInfo(plan).hasOpaqueCoverageHint;
        }
        bool localApplyPaintIsOpaque(const PlatformApplyPlan &plan) const
        {
          return this->localApplyInfo(plan).paintIsOpaque;
        }
        bool hasLocalApplyBoundsHint(const PlatformApplyPlan &plan) const
        {
          return this->localApplyInfo(plan).hasBoundsHint();
        }
        const LayoutBounds *localApplyBoundsHint(const PlatformApplyPlan &plan) const
        {
          return this->localApplyInfo(plan).bounds;
        }
        void markViewDirty(NodeDirtyFlags flags);
        void setFrozen(bool frozen)
        {
          this->runtimeState_.setFrozen(frozen);
        }
        bool isFrozen() const
        {
          return this->runtimeState_.isFrozen();
        }
        bool isApplyingPlatform() const
        {
          return this->updateState_.isApplying();
        }
        bool isComposingPhase() const
        {
          return this->updateState_.isComposing();
        }
        BoundaryComposePhaseScope beginComposePhaseScope()
        {
          return this->updateState_.beginComposeScope();
        }
        BoundaryApplyPhaseScope beginApplyPhaseScope()
        {
          return this->updateState_.beginApplyScope();
        }
        Scene *scene() const
        {
          return this->runtimeState_.currentScene();
        }
        Scene *getScene() const
        {
          if (this->runtimeState_.hasScene())
          {
            return this->runtimeState_.currentScene();
          }
          return this->runtimeState_.currentParentBoundary() ? this->runtimeState_.currentParentBoundary()->getScene()
                                                             : 0;
        }
        void setScene(Scene *scene)
        {
          this->runtimeState_.setScene(scene);
        }
        BoundaryNode *parentBoundary() const
        {
          return this->runtimeState_.currentParentBoundary();
        }
        void setParentBoundary(BoundaryNode *parent)
        {
          this->runtimeState_.setParentBoundary(parent);
        }
        void setLayoutBounds(int x, int y, int width, int height)
        {
          const int normalizedWidth = width < 0 ? 0 : width;
          const int normalizedHeight = height < 0 ? 0 : height;
          const bool changed = this->runtimeState_.setLayoutBounds(x, y, normalizedWidth, normalizedHeight);
          updateState_.noteLayoutBoundsTransition(
              changed, this->runtimeState_.hasParentBoundary(), x, y, normalizedWidth, normalizedHeight);
        }
        void clearLayoutBounds()
        {
          const bool changed = this->runtimeState_.clearLayoutBounds();
          updateState_.noteLayoutBoundsCleared(changed, this->runtimeState_.hasParentBoundary());
        }
        const LayoutBounds &layoutBounds() const
        {
          return this->runtimeState_.currentLayoutBounds();
        }
        bool hasLayoutBounds() const
        {
          return this->runtimeState_.hasLayoutBounds();
        }
        void clearObservedDirtyFlags()
        {
          observedState_.clearDirtyFlags();
        }
        BoundaryComposeResult &composeResult()
        {
          return compositionState_.composeResult();
        }
        const BoundaryComposeResult &composeResult() const
        {
          return compositionState_.composeResult();
        }
        BoundaryUpdateResult &updateResult()
        {
          return updateState_.updateResult();
        }
        const BoundaryUpdateResult &updateResult() const
        {
          return updateState_.updateResult();
        }
        void beginComposeResult(ComposeEvent event, NodeDirtyFlags dirtyFlags)
        {
          compositionState_.beginCompose(event, dirtyFlags);
          // The result must be open before a retained seat source can refuse.
          if (event != COMPOSE_EVENT_DETACH && this->prepareBranchSeatDirtySources(this->branchSeats_))
            this->registerBranchSeatDirtySources();
        }
        /** Completes the open compose window. With an allocation failure or
            boundary-plan refusal recorded, it converts the compose into a
            projection failure instead; the body lives in Boundary.cpp because
            the conversion records the deferred full rebuild on the Scene. */
        void completeComposeResult();
        /** Allocation white flag (#132 ruling 3): a state or node failed to
            materialize during this boundary's open compose window. The flag
            only records; completeComposeResult() converts it. */
        void noteComposeAllocationFailure()
        {
          compositionState_.noteAllocationFailure();
        }
        /** Deterministic capability refusal: a contextless compose window
            reached a branch seat it cannot resolve without a boundary plan.
            Distinct from the allocation white flag (which is transient);
            shares the projection-failure recovery because both must defer a
            full rebuild. */
        void noteComposeBoundaryPlanRequired()
        {
          compositionState_.noteBoundaryPlanRequired();
        }
        virtual void noteStateAllocationFailure()
        {
          this->noteComposeAllocationFailure();
        }
        void clearPhaseResults()
        {
          compositionState_.clearResult();
          updateState_.clearResult();
        }
        /** Per-cycle transition facts: structure self-report, bounds
            transition and ancestor-layout promotion. The walk path clears
            the whole phase result at cycle entry, but the direct-root
            UPDATE path deliberately preserves durable paint metadata and
            bounds hints across cycles -- only transition booleans are
            reset, or a single rebuild would escalate every later
            paint-only update through the layout/ensure pass forever
            (#279 review, #695). */
        void clearTransitionFactsForCycle()
        {
          updateState_.clearTransitionFacts();
        }
        void noteLocalPaintWork()
        {
          assert(updateState_.canMutateLocalPaintMetadata());
          updateState_.noteLocalPaintWork();
        }
        void noteLocalStructureWork()
        {
          assert(updateState_.canMutateLocalPaintMetadata());
          updateState_.noteLocalStructureWork();
        }
        void noteCompositedPaint()
        {
          assert(updateState_.canMutateLocalPaintMetadata());
          updateState_.noteCompositedPaint();
        }
        void noteOpaquePaintCoverage(bool opaque)
        {
          assert(updateState_.canMutateLocalPaintMetadata());
          updateState_.noteOpaquePaintCoverage(opaque);
        }
        void beginPlatformApply()
        {
          updateState_.beginApply();
        }
        void endPlatformApply()
        {
          updateState_.endApply();
        }
        void beginObservedStatePass()
        {
          this->uses_.discardPrepared();
          observedState_.beginPass();
        }
        /** Close the registration pass after every composition exit. */
        void completeObservedStatePass()
        {
          this->observedState_.finishPass(&BoundaryNode::ObservedStateChangedThunk,
              this->compositionState_.allocationFailedValue() ||
              this->compositionState_.boundaryPlanRequiredValue());
        }
        /** Keeps observation registration bounded by the composition walk,
            including early returns; detach never opens a registration pass. */
        class ObservedStatePassScope
        {
        public:
          ObservedStatePassScope(BoundaryNode *boundary, ComposeEvent event)
              : boundary_(event == COMPOSE_EVENT_DETACH ? 0 : boundary)
          {
            if (this->boundary_)
              this->boundary_->beginObservedStatePass();
          }
          ~ObservedStatePassScope()
          {
            if (this->boundary_)
              this->boundary_->completeObservedStatePass();
          }

        private:
          ObservedStatePassScope(const ObservedStatePassScope &);
          ObservedStatePassScope &operator=(const ObservedStatePassScope &);
          BoundaryNode *boundary_;
        };
        void clearObservedStateEntries()
        {
          observedState_.clearEntries(&BoundaryNode::ObservedStateChangedThunk);
        }
        void addObservedDirtyFlags(NodeDirtyFlags flags)
        {
          observedState_.addDirtyFlags(flags);
        }
        bool registerObservedState(loka::core::StateBase *state, NodeDirtyFlags flags, Node *node = 0)
        {
          if (observedState_.registerState(this, state, flags, &BoundaryNode::ObservedStateChangedThunk,
                                           node ? node : this)) return true;
          this->noteComposeAllocationFailure();
          return false;
        }
        /** Removes both ancestor edges held for one state owned by an inner
            scope. Safe during Boundary teardown after the observed ledger has
            already been cleared. */
        void forgetInnerOwnedState(loka::core::StateBase *state)
        {
          this->tracker_.removeState(state);
          this->observedState_.forgetState(
              state, &BoundaryNode::ObservedStateChangedThunk);
        }
        /** Receives a committed transaction from an attached inner owner
            without exposing the Boundary's observed-state ledger. */
        void noteInnerTrackerCommit(
            const loka::core::PushStateTracker *innerTracker)
        {
          NodeDirtyFlags flags = NODE_DIRTY_NONE;
          if (!this->observedState_.dirtyFlagsForCommittedStates(innerTracker, flags))
          {
            flags = this->observedDirtyFlags();
          }
          if (flags != NODE_DIRTY_NONE)
          {
            this->markViewDirty(flags);
          }
        }
        /** Reserve the Boundary-owned seat uses before a declaration commits. */
        bool prepareBranchSeatDirtySources(BoundaryBranchSeatState &scope)
        {
          const std::vector<BoundaryBranchSeatPlanEntry> &plans = scope.plans();
          for (size_t i = 0; i < plans.size(); ++i)
          {
            if (plans[i].dirtySource && !this->uses_.reserve(plans[i].dirtySource))
            {
              this->uses_.discardPrepared();
              this->noteComposeAllocationFailure();
              return false;
            }
            BoundaryBranchSeatState *nested = plans[i].seat()->declaredBranchSeats();
            if (nested && !this->prepareBranchSeatDirtySources(*nested)) return false;
          }
          return true;
        }
        void registerBranchSeatDirtySources()
        {
          this->registerBranchSeatDirtySources(this->branchSeats_);
        }
        void registerBranchSeatDirtySources(BoundaryBranchSeatState &scope)
        {
          const std::vector<BoundaryBranchSeatPlanEntry> &plans = scope.plans();
          for (size_t i = 0; i < plans.size(); ++i)
          {
            if (!plans[i].dirtySource)
            {
              continue;
            }
            const detail::SeatReservation *reservation = plans[i].seat()->seatReservation();
            if (reservation)
              reservation->request().observe(plans[i].dirtySource);
            this->registerObservedState(
                plans[i].dirtySource,
                static_cast<NodeDirtyFlags>(NODE_DIRTY_CHILD | NODE_DIRTY_LAYOUT));
            BoundaryBranchSeatState *nested = plans[i].seat()->declaredBranchSeats();
            if (nested)
              this->registerBranchSeatDirtySources(*nested);
          }
        }
        /** Publish staged declarations and rows before observing the accepted
            scope tree once. Callers choose the owning boundary or a newly
            installed declaration scope; nested scopes are visited recursively.
            Cost: once per enclosing commit, walks that scope's seat sources. */
        void commitBranchSeatRegistrations(BoundaryBranchSeatRuntimeRegistrationPlan &registrations,
                                           BoundaryBranchSeatState &observationScope)
        {
          registrations.commitTo(this->branchSeats_);
          this->registerBranchSeatDirtySources(observationScope);
        }
        /** Outgoing declaration registrations stop owning their sources before
            publication. Surviving seats and the boundary are registered again;
            descendant nodes register in the following child walk. */
        void forgetBranchSeatDirtySources(BoundaryBranchSeatState &scope)
        {
          const std::vector<BoundaryBranchSeatPlanEntry> &plans = scope.plans();
          for (size_t i = 0; i < plans.size(); ++i)
          {
            this->observedState_.forgetState(plans[i].dirtySource, &BoundaryNode::ObservedStateChangedThunk);
            BoundaryBranchSeatState *nested = plans[i].seat()->declaredBranchSeats();
            if (nested)
              this->forgetBranchSeatDirtySources(*nested);
          }
        }
        /** Registers a node's declared external dirty sources against the owning
            boundary, wiring each observed state so a later mutation marks that
            boundary's view dirty. Shared by the generic composeTree walk and the
            direct-root compose path (Scene::prepareRootBoundaryCompose, #127) so
            both re-register on every non-DETACH compose window, paired with
            beginObservedStatePass(). */
        static void declareBoundaryDirtySources(Node *node, BoundaryNode *owner)
        {
          if (!node || !owner)
          {
            return;
          }
          if (!node->prepareObservedUses())
          {
            owner->noteComposeAllocationFailure();
            return;
          }
          class LocalDirtySourceRegistrar : public DirtySourceRegistrar
          {
          public:
            explicit LocalDirtySourceRegistrar(BoundaryNode *boundary, Node *node)
                : boundary_(boundary), node_(node)
            {
            }

            virtual void markDirtyOnChange(loka::core::StateBase *state, NodeDirtyFlags flags)
            {
              if (!boundary_ || !state)
              {
                return;
              }
              boundary_->registerObservedState(state, flags, node_);
            }

          private:
            BoundaryNode *boundary_;
            Node *node_;
          };
          LocalDirtySourceRegistrar registrar(owner, node);
#ifdef TEST_BUILD
          ++testing::paintBaselineStats().dirtySourceDeclarations;
#endif
          node->declareDirtySources(registrar);
        }
        NodeDirtyFlags observedDirtyFlags() const
        {
          return observedState_.currentDirtyFlags();
        }
        bool observedDirtyFlagsForCommittedStates(NodeDirtyFlags &flags) const
        {
          const loka::core::PushStateTracker *pushTracker = this->tracker_.asPushTracker();
          return observedState_.dirtyFlagsForCommittedStates(pushTracker, flags);
        }

#ifdef TEST_BUILD
        /** Internal fixture admission; no production factory consults this bank. */
        detail::NodePartition *installPartitionFixture(const detail::SeatLayoutTable &table)
        {
          return this->seatReservations_.installFixture(table);
        }
#endif

        /** Cold seat installation belongs to this Boundary, independent of arm replacement. */
        const detail::SeatReservation *installSeatReservation(const detail::SeatLayoutTable &table,
                                                               detail::SeatReplacementPolicy policy = detail::RETIRE_BEFORE_BUILD)
        {
          return this->seatReservations_.install(table, policy);
        }

        NodeArena *nodeArena()
        {
          return &nodeArena_;
        }
        const BoundaryBranchSeatPlanEntry *branchSeatPlan(NodeDefinitionBase *definition,
                                                          BoundaryBranchSeatState *scope = 0) const
        {
          if (!definition)
          {
            return 0;
          }
          IBranchSeatDefinition *seat = definition->asBranchSeatDefinition();
          if (!seat)
          {
            return 0;
          }
          return this->branchSeats_.findPlan(
              BoundaryParkedBranchKey(definition->nodeTag(),
                                      definition->compositionSeatSlot(),
                                      seat->branchSeatTypeId(),
                                      scope ? scope : const_cast<BoundaryBranchSeatState *>(&this->branchSeats_)));
        }
        /** A declaration scope cannot die while either runtime ledger borrows it. */
        void assertNoBranchSeatScopeReferences(const BoundaryBranchSeatState *scope) const
        {
#ifdef LOKA_LIFECYCLE_AUDIT
          if (this->branchSeats_.referencesScope(scope) || this->parkedBranches_.referencesScope(scope))
          {
            assert(false && "retiring a declaration with live seat scope references");
            std::abort();
          }
#else
          (void)scope;
#endif
        }

        void registerMaterializedBranchSeat(const BoundaryBranchSeatPlanEntry &plan,
                                            Node *parent,
                                            Node *active,
                                            IStateOwner *stateOwner = 0)
        {
          this->branchSeats_.registerRuntime(plan, parent, active, stateOwner);
        }
        /** Materializes a first declaration in its actual parent owner scope.
            Candidate nested registrations are invisible until success. */
        NodeMaterializationResult materializeDeclaredSeat(ComponentContext &context,
                                                          const BoundaryBranchSeatPlanEntry &plan,
                                                          Node *parent,
                                                          BoundaryBranchSeatRuntimeRegistrationPlan *registrations)
        {
          if (!plan.seat()->prepareSeatReservation())
          {
            this->noteComposeAllocationFailure();
            NodeMaterializationResult refused = {0, true, false};
            return refused;
          }
          const detail::SeatReservation *reservation = plan.seat()->seatReservation();
          if (!reservation)
            return this->materializeAdmittedSeat(context, plan, parent, registrations);
          reservation->request().observe(plan.dirtySource);
          ColdSeatBuild build(*this, context, plan, parent, registrations);
          reservation->partition().build(reservation->layoutTable().layouts(),
                                                reservation->layoutTable().count(), build);
          return build.result;
        }

        class ColdSeatBuild : public detail::NodeBuildOperation
        {
        public:
          ColdSeatBuild(BoundaryNode &owner, ComponentContext &context,
                        const BoundaryBranchSeatPlanEntry &plan, Node *parent,
                        BoundaryBranchSeatRuntimeRegistrationPlan *registrations)
              : owner_(owner), context_(context), plan_(plan), parent_(parent), registrations_(registrations)
          { this->result.root = 0; this->result.allocationFailed = true; this->result.requiresBoundaryPlan = false; }
          virtual bool buildAndAttach(detail::NodeBuildTicket &ticket)
          {
            detail::SeatNodeStorageView storage(ticket);
            ComponentContext context(this->context_);
            context.setNodeStorage(&storage);
            context.setOwner(this->parent_);
            this->result = this->owner_.materializeAdmittedSeat(context, this->plan_, this->parent_, this->registrations_);
            return this->result.root != 0 && !this->result.allocationFailed;
          }
          NodeMaterializationResult result;
        private:
          BoundaryNode &owner_;
          ComponentContext &context_;
          const BoundaryBranchSeatPlanEntry &plan_;
          Node *parent_;
          BoundaryBranchSeatRuntimeRegistrationPlan *registrations_;
        };

        NodeMaterializationResult materializeAdmittedSeat(ComponentContext &context,
                                                          const BoundaryBranchSeatPlanEntry &plan,
                                                          Node *parent,
                                                          BoundaryBranchSeatRuntimeRegistrationPlan *registrations)
        {
          loka::core::OwnedDef<BranchSeatDeclaration> candidate(plan.seat()->declareBranchCandidate(context));
          if (!candidate.isSet())
          {
            this->noteComposeAllocationFailure();
            NodeMaterializationResult failed = {0, true, false};
            return failed;
          }
          BoundaryBranchSeatRuntimeRegistrationPlan nested;
          NodeMaterializationResult result = this->materializeDeclaration(context, plan, *candidate, parent, nested);
          if (result.allocationFailed || result.requiresBoundaryPlan || !result.root)
          {
            if (result.root)
              RetireUnattachedCandidate(result.root, &context);
            result.root = 0;
            return result;
          }
          if (registrations)
          {
            if (!nested.stageDeclaration(plan.seat(), candidate))
            {
              this->noteComposeAllocationFailure();
              RetireUnattachedCandidate(result.root, &context);
              result.root = 0;
              result.allocationFailed = true;
              return result;
            }
            nested.appendTo(*registrations);
          }
          else
          {
            plan.seat()->commitBranchDeclaration(candidate.take());
            this->commitBranchSeatRegistrations(nested, *plan.seat()->declaredBranchSeats());
          }
          return result;
        }

        bool evaluateBranchSeatsForScheduledApply(ComponentContext &context)
        {
          return this->applyCurrentBranchSeatPlan(context);
        }
        /** Reclaims the queue snapshot owned by this Boundary at the head of
            the next tracker run. Retirees added while draining wait for a
            later tracker run. Capacity overflow asserts in Debug; Release reclaims
            that entry with the legacy walk in this run, then continues bounded
            reclamation for fitting entries. A fallback entry is outside the
            zero-upstream claim; no retry request is issued for overflow. */
        void drainRetiredSubtreesAtNextTrackerRun();
        /** Runs the queued releasers of blocks this Boundary is the last
            dropping owner for. Reclamation paths call it before destroying
            the Boundary so no releaser ever runs from a destructor. */
        void drainPendingHeldReleases();
        virtual loka::core::HoldLedger *holdLedger()
        {
          return &this->holdLedger_;
        }
        virtual void reserveHeldArena(size_t totalSize)
        {
          stateArena_.reserve(totalSize);
        }
        virtual void *allocateHeldMemory(size_t size, size_t align)
        {
          return stateArena_.allocate(size, align);
        }
        virtual void registerHeldMemory(
            loka::core::detail::HeldBlockBase *block)
        {
          stateArena_.registerHeld(block);
        }
        virtual void retireHeldBlock(
            loka::core::detail::HeldBlockBase *block);
        virtual void *allocateStateMemory(size_t size, size_t align)
        {
          return stateArena_.allocate(size, align);
        }
        virtual void registerStateMemory(loka::core::StateBase *state, void (*destroy)(loka::core::StateBase *))
        {
          stateArena_.registerState(state, destroy);
        }
        /** Releases only the enclosing arena's storage row. The logical
            ownership row belongs to a Boundary-inner owner and must already
            be detached there. */
        void releaseInnerArenaStateMemory(loka::core::StateBase *state)
        {
          assert(state && state->isArenaAllocated() &&
                 "inner arena release requires an arena state");
          stateArena_.releaseState(state);
        }
        virtual void reserveStateArena(size_t totalSize)
        {
          stateArena_.reserve(totalSize);
        }

        static void
        composeSubtree(Node *node, ComponentContext &parentContext, ComposeEvent event, BoundaryNode *currentBoundary)
        {
          composeTree(node, parentContext, event, currentBoundary);
        }

        static void InvalidateSceneThunk(void *userData);
        static void ObservedStateChangedThunk(void *userData);
        static void ObservedStateDeferredInvalidateThunk(void *userData);

        template <class T> NodeState<T> dangerouslyUseState()
        {
          return dangerouslyUseStateWithValue(T());
        }

        template <class T> NodeState<T> dangerouslyUseState(const T &initial)
        {
          return dangerouslyUseStateWithValue(initial);
        }

        Node *compositionRootNode() const
        {
          return this->childrenHead();
        }
        INestable *compositionRootNestable() const
        {
          Node *root = compositionRootNode();
          return root ? root->asNestable() : 0;
        }

        /** Local rebuilds allocate heap nodes but retain the caller's logical
            owner and this Boundary's storage/lifetime context. Refusal never
            yields a publishable partial root. */
        Node *materializeLocalRebuildNode(ComponentContext &context, NodeDefinitionBase *definition)
        {
          ComponentContext materializationContext(context);
          materializationContext.setBoundary(this);
          if (!materializationContext.stateOwner())
            materializationContext.setStateOwner(this);
          NodeMaterializationResult result =
              NodeComposition::createNodeWithoutArenaResult(definition, materializationContext);
          if (result.requiresBoundaryPlan)
            this->noteComposeBoundaryPlanRequired();
          if (!result.root || result.allocationFailed)
            this->noteComposeAllocationFailure();
          if (result.requiresBoundaryPlan || result.allocationFailed || !result.root)
          {
            if (result.root)
              RetireUnattachedCandidate(result.root, &materializationContext);
            return 0;
          }
          return result.root;
        }

      protected:
        /** Applies scheduled seats, then walks this boundary's children once. */
        void updateCompositionChildren(ComponentContext &context)
        {
          this->evaluateBranchSeatsForScheduledApply(context);
          this->composeOwnedChildren(context, COMPOSE_EVENT_UPDATE);
        }

        /** Dispatches one event through this owner's existing children, resolving
            pending attachments for both UPDATE and retained ATTACH replay. */
        void composeOwnedChildren(ComponentContext &context, ComposeEvent event)
        {
          loka::dsl::CompositionCursor<Node> it(this->childrenHead(), this->childrenCount());
          for (Node *child = it.next(); child; child = it.next())
          {
            // Seat replacement can leave a direct child pending ATTACH. Use
            // the same lifecycle resolver as composeTree's generic child walk.
            ComposeEvent childEvent = child->resolveChildComposeEvent(event);
            this->composeTree(child, context, childEvent, this);
          }
        }

        /** Retires the complete arena allocation and ledger for clock-boundary reclaim. */
        void retireOwnedNodeGeneration(ComponentContext &context);
        void retireOwnedNodeGeneration()
        {
          ComponentContext context;
          this->retireOwnedNodeGeneration(context);
        }

        /** Releases native contexts from an already detached node, then queues it
            for clock-boundary reclaim regardless of heap or arena storage. */
        void retireDetachedNode(ComponentContext &context, Node *node);

        virtual void applyPendingStructureInfo(const LocalApplyInfo &, const PlatformApplyPlan &plan)
        {
          this->applyPendingStructure(plan);
        }

        virtual void applyPendingLayoutInfo(const LocalApplyInfo &, const PlatformApplyPlan &plan)
        {
          this->applyPendingLayout(plan);
        }

        virtual void applyPendingLocalPaintInfo(const LocalApplyInfo &, const PlatformApplyPlan &plan)
        {
          this->applyPendingLocalPaint(plan);
        }

        virtual void applyPendingOpaquePaintInfo(const LocalApplyInfo &, const PlatformApplyPlan &plan)
        {
          this->applyPendingOpaquePaint(plan);
        }

        virtual void applyPendingCompositedPaintInfo(const LocalApplyInfo &, const PlatformApplyPlan &plan)
        {
          this->applyPendingCompositedPaint(plan);
        }

        virtual void applyPendingStructure(const PlatformApplyPlan &) {}

        virtual void applyPendingLayout(const PlatformApplyPlan &) {}

        virtual void applyPendingLocalPaint(const PlatformApplyPlan &) {}

        virtual void applyPendingOpaquePaint(const PlatformApplyPlan &plan)
        {
          this->applyPendingLocalPaint(plan);
        }

        virtual void applyPendingCompositedPaint(const PlatformApplyPlan &plan)
        {
          this->applyPendingLocalPaint(plan);
        }

        static void destroyUncommittedLocalRebuildCandidates(
            BoundaryLocalRebuildPlan &plan);

        class UncommittedLocalRebuildPlanGuard
        {
        public:
          explicit UncommittedLocalRebuildPlanGuard(
              BoundaryLocalRebuildPlan &plan)
              : plan_(&plan), root_(0), oldChildren_(0)
          {
          }

          UncommittedLocalRebuildPlanGuard(
              BoundaryLocalRebuildPlan &plan, INestable &root,
              const std::vector<Node *> &oldChildren)
              : plan_(&plan), root_(&root), oldChildren_(&oldChildren)
          {
          }

          ~UncommittedLocalRebuildPlanGuard()
          {
            if (this->plan_)
            {
              if (this->root_)
              {
                // Candidates are never linked while this guard is armed.
                this->root_->detachChildren();
                for (size_t i = 0; i < this->oldChildren_->size(); ++i)
                  this->root_->addChild((*this->oldChildren_)[i]);
              }
              BoundaryNode::destroyUncommittedLocalRebuildCandidates(
                  *this->plan_);
            }
          }

          void disarm()
          {
            this->plan_ = 0;
          }

        private:
          UncommittedLocalRebuildPlanGuard(const UncommittedLocalRebuildPlanGuard &);
          UncommittedLocalRebuildPlanGuard &operator=(const UncommittedLocalRebuildPlanGuard &);

          BoundaryLocalRebuildPlan *plan_;
          INestable *root_;
          const std::vector<Node *> *oldChildren_;
        };

        bool applyLocalRebuildPlan(ComponentContext &context,
                                   INestable &root,
                                   BoundaryLocalRebuildPlan &plan,
                                   std::vector<Node *> &retainedChildren)
        {
          // Any plan entry that materializes or retires a node must report
          // structure work here or the cycle's apply claims paint-only
          // and skip-capable platforms never run the ensure pass (#277).
          for (size_t i = 0; i < plan.entries.size(); ++i)
          {
            if (plan.entries[i].requiresAttachCompose() ||
                plan.entries[i].detachedNode())
            {
              this->noteLocalStructureWork();
              break;
            }
          }
          std::vector<Node *> detachedChildren;
          root.detachChildrenTo(detachedChildren);
          UncommittedLocalRebuildPlanGuard uncommittedGuard(plan, root, detachedChildren);
          for (size_t i = 0; i < plan.entries.size(); ++i)
          {
            if (!plan.entries[i].keepsLiveNode())
            {
              continue;
            }
            if (plan.entries[i].action == BoundaryLocalRebuildPlanEntry::ACTION_RETAIN ||
                plan.entries[i].action == BoundaryLocalRebuildPlanEntry::ACTION_RECONCILE)
            {
              NodeDefinitionBase *retainedDefinition = plan.entries[i].definition;
              const bool reconciled =
                  plan.entries[i].action == BoundaryLocalRebuildPlanEntry::ACTION_RECONCILE;
              if (!retainedDefinition)
              {
                return false;
              }
              if (reconciled &&
                  !this->reconcileParkedBranch(
                      context, plan.entries[i].node, retainedDefinition))
              {
                return false;
              }
              if (!reconciled &&
                  !retainedDefinition->asBranchSeatDefinition() &&
                  !retainedDefinition->applyPropsToNode(plan.entries[i].node))
              {
                return false;
              }
              retainedChildren.push_back(plan.entries[i].node);
            }
          }
          uncommittedGuard.disarm();

          for (size_t i = 0; i < plan.entries.size(); ++i)
          {
            if (plan.entries[i].keepsLiveNode())
              root.addChild(plan.entries[i].node);
          }

          for (size_t i = 0; i < plan.entries.size(); ++i)
          {
            BoundaryLocalRebuildPlanEntry &entry = plan.entries[i];
            Node *detachedNode = entry.detachedNode();
            if (detachedNode)
            {
              this->retireParkedBranchForRemovedSeat(context, detachedNode);
            }
          }
          this->commitBranchSeatRegistrations(plan.branchSeatRegistrations, this->branchSeats_);
          for (size_t i = 0; i < plan.entries.size(); ++i)
          {
            BoundaryLocalRebuildPlanEntry &entry = plan.entries[i];
            if (entry.requiresAttachCompose())
            {
              this->composeTree(entry.node, context, COMPOSE_EVENT_ATTACH, this);
            }
          }
          for (size_t i = 0; i < plan.entries.size(); ++i)
          {
            BoundaryLocalRebuildPlanEntry &entry = plan.entries[i];
            Node *detachedNode = entry.detachedNode();
            if (detachedNode)
            {
              this->composeTree(detachedNode, context, COMPOSE_EVENT_DETACH, this);
              this->retireDetachedNode(context, detachedNode);
            }
          }
          return true;
        }

        bool buildParkedBranchReentryPlan(ComponentContext &context,
                                          Node *runtimeParent,
                                          INestable &root,
                                          INestableDefinition &desiredRoot,
                                          BoundaryLocalRebuildPlan &plan,
                                          BoundaryBranchSeatState *seatScope = 0)
        {
          plan.clear();
          plan.reserve(desiredRoot.childrenCount());

          // This builder may materialize more than one candidate before a
          // later definition refuses. Until the complete plan is ready, those
          // candidates have no owner except the plan itself.
          UncommittedLocalRebuildPlanGuard uncommittedGuard(plan);

          std::vector<Node *> liveChildren;
          loka::dsl::CompositionCursor<Node> liveIt(root.childrenHead(), root.childrenCount());
          for (Node *live = liveIt.next(); live; live = liveIt.next())
          {
            liveChildren.push_back(live);
          }

          NodeDefinitionBase *definition = desiredRoot.childrenHead();
          size_t slot = 0;
          while (definition)
          {
            NodeDefinitionBase *effectiveDefinition = definition;
            IBranchPolicyScopeDefinition *scope =
                definition->asBranchPolicyScopeDefinition();
            if (scope)
            {
              effectiveDefinition = scope->scopedBranchDefinition();
            }
            const BoundaryBranchSeatPlanEntry *seatPlan = this->branchSeatPlan(effectiveDefinition, seatScope);
            BoundaryBranchSeatRuntimeEntry seatRuntime;
            const bool hasSeatRuntime =
                seatPlan && this->branchSeats_.queryRuntime(seatPlan->key, seatRuntime);
            Node *existing = hasSeatRuntime ? seatRuntime.active : 0;
            if (!seatPlan && !existing &&
                effectiveDefinition->nodeTag() != NODE_TAG_NONE)
            {
              for (size_t i = 0; i < liveChildren.size(); ++i)
              {
                if (liveChildren[i] &&
                    liveChildren[i]->nodeTag() == effectiveDefinition->nodeTag())
                {
                  existing = liveChildren[i];
                  break;
                }
              }
            }
            else if (!seatPlan && !existing && slot < liveChildren.size() &&
                     liveChildren[slot] && liveChildren[slot]->nodeTag() == NODE_TAG_NONE)
            {
              existing = liveChildren[slot];
            }

            if (existing &&
                (seatPlan || effectiveDefinition->isCompatibleWithNode(existing)))
            {
              plan.entries.push_back(
                  scope
                      ? BoundaryLocalRebuildPlanEntry::reconcile(
                            existing,
                            effectiveDefinition,
                            effectiveDefinition->nodeTag())
                      : BoundaryLocalRebuildPlanEntry::retain(
                            existing,
                            effectiveDefinition,
                            effectiveDefinition->nodeTag()));
            }
            else
            {
              Node *created = 0;
              if (seatPlan)
              {
                NodeMaterializationResult materialized = {0, false, false};
                if (!seatPlan->dirtySource || !runtimeParent ||
                    !this->createCurrentBranch(
                        context,
                        *seatPlan,
                        runtimeParent,
                        materialized,
                        plan.branchSeatRegistrations))
                {
                  return false;
                }
                created = materialized.root;
                plan.branchSeatRegistrations.record(
                    *seatPlan, runtimeParent, materialized,
                    hasSeatRuntime && seatRuntime.stateOwner ? seatRuntime.stateOwner : context.stateOwner());
              }
              else
              {
                created = this->materializeLocalRebuildNode(context, effectiveDefinition);
              }
              if (!created)
              {
                return false;
              }
              plan.entries.push_back(
                  existing ? BoundaryLocalRebuildPlanEntry::replace(
                                 created,
                                 existing,
                                 effectiveDefinition,
                                 effectiveDefinition->nodeTag())
                           : BoundaryLocalRebuildPlanEntry::attach(
                                 created,
                                 effectiveDefinition,
                                 effectiveDefinition->nodeTag()));
            }
            definition = definition->nextInComposition;
            ++slot;
          }

          for (size_t i = 0; i < liveChildren.size(); ++i)
          {
            bool represented = false;
            for (size_t planIndex = 0; planIndex < plan.entries.size(); ++planIndex)
            {
              if (plan.entries[planIndex].node == liveChildren[i] ||
                  plan.entries[planIndex].previousNode == liveChildren[i])
              {
                represented = true;
                break;
              }
            }
            if (!represented)
            {
              plan.entries.push_back(
                  BoundaryLocalRebuildPlanEntry::retire(liveChildren[i], liveChildren[i]->nodeTag()));
            }
          }
          this->branchSeats_.reserveRuntimeRegistrations(
              plan.branchSeatRegistrations.count());
          uncommittedGuard.disarm();
          return true;
        }

        bool reconcileParkedBranch(ComponentContext &context,
                                   Node *node,
                                   NodeDefinitionBase *definition,
                                   BoundaryBranchSeatState *seatScope = 0)
        {
          if (!node || !definition || !definition->applyPropsToNode(node))
          {
            return false;
          }
          if (node->asComposable())
          {
            return true;
          }

          INestable *root = node->asNestable();
          INestableDefinition *desiredRoot = definition->asNestableDefinition();
          if (!root || !desiredRoot)
          {
            return root == 0 && desiredRoot == 0;
          }

          BoundaryLocalRebuildPlan plan;
          if (!this->buildParkedBranchReentryPlan(
                  context, node, *root, *desiredRoot, plan, seatScope))
          {
            return false;
          }
          std::vector<Node *> retainedChildren;
          if (!this->applyLocalRebuildPlan(context, *root, plan, retainedChildren))
          {
            return false;
          }
          for (size_t i = 0; i < plan.entries.size(); ++i)
          {
            BoundaryLocalRebuildPlanEntry &entry = plan.entries[i];
            // A dissolved seat has no runtime node at this level. Its value-key
            // plan owns nested branch reconciliation later in this same pass.
            if (entry.action == BoundaryLocalRebuildPlanEntry::ACTION_RETAIN
                && !entry.definition->asBranchSeatDefinition()
                && !this->reconcileParkedBranch(context, entry.node, entry.definition, seatScope))
            {
              return false;
            }
          }
          return true;
        }

        NodeMaterializationResult materializeDeclaration(ComponentContext &context,
                                                         const BoundaryBranchSeatPlanEntry &plan,
                                                         BranchSeatDeclaration &candidate,
                                                         Node *parent,
                                                         BoundaryBranchSeatRuntimeRegistrationPlan &registrations)
        {
          candidate.seats.captureOwned(candidate.composition.root(), plan.key, 0);
          if (!this->prepareBranchSeatDirtySources(candidate.seats))
          {
            NodeMaterializationResult refused = {0, true, false};
            return refused;
          }
          candidate.composition.setContext(&context);
          candidate.composition.collectBranchSeatRegistrationsIn(&registrations);
          NodeMaterializationResult result = candidate.materialize(context, parent);
          candidate.composition.collectBranchSeatRegistrationsIn(0);
          candidate.composition.setContext(0);
          if (result.allocationFailed)
            this->noteComposeAllocationFailure();
          if (result.requiresBoundaryPlan)
            this->noteComposeBoundaryPlanRequired();
          return result;
        }

        /** Carries the seat's installed enclosing owner into build and attach.
            Called once per branch operation; reads only the supplied runtime. */
        ComponentContext branchRuntimeContext(ComponentContext &context,
                                              const BoundaryBranchSeatRuntimeEntry &runtime)
        {
          ComponentContext result(context);
          result.setStateOwner(runtime.stateOwner ? runtime.stateOwner : this);
          return result;
        }

        bool createCurrentBranch(ComponentContext &context,
                                 const BoundaryBranchSeatPlanEntry &plan,
                                 Node *parent,
                                 NodeMaterializationResult &result,
                                 BoundaryBranchSeatRuntimeRegistrationPlan &registrations)
        {
          loka::app::FragmentDefinition emptyBranch;
          NodeDefinitionBase *definition =
              plan.materializedBranchDefinition(emptyBranch);
          NodeComposition composition;
          ComponentContext branchContext(context);
          BoundaryBranchSeatRuntimeEntry runtime;
          if (this->branchSeats_.queryRuntime(plan.key, runtime) && runtime.stateOwner)
            branchContext.setStateOwner(runtime.stateOwner);
          composition.setContext(&branchContext);
          composition.collectBranchSeatRegistrationsIn(&registrations);
          assert(context.boundary() == this);
          result = composition.createNodeFromDefinitionResult(definition, parent, plan.key.scope);
          if (result.requiresBoundaryPlan)
          {
            this->noteComposeBoundaryPlanRequired();
          }
          if (result.allocationFailed)
          {
            this->noteComposeAllocationFailure();
          }
          if (!result.complete())
          {
            RetireUnattachedCandidate(result.root, &branchContext);
            result.root = 0;
            return false;
          }
          Node *created = result.root;
          return created != 0;
        }

        bool isMaterializedEmptyBranch(Node *node) const
        {
          INestable *nestable = node ? node->asNestable() : 0;
          return node &&
                 node->propsTypeId() == loka::app::FragmentProps::staticTypeId() &&
                 nestable && nestable->childrenCount() == 0;
        }

        void retireSeatBranchRoot(ComponentContext &context, Node *branch)
        {
          if (!branch)
          {
            return;
          }
          this->composeTree(branch, context, COMPOSE_EVENT_DETACH, this);
          this->retireDetachedNode(context, branch);
        }

        /** Commits descendant seat death across both Boundary-owned ledgers.
            Runtime mappings are erased one at a time without allocation; each
            erased key first drains both of its descendant arms and then every
            parked resident for that key through the ordinary retire door. */
        void retireOwnedSeatDescendants(
            ComponentContext &context,
            const BoundaryParkedBranchKey &ownerKey,
            unsigned ownerArm)
        {
          BoundaryParkedBranchKey nestedKey(ownerKey);
          unsigned nestedArmCount = 0;
          while (this->branchSeats_.eraseOneOwnedRuntime(ownerKey,
                                                         ownerArm,
                                                         nestedKey,
                                                         nestedArmCount))
          {
            for (unsigned arm = 0; arm < nestedArmCount; ++arm)
            {
              this->retireOwnedSeatDescendants(context, nestedKey, arm);
              Node *parkedBranch = 0;
              while ((parkedBranch = this->parkedBranches_.take(nestedKey, arm)) != 0)
              {
                this->retireSeatBranchRoot(context, parkedBranch);
              }
            }
            NodeDefinitionBase *definition = this->findBranchSeatDefinition(nestedKey);
            IBranchSeatDefinition *seat = definition ? definition->asBranchSeatDefinition() : 0;
            const detail::SeatReservation *reservation = seat ? seat->seatReservation() : 0;
            if (reservation)
              reservation->request().cancel();
            BoundaryBranchSeatState *scope = seat ? seat->declaredBranchSeats() : 0;
            if (scope)
            {
              this->retireDeclarationScope(context, *scope);
              this->forgetBranchSeatDirtySources(*scope);
              seat->commitBranchDeclaration(0);
            }
          }
        }

        /** Retires all parked residents before their declaration's plan scope dies.
            Active residents remain under the outgoing root's ordinary retire door. */
        void retireDeclarationScope(ComponentContext &context, BoundaryBranchSeatState &scope)
        {
          const std::vector<BoundaryBranchSeatPlanEntry> &plans = scope.plans();
          for (size_t i = 0; i < plans.size(); ++i)
          {
            const detail::SeatReservation *reservation = plans[i].seat()->seatReservation();
            if (reservation)
              reservation->request().cancel();
            BoundaryBranchSeatState *nested = plans[i].seat()->declaredBranchSeats();
            if (nested)
              this->retireDeclarationScope(context, *nested);
          }
          this->branchSeats_.eraseScopeRuntime(&scope);
          Node *parked = 0;
          while ((parked = this->parkedBranches_.takeScope(&scope)) != 0)
            this->retireSeatBranchRoot(context, parked);
          this->assertNoBranchSeatScopeReferences(&scope);
        }

        void retireSeatBranch(ComponentContext &context,
                              const BoundaryParkedBranchKey &key,
                              unsigned arm,
                              Node *branch)
        {
          this->retireOwnedSeatDescendants(context, key, arm);
          this->retireSeatBranchRoot(context, branch);
        }

        void retireParkedBranchForRemovedSeat(ComponentContext &context,
                                              Node *activeBranch)
        {
          BoundaryParkedBranchKey key(NODE_TAG_NONE, -1, 0, &this->branchSeats_);
          unsigned activeArm = 0;
          bool hasActiveArm = false;
          unsigned armCount = 0;
          if (!this->branchSeats_.eraseRuntimeForActive(activeBranch,
                                                        key,
                                                        activeArm,
                                                        hasActiveArm,
                                                        armCount))
          {
            return;
          }
          if (hasActiveArm)
          {
            this->retireOwnedSeatDescendants(context, key, activeArm);
          }
          for (unsigned arm = 0; arm < armCount; ++arm)
          {
            if (hasActiveArm && arm == activeArm)
            {
              continue;
            }
            Node *parkedBranch = 0;
            while ((parkedBranch = this->parkedBranches_.take(key, arm)) != 0)
            {
              this->retireSeatBranch(context, key, arm, parkedBranch);
            }
          }
        }

        /** The retained-detach line, as one door. Parking keeps a branch's
            states warm for re-entry, but detach removes Held slots (axiom 14,
            R3): a parked branch is a cross-tick retention, and cross-tick
            retention re-acquires through the owner at its next ATTACH. The
            fact walk and the slot drop travel together so no park site can
            take one without the other; the last drop queues the releaser on
            the owner's existing pool, never firing here. Children only, like
            the fact walk: a nested parked branch already crossed this line
            when it parked. */
        void detachRetainedSubtree(Node *node)
        {
          NotifySubtreeNodeDetached(node);
          DropRetainedHeldSlots(node);
          // A last drop inside the branch queues its releaser on the nearest
          // enclosing Boundary -- which may itself be inside the branch being
          // parked, where the live-tree drain walk will never reach it again.
          // The queue must ride a clock that is still live, so the parking
          // boundary adopts it; the entries run at this boundary's next
          // drain, which is the tick boundary the drop was aimed at.
          AdoptParkedPendingReleases(node, *this);
        }

        static void AdoptParkedPendingReleases(Node *node, BoundaryNode &pool)
        {
          if (!node)
          {
            return;
          }
          BoundaryNode *nested = node->asBoundary();
          if (nested && nested != &pool)
          {
            while (loka::core::detail::HeldBlockBase *block =
                       nested->pendingHeldReleasesHead_)
            {
              nested->pendingHeldReleasesHead_ = block->retireNext();
              if (!nested->pendingHeldReleasesHead_)
              {
                nested->pendingHeldReleasesTail_ = 0;
              }
              block->setRetireNext(0);
              pool.retireHeldBlock(block);
            }
          }
          INestable *nestable = node->asNestable();
          for (Node *child = nestable ? nestable->childrenHead() : 0;
               child;
               child = child->nextInComposition)
          {
            AdoptParkedPendingReleases(child, pool);
          }
        }

        static void DropRetainedHeldSlots(Node *node)
        {
          if (!node)
          {
            return;
          }
          IStateOwner *owner = node->asStateOwner();
          if (owner)
          {
            owner->detachHeldResources();
          }
          INestable *nestable = node->asNestable();
          for (Node *child = nestable ? nestable->childrenHead() : 0;
               child;
               child = child->nextInComposition)
          {
            DropRetainedHeldSlots(child);
          }
        }

        /** Pending candidate refusal uses the existing detach/retire door.
            Called once per refused candidate; walks only that candidate's
            subtree, then queues it on its Boundary's retirement clock. */
        static void ReclaimPendingSeatRoot(Node *root, void *data)
        {
          ComponentContext &context = *static_cast<ComponentContext *>(data);
          context.boundary()->retireSeatBranchRoot(context, root);
        }

        bool replaceSeatBranch(ComponentContext &context,
                               const BoundaryBranchSeatPlanEntry &plan,
                               const BoundaryBranchSeatRuntimeEntry &runtime,
                               bool parkOutgoing,
                               bool reuseParked)
        {
          Node *runtimeParent = runtime.parent;
          Node *outgoing = runtime.active;
          const unsigned outgoingArm = runtime.activeArm;
          const bool hadOutgoingArm = runtime.hasActiveArm;
          Node *incoming = plan.hasSelectedArm && reuseParked
                               ? this->takeParkedBranch(plan.key, plan.selectedArm)
                               : 0;
          NodeDefinitionBase *definition = plan.hasSelectedArm
                                               ? plan.branch(plan.selectedArm).definition
                                               : 0;
          if (incoming
              && ((!definition && !this->isMaterializedEmptyBranch(incoming))
                  || (definition
                      && (!definition->isCompatibleWithNode(incoming)
                          || !this->reconcileParkedBranch(context, incoming, definition, plan.key.scope)))))
          {
            this->retireSeatBranch(context, plan.key, plan.selectedArm, incoming);
            incoming = 0;
          }
          // A redeclared seat's outgoing and new generations share the owner
          // pair (plan.key, 0): retiring the outgoing arm would erase newly
          // published nested mappings. Stage them until retirement finishes
          // and the incoming candidate is complete (#931).
          BoundaryBranchSeatRuntimeRegistrationPlan nestedRegistrations;
          NodeMaterializationResult accepted = {incoming, false, false};
          loka::core::OwnedDef<BranchSeatDeclaration> candidate;
          PendingSubtree pending(&BoundaryNode::RetireUnattachedCandidate, &context);
          if (plan.seat()->needsBranchDeclaration())
          {
            ComponentContext declarationContext = this->branchRuntimeContext(context, runtime);
            candidate.reset(plan.seat()->declareBranchCandidate(declarationContext));
            if (!candidate.isSet())
            {
              this->noteComposeAllocationFailure();
              return false;
            }
            accepted = this->materializeDeclaration(declarationContext, plan, *candidate, runtimeParent, nestedRegistrations);
            PendingSubtree materialized(&BoundaryNode::RetireUnattachedCandidate, &declarationContext);
            materialized.prepare(accepted.root);
            if (context.nodeStorage() && accepted.root && accepted.complete())
            {
              materialized.prepare(materialized.take(), &BoundaryNode::ReclaimPendingSeatRoot, &declarationContext);
              this->composeTree(accepted.root, declarationContext, COMPOSE_EVENT_ATTACH, this);
              accepted.allocationFailed = this->compositionState_.allocationFailedValue();
            }
            if (!accepted.complete() || !accepted.root)
              return false;
            pending.prepare(materialized.take(),
                            context.nodeStorage() ? &BoundaryNode::ReclaimPendingSeatRoot
                                                  : &BoundaryNode::RetireUnattachedCandidate,
                            &context);
            incoming = pending.root();
          }
          if (!incoming)
          {
            if (!this->createCurrentBranch(context, plan, runtimeParent, accepted, nestedRegistrations))
              return false;
            pending.prepare(accepted.root);
          }
          incoming = accepted.root;
          if (!incoming)
          {
            return false;
          }
          // No accepted path reaches this commit with an incomplete result today.
          assert(accepted.complete());
          // Runtime publication is part of the structural commit: reserve the
          // ledger storage now, while nothing has been replaced or retired, so
          // the commit after cleanup cannot allocate (the local-rebuild path's
          // rule, applyLocalRebuildPlan).
          this->branchSeats_.reserveRuntimeRegistrations(nestedRegistrations.count());

          BoundaryBranchSeatRuntimeEntry existingRow;
          INestable *parent = runtimeParent ? runtimeParent->asNestable() : 0;
          const detail::SeatReservation *reservation = plan.seat()->seatReservation();
          const bool installed = this->branchSeats_.queryRuntime(plan.key, existingRow) &&
              (outgoing
                   ? parent && parent->replaceChild(outgoing, incoming)
                   : reservation && this->seatReservations_.installSeatChild(reservation->request(), incoming));
          if (!installed)
          {
            if (pending.root())
              pending.reclaim();
            else
              this->retireSeatBranchRoot(context, incoming);
            return false;
          }

          pending.take();

          if (candidate.isSet() && plan.seat()->declaredBranchSeats())
            this->retireDeclarationScope(context, *plan.seat()->declaredBranchSeats());

          if (outgoing)
          {
            if (parkOutgoing && hadOutgoingArm)
            {
              this->detachRetainedSubtree(outgoing);
              this->parkBranch(plan.key, outgoing, outgoingArm);
            }
            else if (hadOutgoingArm)
            {
              this->retireSeatBranch(context,
                                     plan.key,
                                     outgoingArm,
                                     outgoing);
            }
            else
            {
              this->retireSeatBranchRoot(context, outgoing);
            }
          }
          const bool declaresBranch = candidate.isSet();
          if (declaresBranch)
          {
            BoundaryBranchSeatState *outgoingScope = plan.seat()->declaredBranchSeats();
            if (outgoingScope)
              this->forgetBranchSeatDirtySources(*outgoingScope);
            plan.seat()->commitBranchDeclaration(candidate.take());
          }
          this->commitBranchSeatRegistrations(nestedRegistrations, this->branchSeats_);
          if (declaresBranch)
            declareBoundaryDirtySources(this, this);
          const bool committed = this->branchSeats_.commitActiveArm(plan.key, incoming, plan);
          assert(committed &&
                 "replacing a branch must preserve its definition-side seat mapping");
          if (!committed)
            return false;
          if (context.nodeStorage())
            incoming->markPrecomposedAttach();
          else
            incoming->markPendingAttachForCompose();
          this->noteLocalStructureWork();
          return true;
        }

        /** One serial admitted seat build; the ticket lives through ATTACH. */
        class AdmittedSeatBuild : public detail::NodeBuildOperation
        {
        public:
          AdmittedSeatBuild(BoundaryNode &owner, ComponentContext &context,
                           const BoundaryBranchSeatPlanEntry &plan,
                           const BoundaryBranchSeatRuntimeEntry &runtime)
              : owner_(owner), context_(context), plan_(plan), runtime_(runtime) {}
          virtual bool buildAndAttach(detail::NodeBuildTicket &ticket)
          {
            detail::SeatNodeStorageView storage(ticket);
            ComponentContext context(this->context_);
            context.setNodeStorage(&storage);
            context.setOwner(this->runtime_.parent);
            const bool built = this->owner_.replaceSeatBranch(context, this->plan_, this->runtime_, false, false);
            const detail::SeatReservation *reservation = this->plan_.seat()->seatReservation();
            if (ticket.layoutRefused() && reservation->replacementPolicy() == detail::PRESERVE_INSTALLED
                && !reservation->request().waiting())
              reservation->request().refuse();
            return built;
          }
        private:
          BoundaryNode &owner_;
          ComponentContext &context_;
          const BoundaryBranchSeatPlanEntry &plan_;
          const BoundaryBranchSeatRuntimeEntry &runtime_;
        };

        bool applyWaitingKeyedSeat(ComponentContext &context,
                                   BoundaryBranchSeatPlanEntry &plan,
                                   const BoundaryBranchSeatRuntimeEntry &runtime,
                                   const detail::SeatReservation &reservation)
        {
          detail::SeatBuildRequest &request = reservation.request();
          if (runtime.active && !plan.seat()->needsBranchDeclaration())
          {
            request.settle();
            return true;
          }
          request.mark();
          if (runtime.active)
          {
            Node *outgoing = runtime.active;
            if (!this->seatReservations_.removeSeatChild(request, runtime.parent, outgoing, plan.key.slot))
              return false;
            BoundaryBranchSeatState *scope = plan.seat()->declaredBranchSeats();
            if (scope)
            {
              this->retireDeclarationScope(context, *scope);
              this->forgetBranchSeatDirtySources(*scope);
            }
            plan.seat()->commitBranchDeclaration(0);
            this->retireSeatBranchRoot(context, outgoing);
            const bool vacated = this->branchSeats_.vacateActiveArm(plan.key);
            assert(vacated && "retiring an arm must preserve its seat mapping");
            if (!vacated)
              return false;
            this->noteLocalStructureWork();
            return true;
          }
          AdmittedSeatBuild build(*this, context, plan, runtime);
          request.admit(reservation.layoutTable(), build);
          return true;
        }

        bool applyBranchSeat(ComponentContext &context,
                             BoundaryBranchSeatPlanEntry &mutablePlan,
                             const BoundaryBranchSeatRuntimeEntry &runtime)
        {
          if (!mutablePlan.dirtySource)
          {
            return false;
          }
          const detail::SeatReservation *reservation = mutablePlan.seat()->seatReservation();
          if (reservation && reservation->request().enabled())
          {
            switch (reservation->replacementPolicy())
            {
            case detail::RETIRE_BEFORE_BUILD:
              return this->applyWaitingKeyedSeat(context, mutablePlan, runtime, *reservation);
            case detail::PRESERVE_INSTALLED:
              if (!mutablePlan.seat()->needsBranchDeclaration()) reservation->request().settle();
              else
              {
                AdmittedSeatBuild build(*this, context, mutablePlan, runtime);
                reservation->request().admit(reservation->layoutTable(), build);
              }
              return true;
            }
          }
          if (!runtime.active)
          {
            return false;
          }
          // Liveness is guaranteed by the sole caller, applyScopePlans, which
          // skips non-live runtimes before resolving the plan; the former
          // second isLive() here was a redundant owner-chain scan.
          if (mutablePlan.seat()->needsBranchDeclaration())
          {
            return this->replaceSeatBranch(context, mutablePlan, runtime, false, false);
          }
          if (mutablePlan.seat()->declaredBranchSeats())
          {
            return true;
          }
          mutablePlan.snapshotSelection();

          if (mutablePlan.hasSelectedArm != runtime.hasActiveArm ||
              (mutablePlan.hasSelectedArm &&
               mutablePlan.selectedArm != runtime.activeArm))
          {
            const bool parkOutgoing =
                runtime.hasActiveArm &&
                !mutablePlan.branch(runtime.activeArm).policies.destroyOnDetach;
            return this->replaceSeatBranch(context,
                                           mutablePlan,
                                           runtime,
                                           parkOutgoing,
                                           true);
          }

          return true;
        }

        bool applyCurrentBranchSeatPlan(ComponentContext &context)
        {
          return this->applyScopePlans(context, this->branchSeats_)
                 && this->applyBoundaryParkedBranches(context);
        }

        bool applyScopePlans(ComponentContext &context,
                             BoundaryBranchSeatState &scope)
        {
          const std::vector<BoundaryBranchSeatPlanEntry> &plans = scope.plans();
          for (size_t i = 0; i < plans.size(); ++i)
          {
            BoundaryBranchSeatRuntimeEntry runtime;
            if (!this->branchSeats_.queryRuntime(plans[i].key, runtime) ||
                !this->branchSeats_.isLive(runtime))
            {
              continue;
            }
            // One checked plan resolution per live seat. findPlan() still runs
            // its full multiplicity scan (0 on a contested key), so the release
            // wall stands; the plan supplies both the accepted definition and
            // the mutable entry applyBranchSeat() consumes. This replaces the
            // former findBranchSeatDefinition() + branchSeatPlan() +
            // findPlan(plan->key) triple, which all re-derived this same result.
            BoundaryBranchSeatPlanEntry *plan =
                this->branchSeats_.findPlan(plans[i].key);
            if (!plan || !plan->definition ||
                !this->applyBranchSeat(context, *plan, runtime))
            {
              return false;
            }
            BoundaryBranchSeatState *nested = plans[i].seat()->declaredBranchSeats();
            if (nested && !this->applyScopePlans(context, *nested))
            {
              return false;
            }
          }
          return true;
        }

        bool applyBoundaryParkedBranches(ComponentContext &context)
        {
          for (unsigned i = 0; BoundaryParkedBranchLedger::Entry *parked = this->parkedBranches_.entry(i); ++i)
          {
            BoundaryBranchSeatPlanEntry *plan = this->branchSeats_.findPlan(parked->key);
            BoundaryBranchSeatRuntimeEntry runtime;
            if (!plan || !this->branchSeats_.queryRuntime(plan->key, runtime) ||
                !plan->branch(parked->arm).policies.deliverWhileDetached)
            {
              continue;
            }
            NodeDefinitionBase *branchDefinition = plan->branch(parked->arm).definition;
            if (branchDefinition
                && !this->reconcileParkedBranch(context, parked->branch, branchDefinition, plan->key.scope))
            {
              return false;
            }
          }
          return true;
        }

        NodeDefinitionBase *findBranchSeatDefinition(const BoundaryParkedBranchKey &key) const
        {
          const BoundaryBranchSeatPlanEntry *plan = this->branchSeats_.findPlan(key);
          return plan ? plan->definition : 0;
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
            stateArena_.releaseState(state);
            return;
          }
          DestroyAdoptedHeapState(state);
        }

        virtual void reserveStates(size_t count)
        {
          ownedStates_.reserve(ownedStates_.size() + count);
          tracker_.reserveStates(count);
        }

        static void
        composeTree(Node *node, ComponentContext &parentContext, ComposeEvent event, BoundaryNode *currentBoundary)
        {
          if (!node)
          {
            return;
          }
          if (node->consumePrecomposedAttach(event)) return;
          detail::NodePartition *partition = node->partitionOwner();
          if (event == COMPOSE_EVENT_ATTACH && partition
              && (!parentContext.nodeStorage() || !parentContext.nodeStorage()->uses(partition)))
          {
            // Cold materialization and the normal ATTACH walk share the bank's
            // bounded quota. No sibling ATTACH is moved ahead of its creator.
            detail::NodeBuildTicket ticket(*partition);
            detail::SeatNodeStorageView storage(ticket);
            ComponentContext context(parentContext);
            context.setNodeStorage(&storage);
            composeTree(node, context, event, currentBoundary);
            return;
          }
          BoundaryNode *boundary;
          ComposableNode *composable;
          INestable *nestable;
          {
            PROFILE_SECTION("virt");
            boundary = node->asBoundary();
            composable = node->asComposable();
            nestable = node->asNestable();
          }

          IStateOwner *nodeStateOwner = node->asStateOwner();
          if (nodeStateOwner && event != COMPOSE_EVENT_DETACH
              && !nodeStateOwner->attachStateOwner(currentBoundary, parentContext.stateOwner()))
          {
            if (currentBoundary)
              currentBoundary->noteStateAllocationFailure();
            return;
          }

          if (boundary && event == COMPOSE_EVENT_DETACH)
          {
            boundary->setScene(0);
            boundary->setParentBoundary(0);
            boundary->clearObservedStateEntries();
          }
          // ComposeEvent is an input to the lifecycle state machine, not an
          // observable of its own: ATTACH passes the compose door; nodes and
          // contexts hear changes through the two fact observation points.
          if (event == COMPOSE_EVENT_ATTACH)
          {
            node->applyLifecycleFact(NODE_FACT_ATTACHED);
          }

          BoundaryNode *nextBoundary = currentBoundary;
          if (boundary)
          {
            if (event != COMPOSE_EVENT_DETACH)
            {
              boundary->setParentBoundary(currentBoundary);
              if (currentBoundary)
              {
                boundary->setScene(currentBoundary->getScene());
              }
            }
            boundary->clearObservedDirtyFlags();
            boundary->clearPhaseResults();
            nextBoundary = boundary;
          }
          ObservedStatePassScope observedScope(boundary, event);
          BoundaryComposePhaseScope composeScope =
              boundary ? boundary->beginComposePhaseScope() : BoundaryComposePhaseScope(0);
          if (boundary)
          {
            boundary->beginComposeResult(event, parentContext.dirtyFlags());
#if defined(LOKA_DEBUG_SCENE_UPDATE) && !defined(LOKA_RETRO68)
            loka::platform::DebugLogBoundaryComposeDispatch(static_cast<void *>(boundary),
                                                            static_cast<unsigned int>(event),
                                                            static_cast<unsigned int>(parentContext.dirtyFlags()),
                                                            boundary == currentBoundary ? 1 : 0);
#endif
          }
          if (nextBoundary && event != COMPOSE_EVENT_DETACH)
          {
            nextBoundary->noteLocalPaintWork();
            declareBoundaryDirtySources(node, nextBoundary);
          }
          const bool routesInnerStateOwner =
              nodeStateOwner && nodeStateOwner != boundary;
          ComponentContext *contextForChildren = &parentContext;
          ComponentContext nodeContext(&parentContext);
          {
            PROFILE_SECTION("ctx");
            nodeContext.setStateOwner(routesInnerStateOwner
                                          ? nodeStateOwner
                                          : parentContext.stateOwner());
            nodeContext.setBoundary(nextBoundary);
            nodeContext.setOwner(node);
            nodeContext.setNodeStorage(boundary ? 0 : parentContext.nodeStorage());
            nodeContext.setPlatformController(parentContext.platformController());
            Scene *scene = nextBoundary ? nextBoundary->getScene() : 0;
            if (event == COMPOSE_EVENT_ATTACH && scene && !boundary && !composable && !nestable)
              JoinSceneFocus(*scene, *node);
            nodeContext.setScene(scene);
            nodeContext.setWindow(parentContext.window());
            nodeContext.setDirtyFlags(parentContext.dirtyFlags());
            nodeContext.setComposition(parentContext.composition());
          }

          if (composable)
          {
            NodeComposition *composition = nodeContext.composition();
            if (composition)
            {
              NodeComposition::CompositionScope scope(*composition);
              composable->compose(nodeContext, event);
            }
            else
            {
              composable->compose(nodeContext, event);
            }
            if (boundary)
            {
              boundary->completeComposeResult();
            }
            contextForChildren = &nodeContext;
          }
          else if (routesInnerStateOwner)
          {
            contextForChildren = &nodeContext;
          }
          if (!nestable || (boundary && boundary->ownsChildTraversal(event)))
          {
            if (event == COMPOSE_EVENT_DETACH && nodeStateOwner)
            {
              nodeStateOwner->detachHeldResources();
            }
            return;
          }
          loka::dsl::CompositionCursor<Node> it(nestable->childrenHead(), nestable->childrenCount());
          for (Node *child = it.next(); child; child = it.next())
          {
            ComposeEvent childEvent = child->resolveChildComposeEvent(event);
            composeTree(child, *contextForChildren, childEvent, nextBoundary);
          }
          if (event == COMPOSE_EVENT_DETACH && nodeStateOwner)
          {
            nodeStateOwner->detachHeldResources();
          }
        }

        void registerState(loka::core::StateBase *state)
        {
          tracker_.addState(state);
        }

        // Varargs must end with a typed null terminator (use STATE_NULL from
        // core/util/StateUtil.hpp); a missing terminator reads past the argument list.
        void registerStates(loka::core::StateBase *first, ...)
        {
          loka::core::StateVector states;
          va_list args;
          va_start(args, first);
          for (loka::core::StateBase *s = first; s != 0; s = va_arg(args, loka::core::StateBase *))
          {
            if (s)
            {
              states.push_back(s);
            }
          }
          va_end(args);
          for (size_t i = 0; i < states.size(); ++i)
          {
            tracker_.addState(states[i]);
          }
        }

      private:
        friend class Scene;
        using ComposableNode::beginComposition;

        /** Reject an incomplete initial capture before any factory runs.
            Cost: once per capture; reset visits this composition's definitions.
            A surviving nonempty root is the established replay instruction. */
        bool finishInitialDeclaration()
        {
          if (this->compositionState_.allocationFailedValue())
          {
            this->composition().reset();
            return false;
          }
          this->captureBranchSeatPlan();
          return !this->compositionState_.allocationFailedValue();
        }

        /** Accept only a complete initial materialization, retaining the
            declaration for retry after refusal. Descendant ATTACH failure is
            a separate operation and is not covered by completeness.

            Cost: once per mount attempt; walks this declaration's factories
            and admitted children. Refused roots queue on this Boundary's clock. */
        bool materializeInitialChildren(ComponentContext &context);
        /** Discard a factory-only candidate: withdraw BindingToken callbacks,
            drop Held edges, then queue storage on its Boundary's reclaim clock.
            Immediate callbacks already run are not rolled back. Other state
            registrations retain their existing cleanup paths.
            Cost: once per discard; allocation-free child walk plus withdrawal
            of the candidate's callbacks and Held owner slots. */
        static void RetireUnattachedCandidate(Node *root, void *data);

        /** Captures the initial declaration for subsequent seat updates.
            Only kernel declaration owners and testing access may enter.
            The audit diagnostic detects capture over an admitted ledger. */
        void captureBranchSeatPlan()
        {
#ifdef LOKA_LIFECYCLE_AUDIT
          if (!this->branchSeats_.runtimeEmpty())
          {
            assert(false && "capturing a branch seat plan over a live runtime ledger");
            std::abort();
          }
#endif
          this->composition().assignCompositionSeatSlots();
          this->branchSeats_.capture(this->composition().root());
          if (this->prepareBranchSeatDirtySources(this->branchSeats_))
            this->registerBranchSeatDirtySources();
        }

      private:
        static void JoinSceneFocus(Scene &scene, Node &node);
        static void WithdrawCandidateBindings(Node *node);
        /** Reset provisional rows and declaration scopes after either initial
            rejection. Reuses this Boundary's plan/reset walks once per refusal. */
        void resetRejectedInitialChildren(ComponentContext &context);
        template <class T> NodeState<T> dangerouslyUseStateWithValue(const T &initial)
        {
          loka::core::MutableState<T> *state = new loka::core::MutableState<T>(initial);
          adoptState(state);
          return NodeState<T>(state, this->tracker(), this);
        }

        void clearOwnedStates()
        {
          for (size_t i = 0; i < ownedStates_.size(); ++i)
          {
            loka::core::StateBase *state = ownedStates_[i];
            if (!state)
            {
              continue;
            }
            if (state->isArenaAllocated())
            {
              continue;
            }
            DestroyAdoptedHeapState(state);
          }
          ownedStates_.clear();
        }

        void retireSubtree(Node *node);
        void destroyRetiredSubtree(Node *node, bool bounded = false);
        void destroyRetiredNode(Node *node);
        void drainRetiredSubtrees(bool bounded);
        static void ReclaimPartitionNode(Node *node, void *owner);
        static void ReclaimBoundedPartitionNode(Node *node, void *owner);
        static void ReclaimPlannedNode(Node *node, void *owner);
        void drainAllRetiredSubtrees();
        void releaseOwnedNodeStorage();

        void parkBranch(const BoundaryParkedBranchKey &key, Node *branch, unsigned arm)
        {
          this->parkedBranches_.park(key, branch, arm);
        }
        Node *takeParkedBranch(const BoundaryParkedBranchKey &key, unsigned arm)
        {
          return this->parkedBranches_.take(key, arm);
        }
        loka::core::PushStateTracker tracker_;
        std::vector<loka::core::StateBase *> ownedStates_;
        BoundaryRuntimeState runtimeState_;
        BoundaryUpdateState updateState_;
        BoundaryCompositionState compositionState_;
        BoundaryObservedState observedState_;
        BoundaryParkedBranchLedger parkedBranches_;
        BoundaryBranchSeatState branchSeats_;
        detail::SeatReservations seatReservations_;
        NodeArena nodeArena_;
        StateArena stateArena_;
        loka::core::HoldLedger holdLedger_;
        loka::core::detail::HeldBlockBase *pendingHeldReleasesHead_;
        loka::core::detail::HeldBlockBase *pendingHeldReleasesTail_;
        Node *retiredSubtreesHead_;
        Node *retiredSubtreesTail_;
        std::vector<detail::NodeArena::RetiredNodeGeneration> retiredGenerations_;
        bool drainingRetiredSubtrees_;

        template <class PropsT> friend class StdCompositionBoundaryNodeBase;
        friend class RootBoundaryWrapper;
        friend class ::loka::dsl::testing::SceneTestAccess;
        friend class GenerationDeclaration;
        friend class ::loka::dsl::testing::OwnershipDump;
#ifdef TEST_BUILD
        friend class ::loka::dsl::testing::PartitionReclaimAccess;
        friend class ::loka::dsl::testing::SeatBuildRequestAccess;
#endif

      };

      template <class PropsT, class NodeT> struct BoundaryDefinition : public NodeDefinition<PropsT, NodeT>
      {
        typedef NodeDefinition<PropsT, NodeT> BaseType;
        typedef int IsBoundaryDefinition;
        BoundaryDefinition()
            : BaseType()
        {
        }
        BoundaryDefinition(const PropsT &p)
            : BaseType(p)
        {
        }
        typedef BoundaryDefinition CloneType;
        virtual NodeDefinitionBase *clone() const
        {
          return new BoundaryDefinition(*this);
        }
        virtual bool isBoundary() const
        {
          return true;
        }
      };

    } // namespace scene
  } // namespace app
} // namespace loka

#endif // LOKA_CORE2_SCENE_BOUNDARY_BOUNDARY_HPP
