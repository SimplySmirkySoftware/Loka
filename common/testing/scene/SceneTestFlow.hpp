#ifndef LOKA_DSL_TESTING_SCENE_TEST_FLOW_HPP
#define LOKA_DSL_TESTING_SCENE_TEST_FLOW_HPP

#include <cassert>
#include <cstdio>
#include <string>

#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/Text.hpp"
#include "app/scene/Scene.hpp"
#include "core/resource/Image.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "testing/scene/ScenarioAudit.hpp"
#include "testing/snap/SnapFlow.hpp"
#include "platform/StringUTF8.hpp"

namespace loka
{
  namespace dsl
  {
    namespace testing
    {
      class SceneTestAccess
      {
      public:
        /** Exercise composition lifetime independently of Scene root reclamation. */
        static void notifyComposeEvent(::loka::app::scene::Scene &scene,
                                       ::loka::app::scene::ComposeEvent event)
        {
          scene.notifyComposeEvent(event);
        }
        /** Internal capture probes must not reopen subclass declaration doors. */
        static void captureBranchSeatPlan(::loka::app::scene::BoundaryNode &boundary)
        {
          boundary.captureBranchSeatPlan();
        }

        /** Count real refresh attempts using the Scene's own scheduler. */
        static bool runCountingRefreshes(::loka::app::scene::Scene &scene, int &attempts)
        {
          CountedRefresh probe(scene, attempts);
          return scene.nextTickTracker_.run(&CountedRefresh::refresh, &CountedRefresh::apply, &probe);
        }

      private:
        struct CountedRefresh
        {
          ::loka::app::scene::Scene &scene;
          int &attempts;
          CountedRefresh(::loka::app::scene::Scene &value, int &count) : scene(value), attempts(count) {}
          static bool refresh(void *data)
          {
            CountedRefresh &probe = *static_cast<CountedRefresh *>(data);
            ++probe.attempts;
            return ::loka::app::scene::Scene::RefreshThunk(&probe.scene);
          }
          static void apply(void *data)
          {
            CountedRefresh &probe = *static_cast<CountedRefresh *>(data);
            ::loka::app::scene::Scene::ApplyThunk(&probe.scene);
          }
        };

      public:
        /** Prepare a replacement's logical tree before native projection/install. */
        static void prepareComposition(::loka::app::scene::Scene &scene)
        {
          scene.composeIfNeeded(::loka::app::scene::COMPOSE_EVENT_ATTACH, false);
        }
        /** Exercise root admission independently of attachment and rail presence. */
        static void withoutRoot(::loka::app::scene::Scene &scene, void (*action)(void *), void *data)
        {
          ::loka::app::scene::Node *root = scene.rootNode_;
          scene.rootNode_ = 0;
          action(data);
          scene.rootNode_ = root;
        }
        /** Synchronous fixture setup and sequence probes, outside a Scene run. */
        static void updateAttached(::loka::app::scene::Scene &scene, bool attached)
        {
          assert(!scene.isRunInProgress());
          scene.updateAttached(attached);
        }
        static void unmount(::loka::app::scene::Scene &scene)
        {
          assert(!scene.isRunInProgress());
          scene.unmount();
        }
        static ::loka::app::scene::Node *rootNode(const ::loka::app::scene::Scene &scene)
        {
          return scene.rootNode_;
        }

        static ::loka::app::scene::BoundaryNode *rootBoundary(const ::loka::app::scene::Scene &scene)
        {
          return scene.rootNode_ ? scene.rootNode_->asBoundary() : 0;
        }

        static bool composed(const ::loka::app::scene::Scene &scene)
        {
          return scene.composed_;
        }

        static bool whiteFlagFullRebuildPending(const ::loka::app::scene::Scene &scene)
        {
          return scene.whiteFlagFullRebuildPending_;
        }

        static ::loka::app::scene::SceneDirector &director(::loka::app::scene::Scene &scene)
        {
          return scene.director_;
        }

        static const ::loka::app::scene::SceneDirector &director(const ::loka::app::scene::Scene &scene)
        {
          return scene.director_;
        }

        static const ::loka::app::scene::PlatformApplyPlan &lastApplyPlan(const ::loka::app::scene::Scene &scene)
        {
          return scene.updateCycleState_.lastApplyPlanValue();
        }

        static const ::loka::app::scene::SceneDirector::SceneUpdateSnapshot &
        updateSnapshot(const ::loka::app::scene::Scene &scene)
        {
          return scene.updateCycleState_.pendingSnapshotValue();
        }

        static const ::loka::app::scene::SceneDirector::SceneUpdateSnapshot &
        lastUpdateSnapshot(const ::loka::app::scene::Scene &scene)
        {
          return scene.updateCycleState_.lastAppliedSnapshotValue();
        }

        static bool projectionTransactionHasPending(const ::loka::app::scene::Scene &scene)
        {
          return projectionTransaction(scene).hasPending();
        }

        static ::loka::app::scene::NodeDirtyFlags
        projectionTransactionAggregateDirtyFlags(const ::loka::app::scene::Scene &scene)
        {
          return projectionTransaction(scene).aggregateDirtyFlags();
        }

        static long projectionTransactionTargetCount(const ::loka::app::scene::Scene &scene)
        {
          return projectionTransaction(scene).targetCount();
        }

        static unsigned long projectionTransactionGeneration(const ::loka::app::scene::Scene &scene)
        {
          return scene.director_.projectionTransactionGenerationForTesting();
        }

        static ::loka::app::scene::Node *projectionTransactionFirstTargetNode(const ::loka::app::scene::Scene &scene)
        {
          return projectionTransaction(scene).targetsBegin().node();
        }

        static ::loka::app::scene::NodeDirtyFlags
        projectionTransactionFirstTargetDirtyFlags(const ::loka::app::scene::Scene &scene)
        {
          return projectionTransaction(scene).targetsBegin().dirtyFlags();
        }

      private:
        static const ::loka::app::scene::SceneProjectionTransaction &
        projectionTransaction(const ::loka::app::scene::Scene &scene)
        {
          return scene.director_.updateTransaction_.projectionTransaction();
        }

      public:
        static ::loka::app::scene::NodeDirtyFlags requestedDirtyFlags(const ::loka::app::scene::Scene &scene)
        {
          return scene.director_.requestedDirtyFlags();
        }

        static ::loka::app::scene::NodeDirtyFlags effectiveRequestedDirtyFlags(const ::loka::app::scene::Scene &scene)
        {
          return scene.director_.effectiveRequestedDirtyFlags();
        }

        /** Dirt queued during an active cycle, before the next refresh takes it. */
        static ::loka::app::scene::NodeDirtyFlags accumulatedBoundaryDirtyFlags(
            const ::loka::app::scene::Scene &scene,
            const ::loka::app::scene::BoundaryNode *boundary)
        {
          return scene.director_.updateTransaction_.pendingDirtyFlagsForBoundary(boundary);
        }

        static bool hasRequestedInput(const ::loka::app::scene::Scene &scene)
        {
          return scene.director_.hasRequestedInput();
        }

        static bool requestedFullRebuild(const ::loka::app::scene::Scene &scene)
        {
          return scene.director_.requestedFullRebuild();
        }

        static ::loka::app::scene::NodeDirtyFlags
        snapshotRequestedDirtyFlags(const ::loka::app::scene::SceneDirector::SceneUpdateSnapshot &snapshot)
        {
          return snapshot.request().requestedDirtyFlagsValue();
        }

        static ::loka::app::scene::NodeDirtyFlags
        snapshotTransactionDirtyFlags(const ::loka::app::scene::SceneDirector::SceneUpdateSnapshot &snapshot)
        {
          return snapshot.request().transactionDirtyFlagsValue();
        }

        static ::loka::app::scene::NodeDirtyFlags
        snapshotEffectiveDirtyFlags(const ::loka::app::scene::SceneDirector::SceneUpdateSnapshot &snapshot)
        {
          return snapshot.request().effectiveDirtyFlagsValue();
        }

        static bool snapshotRequestedFullRebuild(const ::loka::app::scene::SceneDirector::SceneUpdateSnapshot &snapshot)
        {
          return snapshot.request().requestedFullRebuildValue();
        }

        static bool snapshotEffectiveFullRebuild(const ::loka::app::scene::SceneDirector::SceneUpdateSnapshot &snapshot)
        {
          return snapshot.request().effectiveFullRebuildRequired();
        }

        static ::loka::app::scene::BoundaryNode *
        snapshotFirstPendingRoot(const ::loka::app::scene::SceneDirector::SceneUpdateSnapshot &snapshot)
        {
          return snapshot.request().firstPendingRootValue();
        }

        static ::loka::app::scene::BoundaryNode *
        snapshotRootBoundary(const ::loka::app::scene::SceneDirector::SceneUpdateSnapshot &snapshot)
        {
          return snapshot.request().rootBoundaryValue();
        }

        static ::loka::app::scene::BoundaryNode *
        snapshotPrimaryRoot(const ::loka::app::scene::SceneDirector::SceneUpdateSnapshot &snapshot)
        {
          return snapshot.request().primaryRoot();
        }

        static unsigned long snapshotGeneration(const ::loka::app::scene::SceneDirector::SceneUpdateSnapshot &snapshot)
        {
          return snapshot.generationValue();
        }

        static bool snapshotLayoutRequired(const ::loka::app::scene::SceneDirector::SceneUpdateSnapshot &snapshot)
        {
          return snapshot.apply().layoutRequired();
        }

        static bool snapshotStructureRequired(const ::loka::app::scene::SceneDirector::SceneUpdateSnapshot &snapshot)
        {
          return snapshot.apply().structureRequired();
        }

        static bool
        snapshotCompositedPaintRequired(const ::loka::app::scene::SceneDirector::SceneUpdateSnapshot &snapshot)
        {
          return snapshot.apply().compositedPaintRequired();
        }

        static bool
        snapshotOpaqueLocalPaintRequired(const ::loka::app::scene::SceneDirector::SceneUpdateSnapshot &snapshot)
        {
          return snapshot.apply().opaqueLocalPaintRequired();
        }

        static bool flushInvalidation(::loka::app::scene::Scene &scene)
        {
          return scene.flushInvalidation();
        }

        static ::loka::app::scene::IPlatformController *platformController(const ::loka::app::scene::Scene &scene)
        {
          return scene.platformController_;
        }
      };

      class BoundaryObservedStateTestAccess
      {
      public:
        static size_t entryCount(
            const ::loka::app::scene::BoundaryObservedState &observedState)
        {
          return observedState.entries.size();
        }

        static const ::loka::app::scene::BoundaryObservedStateBinding *firstBinding(
            const ::loka::app::scene::BoundaryObservedState &observedState)
        {
          return observedState.entries.empty() ? 0 : observedState.entries[0].binding;
        }

        static void appendObservedEntry(::loka::app::scene::BoundaryObservedState &observedState,
                                        ::loka::core::StateBase *state,
                                        ::loka::app::scene::NodeDirtyFlags flags)
        {
          ::loka::app::scene::BoundaryObservedStateEntry entry;
          entry.state = state;
          entry.flags = flags;
          observedState.entries.push_back(entry);
        }

        static void updateFirstEntryForCurrentPass(::loka::app::scene::BoundaryObservedState &observedState,
                                                   ::loka::app::scene::NodeDirtyFlags flags)
        {
          assert(!observedState.entries.empty());
          observedState.entries[0].flags = flags;
        }
      };

      enum SceneTestFlowErrorKind
      {
        FLOW_ERROR_KIND_SCENE_SCENARIO = 1002,
        FLOW_ERROR_KIND_SCENE_TEST_ASSERT = 1003
      };

      enum SceneTestFlowErrorCode
      {
        FLOW_ERROR_SCENE_TEST_NULL_SCENE = 1,
        FLOW_ERROR_SCENE_TEST_ROOT_UNAVAILABLE = 2,
        FLOW_ERROR_SCENE_TEST_NODE_NOT_FOUND = 3,
        FLOW_ERROR_SCENE_TEST_NODE_TYPE_MISMATCH = 4,
        FLOW_ERROR_SCENE_TEST_DUPLICATE_TEST_ID = 5,
        FLOW_ERROR_SCENE_TEST_MISSING_TEST_ID = 6,
        FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE = 7,
        FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED = 8,
        FLOW_ERROR_SCENE_TEST_EXPECTED_APPLY = 9,
        FLOW_ERROR_SCENE_TEST_INVALID_ORDINAL = 10,
        FLOW_ERROR_SCENE_TEST_ORDINAL_OUT_OF_RANGE = 11,
        FLOW_ERROR_SCENE_TEST_BUTTON_DISABLED = 12,
        FLOW_ERROR_SCENE_TEST_CONTROLLER_UNAVAILABLE = 13
      };

      /** Owner-updated monotonic clock borrowed by scheduled scenario steps. */
      class ScenarioClock
      {
      public:
        ScenarioClock()
            : tick_(0)
        {
        }

        void advanceTo(long tick)
        {
          assert(tick >= this->tick_ && "ScenarioClock cannot move backwards");
          this->tick_ = tick;
        }

        long currentTick() const
        {
          return this->tick_;
        }

      private:
        long tick_;
      };

      /** Observable milestones for one ScenarioFlow action's projection. */
      namespace ProjectionEvent
      {
        enum Type
        {
          /** The Scene recorded a new last-applied generation after its
              platform apply callbacks returned. */
          APPLIED = 0,
          /** A new applied generation exists and its mounted platform
              controller reports no pending synchronization. This names the
              Loka-to-native projection boundary, not external compositor or
              display hardware completion. */
          PRESENTED
        };
      } // namespace ProjectionEvent

      namespace scenario_projection_wait_detail
      {
        template <class T> struct Access
        {
          static bool queryAppliedGeneration(const T &, unsigned long &, FlowError &error)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
            return false;
          }

          static bool hasPendingApply(const T &)
          {
            return false;
          }

          static bool queryPendingPresentation(const T &, bool &, FlowError &error)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
            return false;
          }
        };

        template <> struct Access< ::loka::app::scene::Scene *>
        {
          static bool
          queryAppliedGeneration(::loka::app::scene::Scene *const &scene, unsigned long &out, FlowError &error)
          {
            if (!scene)
            {
              error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
              error.code = FLOW_ERROR_SCENE_TEST_NULL_SCENE;
              return false;
            }
            out = SceneTestAccess::snapshotGeneration(SceneTestAccess::lastUpdateSnapshot(*scene));
            return true;
          }

          static bool hasPendingApply(::loka::app::scene::Scene *const &scene)
          {
            return scene && scene->hasPendingInvalidation();
          }

          static bool queryPendingPresentation(::loka::app::scene::Scene *const &scene, bool &out, FlowError &error)
          {
            if (!scene)
            {
              error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
              error.code = FLOW_ERROR_SCENE_TEST_NULL_SCENE;
              return false;
            }
            ::loka::app::scene::IPlatformController *controller = SceneTestAccess::platformController(*scene);
            if (!controller)
            {
              error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
              error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
              return false;
            }
            out = controller->hasPendingSync();
            return true;
          }
        };
      } // namespace scenario_projection_wait_detail

      /** Runs one Scene action to completion, then keeps its output pending until that
          action's requested projection has crossed the selected milestone. */
      namespace scenario_step_detail
      {
        /** True when AdapterT (or a base) declares a member named diagnostic. Detected by
            name ambiguity against a probe base, because the sizeof-of-call form answered
            true for every type on GCC 4.2 and Apple clang up to Xcode 10 (#1248). */
        template <class AdapterT> class HasDiagnostic
        {
          typedef char Yes;
          struct No
          {
            char pad[2];
          };
          struct Probe
          {
            int diagnostic;
          };
          struct Both : AdapterT, Probe
          {
          };
          template <class U, U> struct Check;
          template <class U> static No test(Check<int Probe::*, &U::diagnostic> *);
          template <class U> static Yes test(...);

        public:
          enum
          {
            value = sizeof(test<Both>(0)) == sizeof(Yes)
          };
        };

        template <class AdapterT, bool Has = HasDiagnostic<AdapterT>::value> struct Diagnostic
        {
          static const char *get(const AdapterT &)
          {
            return 0;
          }
        };

        template <class AdapterT> struct Diagnostic<AdapterT, true>
        {
          static const char *get(const AdapterT &adapter)
          {
            return adapter.diagnostic();
          }
        };
      } // namespace scenario_step_detail

      template <class AdapterT> class ProjectionWaitAdapter
      {
      public:
        typedef typename AdapterT::In In;
        typedef typename AdapterT::Out Out;
        typedef char LokaProjectionWaitAdapterRequiresSceneInput
            [flow_detail::IsSame<In, ::loka::app::scene::Scene *>::value ? 1 : -1];
        typedef char LokaProjectionWaitAdapterRequiresSceneOutput
            [flow_detail::IsSame<Out, ::loka::app::scene::Scene *>::value ? 1 : -1];

        ProjectionWaitAdapter(const AdapterT &adapter, ProjectionEvent::Type event)
            : adapter_(adapter),
              event_(event),
              phase_(PHASE_ACTION_NOT_STARTED),
              appliedGenerationBeforeAction_(0),
              completedOut_()
        {
        }

        /** Forwards the wrapped action's failure diagnostic, so message= survives waitUntil. */
        const char *diagnostic() const
        {
          return scenario_step_detail::Diagnostic<AdapterT>::get(this->adapter_);
        }

        StepRunStatus run(const In &in, Out &out, FlowError &error) const
        {
          if (this->phase_ == PHASE_COMPLETED)
          {
            out = this->completedOut_;
            return FLOW_STEP_SUCCEEDED;
          }
          if (this->phase_ == PHASE_WAITING)
          {
            out = this->completedOut_;
            return this->advanceWait(out, error);
          }

          if (this->phase_ == PHASE_ACTION_NOT_STARTED)
          {
            if (!scenario_projection_wait_detail::Access<In>::queryAppliedGeneration(
                    in, this->appliedGenerationBeforeAction_, error))
            {
              return FLOW_STEP_FAILED;
            }
            this->phase_ = PHASE_ACTION_RUNNING;
          }
          assert(this->phase_ == PHASE_ACTION_RUNNING
                 && "ProjectionWaitAdapter action must be running before invocation");
          const StepRunStatus status = this->adapter_.run(in, out, error);
          if (status != FLOW_STEP_SUCCEEDED)
          {
            return status;
          }

          this->completedOut_ = out;
          this->phase_ = PHASE_WAITING;
          return this->advanceWait(out, error);
        }

      private:
        enum Phase
        {
          PHASE_ACTION_NOT_STARTED = 0,
          PHASE_ACTION_RUNNING,
          PHASE_WAITING,
          PHASE_COMPLETED
        };

        StepRunStatus advanceWait(Out &out, FlowError &error) const
        {
          switch (this->event_)
          {
          case ProjectionEvent::APPLIED:
          case ProjectionEvent::PRESENTED:
          {
            unsigned long appliedGeneration = 0;
            if (!scenario_projection_wait_detail::Access<Out>::queryAppliedGeneration(out, appliedGeneration, error))
            {
              return FLOW_STEP_FAILED;
            }
            if (appliedGeneration != this->appliedGenerationBeforeAction_)
            {
              if (this->event_ == ProjectionEvent::PRESENTED)
              {
                bool pendingPresentation = false;
                if (!scenario_projection_wait_detail::Access<Out>::queryPendingPresentation(
                        out, pendingPresentation, error))
                {
                  return FLOW_STEP_FAILED;
                }
                if (pendingPresentation)
                {
                  return FLOW_STEP_PENDING;
                }
              }
              this->phase_ = PHASE_COMPLETED;
              return FLOW_STEP_SUCCEEDED;
            }
            if (scenario_projection_wait_detail::Access<Out>::hasPendingApply(out))
            {
              return FLOW_STEP_PENDING;
            }
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_EXPECTED_APPLY;
            return FLOW_STEP_FAILED;
          }
          }
          assert(false && "ProjectionWaitAdapter received an unknown ProjectionEvent");
          error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
          error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
          return FLOW_STEP_FAILED;
        }

        AdapterT adapter_;
        ProjectionEvent::Type event_;
        mutable Phase phase_;
        mutable unsigned long appliedGenerationBeforeAction_;
        mutable Out completedOut_;
      };

      namespace scenario_projection_wait_detail
      {
        template <class T> struct IsWaitAdapter
        {
          enum
          {
            value = 0
          };
        };

        template <class AdapterT> struct IsWaitAdapter<ProjectionWaitAdapter<AdapterT> >
        {
          enum
          {
            value = 1
          };
        };
      } // namespace scenario_projection_wait_detail


      /** Caches the first terminal outcome of an owned adapter, including failure.
          Construct a fresh wrapper for a new execution; copies carry the current
          snapshot. Pending is polled again, so pending adapters must be idempotent
          across polls. Recursive entry returns pending without invoking the adapter.
          An optional public const char *diagnostic() const is copied on failure;
          the owned copy is limited to 256 bytes including a "..." truncation marker.
          Use Then(RunOnce(adapter)).named(name) or AtTick(tick, RunOnce(adapter)). */
      template <class AdapterT> class RunOnceAdapter
      {
      public:
        typedef typename AdapterT::In In;
        typedef typename AdapterT::Out Out;

        explicit RunOnceAdapter(const AdapterT &adapter)
            : adapter_(adapter),
              phase_(READY),
              output_(),
              error_(),
              message_()
        {
        }

        StepRunStatus run(const In &in, Out &out, FlowError &error) const
        {
          if (this->phase_ == SUCCEEDED)
          {
            out = this->output_;
            return FLOW_STEP_SUCCEEDED;
          }
          if (this->phase_ == FAILED)
          {
            error = this->error_;
            return FLOW_STEP_FAILED;
          }
          if (this->phase_ == ENTERED)
          {
            return FLOW_STEP_PENDING;
          }
          this->phase_ = ENTERED;
          const StepRunStatus status = this->adapter_.run(in, out, error);
          switch (status)
          {
          case FLOW_STEP_PENDING:
            this->phase_ = READY;
            break;
          case FLOW_STEP_SUCCEEDED:
            this->output_ = out;
            this->phase_ = SUCCEEDED;
            break;
          case FLOW_STEP_FAILED:
            this->error_ = error;
            this->message_ =
                scenario_audit_detail::CopyDiagnostic(scenario_step_detail::Diagnostic<AdapterT>::get(this->adapter_));
            this->phase_ = FAILED;
            break;
          }
          return status;
        }

        const char *diagnostic() const
        {
          return this->message_.c_str();
        }

      private:
        enum Phase
        {
          READY,
          ENTERED,
          SUCCEEDED,
          FAILED
        };
        AdapterT adapter_;
        mutable Phase phase_;
        mutable Out output_;
        mutable FlowError error_;
        mutable std::string message_;
      };

      template <class AdapterT> inline RunOnceAdapter<AdapterT> RunOnce(const AdapterT &adapter)
      {
        return RunOnceAdapter<AdapterT>(adapter);
      }

      /** Polls for empty Scene invalidation and mounted-controller work queues.
          This promises work queues empty, not pixels presented (Classic sync does
          not track paint). Never flushes or spins; an idle Scene succeeds immediately.
          The caller owns Scene lifetime and any separate transition/liveness gate. */
      class Settle
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef ::loka::app::scene::Scene *Out;

        StepRunStatus run(const In &in, Out &out, FlowError &error) const
        {
          out = in;
          if (!in)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_NULL_SCENE;
            return FLOW_STEP_FAILED;
          }
          ::loka::app::scene::IPlatformController *controller = SceneTestAccess::platformController(*in);
          if (!controller)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_CONTROLLER_UNAVAILABLE;
            return FLOW_STEP_FAILED;
          }
          return in->hasPendingInvalidation() || controller->hasPendingSync() ? FLOW_STEP_PENDING : FLOW_STEP_SUCCEEDED;
        }
      };

      /** Schedules one typed Flow action at or after a scenario tick. The
          completed output is replayed on later chain runs without repeating
          the action. The clock owner must outlive this adapter. */
      template <class AdapterT> class AtTickAdapter
      {
      public:
        typedef typename AdapterT::In In;
        typedef typename AdapterT::Out Out;

        AtTickAdapter(const ScenarioClock &clock,
                      long dueTick,
                      const AdapterT &adapter,
                      int stepId = 0,
                      const char *name = 0,
                      ScenarioAuditSink *audit = 0)
            : clock_(&clock),
              dueTick_(dueTick),
              adapter_(adapter),
              audit_(audit, stepId, name, dueTick),
              completed_(false),
              completedOut_()
        {
          assert(dueTick >= 0 && "AtTick requires a non-negative scenario tick");
        }

        StepRunStatus run(const In &in, Out &out, FlowError &error) const
        {
          if (this->completed_)
          {
            out = this->completedOut_;
            return FLOW_STEP_SUCCEEDED;
          }
          if (this->clock_->currentTick() < this->dueTick_)
          {
            return FLOW_STEP_PENDING;
          }
          const StepRunStatus status = this->adapter_.run(in, out, error);
          if (status != FLOW_STEP_PENDING
              && !this->audit_.emit(
                  this->clock_->currentTick(),
                  status,
                  error,
                  status == FLOW_STEP_FAILED ? scenario_step_detail::Diagnostic<AdapterT>::get(this->adapter_) : 0))
          {
            return FLOW_STEP_FAILED;
          }
          if (status == FLOW_STEP_SUCCEEDED)
          {
            this->completedOut_ = out;
            this->completed_ = true;
          }
          return status;
        }

      private:
        const ScenarioClock *clock_;
        long dueTick_;
        AdapterT adapter_;
        mutable scenario_audit_detail::StepTerminalEmitter audit_;
        mutable bool completed_;
        mutable Out completedOut_;
      };

      template <class AdapterT>
      inline AtTickAdapter<AdapterT> AtTick(const ScenarioClock &clock, long dueTick, const AdapterT &adapter)
      {
        return AtTickAdapter<AdapterT>(clock, dueTick, adapter);
      }

      template <class AdapterT> class AtTickSpec
      {
      public:
        AtTickSpec(long dueTick, const AdapterT &adapter)
            : dueTick_(dueTick),
              adapter_(adapter),
              name_(),
              successTarget_(0)
        {
        }

        AtTickSpec &onSuccess(typename AdapterT::Out *target)
        {
          this->successTarget_ = target;
          return *this;
        }

        /** Gives this scheduled action an owned audit identity. */
        AtTickSpec &named(const char *name)
        {
          this->name_ = name ? name : "";
          return *this;
        }

        /** Requires this Scene action to cross the selected projection
            milestone before the scenario advances. */
        AtTickSpec<ProjectionWaitAdapter<AdapterT> > waitUntil(ProjectionEvent::Type event) const
        {
          typedef typename AdapterT::In AdapterIn;
          typedef typename AdapterT::Out AdapterOut;
          typedef char LokaScenarioProjectionWaitRequiresSceneInput
              [flow_detail::IsSame<AdapterIn, ::loka::app::scene::Scene *>::value ? 1 : -1];
          typedef char LokaScenarioProjectionWaitRequiresSceneOutput
              [flow_detail::IsSame<AdapterOut, ::loka::app::scene::Scene *>::value ? 1 : -1];
          typedef char LokaScenarioProjectionWaitCanOnlyBeDeclaredOnce
              [scenario_projection_wait_detail::IsWaitAdapter<AdapterT>::value ? -1 : 1];
          (void)sizeof(LokaScenarioProjectionWaitRequiresSceneInput);
          (void)sizeof(LokaScenarioProjectionWaitRequiresSceneOutput);
          (void)sizeof(LokaScenarioProjectionWaitCanOnlyBeDeclaredOnce);
          AtTickSpec<ProjectionWaitAdapter<AdapterT> > result(this->dueTick_,
                                                              ProjectionWaitAdapter<AdapterT>(this->adapter_, event));
          result.named(this->name_.c_str());
          if (this->successTarget_)
          {
            result.onSuccess(this->successTarget_);
          }
          return result;
        }

        long dueTick() const
        {
          return this->dueTick_;
        }

        const AdapterT &adapter() const
        {
          return this->adapter_;
        }

        const char *name() const
        {
          return this->name_.c_str();
        }

        typename AdapterT::Out *successTarget() const
        {
          return this->successTarget_;
        }

      private:
        long dueTick_;
        AdapterT adapter_;
        std::string name_;
        typename AdapterT::Out *successTarget_;
      };

      /** Declares when one scenario action becomes eligible. ScenarioFlow
          supplies the clock, input, and mechanical Flow step identity. */
      template <class AdapterT> inline AtTickSpec<AdapterT> AtTick(long dueTick, const AdapterT &adapter)
      {
        return AtTickSpec<AdapterT>(dueTick, adapter);
      }

      /** Declares a scenario action eligible as soon as its predecessor has
          completed. Timing-sensitive scenarios should continue to use AtTick. */
      template <class AdapterT> inline AtTickSpec<AdapterT> Then(const AdapterT &adapter)
      {
        return AtTickSpec<AdapterT>(0, adapter);
      }

      template <class InT, class OutT> class ScenarioFlowChain
      {
      public:
        ScenarioFlowChain(const ScenarioClock &clock,
                          const FlowChain<InT, OutT> &flow,
                          int nextStepId,
                          ScenarioAuditSink *audit)
            : clock_(&clock),
              flow_(flow),
              nextStepId_(nextStepId),
              audit_(audit)
        {
        }

        template <class AdapterT>
        ScenarioFlowChain<InT, typename AdapterT::Out> operator|(const AtTickSpec<AdapterT> &step) const
        {
          typedef typename AdapterT::In NextIn;
          typedef typename AdapterT::Out NextOut;
          typedef char LokaScenarioFlowStepTypeMismatch[(flow_detail::IsSame<OutT, NextIn>::value) ? 1 : -1];
          (void)sizeof(LokaScenarioFlowStepTypeMismatch);
          StepSpec<AtTickAdapter<AdapterT> > scheduled(
              this->nextStepId_,
              AtTickAdapter<AdapterT>(
                  *this->clock_, step.dueTick(), step.adapter(), this->nextStepId_, step.name(), this->audit_));
          if (step.successTarget())
          {
            scheduled.onSuccess(step.successTarget());
          }
          const FlowChain<InT, NextOut> next = this->flow_ | scheduled;
          return ScenarioFlowChain<InT, NextOut>(
              *this->clock_, next, this->nextStepId_ + 1, this->audit_);
        }

        FlowChain<InT, OutT> flow() const
        {
          return this->flow_;
        }

      private:
        const ScenarioClock *clock_;
        FlowChain<InT, OutT> flow_;
        int nextStepId_;
        ScenarioAuditSink *audit_;
      };

      template <class InT> class ScenarioFlowStart
      {
      public:
        ScenarioFlowStart(const ScenarioClock &clock, const InT *input)
            : clock_(&clock),
              input_(input),
              audit_(0)
        {
          assert(input != 0 && "ScenarioFlow requires a borrowed input");
        }

        /** Returns a scheduled-flow start that borrows one audit sink for the
            lifetime of the completed Flow. */
        ScenarioFlowStart auditTo(ScenarioAuditSink *audit) const
        {
          ScenarioFlowStart result(*this);
          result.audit_ = audit;
          return result;
        }

        template <class AdapterT>
        ScenarioFlowChain<InT, typename AdapterT::Out> operator|(const AtTickSpec<AdapterT> &step) const
        {
          typedef typename AdapterT::In FirstIn;
          typedef typename AdapterT::Out FirstOut;
          typedef char LokaScenarioFlowInputTypeMismatch[(flow_detail::IsSame<InT, FirstIn>::value) ? 1 : -1];
          (void)sizeof(LokaScenarioFlowInputTypeMismatch);
          StepSpec<AtTickAdapter<AdapterT> > scheduled(
              1, AtTickAdapter<AdapterT>(*this->clock_, step.dueTick(), step.adapter(), 1, step.name(), this->audit_));
          scheduled.input(this->input_);
          if (step.successTarget())
          {
            scheduled.onSuccess(step.successTarget());
          }
          const FlowChain<InT, FirstOut> first = Flow() | scheduled;
          return ScenarioFlowChain<InT, FirstOut>(*this->clock_, first, 2, this->audit_);
        }

      private:
        const ScenarioClock *clock_;
        const InT *input_;
        ScenarioAuditSink *audit_;
      };

      /** Starts a typed scheduled scenario over one borrowed Flow input. */
      template <class InT>
      inline ScenarioFlowStart<InT> ScenarioFlow(const ScenarioClock &clock, const InT *input)
      {
        return ScenarioFlowStart<InT>(clock, input);
      }

      template <class NodeT> struct SceneNodeCast;
      // Add explicit specializations for each supported node type so misuse fails at compile time,
      // not later as an unresolved symbol.

      template <class NodeT> struct SceneNodeCast
      {
        static NodeT *cast(::loka::app::scene::Node *)
        {
          typedef char LokaSceneNodeCastRequiresSpecialization[(sizeof(NodeT) == 0) ? 1 : -1];
          (void)sizeof(LokaSceneNodeCastRequiresSpecialization);
          return 0;
        }
      };

      template <> struct SceneNodeCast< ::loka::app::scene::Node>
      {
        static const char *name()
        {
          return "Node";
        }

        static ::loka::app::scene::Node *cast(::loka::app::scene::Node *node)
        {
          return node;
        }
      };

      template <> struct SceneNodeCast< ::loka::app::TextNode>
      {
        static const char *name()
        {
          return "Text";
        }

        static ::loka::app::TextNode *cast(::loka::app::scene::Node *node)
        {
          return node ? node->asTextNode() : 0;
        }
      };

      template <> struct SceneNodeCast< ::loka::app::ButtonNode>
      {
        static const char *name()
        {
          return "Button";
        }

        static ::loka::app::ButtonNode *cast(::loka::app::scene::Node *node)
        {
          return node ? node->asButtonNode() : 0;
        }
      };

      template <> struct SceneNodeCast< ::loka::app::CellNode>
      {
        static const char *name()
        {
          return "Cell";
        }

        static ::loka::app::CellNode *cast(::loka::app::scene::Node *node)
        {
          return node ? node->asCellNode() : 0;
        }
      };

      template <> struct SceneNodeCast< ::loka::app::EditTextNode>
      {
        static const char *name()
        {
          return "EditText";
        }

        static ::loka::app::EditTextNode *cast(::loka::app::scene::Node *node)
        {
          return node ? node->asEditTextNode() : 0;
        }
      };

      /** Immutable description of the k-th typed descendant below one scene-global anchor. */
      template <class NodeT> class NodeSelector
      {
      public:
        NodeSelector(const std::string &anchorId, long ordinal)
            : anchorId_(anchorId),
              ordinal_(ordinal)
        {
        }

        const std::string &anchorId() const
        {
          return this->anchorId_;
        }

        long ordinal() const
        {
          return this->ordinal_;
        }

        std::string describe() const
        {
          char ordinalText[32];
          ::snprintf(ordinalText, sizeof(ordinalText), "%ld", this->ordinal_);
          return this->anchorId_ + "/" + SceneNodeCast<NodeT>::name() + "[" + ordinalText + "]";
        }

      private:
        std::string anchorId_;
        long ordinal_;
      };

      /** Starts a typed descendant selector at one scene-global TEST_ID anchor. */
      class WithinAnchor
      {
      public:
        explicit WithinAnchor(const char *anchorId)
            : anchorId_(anchorId ? anchorId : "")
        {
        }

        template <class NodeT> NodeSelector<NodeT> descendant(long ordinal) const
        {
          return NodeSelector<NodeT>(this->anchorId_, ordinal);
        }

      private:
        std::string anchorId_;
      };

      inline WithinAnchor Within(const char *anchorId)
      {
        return WithinAnchor(anchorId);
      }

      namespace scene_test_detail
      {
        template <class NodeT>
        static void findNodeByIdRecursive(::loka::app::scene::Node *node,
                                          const std::string &testId,
                                          long &idMatches,
                                          long &typedMatches,
                                          NodeT *&result)
        {
          if (!node)
          {
            return;
          }

          if (node->testId() == testId)
          {
            ++idMatches;
            NodeT *typed = SceneNodeCast<NodeT>::cast(node);
            if (typed)
            {
              ++typedMatches;
              if (!result)
              {
                result = typed;
              }
            }
          }

          ::loka::app::scene::INestable *nestable = node->asNestable();
          if (!nestable)
          {
            return;
          }

          ::loka::app::scene::Node *child = nestable->childrenHead();
          while (child)
          {
            findNodeByIdRecursive<NodeT>(child, testId, idMatches, typedMatches, result);
            child = child->nextInComposition;
          }
        }

        template <class NodeT>
        static void findDescendantByOrdinalRecursive(::loka::app::scene::Node *node,
                                                     long ordinal,
                                                     long &typedMatches,
                                                     NodeT *&result)
        {
          if (!node || result)
          {
            return;
          }

          NodeT *typed = SceneNodeCast<NodeT>::cast(node);
          if (typed)
          {
            ++typedMatches;
            if (typedMatches == ordinal)
            {
              result = typed;
              return;
            }
          }

          ::loka::app::scene::INestable *nestable = node->asNestable();
          if (!nestable)
          {
            return;
          }

          ::loka::app::scene::Node *child = nestable->childrenHead();
          while (child && !result)
          {
            findDescendantByOrdinalRecursive<NodeT>(child, ordinal, typedMatches, result);
            child = child->nextInComposition;
          }
        }
      } // namespace scene_test_detail

      template <class NodeT>
      static StepRunStatus
      LookupNodeById(::loka::app::scene::Scene *scene, const std::string &testId, NodeT *&out, FlowError &error);

      template <class NodeT> class FindNodeByIdAdapter
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef NodeT *Out;

        explicit FindNodeByIdAdapter(const char *testId)
            : testId_(testId ? testId : "")
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          return LookupNodeById<NodeT>(in, testId_, out, error);
        }

      private:
        std::string testId_;
      };

      template <class NodeT> inline FindNodeByIdAdapter<NodeT> FindNodeById(const char *testId)
      {
        return FindNodeByIdAdapter<NodeT>(testId);
      }

      template <class NodeT>
      static StepRunStatus
      LookupNodeById(::loka::app::scene::Scene *scene, const std::string &testId, NodeT *&out, FlowError &error)
      {
        out = 0;
        if (!scene)
        {
          error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
          error.code = FLOW_ERROR_SCENE_TEST_NULL_SCENE;
          return FLOW_STEP_FAILED;
        }
        ::loka::app::scene::Node *root = SceneTestAccess::rootNode(*scene);
        if (!root)
        {
          error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
          error.code = FLOW_ERROR_SCENE_TEST_ROOT_UNAVAILABLE;
          return FLOW_STEP_FAILED;
        }
        long idMatches = 0;
        long typedMatches = 0;
        scene_test_detail::findNodeByIdRecursive<NodeT>(root, testId, idMatches, typedMatches, out);
        if (idMatches == 0)
        {
          error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
          error.code = FLOW_ERROR_SCENE_TEST_NODE_NOT_FOUND;
          return FLOW_STEP_FAILED;
        }
        if (idMatches > 1)
        {
          error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
          error.code = FLOW_ERROR_SCENE_TEST_DUPLICATE_TEST_ID;
          return FLOW_STEP_FAILED;
        }
        if (typedMatches == 0 || !out)
        {
          error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
          error.code = FLOW_ERROR_SCENE_TEST_NODE_TYPE_MISMATCH;
          return FLOW_STEP_FAILED;
        }
        return FLOW_STEP_SUCCEEDED;
      }

      /** Resolves a typed ordinal selector against the scene's current materialized tree. */
      template <class NodeT>
      static StepRunStatus ResolveSelector(::loka::app::scene::Scene *scene,
                                           const NodeSelector<NodeT> &selector,
                                           NodeT *&out,
                                           FlowError &error)
      {
        out = 0;
        if (selector.anchorId().empty())
        {
          // An omitted anchor would otherwise match every id-less node and,
          // when exactly one exists, silently become a scene-wide lookup.
          error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
          error.code = FLOW_ERROR_SCENE_TEST_MISSING_TEST_ID;
          return FLOW_STEP_FAILED;
        }
        ::loka::app::scene::Node *anchor = 0;
        StepRunStatus anchorStatus =
            LookupNodeById< ::loka::app::scene::Node>(scene, selector.anchorId(), anchor, error);
        if (anchorStatus != FLOW_STEP_SUCCEEDED)
        {
          return anchorStatus;
        }
        if (selector.ordinal() < 1)
        {
          error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
          error.code = FLOW_ERROR_SCENE_TEST_INVALID_ORDINAL;
          return FLOW_STEP_FAILED;
        }

        ::loka::app::scene::INestable *nestable = anchor->asNestable();
        long typedMatches = 0;
        ::loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0;
        while (child && !out)
        {
          scene_test_detail::findDescendantByOrdinalRecursive<NodeT>(
              child, selector.ordinal(), typedMatches, out);
          child = child->nextInComposition;
        }
        if (!out)
        {
          error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
          error.code = FLOW_ERROR_SCENE_TEST_ORDINAL_OUT_OF_RANGE;
          return FLOW_STEP_FAILED;
        }
        return FLOW_STEP_SUCCEEDED;
      }

      namespace scene_test_detail
      {
        /** Internal sum value that lets one action implementation preserve the
            direct-id API while also accepting typed ordinal selectors. */
        template <class NodeT> class NodeTarget
        {
        public:
          explicit NodeTarget(const char *testId)
              : idOrAnchor_(testId ? testId : ""),
                ordinal_(0),
                selector_(false)
          {
          }

          explicit NodeTarget(const NodeSelector<NodeT> &selector)
              : idOrAnchor_(selector.anchorId()),
                ordinal_(selector.ordinal()),
                selector_(true)
          {
          }

          StepRunStatus resolve(::loka::app::scene::Scene *scene, NodeT *&out, FlowError &error) const
          {
            if (!this->selector_)
            {
              return LookupNodeById<NodeT>(scene, this->idOrAnchor_, out, error);
            }
            return ResolveSelector<NodeT>(
                scene, NodeSelector<NodeT>(this->idOrAnchor_, this->ordinal_), out, error);
          }

          std::string describe() const
          {
            if (!this->selector_)
            {
              return this->idOrAnchor_;
            }
            return NodeSelector<NodeT>(this->idOrAnchor_, this->ordinal_).describe();
          }

        private:
          std::string idOrAnchor_;
          long ordinal_;
          bool selector_;
        };
      } // namespace scene_test_detail

      template <class NodeT> struct SceneCaptureTraits;

      template <> struct SceneCaptureTraits< ::loka::app::TextNode>
      {
        static bool capture(::loka::app::TextNode *node, SnapRecord &out)
        {
          if (!node || !node->props.text_)
          {
            return false;
          }
          std::string utf8;
          if (!::loka::platform::CollectUtf8(node->props.text_->get(), utf8))
          {
            return false;
          }
          out.set("text.value", utf8.c_str());
          return true;
        }
      };

      namespace scene_test_detail
      {
        template <class NodeT>
        static StepRunStatus captureNodeWithIdentity(NodeT *node,
                                                     const std::string &identity,
                                                     const std::string &testName,
                                                     const std::string &stepName,
                                                     long tick,
                                                     long scenarioVersion,
                                                     const std::string &status,
                                                     SnapRecord &out,
                                                     FlowError &error)
        {
          if (!node)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_NODE_NOT_FOUND;
            return FLOW_STEP_FAILED;
          }

          BuildSnapV1RecordAdapter base(
              testName.c_str(), stepName.c_str(), identity.c_str(), tick, scenarioVersion, status.c_str());
          const int unused = 0;
          FlowError ignored;
          if (base.run(unused, out, ignored) != FLOW_STEP_SUCCEEDED)
          {
            error.kind = FLOW_ERROR_KIND_SNAP;
            error.code = FLOW_ERROR_SNAP_WRITE_FAILED;
            return FLOW_STEP_FAILED;
          }
          if (!SceneCaptureTraits<NodeT>::capture(node, out))
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
            return FLOW_STEP_FAILED;
          }
          return FLOW_STEP_SUCCEEDED;
        }
      } // namespace scene_test_detail

      template <class NodeT> class CaptureNodeAdapter
      {
      public:
        typedef NodeT *In;
        typedef SnapRecord Out;

        CaptureNodeAdapter(const char *testName,
                           const char *stepName,
                           long tick,
                           long scenarioVersion,
                           const char *status = SnapStatusOk())
            : testName_(testName ? testName : ""),
              stepName_(stepName ? stepName : ""),
              tick_(tick),
              scenarioVersion_(scenarioVersion),
              status_(status ? status : SnapStatusOk())
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          if (!in)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_NODE_NOT_FOUND;
            return FLOW_STEP_FAILED;
          }
          const std::string &nodeId = in->testId();
          if (nodeId.empty())
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_MISSING_TEST_ID;
            return FLOW_STEP_FAILED;
          }

          return scene_test_detail::captureNodeWithIdentity<NodeT>(
              in, nodeId, testName_, stepName_, tick_, scenarioVersion_, status_, out, error);
        }

      private:
        std::string testName_;
        std::string stepName_;
        long tick_;
        long scenarioVersion_;
        std::string status_;
      };

      template <class NodeT>
      inline CaptureNodeAdapter<NodeT>
      CaptureNode(const char *testName, const char *stepName, long tick, long scenarioVersion, const char *status)
      {
        return CaptureNodeAdapter<NodeT>(testName, stepName, tick, scenarioVersion, status);
      }

      template <class NodeT>
      inline CaptureNodeAdapter<NodeT>
      CaptureNode(const char *testName, const char *stepName, long tick, long scenarioVersion)
      {
        return CaptureNodeAdapter<NodeT>(testName, stepName, tick, scenarioVersion);
      }

      class CaptureSceneAdapter
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef SnapRecord Out;

        CaptureSceneAdapter(const char *testName,
                            const char *stepName,
                            long tick,
                            long scenarioVersion,
                            const char *status = SnapStatusOk())
            : testName_(testName ? testName : ""),
              stepName_(stepName ? stepName : ""),
              tick_(tick),
              scenarioVersion_(scenarioVersion),
              status_(status ? status : SnapStatusOk())
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          if (!in)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_NULL_SCENE;
            return FLOW_STEP_FAILED;
          }

          BuildSnapV1RecordAdapter base(
              testName_.c_str(), stepName_.c_str(), "Scene", tick_, scenarioVersion_, status_.c_str());
          const int unused = 0;
          FlowError ignored;
          if (base.run(unused, out, ignored) != FLOW_STEP_SUCCEEDED)
          {
            error.kind = FLOW_ERROR_KIND_SNAP;
            error.code = FLOW_ERROR_SNAP_WRITE_FAILED;
            return FLOW_STEP_FAILED;
          }

          ::loka::app::scene::Node *root = SceneTestAccess::rootNode(*in);
          ::loka::app::scene::BoundaryNode *boundary = SceneTestAccess::rootBoundary(*in);
          out.setInt("scene.root.present", root ? 1 : 0);
          out.setInt("scene.root_boundary.present", boundary ? 1 : 0);
          if (root)
          {
            out.setInt("scene.root.kind", static_cast<long>(root->kind()));
            if (!root->testId().empty())
            {
              out.set("scene.root.test_id", root->testId().c_str());
            }
          }
          return FLOW_STEP_SUCCEEDED;
        }

      private:
        std::string testName_;
        std::string stepName_;
        long tick_;
        long scenarioVersion_;
        std::string status_;
      };

      inline CaptureSceneAdapter
      CaptureScene(const char *testName, const char *stepName, long tick, long scenarioVersion, const char *status)
      {
        return CaptureSceneAdapter(testName, stepName, tick, scenarioVersion, status);
      }

      inline CaptureSceneAdapter
      CaptureScene(const char *testName, const char *stepName, long tick, long scenarioVersion)
      {
        return CaptureSceneAdapter(testName, stepName, tick, scenarioVersion);
      }

      /** Resolved capture payload, borrowed only during one capture invocation. */
      struct ViewCaptureTarget
      {
        ViewCaptureTarget()
            : node(0),
              hasContext(false),
              capturableBitmap(0),
              kind(0),
              testId()
        {
        }

        ::loka::app::scene::Node *node;
        bool hasContext;
        const ::loka::app::scene::ICapturableBitmap *capturableBitmap;
        long kind;
        std::string testId;
      };

      /** A scheduled capture carries a key, never a descendant node or context.
          The scene must outlive the scenario. Resolve only inside the consuming
          step; the resulting pointers are borrowed for that invocation. */
      class ViewCaptureKey
      {
      public:
        ViewCaptureKey() : scene_(0), testId_() {}
        ViewCaptureKey(::loka::app::scene::Scene *scene, const std::string &testId)
            : scene_(scene), testId_(testId) {}

        StepRunStatus resolve(ViewCaptureTarget &out, FlowError &error) const
        {
          ::loka::app::scene::Node *node = 0;
          StepRunStatus status = LookupNodeById< ::loka::app::scene::Node>(
              this->scene_, this->testId_, node, error);
          if (status != FLOW_STEP_SUCCEEDED)
          {
            return status;
          }
          out.node = node;
          out.hasContext = node->getContext() != 0;
          out.capturableBitmap = node->getContext() ? node->getContext()->asCapturableBitmap() : 0;
          out.kind = static_cast<long>(node->kind());
          out.testId = node->testId();
          return FLOW_STEP_SUCCEEDED;
        }

      private:
        ::loka::app::scene::Scene *scene_;
        std::string testId_;
      };

      /** Selects a capture key. Target existence is checked at consumption. */
      class CaptureViewTargetAdapter
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef ViewCaptureKey Out;

        explicit CaptureViewTargetAdapter(const char *testId)
            : testId_(testId ? testId : "")
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &) const
        {
          out = ViewCaptureKey(in, this->testId_);
          return FLOW_STEP_SUCCEEDED;
        }

      private:
        std::string testId_;
      };

      inline CaptureViewTargetAdapter CaptureViewTarget(const char *testId)
      {
        return CaptureViewTargetAdapter(testId);
      }

      class CaptureViewTargetSnapAdapter
      {
      public:
        typedef ViewCaptureKey In;
        typedef SnapRecord Out;

        CaptureViewTargetSnapAdapter(const char *testName,
                                     const char *stepName,
                                     long tick,
                                     long scenarioVersion,
                                     const char *status = SnapStatusOk())
            : testName_(testName ? testName : ""),
              stepName_(stepName ? stepName : ""),
              tick_(tick),
              scenarioVersion_(scenarioVersion),
              status_(status ? status : SnapStatusOk())
        {
        }

        StepRunStatus run(In const &key, Out &out, FlowError &error) const
        {
          ViewCaptureTarget in;
          const StepRunStatus status = key.resolve(in, error);
          if (status != FLOW_STEP_SUCCEEDED)
          {
            return status;
          }

          BuildSnapV1RecordAdapter base(testName_.c_str(),
                                        stepName_.c_str(),
                                        in.testId.empty() ? "ViewTarget" : in.testId.c_str(),
                                        tick_,
                                        scenarioVersion_,
                                        status_.c_str());
          const int unused = 0;
          FlowError ignored;
          if (base.run(unused, out, ignored) != FLOW_STEP_SUCCEEDED)
          {
            error.kind = FLOW_ERROR_KIND_SNAP;
            error.code = FLOW_ERROR_SNAP_WRITE_FAILED;
            return FLOW_STEP_FAILED;
          }

          out.setInt("view.target.present", 1);
          out.setInt("view.context.present", in.hasContext ? 1 : 0);
          out.setInt("view.node.kind", in.kind);
          if (!in.testId.empty())
          {
            out.set("view.node.test_id", in.testId.c_str());
          }
          return FLOW_STEP_SUCCEEDED;
        }

      private:
        std::string testName_;
        std::string stepName_;
        long tick_;
        long scenarioVersion_;
        std::string status_;
      };

      inline CaptureViewTargetSnapAdapter
      CaptureView(const char *testName, const char *stepName, long tick, long scenarioVersion, const char *status)
      {
        return CaptureViewTargetSnapAdapter(testName, stepName, tick, scenarioVersion, status);
      }

      inline CaptureViewTargetSnapAdapter
      CaptureView(const char *testName, const char *stepName, long tick, long scenarioVersion)
      {
        return CaptureViewTargetSnapAdapter(testName, stepName, tick, scenarioVersion);
      }

      struct ViewBitmapCapture
      {
        ViewBitmapCapture()
            : image(::loka::core::resource::Image::Empty()),
              captured(false),
              width(0),
              height(0)
        {
        }

        ::loka::core::resource::Image image;
        bool captured;
        int width;
        int height;
      };

      class CaptureViewBitmapAdapter
      {
      public:
        typedef ViewCaptureKey In;
        typedef ViewBitmapCapture Out;
        typedef bool (*CaptureFn)(const ViewCaptureTarget &target,
                                  ViewBitmapCapture &out,
                                  FlowError &error,
                                  void *user);

        CaptureViewBitmapAdapter(CaptureFn captureFn, void *user)
            : captureFn_(captureFn),
              user_(user)
        {
        }

        StepRunStatus run(In const &key, Out &out, FlowError &error) const
        {
          ViewCaptureTarget in;
          const StepRunStatus status = key.resolve(in, error);
          if (status != FLOW_STEP_SUCCEEDED)
          {
            return status;
          }
          if (!this->captureFn_)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
            return FLOW_STEP_FAILED;
          }
          if (!this->captureFn_(in, out, error, this->user_))
          {
            if (error.kind == 0 && error.code == 0)
            {
              error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
              error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
            }
            return FLOW_STEP_FAILED;
          }
          return FLOW_STEP_SUCCEEDED;
        }

      private:
        CaptureFn captureFn_;
        void *user_;
      };

      inline CaptureViewBitmapAdapter CaptureViewBitmap(CaptureViewBitmapAdapter::CaptureFn captureFn, void *user)
      {
        return CaptureViewBitmapAdapter(captureFn, user);
      }

      class CaptureViewBitmapFromPlatformAdapter
      {
      public:
        typedef ViewCaptureKey In;
        typedef ViewBitmapCapture Out;

        StepRunStatus run(In const &key, Out &out, FlowError &error) const
        {
          ViewCaptureTarget in;
          const StepRunStatus status = key.resolve(in, error);
          if (status != FLOW_STEP_SUCCEEDED)
          {
            return status;
          }
          if (!in.capturableBitmap)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
            return FLOW_STEP_FAILED;
          }
          if (!in.capturableBitmap->captureBitmap(out.image))
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
            return FLOW_STEP_FAILED;
          }
          out.captured = out.image.isValid();
          out.width = out.image.width();
          out.height = out.image.height();
          return FLOW_STEP_SUCCEEDED;
        }
      };

      inline CaptureViewBitmapFromPlatformAdapter CaptureViewBitmapFromPlatform()
      {
        return CaptureViewBitmapFromPlatformAdapter();
      }

      class CaptureViewBitmapSnapAdapter
      {
      public:
        typedef ViewBitmapCapture In;
        typedef SnapRecord Out;

        CaptureViewBitmapSnapAdapter(const char *testName,
                                     const char *stepName,
                                     long tick,
                                     long scenarioVersion,
                                     const char *status = SnapStatusOk())
            : testName_(testName ? testName : ""),
              stepName_(stepName ? stepName : ""),
              tick_(tick),
              scenarioVersion_(scenarioVersion),
              status_(status ? status : SnapStatusOk())
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &) const
        {
          BuildSnapV1RecordAdapter base(this->testName_.c_str(),
                                        this->stepName_.c_str(),
                                        "ViewBitmap",
                                        this->tick_,
                                        this->scenarioVersion_,
                                        this->status_.c_str());
          const int unused = 0;
          FlowError ignored;
          if (base.run(unused, out, ignored) != FLOW_STEP_SUCCEEDED)
          {
            return FLOW_STEP_FAILED;
          }
          out.setInt("view.bitmap.captured", in.captured ? 1 : 0);
          out.setInt("view.bitmap.image.valid", in.image.isValid() ? 1 : 0);
          out.setInt("view.bitmap.width", in.width);
          out.setInt("view.bitmap.height", in.height);
          return FLOW_STEP_SUCCEEDED;
        }

      private:
        std::string testName_;
        std::string stepName_;
        long tick_;
        long scenarioVersion_;
        std::string status_;
      };

      inline CaptureViewBitmapSnapAdapter CaptureViewBitmapSnap(
          const char *testName, const char *stepName, long tick, long scenarioVersion, const char *status)
      {
        return CaptureViewBitmapSnapAdapter(testName, stepName, tick, scenarioVersion, status);
      }

      inline CaptureViewBitmapSnapAdapter
      CaptureViewBitmapSnap(const char *testName, const char *stepName, long tick, long scenarioVersion)
      {
        return CaptureViewBitmapSnapAdapter(testName, stepName, tick, scenarioVersion);
      }

      class SnapTextAdapter
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef SnapRecord Out;

        SnapTextAdapter(const char *testId,
                        const char *testName,
                        const char *stepName,
                        long tick,
                        long scenarioVersion,
                        const char *status = SnapStatusOk())
            : target_(testId),
              testName_(testName ? testName : ""),
              stepName_(stepName ? stepName : ""),
              tick_(tick),
              scenarioVersion_(scenarioVersion),
              status_(status ? status : SnapStatusOk())
        {
        }

        SnapTextAdapter(const NodeSelector< ::loka::app::TextNode> &selector,
                        const char *testName,
                        const char *stepName,
                        long tick,
                        long scenarioVersion,
                        const char *status = SnapStatusOk())
            : target_(selector),
              testName_(testName ? testName : ""),
              stepName_(stepName ? stepName : ""),
              tick_(tick),
              scenarioVersion_(scenarioVersion),
              status_(status ? status : SnapStatusOk())
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          ::loka::app::TextNode *node = 0;
          StepRunStatus lookupStatus = target_.resolve(in, node, error);
          if (lookupStatus != FLOW_STEP_SUCCEEDED)
          {
            return lookupStatus;
          }
          return scene_test_detail::captureNodeWithIdentity< ::loka::app::TextNode>(node,
                                                                                    target_.describe(),
                                                                                    testName_,
                                                                                    stepName_,
                                                                                    tick_,
                                                                                    scenarioVersion_,
                                                                                    status_,
                                                                                    out,
                                                                                    error);
        }

      private:
        scene_test_detail::NodeTarget< ::loka::app::TextNode> target_;
        std::string testName_;
        std::string stepName_;
        long tick_;
        long scenarioVersion_;
        std::string status_;
      };

      inline SnapTextAdapter SnapText(const char *testId,
                                      const char *testName,
                                      const char *stepName,
                                      long tick,
                                      long scenarioVersion,
                                      const char *status)
      {
        return SnapTextAdapter(testId, testName, stepName, tick, scenarioVersion, status);
      }

      inline SnapTextAdapter
      SnapText(const char *testId, const char *testName, const char *stepName, long tick, long scenarioVersion)
      {
        return SnapTextAdapter(testId, testName, stepName, tick, scenarioVersion);
      }

      inline SnapTextAdapter SnapText(const NodeSelector< ::loka::app::TextNode> &selector,
                                      const char *testName,
                                      const char *stepName,
                                      long tick,
                                      long scenarioVersion,
                                      const char *status)
      {
        return SnapTextAdapter(selector, testName, stepName, tick, scenarioVersion, status);
      }

      inline SnapTextAdapter SnapText(const NodeSelector< ::loka::app::TextNode> &selector,
                                      const char *testName,
                                      const char *stepName,
                                      long tick,
                                      long scenarioVersion)
      {
        return SnapTextAdapter(selector, testName, stepName, tick, scenarioVersion);
      }

      class AssertSnapStringEqualsAdapter
      {
      public:
        typedef SnapRecord In;
        typedef SnapRecord Out;

        AssertSnapStringEqualsAdapter(const char *key, const char *expected)
            : key_(key ? key : ""),
              expected_(expected ? expected : "")
        {
        }

        StepRunStatus run(const In &in, Out &out, FlowError &error) const
        {
          out = in;
          std::string actual;
          if (key_.empty() || !in.get(key_.c_str(), actual))
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
            return FLOW_STEP_FAILED;
          }
          if (actual != expected_)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_TEST_ASSERT;
            error.code = FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED;
            return FLOW_STEP_FAILED;
          }
          return FLOW_STEP_SUCCEEDED;
        }

      private:
        std::string key_;
        std::string expected_;
      };

      inline AssertSnapStringEqualsAdapter AssertSnapStringEquals(const char *key, const char *expected)
      {
        return AssertSnapStringEqualsAdapter(key, expected);
      }

      class CheckSnapStringEqualsAdapter
      {
      public:
        typedef SnapRecord In;
        typedef SnapRecord Out;

        CheckSnapStringEqualsAdapter(const char *key, const char *expected)
            : key_(key ? key : ""),
              expected_(expected ? expected : "")
        {
        }

        StepRunStatus run(const In &in, Out &out, FlowError &error) const
        {
          out = in;
          return AssertSnapStringEquals(key_.c_str(), expected_.c_str()).run(in, out, error);
        }

      private:
        std::string key_;
        std::string expected_;
      };

      inline CheckSnapStringEqualsAdapter CheckSnapStringEquals(const char *key, const char *expected)
      {
        return CheckSnapStringEqualsAdapter(key, expected);
      }

      class AssertSnapIntLessEqualAdapter
      {
      public:
        typedef SnapRecord In;
        typedef SnapRecord Out;

        AssertSnapIntLessEqualAdapter(const char *key, long maxValue)
            : key_(key ? key : ""),
              maxValue_(maxValue)
        {
        }

        StepRunStatus run(const In &in, Out &out, FlowError &error) const
        {
          out = in;
          long actual = 0;
          if (key_.empty() || !in.getInt(key_.c_str(), actual))
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
            return FLOW_STEP_FAILED;
          }
          if (actual > maxValue_)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_TEST_ASSERT;
            error.code = FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED;
            return FLOW_STEP_FAILED;
          }
          return FLOW_STEP_SUCCEEDED;
        }

      private:
        std::string key_;
        long maxValue_;
      };

      inline AssertSnapIntLessEqualAdapter AssertSnapIntLessEqual(const char *key, long maxValue)
      {
        return AssertSnapIntLessEqualAdapter(key, maxValue);
      }

      class AssertSnapIntEqualsAdapter
      {
      public:
        typedef SnapRecord In;
        typedef SnapRecord Out;

        AssertSnapIntEqualsAdapter(const char *key, long expectedValue)
            : key_(key ? key : ""),
              expectedValue_(expectedValue)
        {
        }

        StepRunStatus run(const In &in, Out &out, FlowError &error) const
        {
          out = in;
          long actual = 0;
          if (key_.empty() || !in.getInt(key_.c_str(), actual))
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
            return FLOW_STEP_FAILED;
          }
          if (actual != expectedValue_)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_TEST_ASSERT;
            error.code = FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED;
            return FLOW_STEP_FAILED;
          }
          return FLOW_STEP_SUCCEEDED;
        }

      private:
        std::string key_;
        long expectedValue_;
      };

      inline AssertSnapIntEqualsAdapter AssertSnapIntEquals(const char *key, long expectedValue)
      {
        return AssertSnapIntEqualsAdapter(key, expectedValue);
      }

      class AssertSnapIntGreaterEqualAdapter
      {
      public:
        typedef SnapRecord In;
        typedef SnapRecord Out;

        AssertSnapIntGreaterEqualAdapter(const char *key, long minValue)
            : key_(key ? key : ""),
              minValue_(minValue)
        {
        }

        StepRunStatus run(const In &in, Out &out, FlowError &error) const
        {
          out = in;
          long actual = 0;
          if (key_.empty() || !in.getInt(key_.c_str(), actual))
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
            return FLOW_STEP_FAILED;
          }
          if (actual < minValue_)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_TEST_ASSERT;
            error.code = FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED;
            return FLOW_STEP_FAILED;
          }
          return FLOW_STEP_SUCCEEDED;
        }

      private:
        std::string key_;
        long minValue_;
      };

      inline AssertSnapIntGreaterEqualAdapter AssertSnapIntGreaterEqual(const char *key, long minValue)
      {
        return AssertSnapIntGreaterEqualAdapter(key, minValue);
      }

      class AssertSnapIntMaskHasBitsAdapter
      {
      public:
        typedef SnapRecord In;
        typedef SnapRecord Out;

        AssertSnapIntMaskHasBitsAdapter(const char *key, long mask)
            : key_(key ? key : ""),
              mask_(mask)
        {
        }

        StepRunStatus run(const In &in, Out &out, FlowError &error) const
        {
          out = in;
          long actual = 0;
          if (key_.empty() || !in.getInt(key_.c_str(), actual))
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
            return FLOW_STEP_FAILED;
          }
          if ((actual & mask_) != mask_)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_TEST_ASSERT;
            error.code = FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED;
            return FLOW_STEP_FAILED;
          }
          return FLOW_STEP_SUCCEEDED;
        }

      private:
        std::string key_;
        long mask_;
      };

      inline AssertSnapIntMaskHasBitsAdapter AssertSnapIntMaskHasBits(const char *key, long mask)
      {
        return AssertSnapIntMaskHasBitsAdapter(key, mask);
      }

      class CheckDirtyHasBitsAdapter
      {
      public:
        typedef SnapRecord In;
        typedef SnapRecord Out;

        explicit CheckDirtyHasBitsAdapter(long mask)
            : mask_(mask)
        {
        }

        StepRunStatus run(const In &in, Out &out, FlowError &error) const
        {
          out = in;
          return AssertSnapIntMaskHasBits("dirty.mask", mask_).run(in, out, error);
        }

      private:
        long mask_;
      };

      inline CheckDirtyHasBitsAdapter CheckDirtyHasBits(long mask)
      {
        return CheckDirtyHasBitsAdapter(mask);
      }

      class CheckDirtyEqualsAdapter
      {
      public:
        typedef SnapRecord In;
        typedef SnapRecord Out;

        explicit CheckDirtyEqualsAdapter(long expectedMask)
            : expectedMask_(expectedMask)
        {
        }

        StepRunStatus run(const In &in, Out &out, FlowError &error) const
        {
          out = in;
          return AssertSnapIntEquals("dirty.mask", expectedMask_).run(in, out, error);
        }

      private:
        long expectedMask_;
      };

      inline CheckDirtyEqualsAdapter CheckDirtyEquals(long expectedMask)
      {
        return CheckDirtyEqualsAdapter(expectedMask);
      }

      class CheckSnapIntGreaterEqualAdapter
      {
      public:
        typedef SnapRecord In;
        typedef SnapRecord Out;

        CheckSnapIntGreaterEqualAdapter(const char *key, long minValue)
            : key_(key ? key : ""),
              minValue_(minValue)
        {
        }

        StepRunStatus run(const In &in, Out &out, FlowError &error) const
        {
          out = in;
          return AssertSnapIntGreaterEqual(key_.c_str(), minValue_).run(in, out, error);
        }

      private:
        std::string key_;
        long minValue_;
      };

      inline CheckSnapIntGreaterEqualAdapter CheckSnapIntGreaterEqual(const char *key, long minValue)
      {
        return CheckSnapIntGreaterEqualAdapter(key, minValue);
      }

      class CheckTextAdapter
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef ::loka::app::scene::Scene *Out;

        CheckTextAdapter(const char *testId, const char *expected)
            : target_(testId),
              expected_(expected ? expected : "")
        {
        }

        CheckTextAdapter(const NodeSelector< ::loka::app::TextNode> &selector, const char *expected)
            : target_(selector),
              expected_(expected ? expected : "")
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          out = in;
          ::loka::app::TextNode *node = 0;
          StepRunStatus lookupStatus = target_.resolve(in, node, error);
          if (lookupStatus != FLOW_STEP_SUCCEEDED)
          {
            return lookupStatus;
          }

          std::string actual;
          if (!node->props.text_ || !::loka::platform::CollectUtf8(node->props.text_->get(), actual))
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
            return FLOW_STEP_FAILED;
          }
          if (actual != expected_)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_TEST_ASSERT;
            error.code = FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED;
            return FLOW_STEP_FAILED;
          }
          return FLOW_STEP_SUCCEEDED;
        }

      private:
        scene_test_detail::NodeTarget< ::loka::app::TextNode> target_;
        std::string expected_;
      };

      inline CheckTextAdapter CheckText(const char *testId, const char *expected)
      {
        return CheckTextAdapter(testId, expected);
      }

      inline CheckTextAdapter CheckText(const NodeSelector< ::loka::app::TextNode> &selector, const char *expected)
      {
        return CheckTextAdapter(selector, expected);
      }

      class FlushSceneInvalidationAdapter
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef ::loka::app::scene::Scene *Out;

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          out = in;
          if (!in)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_NULL_SCENE;
            return FLOW_STEP_FAILED;
          }
          SceneTestAccess::flushInvalidation(*in);
          return FLOW_STEP_SUCCEEDED;
        }
      };

      inline FlushSceneInvalidationAdapter FlushSceneInvalidation()
      {
        return FlushSceneInvalidationAdapter();
      }

      class SetStringStateAdapter
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef ::loka::app::scene::Scene *Out;

        SetStringStateAdapter(::loka::core::MutableState< ::loka::core::String> *state, const char *value)
            : state_(state),
              value_(value ? value : "")
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          out = in;
          if (!in || !state_)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_NULL_SCENE;
            return FLOW_STEP_FAILED;
          }
          ::loka::app::scene::BoundaryNode *boundary = SceneTestAccess::rootBoundary(*in);
          if (!boundary || !boundary->tracker())
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_ROOT_UNAVAILABLE;
            return FLOW_STEP_FAILED;
          }
          ::loka::core::StateTrackerGuard guard(boundary->tracker());
          state_->set(::loka::core::String::Literal(value_.c_str()));
          return FLOW_STEP_SUCCEEDED;
        }

      private:
        ::loka::core::MutableState< ::loka::core::String> *state_;
        std::string value_;
      };

      inline SetStringStateAdapter SetStringState(::loka::core::MutableState< ::loka::core::String> *state,
                                                  const char *value)
      {
        return SetStringStateAdapter(state, value);
      }

      class SetStringStateAndFlushAdapter
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef ::loka::app::scene::Scene *Out;

        SetStringStateAndFlushAdapter(::loka::core::MutableState< ::loka::core::String> *state, const char *value)
            : state_(state),
              value_(value ? value : "")
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          out = in;
          StepRunStatus updateStatus = SetStringState(state_, value_.c_str()).run(in, out, error);
          if (updateStatus != FLOW_STEP_SUCCEEDED)
          {
            return updateStatus;
          }
          return FlushSceneInvalidation().run(out, out, error);
        }

      private:
        ::loka::core::MutableState< ::loka::core::String> *state_;
        std::string value_;
      };

      inline SetStringStateAndFlushAdapter
      SetStringStateAndFlush(::loka::core::MutableState< ::loka::core::String> *state, const char *value)
      {
        return SetStringStateAndFlushAdapter(state, value);
      }

      class SetBoolStateAdapter
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef ::loka::app::scene::Scene *Out;

        SetBoolStateAdapter(::loka::core::MutableState<bool> *state, bool value)
            : state_(state),
              value_(value)
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          out = in;
          if (!in || !state_)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_NULL_SCENE;
            return FLOW_STEP_FAILED;
          }
          ::loka::app::scene::BoundaryNode *boundary = SceneTestAccess::rootBoundary(*in);
          if (!boundary || !boundary->tracker())
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_ROOT_UNAVAILABLE;
            return FLOW_STEP_FAILED;
          }
          ::loka::core::StateTrackerGuard guard(boundary->tracker());
          state_->set(value_);
          return FLOW_STEP_SUCCEEDED;
        }

      private:
        ::loka::core::MutableState<bool> *state_;
        bool value_;
      };

      inline SetBoolStateAdapter SetBoolState(::loka::core::MutableState<bool> *state, bool value)
      {
        return SetBoolStateAdapter(state, value);
      }

      class SetBoolStateAndFlushAdapter
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef ::loka::app::scene::Scene *Out;

        SetBoolStateAndFlushAdapter(::loka::core::MutableState<bool> *state, bool value)
            : state_(state),
              value_(value)
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          out = in;
          StepRunStatus updateStatus = SetBoolState(state_, value_).run(in, out, error);
          if (updateStatus != FLOW_STEP_SUCCEEDED)
          {
            return updateStatus;
          }
          return FlushSceneInvalidation().run(out, out, error);
        }

      private:
        ::loka::core::MutableState<bool> *state_;
        bool value_;
      };

      inline SetBoolStateAndFlushAdapter SetBoolStateAndFlush(::loka::core::MutableState<bool> *state, bool value)
      {
        return SetBoolStateAndFlushAdapter(state, value);
      }

      class SetIntStateAdapter
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef ::loka::app::scene::Scene *Out;

        SetIntStateAdapter(::loka::core::MutableState<int> *state, int value)
            : state_(state),
              value_(value)
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          out = in;
          if (!in || !state_)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_NULL_SCENE;
            return FLOW_STEP_FAILED;
          }
          ::loka::app::scene::BoundaryNode *boundary = SceneTestAccess::rootBoundary(*in);
          if (!boundary || !boundary->tracker())
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_ROOT_UNAVAILABLE;
            return FLOW_STEP_FAILED;
          }
          ::loka::core::StateTrackerGuard guard(boundary->tracker());
          state_->set(value_);
          return FLOW_STEP_SUCCEEDED;
        }

      private:
        ::loka::core::MutableState<int> *state_;
        int value_;
      };

      inline SetIntStateAdapter SetIntState(::loka::core::MutableState<int> *state, int value)
      {
        return SetIntStateAdapter(state, value);
      }

      class SetIntStateAndFlushAdapter
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef ::loka::app::scene::Scene *Out;

        SetIntStateAndFlushAdapter(::loka::core::MutableState<int> *state, int value)
            : state_(state),
              value_(value)
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          out = in;
          StepRunStatus updateStatus = SetIntState(state_, value_).run(in, out, error);
          if (updateStatus != FLOW_STEP_SUCCEEDED)
          {
            return updateStatus;
          }
          return FlushSceneInvalidation().run(out, out, error);
        }

      private:
        ::loka::core::MutableState<int> *state_;
        int value_;
      };

      inline SetIntStateAndFlushAdapter SetIntStateAndFlush(::loka::core::MutableState<int> *state, int value)
      {
        return SetIntStateAndFlushAdapter(state, value);
      }

      class EnterTextByIdAndFlushAdapter
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef ::loka::app::scene::Scene *Out;

        EnterTextByIdAndFlushAdapter(const char *testId, const char *value)
            : target_(testId),
              value_(value ? value : "")
        {
        }

        EnterTextByIdAndFlushAdapter(const NodeSelector< ::loka::app::EditTextNode> &selector, const char *value)
            : target_(selector),
              value_(value ? value : "")
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          out = in;
          ::loka::app::EditTextNode *editText = 0;
          StepRunStatus lookupStatus = target_.resolve(in, editText, error);
          if (lookupStatus != FLOW_STEP_SUCCEEDED)
          {
            return lookupStatus;
          }
          ::loka::app::scene::BoundaryNode *boundary = SceneTestAccess::rootBoundary(*in);
          if (!boundary || !boundary->tracker())
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_ROOT_UNAVAILABLE;
            return FLOW_STEP_FAILED;
          }
          if (!editText->props.text_.isValid())
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
            return FLOW_STEP_FAILED;
          }
          {
            ::loka::core::StateTrackerGuard guard(boundary->tracker());
            editText->props.text_.set(::loka::core::String(value_), true);
          }
          return FlushSceneInvalidation().run(out, out, error);
        }

      private:
        scene_test_detail::NodeTarget< ::loka::app::EditTextNode> target_;
        std::string value_;
      };

      /** Scenario-facing text input. The forced write enters the EditText's
          bound mutable state under the Scene tracker, matching native
          text-change actuation, then flushes the resulting projection. */
      inline EnterTextByIdAndFlushAdapter EnterText(const char *testId, const char *value)
      {
        return EnterTextByIdAndFlushAdapter(testId, value);
      }

      inline EnterTextByIdAndFlushAdapter
      EnterText(const NodeSelector< ::loka::app::EditTextNode> &selector, const char *value)
      {
        return EnterTextByIdAndFlushAdapter(selector, value);
      }

      template <class NodeT> struct SceneClickTraits;

      template <> struct SceneClickTraits< ::loka::app::ButtonNode>
      {
        static bool enabled(const ::loka::app::ButtonNode *button)
        {
          return !button->props.enabled_ || button->props.enabled_->get();
        }

        static ::loka::core::EmitterState *emitter(::loka::app::ButtonNode *button)
        {
          return button->props.onClick_;
        }
      };

      template <> struct SceneClickTraits< ::loka::app::CellNode>
      {
        static bool enabled(const ::loka::app::CellNode *)
        {
          return true;
        }

        static ::loka::core::EmitterState *emitter(::loka::app::CellNode *cell)
        {
          return cell->props.onClick_;
        }
      };

      /** Disabled Button clicks fail by default. Opt in to CLICK_DISABLED_IS_NOOP
          only when deliberately clicking a disabled Button to verify nothing happens. */
      enum ClickPolicy
      {
        CLICK_DISABLED_IS_NOOP,
        CLICK_DISABLED_FAILS
      };

      const ClickPolicy DEFAULT_CLICK_POLICY = CLICK_DISABLED_FAILS;

      template <class NodeT>
      static StepRunStatus EmitNodeClick(::loka::app::scene::Scene *scene,
                                         const scene_test_detail::NodeTarget<NodeT> &target,
                                         FlowError &error,
                                         ClickPolicy policy = DEFAULT_CLICK_POLICY)
      {
        NodeT *node = 0;
        StepRunStatus lookupStatus = target.resolve(scene, node, error);
        if (lookupStatus != FLOW_STEP_SUCCEEDED)
        {
          return lookupStatus;
        }
        ::loka::app::scene::BoundaryNode *boundary = SceneTestAccess::rootBoundary(*scene);
        if (!boundary || !boundary->tracker())
        {
          error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
          error.code = FLOW_ERROR_SCENE_TEST_ROOT_UNAVAILABLE;
          return FLOW_STEP_FAILED;
        }
        if (!SceneClickTraits<NodeT>::enabled(node))
        {
          if (policy == CLICK_DISABLED_FAILS)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_BUTTON_DISABLED;
            return FLOW_STEP_FAILED;
          }
          return FLOW_STEP_SUCCEEDED;
        }
        ::loka::core::EmitterState *emitter = SceneClickTraits<NodeT>::emitter(node);
        if (!emitter)
        {
          error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
          error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
          return FLOW_STEP_FAILED;
        }
        ::loka::core::StateTrackerGuard guard(boundary->tracker());
        emitter->emit();
        return FLOW_STEP_SUCCEEDED;
      }

      class ClickButtonByIdAdapter
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef ::loka::app::scene::Scene *Out;

        explicit ClickButtonByIdAdapter(const char *testId, ClickPolicy policy = DEFAULT_CLICK_POLICY)
            : target_(testId),
              policy_(policy)
        {
        }

        explicit ClickButtonByIdAdapter(const NodeSelector< ::loka::app::ButtonNode> &selector,
                                        ClickPolicy policy = DEFAULT_CLICK_POLICY)
            : target_(selector),
              policy_(policy)
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          out = in;
          if (!in)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_NULL_SCENE;
            return FLOW_STEP_FAILED;
          }
          return EmitNodeClick< ::loka::app::ButtonNode>(in, target_, error, this->policy_);
        }

      private:
        scene_test_detail::NodeTarget< ::loka::app::ButtonNode> target_;
        ClickPolicy policy_;
      };

      inline ClickButtonByIdAdapter ClickButtonById(const char *testId, ClickPolicy policy = DEFAULT_CLICK_POLICY)
      {
        return ClickButtonByIdAdapter(testId, policy);
      }

      inline ClickButtonByIdAdapter ClickButtonById(const NodeSelector< ::loka::app::ButtonNode> &selector,
                                                    ClickPolicy policy = DEFAULT_CLICK_POLICY)
      {
        return ClickButtonByIdAdapter(selector, policy);
      }

      class ClickButtonByIdAndFlushAdapter
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef ::loka::app::scene::Scene *Out;

        explicit ClickButtonByIdAndFlushAdapter(const char *testId, ClickPolicy policy = DEFAULT_CLICK_POLICY)
            : target_(testId),
              policy_(policy)
        {
        }

        explicit ClickButtonByIdAndFlushAdapter(const NodeSelector< ::loka::app::ButtonNode> &selector,
                                                ClickPolicy policy = DEFAULT_CLICK_POLICY)
            : target_(selector),
              policy_(policy)
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          out = in;
          StepRunStatus clickStatus = EmitNodeClick< ::loka::app::ButtonNode>(in, target_, error, this->policy_);
          if (clickStatus != FLOW_STEP_SUCCEEDED)
          {
            return clickStatus;
          }
          return FlushSceneInvalidation().run(out, out, error);
        }

      private:
        scene_test_detail::NodeTarget< ::loka::app::ButtonNode> target_;
        ClickPolicy policy_;
      };

      inline ClickButtonByIdAndFlushAdapter ClickButtonByIdAndFlush(const char *testId,
                                                                    ClickPolicy policy = DEFAULT_CLICK_POLICY)
      {
        return ClickButtonByIdAndFlushAdapter(testId, policy);
      }

      inline ClickButtonByIdAndFlushAdapter
      ClickButtonByIdAndFlush(const NodeSelector< ::loka::app::ButtonNode> &selector,
                              ClickPolicy policy = DEFAULT_CLICK_POLICY)
      {
        return ClickButtonByIdAndFlushAdapter(selector, policy);
      }

      /** Scenario-facing button action. A click includes the resulting Scene
          flush so the following action observes the completed projection. */
      inline ClickButtonByIdAndFlushAdapter ClickButton(const char *testId, ClickPolicy policy = DEFAULT_CLICK_POLICY)
      {
        return ClickButtonByIdAndFlush(testId, policy);
      }

      inline ClickButtonByIdAndFlushAdapter ClickButton(const NodeSelector< ::loka::app::ButtonNode> &selector,
                                                        ClickPolicy policy = DEFAULT_CLICK_POLICY)
      {
        return ClickButtonByIdAndFlush(selector, policy);
      }

      class ClickCellByIdAndFlushAdapter
      {
      public:
        typedef ::loka::app::scene::Scene *In;
        typedef ::loka::app::scene::Scene *Out;

        explicit ClickCellByIdAndFlushAdapter(const char *testId)
            : target_(testId)
        {
        }

        explicit ClickCellByIdAndFlushAdapter(const NodeSelector< ::loka::app::CellNode> &selector)
            : target_(selector)
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          out = in;
          if (!in)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_NULL_SCENE;
            return FLOW_STEP_FAILED;
          }
          StepRunStatus clickStatus = EmitNodeClick< ::loka::app::CellNode>(in, target_, error);
          if (clickStatus != FLOW_STEP_SUCCEEDED)
          {
            return clickStatus;
          }
          return FlushSceneInvalidation().run(out, out, error);
        }

      private:
        scene_test_detail::NodeTarget< ::loka::app::CellNode> target_;
      };

      /** Scenario-facing Cell action through the Cell's ordinary emitter. */
      inline ClickCellByIdAndFlushAdapter ClickCell(const char *testId)
      {
        return ClickCellByIdAndFlushAdapter(testId);
      }

      inline ClickCellByIdAndFlushAdapter ClickCell(const NodeSelector< ::loka::app::CellNode> &selector)
      {
        return ClickCellByIdAndFlushAdapter(selector);
      }

      class CheckTimingLessEqualAdapter
      {
      public:
        typedef SnapRecord In;
        typedef SnapRecord Out;

        CheckTimingLessEqualAdapter(const char *key, long maxValue)
            : key_(key ? key : ""),
              maxValue_(maxValue)
        {
        }

        StepRunStatus run(const In &in, Out &out, FlowError &error) const
        {
          out = in;
          return AssertSnapIntLessEqual(key_.c_str(), maxValue_).run(in, out, error);
        }

      private:
        std::string key_;
        long maxValue_;
      };

      inline CheckTimingLessEqualAdapter CheckTimingLessEqual(const char *key, long maxValue)
      {
        return CheckTimingLessEqualAdapter(key, maxValue);
      }

      class AssertTextEqualsAdapter
      {
      public:
        typedef ::loka::app::TextNode *In;
        typedef ::loka::app::TextNode *Out;

        explicit AssertTextEqualsAdapter(const char *expected)
            : expected_(expected ? expected : "")
        {
        }

        StepRunStatus run(In const &in, Out &out, FlowError &error) const
        {
          out = in;
          if (!in || !in->props.text_)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
            return FLOW_STEP_FAILED;
          }

          std::string actual;
          if (!::loka::platform::CollectUtf8(in->props.text_->get(), actual))
          {
            error.kind = FLOW_ERROR_KIND_SCENE_SCENARIO;
            error.code = FLOW_ERROR_SCENE_TEST_INVALID_CAPTURE_VALUE;
            return FLOW_STEP_FAILED;
          }

          if (actual != expected_)
          {
            error.kind = FLOW_ERROR_KIND_SCENE_TEST_ASSERT;
            error.code = FLOW_ERROR_SCENE_TEST_ASSERTION_FAILED;
            return FLOW_STEP_FAILED;
          }
          return FLOW_STEP_SUCCEEDED;
        }

      private:
        std::string expected_;
      };

      inline AssertTextEqualsAdapter AssertTextEquals(const char *expected)
      {
        return AssertTextEqualsAdapter(expected);
      }
    } // namespace testing
  } // namespace dsl
} // namespace loka

#endif // LOKA_DSL_TESTING_SCENE_TEST_FLOW_HPP
