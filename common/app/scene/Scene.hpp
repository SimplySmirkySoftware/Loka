#ifndef LOKA_CORE2_SCENE_SCENE_HPP
#define LOKA_CORE2_SCENE_SCENE_HPP

#include "app/scene/SceneTextDocuments.hpp"
#include "core/Operation.hpp"
#include "core/diag/LifecycleAudit.hpp"
#include "core/State.hpp"
#include <cassert>
#if defined(LOKA_DEBUG_SCENE_UPDATE) && !defined(LOKA_RETRO68)
#include "platform/debug/DebugLog.hpp"
#endif
#include "app/scene/Node.hpp" // Required for NodeDefinitionBase.
#include "app/scene/projection/PlatformController.hpp"
#include "app/scene/context/ComponentContext.hpp"
#include "app/scene/composition/NodeComposition.hpp"
#include "app/scene/projection/PlatformApplyPlan.hpp"
#include "app/scene/SceneDirector.hpp"
#include "app/scene/boundary/Boundary.hpp"
#include "core/Profiler.hpp"
#include "core/scheduler/NextTickTracker.hpp"
#include "core/util/OwnedDef.hpp"
#include "dsl/composition/CompositionDiff.hpp"

class Window;
class SceneManager;

enum SceneLifecycle
{
  ON_CREATE = 0,
  ON_ATTACH = 1,
  ON_DETACH = 2
};

namespace loka
{
  namespace dsl
  {
    namespace testing
    {
      class SceneTestAccess;
    }
  } // namespace dsl

  namespace app
  {
    namespace detail
    {
      class SceneRetirePool;
    }

    namespace scene
    {
      // Forward declaration only. Include the concrete type where needed.
      struct NodeComposition;
      inline static bool CanRelaxFullRebuildForChildOnlyUpdate(const SceneDirector::SceneUpdateSnapshot &snapshot);
      inline static PlatformApplyPlan::PaintKind ResolvePaintKind(const SceneDirector::SceneUpdateSnapshot &snapshot);

      class RootBoundaryWrapper : public BoundaryNode
      {
      public:
        explicit RootBoundaryWrapper(NodeDefinitionBase *def)
            : def_(def),
              composed_(false)
        {
        }
        virtual ~RootBoundaryWrapper() {}

        /** The plain-root wrapper owns child traversal on ATTACH and UPDATE. */
        virtual bool ownsChildTraversal(ComposeEvent event) const
        {
          return event == COMPOSE_EVENT_ATTACH || event == COMPOSE_EVENT_UPDATE;
        }

      protected:
        void detachExistingChildren(ComponentContext &context)
        {
          loka::dsl::CompositionCursor<Node> it(this->childrenHead(), this->childrenCount());
          for (Node *child = it.next(); child; child = it.next())
          {
            this->composeTree(child, context, COMPOSE_EVENT_DETACH, this);
          }
        }

        virtual void composeWithContext(ComponentContext &context, ComposeEvent event)
        {
          if (event == COMPOSE_EVENT_DETACH)
          {
            this->detachExistingChildren(context);
            NodeComposition &composition = this->beginComposition(context);
            this->detachNode(composition);
            this->composed_ = false;
            return;
          }
          if (event != COMPOSE_EVENT_ATTACH && event != COMPOSE_EVENT_UPDATE)
          {
            return;
          }
          if (event == COMPOSE_EVENT_UPDATE && this->isFrozen())
          {
            return;
          }
          if (!def_)
          {
            return;
          }
          if (event == COMPOSE_EVENT_UPDATE)
          {
            if (this->composed_)
            {
              this->updateCompositionChildren(context);
              return;
            }
            event = COMPOSE_EVENT_ATTACH;
          }
          if (this->composition().root() && !this->composed_)
          {
            this->composed_ = this->materializeInitialChildren(context);
            return;
          }
          if (this->childrenHead())
          {
            this->detachExistingChildren(context);
            std::vector<Node *> detached;
            this->detachChildrenTo(detached);
            for (size_t i = 0; i < detached.size(); ++i)
            {
              this->retireDetachedNode(context, detached[i]);
            }
            this->retireOwnedNodeGeneration(context);
          }
          else
          {
            this->clearChildren();
          }
          NodeComposition *composition = &this->beginDeclaringWindow(context);
          this->attachNode(*composition);
          {
            NodeComposition::CompositionScope scope(*composition);
            composition->declare(*def_);
          }
          if (!this->finishInitialDeclaration())
            return;
          this->composed_ = this->materializeInitialChildren(context);
        }

      private:
        NodeDefinitionBase *def_;
        bool composed_;
      };

      class Scene LOKA_AUDITED(Scene)
      {
      public:
        struct SceneCompositionDiff : public loka::dsl::CompositionDiff
        {
          SceneCompositionDiff()
              : loka::dsl::CompositionDiff(),
                flags(NODE_DIRTY_NONE)
          {
            fullRebuild = false;
          }
          void clear()
          {
            loka::dsl::CompositionDiff::clear();
            flags = NODE_DIRTY_NONE;
            fullRebuild = false;
          }
          NodeDirtyFlags flags;
        };

        struct SceneUpdateCycleState
        {
          struct RetainedApplyState
          {
            RetainedApplyState()
                : lastApplyPlan(),
                  lastAppliedSnapshot()
            {
            }

            void clear()
            {
              lastApplyPlan.clear();
              lastAppliedSnapshot = SceneDirector::SceneUpdateSnapshot();
            }

            void record(const PlatformApplyPlan &plan, const SceneDirector::SceneUpdateSnapshot &snapshot)
            {
              lastApplyPlan = plan;
              lastAppliedSnapshot = snapshot;
            }

            const PlatformApplyPlan &applyPlan() const
            {
              return lastApplyPlan;
            }

            const SceneDirector::SceneUpdateSnapshot &appliedSnapshot() const
            {
              return lastAppliedSnapshot;
            }

          private:
            PlatformApplyPlan lastApplyPlan;
            SceneDirector::SceneUpdateSnapshot lastAppliedSnapshot;
          };

          SceneUpdateCycleState()
              : compositionDiff(),
                pendingSnapshot(),
                retainedApply()
          {
          }

          void discardPending()
          {
            clearPendingState();
          }

          void discardRetainedState()
          {
            clearPendingState();
            retainedApply.clear();
          }

          void applyRequestSnapshot(const SceneDirector::SceneUpdateRequestSnapshot &request)
          {
            compositionDiff.flags = request.effectiveDirtyFlagsValue();
            compositionDiff.fullRebuild = request.effectiveFullRebuildRequired();
          }

          void beginRefresh(const SceneDirector::SceneUpdateRequestSnapshot &request)
          {
            compositionDiff.valid = true;
            applyRequestSnapshot(request);
          }

          void recordPendingSnapshot(const SceneDirector::SceneUpdateSnapshot &snapshot)
          {
            pendingSnapshot = snapshot;
            applyRequestSnapshot(pendingSnapshot.request());
          }

          void completeApply(const PlatformApplyPlan &plan)
          {
            retainedApply.record(plan, pendingSnapshot);
            clearPendingState();
          }

          NodeDirtyFlags effectiveApplyDirtyFlags() const
          {
            return compositionDiff.flags == NODE_DIRTY_NONE ? NODE_DIRTY_PROPS : compositionDiff.flags;
          }

          NodeDirtyFlags refreshDirtyFlags() const
          {
            return compositionDiff.flags;
          }

          bool refreshFullRebuild() const
          {
            return compositionDiff.fullRebuild;
          }

          NodeDirtyFlags effectivePendingRequestDirtyFlags() const
          {
            return pendingSnapshot.request().effectiveDirtyFlagsValue();
          }

          NodeDirtyFlags aggregateTransactionDirtyFlags() const
          {
            return pendingSnapshot.request().transactionDirtyFlagsValue();
          }

          bool refreshStructureRequired() const
          {
            return pendingSnapshot.apply().structureRequired();
          }

          bool refreshLayoutRequired() const
          {
            return pendingSnapshot.apply().layoutRequired();
          }

          const SceneCompositionDiff &compositionDiffValue() const
          {
            return compositionDiff;
          }

          const PlatformApplyPlan &lastApplyPlanValue() const
          {
            return retainedApply.applyPlan();
          }

          const SceneDirector::SceneUpdateSnapshot &pendingSnapshotValue() const
          {
            return pendingSnapshot;
          }

          const SceneDirector::SceneUpdateSnapshot &lastAppliedSnapshotValue() const
          {
            return retainedApply.appliedSnapshot();
          }

        private:
          void clearPendingState()
          {
            compositionDiff.clear();
            pendingSnapshot = SceneDirector::SceneUpdateSnapshot();
          }

          SceneCompositionDiff compositionDiff;
          SceneDirector::SceneUpdateSnapshot pendingSnapshot;
          RetainedApplyState retainedApply;
        };
        // Accept Boundary definitions only (compile-time check via IsBoundaryDefinition).
        template <class DefT>
        explicit Scene(DefT *def, typename DefT::IsBoundaryDefinition * = 0)
            : lifecycle_(ON_CREATE),
              attached_(false),
              rootDefinition_(def),
              rootNode_(0),
              platformController_(0),
              window_(0),
              retiredNextScene_(0),
              mounted_(false),
              composed_(false),
              director_(),
              updateCycleState_(),
              whiteFlagFullRebuildPending_(false),
              cycleWhiteFlagFullRebuild_(false)
        {
          assert(def && "Scene requires a root definition");
          director_.attach(this);
        }
        // Construct from NodeDefinitionBase and auto-wrap non-boundary roots.
        explicit Scene(NodeDefinitionBase *def)
            : lifecycle_(ON_CREATE),
              attached_(false),
              rootDefinition_(def),
              rootNode_(0),
              platformController_(0),
              window_(0),
              retiredNextScene_(0),
              mounted_(false),
              composed_(false),
              director_(),
              updateCycleState_(),
              whiteFlagFullRebuildPending_(false),
              cycleWhiteFlagFullRebuild_(false)
        {
          assert(def && "Scene requires a root definition");
          director_.attach(this);
        }
        // Clone and take ownership of the root definition.
        template <class DefT>
        explicit Scene(const DefT &def, typename DefT::IsBoundaryDefinition * = 0)
            : lifecycle_(ON_CREATE),
              attached_(false),
              rootDefinition_(def.clone()),
              rootNode_(0),
              platformController_(0),
              window_(0),
              retiredNextScene_(0),
              mounted_(false),
              composed_(false),
              director_(),
              updateCycleState_(),
              whiteFlagFullRebuildPending_(false),
              cycleWhiteFlagFullRebuild_(false)
        {
          director_.attach(this);
        }
        virtual ~Scene()
        {
#ifdef LOKA_LIFECYCLE_AUDIT
          assert(!this->isBorrowOpen());
#endif
          director_.detach();
          unmount();
          rootDefinition_.reset();
        }

        NodeDefinitionBase *getRootDefinition() const
        {
          return rootDefinition_.get();
        }
        Window *getWindow() const
        {
          return window_;
        }
        void setWindow(Window *window)
        {
          window_ = window;
        }
        const SceneCompositionDiff &compositionDiff() const
        {
          return updateCycleState_.compositionDiffValue();
        }
        SceneDirector &director()
        {
          return director_;
        }
        const SceneDirector &director() const
        {
          return director_;
        }
        /** Published menu snapshot; borrows root-owned State/Emitter endpoints.
            Null before complete composition and after detach. */
        const MenuBarDefinition *menuBar() const
        {
          return this->menuBar_.get();
        }

        size_t liveNodeCount() const
        {
          return countLiveNodes(rootNode_);
        }

        // Expose lifecycle and attached states for observation.
        loka::core::State<SceneLifecycle> *getLifecycleState()
        {
          return &lifecycle_;
        }
        loka::core::State<bool> *getAttachedState()
        {
          return &attached_;
        }

      private:
        /** Structural lifecycle primitives belong to the seat and Window death
            path. Application callers use SceneManager requests at admission. */
        void updateAttached(bool v)
        {
          setAttached(v);
          if (mounted_)
          {
            if (v)
            {
              composeIfNeeded(COMPOSE_EVENT_ATTACH);
              if (composed_ && director_.firstPendingBoundary())
                queueInvalidate();
            }
            else
            {
              // The shared detach path clears queued boundary targets before
              // a later attach can reuse the Scene.
              this->detachComposition();
            }
          }
        }
        void updateLifecycle(SceneLifecycle v)
        {
          setLifecycle(v);
        }

      public:
        /** True after a mount whose root creation or attach compose was refused for
            lack of memory: nothing is composed and the window shows no content until
            an external refresh retries (#132 ruling 3). */
        bool composeRefusedForMemory() const
        {
          return this->mounted_ && !this->composed_ && this->whiteFlagFullRebuildPending_;
        }

        bool mount(IPlatformController *platformController)
        {
          assert(platformController && "Scene::mount requires a platform controller");
          if (this->mounted_ || platformController->borrowPhase().open())
          {
#ifdef LOKA_LIFECYCLE_AUDIT
            assert(false && "Scene mount during a borrow or remount");
#endif
            return false;
          }
          platformController_ = platformController;
          mounted_ = true;
          ensureRootNode();
          if (attached_.get())
          {
            composeIfNeeded(COMPOSE_EVENT_ATTACH);
          }
          return true;
        }

      private:
        void unmount()
        {
#ifdef LOKA_LIFECYCLE_AUDIT
          assert(!this->isBorrowOpen());
#endif
          this->detachComposition();
          mounted_ = false;
          platformController_ = 0;
        }

      public:
        void requestInvalidate(NodeDirtyFlags flags = NODE_DIRTY_PROPS)
        {
#if defined(LOKA_DEBUG_SCENE_UPDATE) && !defined(LOKA_RETRO68)
          const bool alreadyPending = nextTickTracker_.hasPendingRequest();
          if (alreadyPending)
          {
            loka::platform::DebugLogSceneUpdateMerged(static_cast<void *>(this));
          }
          else
          {
            loka::platform::DebugLogSceneUpdateQueued(static_cast<void *>(this));
          }
#endif
          SceneDirector::BoundaryUpdateRequest request;
          request.flags = flags == NODE_DIRTY_NONE ? NODE_DIRTY_PROPS : flags;
          request.flushImmediately = false;
          BoundaryNode *rootBoundary = rootNode_ ? rootNode_->asBoundary() : 0;
          if (rootBoundary)
          {
            request.boundary = rootBoundary;
            director_.registerBoundaryUpdate(request);
          }
          queueInvalidate();
        }

        void requestBoundaryUpdate(BoundaryNode *boundary, NodeDirtyFlags flags, bool flushImmediately)
        {
          director_.requestBoundaryUpdate(boundary, flags, flushImmediately);
        }

        bool flushInvalidation()
        {
          if (this->isBorrowOpen() || loka::core::Operation::isSettling())
            return false;
          return nextTickTracker_.run(&Scene::RefreshThunk, &Scene::ApplyThunk, this);
        }

        /** True throughout refresh and apply, including direct invalidate()
            runs. Admission must preserve this Scene until the run returns. */
        bool isRunInProgress() const
        {
          return this->nextTickTracker_.inProgress();
        }

        /** Focus membership survives parking and is emptied by composition teardown. */
        SceneFocus &focus() { return this->focus_; }
        SceneTextDocuments &textDocuments()
        {
          return this->textDocuments_;
        }

        /** The controller borrow interval is independent of focus publication. */
        bool isBorrowOpen() const
        {
          return this->platformController_ && this->platformController_->borrowPhase().open();
        }

        /** Admission and close reclamation exclude all active borrow phases. */
        bool isBusy() const
        {
          return this->isRunInProgress() || this->focus_.isPublishing() || this->textDocuments_.isPublishing()
                 || this->isBorrowOpen();
        }

        bool hasPendingInvalidation() const
        {
          return nextTickTracker_.hasPendingRequest();
        }

        /** Requests retirement reclamation at the next tracker run, never in
            the current tracker drain loop. */
        void requestRetiredSubtreeDrainAfterRun()
        {
          nextTickTracker_.requestAfterRun();
        }

        /** Requeues layout work without entering the current scheduler drain. */
        void requestLayoutAfterRun()
        {
          this->requestInvalidate(NODE_DIRTY_LAYOUT);
          this->nextTickTracker_.requestAfterRun();
        }

        void invalidate(NodeDirtyFlags flags = NODE_DIRTY_PROPS)
        {
          requestInvalidate(flags);
          flushInvalidation();
        }

        /** Records a compose allocation refusal. The next external update
            requests full platform re-projection; an uncomposed root retries
            ATTACH. Mounted boundaries still update their existing children
            and seats. Recording never requests a tick. */
        void noteComposeAllocationFailure()
        {
          whiteFlagFullRebuildPending_ = true;
        }

      protected:
        void setAttached(bool v)
        {
          attached_.set(v);
        }
        void setLifecycle(SceneLifecycle v)
        {
          lifecycle_.set(v);
        }

        loka::core::MutableState<SceneLifecycle> lifecycle_;
        loka::core::MutableState<bool> attached_;
        loka::core::OwnedDef<NodeDefinitionBase> rootDefinition_;
        SceneFocus focus_;
        SceneTextDocuments textDocuments_;
        Node *rootNode_;
        loka::core::OwnedDef<MenuBarDefinition> menuBar_;
        IPlatformController *platformController_;
        Window *window_;
        Scene *retiredNextScene_;
        bool mounted_;
        bool composed_;
        loka::core::NextTickTracker nextTickTracker_;
        SceneDirector director_;
        SceneUpdateCycleState updateCycleState_;
        /** White flag recorded by a failed compose (#132 ruling 3), armed
            until the next externally caused refresh cycle consumes it. It
            never schedules a tick by itself. */
        bool whiteFlagFullRebuildPending_;
        /** Latched at refresh begin for the cycle in flight: folds the full
            rebuild into both request snapshot builds of that cycle. */
        bool cycleWhiteFlagFullRebuild_;

        // SceneManager owns lifecycle_/attached mutations.
        friend class ::SceneManager;
        friend class ::Window;
        friend class loka::app::detail::SceneRetirePool;
        friend class SceneDirector;
        friend class TextDocumentRegistration;
        friend class ::loka::dsl::testing::SceneTestAccess;

      private:
        void queueInvalidate()
        {
          nextTickTracker_.request();
        }

        static bool RefreshThunk(void *userData)
        {
          Scene *scene = static_cast<Scene *>(userData);
          if (!scene)
          {
            return false;
          }
          drainLiveBoundaryRetireQueues(scene->rootNode_);
          // Reclamation alone is silent and must not produce an apply snapshot.
          return scene->refreshComposition();
        }

        static void ApplyThunk(void *userData)
        {
          Scene *scene = static_cast<Scene *>(userData);
          if (scene)
          {
            scene->applyComposition();
          }
        }

        bool beginPendingRefreshCycle()
        {
          if (!director_.beginRefreshCycle())
          {
            return false;
          }
          // Consume the recorded white flag into this externally caused
          // cycle. A compose failure during the cycle re-arms it, so a still
          // starved machine keeps waiting for the next external drive
          // instead of looping.
          cycleWhiteFlagFullRebuild_ = whiteFlagFullRebuildPending_;
          whiteFlagFullRebuildPending_ = false;
          updateCycleState_.beginRefresh(
              whiteFlagAdjustedRequestSnapshot(director_.buildRefreshRequestSnapshot(rootNode_)));
          return true;
        }

        /** Folds a consumed white flag into both request captures. CHILD
            requests the platform structure pass, and re-derivation after
            relaxation forces full re-projection of the current logical tree.
            It does not redeclare a mounted root. */
        SceneDirector::SceneUpdateRequestSnapshot
        whiteFlagAdjustedRequestSnapshot(const SceneDirector::SceneUpdateRequestSnapshot &request) const
        {
          if (!cycleWhiteFlagFullRebuild_)
          {
            return request;
          }
          SceneDirector::SceneUpdateRequestSnapshot adjusted = request;
          adjusted.includeDirtyFlags(NODE_DIRTY_CHILD);
          adjusted.deriveEffectiveFullRebuild();
          return adjusted;
        }

        SceneDirector::SceneUpdateSnapshot buildPendingRefreshSnapshot() const
        {
          SceneDirector::SceneUpdateSnapshot snapshot = director_.buildUpdateSnapshot(rootNode_, this);
          if (!cycleWhiteFlagFullRebuild_ || !snapshot.hasGeneration())
          {
            return snapshot;
          }
          return SceneDirector::SceneUpdateSnapshot(snapshot.generationValue(),
                                                    whiteFlagAdjustedRequestSnapshot(snapshot.request()),
                                                    snapshot.apply());
        }

        void capturePendingRefreshSnapshot()
        {
          updateCycleState_.recordPendingSnapshot(buildPendingRefreshSnapshot());
        }

        void composePendingRefreshCycle()
        {
          notifyComposeEvent(COMPOSE_EVENT_UPDATE);
        }

        void *sceneIdentity() const
        {
          return static_cast<void *>(const_cast<Scene *>(this));
        }

        void logRefreshDecision() const
        {
#if defined(LOKA_DEBUG_SCENE_UPDATE) && !defined(LOKA_RETRO68)
          loka::platform::DebugLogSceneDecision(sceneIdentity(),
                                                updateCycleState_.refreshStructureRequired() ? 1 : 0,
                                                updateCycleState_.refreshLayoutRequired() ? 1 : 0);
#endif
        }

        void logRefreshFlags() const
        {
#if defined(LOKA_DEBUG_SCENE_UPDATE) && !defined(LOKA_RETRO68)
          loka::platform::DebugLogSceneFlags(
              sceneIdentity(),
              "refresh",
              static_cast<unsigned int>(updateCycleState_.refreshDirtyFlags()),
              static_cast<unsigned int>(updateCycleState_.effectivePendingRequestDirtyFlags()),
              updateCycleState_.refreshFullRebuild() ? 1 : 0);
#endif
        }

        void logApplyFlags(NodeDirtyFlags flags) const
        {
#if defined(LOKA_DEBUG_SCENE_UPDATE) && !defined(LOKA_RETRO68)
          loka::platform::DebugLogSceneFlags(
              sceneIdentity(),
              "apply",
              static_cast<unsigned int>(flags),
              static_cast<unsigned int>(updateCycleState_.aggregateTransactionDirtyFlags()),
              updateCycleState_.refreshFullRebuild() ? 1 : 0);
#else
          (void)flags;
#endif
        }

        void executePendingApplyCycle(NodeDirtyFlags flags)
        {
          updateCycleState_.completeApply(buildExecutedApplyPlan(flags));
        }

        PlatformApplyPlan buildExecutedApplyPlan(NodeDirtyFlags flags)
        {
          return director_.executeApplyPlan(rootNode_,
                                            platformController_,
                                            updateCycleState_.pendingSnapshotValue(),
                                            flags,
                                            updateCycleState_.refreshFullRebuild());
        }

        void clearPendingRefreshCycle()
        {
          updateCycleState_.discardPending();
          director_.clearUpdateTransaction();
        }

        void clearMountedUpdateState()
        {
          updateCycleState_.discardRetainedState();
          director_.clearUpdateTransaction();
          whiteFlagFullRebuildPending_ = false;
          cycleWhiteFlagFullRebuild_ = false;
        }

        void ensureRootNode()
        {
          if (rootNode_)
          {
            return;
          }
          assert(rootDefinition_.isSet() && "Scene requires a root definition");
          if (rootDefinition_->isBoundary())
          {
            rootNode_ = rootDefinition_->create();
            if (!rootNode_)
            {
              // The allocation gate refused the boundary root (#132 ruling 3 /
              // #140 P2). Do not assert: arm the scene white flag and leave the
              // scene uncomposed. The next externally caused refresh retries
              // root creation once the backend heals. Recording never schedules
              // a tick, so a starved machine waits instead of busy-retrying.
              noteComposeAllocationFailure();
              return;
            }
          }
          else
          {
            // A non-boundary root wraps in a plain-new RootBoundaryWrapper,
            // which aborts on OOM via the #137 nothrow-abort operators (never
            // returns 0), so the assert below stays a genuine invariant.
            rootNode_ = new RootBoundaryWrapper(rootDefinition_.get());
          }
          if (!rootNode_)
          {
            // create() returned 0: the allocation gate's backend gave up
            // (#132 S2b). Leave the root unbuilt; mount()/composeIfNeeded()
            // guard a null root under the #70 nullable-creation contract.
            // Surfacing this as an app-visible failure is deferred to S3.
            return;
          }
          BoundaryNode *boundary = rootNode_->asBoundary();
          assert(boundary && "Scene root must be a Boundary node");
          boundary->setScene(this);
          boundary->setParentBoundary(0);
        }

        void composeIfNeeded(ComposeEvent event, bool publish = true)
        {
          if (composed_ || !platformController_)
          {
            return;
          }
          if (!rootNode_)
          {
            ensureRootNode();
          }
          if (!rootNode_)
          {
            // Root allocation was refused (#132 ruling 3 / #140 P2). The white
            // flag is armed by ensureRootNode; stay uncomposed so nothing
            // derefs a null root. A later externally caused refresh retries
            // this path via refreshComposition once the backend heals.
            return;
          }
          BoundaryNode *boundary = rootNode_->asBoundary();
          assert(boundary && "Scene root must be a Boundary node");
          ComponentContext rootContext;
          rootContext.setBoundary(boundary);
          rootContext.setPlatformController(platformController_);
          rootContext.setScene(this);
          rootContext.setWindow(this->getWindow());
          if (rootDefinition_.isSet() && !rootDefinition_->isBoundary())
          {
            BoundaryNode::composeSubtree(rootNode_, rootContext, event, 0);
            if (boundary->composeResult().allocationFailed)
            {
              // Keep the white flag armed; retry ATTACH on the next external refresh.
              return;
            }
          }
          else
          {
            BoundaryNode::ObservedStatePassScope observedScope(boundary, event);
            prepareRootBoundaryCompose(boundary, rootContext, event);
            if (event == COMPOSE_EVENT_ATTACH)
              boundary->attachParticipants(*this);
            boundary->compose(rootContext, event);
            completeRootBoundaryCompose(boundary);
            if (boundary->composeResult().allocationFailed)
            {
              // White flag (#132 ruling 3, Codex P2): root create() succeeded
              // but the attach compose refused a state or child-node
              // allocation, so completeRootBoundaryCompose already converted
              // this window into a projection failure and (re-)armed the scene
              // white flag via Scene::noteComposeAllocationFailure. Do not
              // publish a partial root or mark the scene composed: leaving
              // composed_ false keeps the armed flag alive (refreshComposition
              // only clears it on a clean compose), so the next externally
              // caused refresh retries the attach once the backend heals
              // instead of stranding partial content and losing the retry.
              return;
            }
          }
          this->menuBar_.reset(boundary->composition().menuBar_.take());
          if (publish)
            this->projectInitial();
          composed_ = true;
        }

        void projectInitial()
        {
          this->textDocuments_.createStores(*this->platformController_);
          this->platformController_->onChange(this->rootNode_, NODE_DIRTY_INITIAL, true);
        }

        void notifyComposeEvent(ComposeEvent event)
        {
          if (!rootNode_)
          {
            return;
          }
          BoundaryNode *boundary = rootNode_->asBoundary();
          if (!boundary)
          {
            return;
          }
          ComponentContext rootContext;
          rootContext.setBoundary(boundary);
          rootContext.setPlatformController(platformController_);
          rootContext.setScene(this);
          rootContext.setWindow(this->getWindow());
          rootContext.setDirtyFlags(updateCycleState_.refreshDirtyFlags());
          if (event != COMPOSE_EVENT_UPDATE && rootDefinition_.isSet() && !rootDefinition_->isBoundary())
          {
            BoundaryNode::composeSubtree(rootNode_, rootContext, event, 0);
          }
          else
          {
            BoundaryNode::ObservedStatePassScope observedScope(boundary, event);
            prepareRootBoundaryCompose(boundary, rootContext, event);
            if (event == COMPOSE_EVENT_ATTACH)
              boundary->attachParticipants(*this);
            boundary->compose(rootContext, event);
            if (event == COMPOSE_EVENT_DETACH)
            {
              loka::dsl::CompositionCursor<Node> it(boundary->childrenHead(), boundary->childrenCount());
              for (Node *child = it.next(); child; child = it.next())
              {
                BoundaryNode::composeSubtree(child, rootContext, event, boundary);
              }
              // The direct-root path bypasses composeTree for the Boundary
              // itself. Its owner slots still cross the same synchronous
              // detach line, after descendant owners have dropped theirs.
              boundary->detachHeldResources();
            }
            completeRootBoundaryCompose(boundary);
          }
        }

        static void
        prepareRootBoundaryCompose(BoundaryNode *boundary, ComponentContext &rootContext, ComposeEvent event)
        {
          if (!boundary)
          {
            return;
          }
          if (event == COMPOSE_EVENT_DETACH)
          {
            boundary->setScene(0);
            boundary->setParentBoundary(0);
            boundary->clearObservedStateEntries();
          }
          else
          {
            boundary->setParentBoundary(0);
          }
          if (event != COMPOSE_EVENT_UPDATE)
          {
            boundary->clearPhaseResults();
          }
          else
          {
            boundary->clearTransitionFactsForCycle();
          }
          boundary->beginComposeResult(event, rootContext.dirtyFlags());
          // Keep the direct-root union reset after retained-seat registration,
          // as before; only refusal recording moves inside the open result.
          boundary->clearObservedDirtyFlags();
          if (event != COMPOSE_EVENT_DETACH)
          {
            // #127: the generic composeTree walk registers a boundary's own
            // declared dirty sources; the direct-root path bypasses composeTree,
            // so without this the root boundary's ordinary observed state is
            // never (re-)bound and a later write is silently dropped.
            BoundaryNode::declareBoundaryDirtySources(boundary, boundary);
          }
        }

        static void completeRootBoundaryCompose(BoundaryNode *boundary)
        {
          if (!boundary)
          {
            return;
          }
          boundary->completeComposeResult();
        }

        bool refreshComposition()
        {
          if (!attached_.get())
          {
            if (!composed_)
              clearPendingRefreshCycle();
            return false;
          }
          // #132 ruling 3 / #144: retry a refused mount only on external
          // boundary work. Candidate retirement queues a drain-only run, which
          // must not start another materialization and retirement cycle.
          // With no root there can be no candidate retirement; retain the
          // root-factory retry. The plain-root wrapper is itself a Boundary.
          if (!composed_ && mounted_ && platformController_ && whiteFlagFullRebuildPending_
              && (!rootNode_ || director_.firstPendingBoundary()))
          {
            composeIfNeeded(COMPOSE_EVENT_ATTACH);
            if (composed_)
            {
              whiteFlagFullRebuildPending_ = false;
            }
          }
          if (!mounted_ || !platformController_ || !composed_)
          {
            clearPendingRefreshCycle();
            return false;
          }
          if (director_.isUpdateCycleActive())
          {
            return false;
          }
          if (!beginPendingRefreshCycle())
          {
            clearPendingRefreshCycle();
            return false;
          }
          composePendingRefreshCycle();
          capturePendingRefreshSnapshot();
          logRefreshDecision();
          logRefreshFlags();
          return true;
        }

        void applyComposition()
        {
          if (!platformController_ || !rootNode_)
          {
            return;
          }
          director_.beginApplyCycle();
          if (whiteFlagFullRebuildPending_)
          {
            // A compose white flag surrendered this cycle (#132 ruling 3):
            // the compose is a projection failure, so keep the platform on
            // the previously applied content and discard the pending
            // snapshot. The recorded full rebuild re-projects everything at
            // the next externally caused update; requesting a tick here
            // would busy-retry a starved machine.
            updateCycleState_.discardPending();
            director_.completeUpdateCycle();
            return;
          }
          platformController_->beginApplyCycle();
          // Living lifecycle transitions (A <-> D) reach contexts here, at
          // the head of apply — before projection, so the frame presents the
          // delivered state. A same-tick round trip nets out to silence.
          Node::DeliverLifecycleFactsSubtree(rootNode_);
          const NodeDirtyFlags flags = updateCycleState_.effectiveApplyDirtyFlags();
          logApplyFlags(flags);
          executePendingApplyCycle(flags);
          director_.completeUpdateCycle();
        }

        /** Runs the detach walk and teardown inside this Scene's run window,
            so a write from a detach hook or a Held releaser queues the next
            run instead of entering one (#45 item 1). Calling this from the
            Scene's own refresh/apply is misuse: the outer pass would resume
            on destroyed nodes. An inert nested RunScope is bookkeeping, not
            permission. */
        void detachComposition()
        {
          loka::core::NextTickTracker::RunScope run(this->nextTickTracker_);
          this->teardownComposition();
          this->clearMountedUpdateState();
          // A request made during the walk can survive the window. Its next
          // run finds a detached/unmounted Scene and does not compose, except
          // after remount root-allocation refusal (!rootNode_ with the white
          // flag): it allows one more existing bounded ATTACH retry. Cleared
          // boundary entries never route it back to the torn-down boundaries.
        }

        void teardownComposition()
        {
#ifdef LOKA_LIFECYCLE_AUDIT
          assert(!this->isBorrowOpen());
#endif
          // Revoke projection borrows before any app detach hook or release.
          if (platformController_)
            platformController_->releaseMenu();
          this->menuBar_.reset();
          this->notifyComposeEvent(COMPOSE_EVENT_DETACH);
          if (!rootNode_)
          {
            this->focus_.disconnectAll();
            this->textDocuments_.disconnectAll();
            return;
          }
          Node::MarkSubtreeLifecycleFact(rootNode_, NODE_FACT_RETIRED);
          // Teardown drains its own queues first: the detach line has already
          // dropped every owner slot, and a queued Held releaser is observable
          // app code. Running it here keeps reclamation callback-free instead
          // of firing it from a Boundary destructor.
          drainLiveBoundaryRetireQueues(rootNode_);
          if (platformController_)
          {
            platformController_->releaseNodeContexts(rootNode_);
            platformController_->destroy();
          }
          composed_ = false;
          // Gate-created root or plain-new RootBoundaryWrapper; never arena.
          DestroyHeapNode(rootNode_);
          rootNode_ = 0;
          this->focus_.disconnectAll();
          this->textDocuments_.disconnectAll();
        }

        static size_t countLiveNodes(Node *node)
        {
          if (!node)
          {
            return 0;
          }
          size_t count = 1;
          INestable *nestable = node->asNestable();
          if (!nestable)
          {
            return count;
          }
          loka::dsl::CompositionCursor<Node> it(nestable->childrenHead(), nestable->childrenCount());
          for (Node *child = it.next(); child; child = it.next())
          {
            count += countLiveNodes(child);
          }
          return count;
        }

        static void drainLiveBoundaryRetireQueues(Node *node)
        {
          if (!node)
          {
            return;
          }
          BoundaryNode *boundary = node->asBoundary();
          if (boundary)
          {
            boundary->drainRetiredSubtreesAtNextTrackerRun();
          }
          INestable *nestable = node->asNestable();
          if (!nestable)
          {
            return;
          }
          loka::dsl::CompositionCursor<Node> it(nestable->childrenHead(), nestable->childrenCount());
          for (Node *child = it.next(); child; child = it.next())
          {
            drainLiveBoundaryRetireQueues(child);
          }
        }

        // Default constructor intentionally not implemented to forbid rootless scenes
        Scene();
      };

      inline SceneDirector::SceneDirector()
          : scene_(0),
            updateTransaction_(),
            activeTransaction_(),
            phase_(UPDATE_CYCLE_IDLE)
      {
      }

      inline void SceneDirector::attach(Scene *scene)
      {
        scene_ = scene;
        clearUpdateTransaction();
      }

      inline void SceneDirector::detach()
      {
        scene_ = 0;
        clearUpdateTransaction();
      }

      inline bool SceneDirector::beginRefreshCycle()
      {
        if (phase_ != UPDATE_CYCLE_IDLE)
        {
          return false;
        }
        if (!updateTransaction_.hasAccumulatedUpdates())
        {
          return false;
        }
        activeTransaction_.swapTransaction(updateTransaction_);
        updateTransaction_.retainGeneration(activeTransaction_.generationValue());
        phase_ = UPDATE_CYCLE_REFRESH;
        return activeTransaction_.hasAccumulatedUpdates();
      }

      inline void SceneDirector::beginApplyCycle()
      {
        if (phase_ == UPDATE_CYCLE_REFRESH)
        {
          phase_ = UPDATE_CYCLE_APPLY;
        }
      }

      inline void SceneDirector::completeUpdateCycle()
      {
        activeTransaction_.clearTransaction();
        phase_ = UPDATE_CYCLE_IDLE;
        if (scene_ && updateTransaction_.hasAccumulatedUpdates())
        {
          scene_->queueInvalidate();
        }
      }

      inline SceneDirector::UpdateCyclePhase SceneDirector::phase() const
      {
        return phase_;
      }

      inline bool SceneDirector::isUpdateCycleActive() const
      {
        return phase_ != UPDATE_CYCLE_IDLE;
      }

      inline void
      SceneDirector::requestBoundaryUpdate(BoundaryNode *boundary, NodeDirtyFlags flags, bool flushImmediately)
      {
        if (!scene_)
        {
          return;
        }
        const BoundaryUpdateRequest request = normalizeBoundaryUpdateRequest(boundary, flags, flushImmediately);
        registerBoundaryUpdate(request);
        applyBoundaryUpdateRequest(request);
      }

      inline SceneDirector::BoundaryUpdateRequest SceneDirector::normalizeBoundaryUpdateRequest(
          BoundaryNode *boundary, NodeDirtyFlags flags, bool flushImmediately) const
      {
        BoundaryUpdateRequest request(boundary, flags, flushImmediately);
        if (request.flags == NODE_DIRTY_NONE)
        {
          request.flags = NODE_DIRTY_PROPS;
        }
        return request;
      }

      inline void SceneDirector::registerBoundaryUpdate(const BoundaryUpdateRequest &request)
      {
        if (!scene_ || !request.boundary)
        {
          return;
        }
        // New requests always accumulate into the pending transaction. When a
        // cycle is active, they become work for the next run.
        updateTransaction_.enqueueBoundaryUpdate(request);
      }

      inline void SceneDirector::applyBoundaryUpdateRequest(const BoundaryUpdateRequest &request) const
      {
        if (!scene_)
        {
          return;
        }
        if (isUpdateCycleActive())
        {
          return;
        }
        scene_->queueInvalidate();
        if (request.flushImmediately)
        {
          scene_->flushInvalidation();
        }
      }

      inline NodeDirtyFlags SceneDirector::aggregateDirtyFlags() const
      {
        return transactionForCurrentCycle().aggregateDirtyFlags();
      }

      inline NodeDirtyFlags SceneDirector::requestedDirtyFlags() const
      {
        return transactionForCurrentCycle().requestedDirtyFlags();
      }

      inline NodeDirtyFlags SceneDirector::effectiveRequestedDirtyFlags() const
      {
        return transactionForCurrentCycle().effectiveRequestedDirtyFlags();
      }

      inline bool SceneDirector::hasRequestedInput() const
      {
        return transactionForCurrentCycle().hasRequestedInput();
      }

      inline bool SceneDirector::requestedFullRebuild() const
      {
        return transactionForCurrentCycle().requestedFullRebuild();
      }

      inline NodeDirtyFlags SceneDirector::pendingDirtyFlagsForBoundary(const BoundaryNode *boundary) const
      {
        return transactionForCurrentCycle().pendingDirtyFlagsForBoundary(boundary);
      }

      inline bool SceneDirector::hasPendingBoundary(const BoundaryNode *boundary) const
      {
        return transactionForCurrentCycle().hasPendingBoundary(boundary);
      }

      inline BoundaryNode *SceneDirector::firstPendingBoundary() const
      {
        return transactionForCurrentCycle().firstPendingBoundary();
      }

      inline BoundaryNode *SceneDirector::topMostRequestedBoundary(BoundaryNode *boundary) const
      {
        if (!boundary)
        {
          return 0;
        }
        BoundaryNode *top = boundary;
        BoundaryNode *parent = boundary->parentBoundary();
        while (parent)
        {
          const NodeDirtyFlags parentFlags = pendingDirtyFlagsForBoundary(parent);
          const NodeDirtyFlags boundaryFlags = pendingDirtyFlagsForBoundary(boundary);
          if (!hasPendingBoundary(parent) || parentFlags == NODE_DIRTY_NONE)
          {
            break;
          }
          if (parentFlags == NODE_DIRTY_CHILD && boundaryFlags != NODE_DIRTY_NONE)
          {
            break;
          }
          top = parent;
          boundary = parent;
          parent = parent->parentBoundary();
        }
        return top;
      }

      inline bool SceneDirector::isBoundaryUpdateRoot(BoundaryNode *boundary) const
      {
        if (!boundary || !hasPendingBoundary(boundary))
        {
          return false;
        }
        return topMostRequestedBoundary(boundary) == boundary;
      }

      inline BoundaryNode *SceneDirector::firstPendingUpdateRoot() const
      {
        return nextPendingUpdateRoot(0);
      }

      inline BoundaryNode *SceneDirector::rootBoundaryFor(Node *rootNode) const
      {
        return rootNode ? rootNode->asBoundary() : 0;
      }

      inline BoundaryNode *SceneDirector::primaryUpdateRootFor(Node *rootNode) const
      {
        BoundaryNode *root = firstPendingUpdateRoot();
        return root ? root : rootBoundaryFor(rootNode);
      }

      inline SceneDirector::SceneUpdateRequestSnapshot SceneDirector::buildRefreshRequestSnapshot(Node *rootNode) const
      {
        return activeTransaction_.buildRequestSnapshot(rootBoundaryFor(rootNode), primaryUpdateRootFor(rootNode));
      }

      inline SceneDirector::SceneUpdateSnapshot SceneDirector::buildUpdateSnapshot(Node *rootNode,
                                                                                   const Scene *scene) const
      {
        const unsigned long generation = activeTransaction_.snapshotGeneration();
        if (generation == 0)
        {
          return SceneUpdateSnapshot();
        }
        return finalizeUpdateSnapshot(generation, buildRefreshRequestSnapshot(rootNode), scene);
      }

      inline SceneDirector::SceneUpdateSnapshot SceneDirector::finalizeUpdateSnapshot(
          unsigned long generation, const SceneUpdateRequestSnapshot &request, const Scene *scene) const
      {
        SceneUpdateRequestSnapshot finalizedRequest = request;
        SceneUpdateApplySnapshot apply = buildApplySnapshot(scene);
        if (apply.layoutRequired())
        {
          finalizedRequest.includeDirtyFlags(NODE_DIRTY_LAYOUT);
        }
        SceneUpdateSnapshot snapshot(generation, finalizedRequest, apply);
        if (CanRelaxFullRebuildForChildOnlyUpdate(snapshot))
        {
          finalizedRequest.relaxFullRebuild();
        }
        return SceneUpdateSnapshot(generation, finalizedRequest, apply);
      }

      inline SceneDirector::SceneUpdateApplySnapshot SceneDirector::buildApplySnapshot(const Scene *scene) const
      {
        (void)scene;
        struct ApplySnapshotAccumulator
        {
          ApplySnapshotAccumulator()
              : layoutRequired(false),
                requiresCompositedPaint(false),
                hasOpaqueLocalPaint(true),
                sawPaintWork(false),
                localStructureWork(false)
          {
          }

          void observeUpdateResult(const BoundaryUpdateResult &updateResult)
          {
            if (updateResult.hasStructureWork())
            {
              localStructureWork = true;
            }
            if (updateResult.actualBoundsChanged || updateResult.affectsAncestorLayout)
            {
              layoutRequired = true;
            }
            if (updateResult.requiresCompositedPaint())
            {
              requiresCompositedPaint = true;
              hasOpaqueLocalPaint = false;
            }
            else if (updateResult.hasPaintWork())
            {
              sawPaintWork = true;
              if (!updateResult.hasOpaqueCoverageHint() || !updateResult.opaqueCoverageHintValue())
              {
                hasOpaqueLocalPaint = false;
              }
            }
          }

          SceneUpdateApplySnapshot build() const
          {
            SceneUpdateApplySnapshot snapshot;
            bool opaqueLocalPaint = requiresCompositedPaint ? false : (sawPaintWork && hasOpaqueLocalPaint);
            snapshot.setRequirements(
                layoutRequired, requiresCompositedPaint, opaqueLocalPaint);
            if (localStructureWork)
            {
              snapshot.noteLocalStructureWork();
            }
            return snapshot;
          }

          bool layoutRequired;
          bool requiresCompositedPaint;
          bool hasOpaqueLocalPaint;
          bool sawPaintWork;
          bool localStructureWork;
        };

        ApplySnapshotAccumulator accumulator;
        PendingUpdateRootCursor updateRoots(this);
        BoundaryNode *root = updateRoots.next();
        while (root)
        {
          const BoundaryUpdateResult &updateResult = root->updateResult();
          accumulator.observeUpdateResult(updateResult);

          root = updateRoots.next();
        }
        return accumulator.build();
      }

      inline PlatformApplyPlan SceneDirector::buildPlatformApplyPlan(const SceneUpdateSnapshot &snapshot) const
      {
        PlatformApplyPlan plan;
        plan.structureChanged = snapshot.requiresStructureChange();
        plan.layoutChanged = snapshot.requiresLayoutChange();
        plan.paintKind = ResolvePaintKind(snapshot);
        plan.setPrimaryRoot(snapshot.request().primaryRoot());
        return plan;
      }

      inline PlatformApplyPlan SceneDirector::executeApplyPlan(Node *rootNode,
                                                               IPlatformController *platformController,
                                                               const SceneUpdateSnapshot &snapshot,
                                                               NodeDirtyFlags globalDirtyFlags,
                                                               bool fullRebuild) const
      {
        PlatformApplyPlan plan = buildPlatformApplyPlan(snapshot);
        applyPlatformApplyPlan(rootNode, platformController, plan, globalDirtyFlags, fullRebuild);
        return plan;
      }

      inline void SceneDirector::applyPlatformApplyPlan(Node *rootNode,
                                                        IPlatformController *platformController,
                                                        const PlatformApplyPlan &plan,
                                                        NodeDirtyFlags globalDirtyFlags,
                                                        bool fullRebuild) const
      {
        applyPendingBoundaryUpdates(rootNode, plan);
        if (shouldApplyGlobalChange(platformController, plan) && platformController)
        {
          // A structure-bearing apply is a child-grade change even when the
          // request only carried PROPS: platforms gate their layout/ensure
          // pass on the flags, and new nodes need that pass to exist
          // natively (#277).
          NodeDirtyFlags effectiveFlags = globalDirtyFlags;
          if (plan.structureChanged)
          {
            effectiveFlags = static_cast<NodeDirtyFlags>(effectiveFlags | NODE_DIRTY_CHILD);
          }
          platformController->onChange(rootNode, effectiveFlags, fullRebuild);
        }
      }

      inline void
      SceneDirector::applyPendingBoundaryUpdate(Node *rootNode, BoundaryNode *root, const PlatformApplyPlan &plan) const
      {
        assert(root && (isBoundaryUpdateRoot(root) || (root == rootBoundaryFor(rootNode) && !firstPendingUpdateRoot())));
#ifdef TEST_BUILD
        ++testing::paintBaselineStats().boundaryUpdateVisits;
#endif
        PlatformApplyPlan localPlan = plan.forBoundary(root);
        const BoundaryNode::LocalApplyInfo localInfo = root->localApplyInfo(localPlan);
        if (localPlan.hasBoundaryApplyWork(root) && root->scene() && root->scene()->platformController_)
        {
#ifdef TEST_BUILD
          ++testing::paintBaselineStats().boundaryApplyCallbacks;
#endif
          root->scene()->platformController_->onBoundaryApply(rootNode, root, localInfo, localPlan);
        }
        root->applyPendingUpdate(localPlan);
      }

      inline void SceneDirector::applyPendingBoundaryUpdates(Node *rootNode, const PlatformApplyPlan &plan) const
      {
        PendingUpdateRootCursor updateRoots(this);
        BoundaryNode *root = updateRoots.next();
        if (!root)
        {
          root = rootBoundaryFor(rootNode);
        }
        while (root)
        {
          BoundaryApplyPhaseScope applyScope = root->beginApplyPhaseScope();
          applyPendingBoundaryUpdate(rootNode, root, plan);
          root = updateRoots.next();
        }
      }

      inline bool SceneDirector::shouldApplyGlobalChange(IPlatformController *platformController,
                                                         const PlatformApplyPlan &plan) const
      {
        return !platformController || !platformController->canSkipGlobalChangeForBoundaryLocalPaint()
               || !plan.isPaintOnlyWork();
      }

      inline void SceneDirector::SceneUpdateTransaction::clearTransaction()
      {
        clearAccumulatedState();
      }

      inline void SceneDirector::SceneUpdateTransaction::AccumulatedState::enqueueBoundaryUpdate(
          const BoundaryUpdateRequest &request)
      {
        if (!request.boundary)
        {
          return;
        }
        enqueueRequestedInput(request.flags);
        enqueueProjectionTarget(request.boundary, request.flags);
      }

      inline static bool IsBoundaryDescendantOf(const BoundaryNode *boundary, const BoundaryNode *ancestor)
      {
        const BoundaryNode *current = boundary ? boundary->parentBoundary() : 0;
        while (current)
        {
          if (current == ancestor)
          {
            return true;
          }
          current = current->parentBoundary();
        }
        return false;
      }

      inline SceneDirector::PendingUpdateRootAnalysis::PendingUpdateRootAnalysis(const SceneDirector *director)
          : director(director),
            seenHead(0),
            seenTail(0)
      {
      }

      inline SceneDirector::PendingUpdateRootAnalysis::~PendingUpdateRootAnalysis()
      {
        SeenRoot *entry = seenHead;
        while (entry)
        {
          SeenRoot *next = entry->next;
          delete entry;
          entry = next;
        }
        seenHead = 0;
        seenTail = 0;
      }

      inline bool SceneDirector::PendingUpdateRootAnalysis::hasSeenRoot(BoundaryNode *root) const
      {
        if (!root)
        {
          return false;
        }
        SeenRoot *entry = seenHead;
        while (entry)
        {
          if (entry->root == root)
          {
            return true;
          }
          entry = entry->next;
        }
        return false;
      }

      inline void SceneDirector::PendingUpdateRootAnalysis::recordSeenRoot(BoundaryNode *root)
      {
        if (!root || hasSeenRoot(root))
        {
          return;
        }
        SeenRoot *entry = new SeenRoot();
        entry->root = root;
        if (!seenHead)
        {
          seenHead = entry;
          seenTail = entry;
        }
        else
        {
          seenTail->next = entry;
          seenTail = entry;
        }
      }

      inline bool SceneDirector::PendingUpdateRootAnalysis::shouldSkip(BoundaryNode *boundary, BoundaryNode *root) const
      {
        if (!director || !boundary || !root)
        {
          return true;
        }
        if (hasSeenRoot(root))
        {
          return true;
        }
        return false;
      }

      inline SceneDirector::PendingUpdateRootCursor::PendingUpdateRootCursor(const SceneDirector *director)
          : director(director),
            iterator(director ? director->activeTransaction_.projectionTransaction().targetsBegin()
                              : SceneProjectionTransaction::ConstIterator()),
            analysis(director)
      {
      }

      inline BoundaryNode *SceneDirector::PendingUpdateRootCursor::next()
      {
        while (iterator.isValid())
        {
          Node *node = iterator.node();
          iterator.next();

          BoundaryNode *boundary = node ? node->asBoundary() : 0;
          if (!boundary)
          {
            continue;
          }

          BoundaryNode *root = director ? director->topMostRequestedBoundary(boundary) : 0;
          if (root && !analysis.shouldSkip(boundary, root))
          {
            analysis.recordSeenRoot(root);
            return root;
          }
        }
        return 0;
      }

      inline static bool CanRelaxFullRebuildForChildOnlyUpdate(const SceneDirector::SceneUpdateSnapshot &snapshot)
      {
        return snapshot.request().effectiveFullRebuildRequired()
               && snapshot.request().hasEffectiveDirtyFlag(NODE_DIRTY_CHILD) && !snapshot.apply().structureRequired();
      }

      inline static PlatformApplyPlan::PaintKind ResolvePaintKind(const SceneDirector::SceneUpdateSnapshot &snapshot)
      {
        if (snapshot.requiresCompositedPaint())
        {
          return PlatformApplyPlan::PAINT_COMPOSITED;
        }
        if (snapshot.requiresOpaqueLocalPaint())
        {
          return PlatformApplyPlan::PAINT_LOCAL_OPAQUE;
        }
        if (snapshot.hasAnyPaintChange())
        {
          return PlatformApplyPlan::PAINT_LOCAL;
        }
        return PlatformApplyPlan::PAINT_NONE;
      }

      inline BoundaryNode *SceneDirector::nextPendingUpdateRoot(BoundaryNode *afterRoot) const
      {
        bool started = afterRoot == 0;
        PendingUpdateRootCursor cursor(this);
        BoundaryNode *root = cursor.next();
        while (root)
        {
          if (started)
          {
            return root;
          }
          started = root == afterRoot;
          root = cursor.next();
        }
        return 0;
      }

      inline void SceneDirector::clearUpdateTransaction()
      {
        updateTransaction_.clearTransaction();
        activeTransaction_.clearTransaction();
        phase_ = UPDATE_CYCLE_IDLE;
      }

      inline void SceneDirector::SceneUpdateTransaction::enqueueBoundaryUpdate(const BoundaryUpdateRequest &request)
      {
        accumulatedState.enqueueBoundaryUpdate(request);
      }

      inline const SceneDirector::SceneUpdateTransaction &SceneDirector::transactionForCurrentCycle() const
      {
        return isUpdateCycleActive() ? activeTransaction_ : updateTransaction_;
      }

      inline SceneDirector::SceneUpdateTransaction &SceneDirector::transactionForCurrentCycle()
      {
        return isUpdateCycleActive() ? activeTransaction_ : updateTransaction_;
      }

    } // namespace scene
  } // namespace app
} // namespace loka

#endif // LOKA_CORE2_SCENE_SCENE_HPP
