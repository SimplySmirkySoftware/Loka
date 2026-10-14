#ifndef LOKA_DSL_FLOW_HPP
#define LOKA_DSL_FLOW_HPP

#include <cassert>
#include <vector>

#include "core/State.hpp"
#include "core/util/StateTrackerGuard.hpp"

namespace loka
{
  namespace dsl
  {
    enum StepRunStatus
    {
      FLOW_STEP_PENDING = 0,
      FLOW_STEP_SUCCEEDED = 1,
      FLOW_STEP_FAILED = 2
    };

    enum FlowRunResult
    {
      FLOW_RUN_PENDING = 0,
      FLOW_RUN_SUCCEEDED = 1,
      FLOW_RUN_FAILED = 2,
      FLOW_RUN_CANCELED = 3
    };

    struct FlowError
    {
      int kind;
      int code;
      FlowError()
          : kind(0),
            code(0)
      {
      }
    };

    enum
    {
      FLOW_ERROR_KIND_FLOW = 1000
    };

    enum
    {
      FLOW_ERROR_CODE_STEP_PENDING_TIMEOUT = 1001,
      FLOW_ERROR_CODE_ASSERT_PREDICATE_FAILED = 1002,
      FLOW_ERROR_CODE_MATCH_CHILD_OUTPUT_MISSING = 1003
    };

    enum FlowHandleResult
    {
      FLOW_ERROR_UNHANDLED = 0,
      FLOW_ERROR_HANDLED = 1
    };

    template <typename AdapterT> class RuntimeStep;
    template <typename InT, typename OutT> class MatchAdapter;

    namespace flow_detail
    {
      /** A run transaction released at PENDING or terminal, before finally.
          Internal success continuations borrow the original bracket. */
      class RunBracket
      {
      public:
        explicit RunBracket(loka::core::StateTracker *tracker)
            : tracker_(tracker), mode_(open(tracker))
        {
          if (this->mode_ == OWNED)
            this->tracker_->begin();
        }

        ~RunBracket() { this->release(); }

        void release()
        {
          loka::core::StateTracker *tracker = this->tracker_;
          this->tracker_ = 0;
          if (tracker && this->mode_ == OWNED)
            tracker->end();
        }

      private:
        enum Mode { NONE, JOINED, OWNED };
        loka::core::StateTracker *tracker_;
        const Mode mode_;

        static Mode open(loka::core::StateTracker *tracker)
        {
          using namespace loka::core;
          if (!tracker)
            return NONE;
          // Mirrors StateTrackerGuard's enrollment policy; release timing is
          // Flow-specific because finally runs after the owned bracket ends.
          switch (Operation::openActive(tracker))
          {
          case OPEN_OK:
          case OPEN_ALREADY_OPEN:
            return JOINED;
          case OPEN_REFUSED_BUSY:
          case OPEN_REFUSED_CLOSING:
          case OPEN_NO_CLOCK:
          case OPEN_CLOCK_REFUSED:
          case OPEN_REFUSED_NOT_PUSH:
            return OWNED;
          }
          return OWNED;
        }

        RunBracket(const RunBracket &);
        RunBracket &operator=(const RunBracket &);
      };

      template <typename A, typename B> struct IsSame
      {
        enum
        {
          value = 0
        };
      };

      template <typename A> struct IsSame<A, A>
      {
        enum
        {
          value = 1
        };
      };

      template <typename AdapterT> struct RuntimeStepCancel
      {
        static void request(AdapterT &)
        {
        }

        static void clear(AdapterT &)
        {
        }

        static void settle(AdapterT &)
        {
        }
      };

      template <typename InT, typename OutT> struct RuntimeStepCancel<MatchAdapter<InT, OutT> >
      {
        static void request(MatchAdapter<InT, OutT> &adapter);
        static void clear(MatchAdapter<InT, OutT> &adapter);
        static void settle(MatchAdapter<InT, OutT> &adapter);
      };
    } // namespace flow_detail

    struct MutableStateBinding
    {
      void *statePtr;
      loka::core::StateTracker *tracker;
      void (*setter)(void *statePtr, const void *valuePtr, loka::core::StateTracker *tracker);
    };

    template <typename T>
    static void ApplyMutableStateValue(void *statePtr, const T &value, loka::core::StateTracker *tracker)
    {
      assert(statePtr != 0 && "ApplyMutableStateValue requires a target state");
      assert(tracker != 0 && "ApplyMutableStateValue requires a tracker");
      loka::core::StateTrackerGuard guard(tracker);
      static_cast<loka::core::MutableState<T> *>(statePtr)->set(value, true);
    }

    template <typename T>
    static void MutableStateSetter(void *statePtr, const void *valuePtr, loka::core::StateTracker *tracker)
    {
      ApplyMutableStateValue<T>(statePtr, *static_cast<const T *>(valuePtr), tracker);
    }

    struct MutableStateFieldBinding
    {
      void *statePtr;
      loka::core::StateTracker *tracker;
      void (*setter)(void *statePtr,
                     const void *outputPtr,
                     std::size_t offset,
                     loka::core::StateTracker *tracker);
      std::size_t offset;
    };

    template <typename FieldT>
    static void MutableStateFieldSetter(void *statePtr,
                                        const void *outputPtr,
                                        std::size_t offset,
                                        loka::core::StateTracker *tracker)
    {
      const char *base = static_cast<const char *>(outputPtr);
      const FieldT *field = reinterpret_cast<const FieldT *>(base + offset);
      ApplyMutableStateValue<FieldT>(statePtr, *field, tracker);
    }

    template <typename AdapterT> class StepSpec
    {
    public:
      typedef typename AdapterT::In In;
      typedef typename AdapterT::Out Out;
      typedef void (*SuccessFn)(const Out &, void *);
      typedef void (*FinallyFn)(void *);
      typedef bool (*ErrorMatcherFn)(const FlowError &, void *);
      typedef FlowHandleResult (*ErrorHandlerFn)(const FlowError &, void *);

      struct SuccessCallback
      {
        SuccessFn fn;
        void *user;
        int resumeStepId;
      };

      struct FailureCallback
      {
        ErrorMatcherFn matcher;
        ErrorHandlerFn handler;
        void *user;
        int resumeStepId;
      };

      explicit StepSpec(int id, const AdapterT &adapter)
          : id_(id),
            adapter_(adapter),
            input_(0),
            finallyFn_(0),
            finallyUser_(0),
            pendingTimeout_(-1)
      {
      }

      StepSpec &input(const In *value)
      {
        this->input_ = value;
        return *this;
      }

      StepSpec &onSuccess(SuccessFn fn, void *user)
      {
        SuccessCallback cb;
        cb.fn = fn;
        cb.user = user;
        cb.resumeStepId = -1;
        this->successCallbacks_.push_back(cb);
        return *this;
      }

      StepSpec &onSuccess(SuccessFn fn, void *user, int resumeStepId)
      {
        SuccessCallback cb;
        cb.fn = fn;
        cb.user = user;
        cb.resumeStepId = resumeStepId;
        this->successCallbacks_.push_back(cb);
        return *this;
      }

      StepSpec &onSuccess(Out *targetState)
      {
        this->successStates_.push_back(targetState);
        return *this;
      }

      StepSpec &onSuccess(Out *targetState, int resumeStepId)
      {
        this->successStates_.push_back(targetState);
        SuccessCallback cb;
        cb.fn = 0;
        cb.user = 0;
        cb.resumeStepId = resumeStepId;
        this->successCallbacks_.push_back(cb);
        return *this;
      }

      /** Applies a successful output to borrowed State within its owner's
          borrowed tracker transaction. Inside a rail turn that transaction
          joins the turn's clock (core::Operation), so the State and its
          tracker must outlive the turn in which the Flow ran, not only the
          execution; an owner that must die earlier unregisters its States
          first, which lets its empty ledger leave the clock. Outside a turn
          the bracket closes before onSuccess returns, as before. */
      StepSpec &onSuccess(loka::core::MutableState<Out> *state, loka::core::StateTracker *tracker)
      {
        assert(state != 0 && "StepSpec::onSuccess requires a target state");
        assert(tracker != 0 && "StepSpec::onSuccess requires a tracker");
        MutableStateBinding binding;
        binding.statePtr = state;
        binding.tracker = tracker;
        binding.setter = &MutableStateSetter<Out>;
        this->mutableStateBindings_.push_back(binding);
        return *this;
      }

      template <typename FieldT, typename ClassT>
      StepSpec &onSuccess(loka::core::MutableState<FieldT> *state,
                          loka::core::StateTracker *tracker,
                          FieldT ClassT::*member)
      {
        typedef char LokaFieldBindingTypeMismatch[(flow_detail::IsSame<Out, ClassT>::value) ? 1 : -1];
        (void)sizeof(LokaFieldBindingTypeMismatch);
        assert(state != 0 && "StepSpec::onSuccess requires a target state");
        assert(tracker != 0 && "StepSpec::onSuccess requires a tracker");
        MutableStateFieldBinding binding;
        binding.statePtr = state;
        binding.tracker = tracker;
        binding.setter = &MutableStateFieldSetter<FieldT>;
        Out temp = Out();
        binding.offset = static_cast<std::size_t>(reinterpret_cast<const char *>(&(temp.*member))
                                                  - reinterpret_cast<const char *>(&temp));
        this->fieldBindings_.push_back(binding);
        return *this;
      }

      StepSpec &onFailure(ErrorMatcherFn matcher, ErrorHandlerFn handler, void *user)
      {
        FailureCallback cb;
        cb.matcher = matcher;
        cb.handler = handler;
        cb.user = user;
        cb.resumeStepId = -1;
        this->failureCallbacks_.push_back(cb);
        return *this;
      }

      StepSpec &onFailure(ErrorMatcherFn matcher, ErrorHandlerFn handler, void *user, int resumeStepId)
      {
        FailureCallback cb;
        cb.matcher = matcher;
        cb.handler = handler;
        cb.user = user;
        cb.resumeStepId = resumeStepId;
        this->failureCallbacks_.push_back(cb);
        return *this;
      }

      StepSpec &onFailure(ErrorHandlerFn handler, void *user)
      {
        return this->onFailure(&StepSpec::alwaysMatch, handler, user);
      }

      StepSpec &onFailure(ErrorHandlerFn handler, void *user, int resumeStepId)
      {
        return this->onFailure(&StepSpec::alwaysMatch, handler, user, resumeStepId);
      }

      StepSpec &onFinally(FinallyFn fn, void *user)
      {
        this->finallyFn_ = fn;
        this->finallyUser_ = user;
        return *this;
      }

      StepSpec &timeoutPending(int maxPendingRuns)
      {
        this->pendingTimeout_ = maxPendingRuns;
        return *this;
      }

      static bool alwaysMatch(const FlowError &, void *)
      {
        return true;
      }

      int id() const
      {
        return this->id_;
      }

      const AdapterT &adapter() const
      {
        return this->adapter_;
      }

      const In *inputPtr() const
      {
        return this->input_;
      }

      const std::vector<SuccessCallback> &successCallbacks() const
      {
        return this->successCallbacks_;
      }

      const std::vector<Out *> &successStates() const
      {
        return this->successStates_;
      }

      const std::vector<FailureCallback> &failureCallbacks() const
      {
        return this->failureCallbacks_;
      }

      const std::vector<MutableStateBinding> &mutableStateBindings() const
      {
        return this->mutableStateBindings_;
      }

      const std::vector<MutableStateFieldBinding> &fieldBindings() const
      {
        return this->fieldBindings_;
      }

      FinallyFn finallyFn() const
      {
        return this->finallyFn_;
      }

      void *finallyUser() const
      {
        return this->finallyUser_;
      }

      int pendingTimeout() const
      {
        return this->pendingTimeout_;
      }

    private:
      friend class RuntimeStep<AdapterT>;

      AdapterT &adapterForRuntime()
      {
        return this->adapter_;
      }

      int id_;
      AdapterT adapter_;
      const In *input_;
      std::vector<SuccessCallback> successCallbacks_;
      std::vector<Out *> successStates_;
      std::vector<FailureCallback> failureCallbacks_;
      std::vector<MutableStateBinding> mutableStateBindings_;
      std::vector<MutableStateFieldBinding> fieldBindings_;
      FinallyFn finallyFn_;
      void *finallyUser_;
      int pendingTimeout_;
    };

    template <typename AdapterT> StepSpec<AdapterT> Step(int id, const AdapterT &adapter)
    {
      return StepSpec<AdapterT>(id, adapter);
    }

    template <typename T> class AssertPredicateAdapter
    {
    public:
      typedef T In;
      typedef T Out;
      typedef bool (*PredicateFn)(const T &, void *);

      AssertPredicateAdapter(PredicateFn predicate,
                             void *user,
                             int errorKind = FLOW_ERROR_KIND_FLOW,
                             int errorCode = FLOW_ERROR_CODE_ASSERT_PREDICATE_FAILED)
          : predicate_(predicate),
            user_(user),
            errorKind_(errorKind),
            errorCode_(errorCode)
      {
      }

      StepRunStatus run(const T &in, T &out, FlowError &error) const
      {
        out = in;
        if (!this->predicate_ || this->predicate_(in, this->user_))
        {
          return FLOW_STEP_SUCCEEDED;
        }
        error.kind = this->errorKind_;
        error.code = this->errorCode_;
        return FLOW_STEP_FAILED;
      }

    private:
      PredicateFn predicate_;
      void *user_;
      int errorKind_;
      int errorCode_;
    };

    template <typename T>
    AssertPredicateAdapter<T> AssertPredicate(typename AssertPredicateAdapter<T>::PredicateFn predicate,
                                              void *user,
                                              int errorKind = FLOW_ERROR_KIND_FLOW,
                                              int errorCode = FLOW_ERROR_CODE_ASSERT_PREDICATE_FAILED)
    {
      return AssertPredicateAdapter<T>(predicate, user, errorKind, errorCode);
    }

    class FlowChainImpl
    {
    public:
      FlowChainImpl()
          : finallyFn_(0),
            finallyUser_(0),
            loadingState_(0),
            tracker_(0),
            triggerState_(0),
            triggerInputBuffer_(0),
            triggerReadFn_(0),
            triggerDeleteFn_(0),
            triggerCallback_(0),
            triggerRunning_(false),
            cancelRequested_(false),
            runBeginFn_(0),
            runEndFn_(0),
            runHookFlow_(0),
            runHookUser_(0),
            refs_(1),
            runPins_(0)
      {
      }

      ~FlowChainImpl()
      {
        this->clearTrigger();
        for (std::size_t i = 0; i < this->steps_.size(); ++i)
        {
          delete this->steps_[i];
        }
      }

      bool isShared() const
      {
        return this->refs_ > 1;
      }

      void retain()
      {
        ++this->refs_;
      }

      void release()
      {
        --this->refs_;
        if (this->refs_ == 0 && this->runPins_ == 0)
        {
          delete this;
        }
      }

      struct FlowSuccessCallback
      {
        void (*fn)(void *);
        void *user;
        int resumeStepId;
      };

      struct FlowFailureCallback
      {
        bool (*matcher)(const FlowError &, void *);
        FlowHandleResult (*handler)(const FlowError &, void *);
        void *user;
        int resumeStepId;
      };

      typedef void (*RunLifecycleFn)(void *, void *);

      std::vector<FlowSuccessCallback> successCallbacks_;
      std::vector<FlowFailureCallback> failureCallbacks_;
      void (*finallyFn_)(void *);
      void *finallyUser_;
      bool *loadingState_;
      loka::core::StateTracker *tracker_;

      // Trigger state members
      loka::core::StateBase *triggerState_;
      void *triggerInputBuffer_;
      void (*triggerReadFn_)(void *buffer, loka::core::StateBase *state);
      void (*triggerDeleteFn_)(void *buffer);
      loka::core::StateBase::OnChangeFn triggerCallback_;
      bool triggerRunning_;
      mutable bool cancelRequested_;
      RunLifecycleFn runBeginFn_;
      RunLifecycleFn runEndFn_;
      void *runHookFlow_;
      void *runHookUser_;

      class IRuntimeStep
      {
      public:
        virtual ~IRuntimeStep() {}
        virtual IRuntimeStep *clone() const = 0;
        virtual int stepId() const = 0;
        virtual void requestCancel() {}
        virtual void clearCancel() {}
        // Called when the owning chain consumes its cancellation, so a step
        // that owns a pending child can settle it at the child's boundary.
        virtual void settleCancel() {}
        virtual void setInputOverride(const void *) {}
        virtual bool isPending() const = 0;
        virtual void resetRunObservations() = 0;
        virtual bool lastFailure(FlowError &errorOut) const = 0;
        // Non-null only when this step produced its output in the current
        // run; a handled failure leaves the output unproduced.
        virtual const void *producedOutputPtr() const = 0;
        virtual StepRunStatus run(const void *inputFromPrev,
                                  FlowError &error,
                                  bool &errorHandled,
                                  int &resumeStepId,
                                  int &successResumeStepId) = 0;
        virtual const void *outputPtr() const = 0;
      };

      std::vector<IRuntimeStep *> steps_;

      FlowChainImpl *clone() const
      {
        FlowChainImpl *next = new FlowChainImpl();
        next->successCallbacks_ = this->successCallbacks_;
        next->failureCallbacks_ = this->failureCallbacks_;
        next->finallyFn_ = this->finallyFn_;
        next->finallyUser_ = this->finallyUser_;
        next->loadingState_ = this->loadingState_;
        next->tracker_ = this->tracker_;
        next->cancelRequested_ = this->cancelRequested_;
        // trigger is NOT cloned
        for (std::size_t i = 0; i < this->steps_.size(); ++i)
        {
          next->steps_.push_back(this->steps_[i]->clone());
        }
        return next;
      }

      // --- Methods moved from FlowChain ---

      void terminalCleanup(flow_detail::RunBracket &bracket) const
      {
        bracket.release();
        if (this->finallyFn_ != 0)
        {
          this->finallyFn_(this->finallyUser_);
        }
        if (this->loadingState_ != 0)
        {
          *this->loadingState_ = false;
        }
      }

      bool findStepIndex(int stepId, std::size_t &indexOut) const
      {
        for (std::size_t i = 0; i < this->steps_.size(); ++i)
        {
          if (this->steps_[i]->stepId() == stepId)
          {
            indexOut = i;
            return true;
          }
        }
        return false;
      }

      FlowRunResult runPinnedFromIndex(std::size_t startIndex) const
      {
        for (std::size_t i = 0; i < this->steps_.size(); ++i)
        {
          this->steps_[i]->resetRunObservations();
        }
        RunPinScope runPinScope(this);
        return this->runCoreFromIndex(startIndex);
      }

      FlowRunResult runPinnedFromStepId(int stepId) const
      {
        std::size_t start = 0;
        if (!this->findStepIndex(stepId, start))
        {
          return FLOW_RUN_FAILED;
        }
        return this->runPinnedFromIndex(start);
      }

      bool setFirstStepInputOverride(const void *input) const
      {
        if (this->steps_.empty())
        {
          return false;
        }
        this->steps_[0]->setInputOverride(input);
        return true;
      }

      FlowRunResult runPinnedWithInput(const void *input) const
      {
        if (!this->setFirstStepInputOverride(input))
        {
          return FLOW_RUN_FAILED;
        }
        return this->runPinnedFromIndex(0);
      }

      bool pendingStepId(int &stepIdOut) const
      {
        for (std::size_t i = 0; i < this->steps_.size(); ++i)
        {
          if (this->steps_[i]->isPending())
          {
            stepIdOut = this->steps_[i]->stepId();
            return true;
          }
        }
        return false;
      }

      const void *lastProducedOutputPtr() const
      {
        return this->steps_.empty() ? 0 : this->steps_[this->steps_.size() - 1]->producedOutputPtr();
      }

      void settleCancelOnSteps() const
      {
        for (std::size_t i = 0; i < this->steps_.size(); ++i)
        {
          this->steps_[i]->settleCancel();
        }
      }

      bool lastFailure(FlowError &errorOut) const
      {
        for (std::size_t i = this->steps_.size(); i > 0; --i)
        {
          if (this->steps_[i - 1]->lastFailure(errorOut))
          {
            return true;
          }
        }
        return false;
      }

      void requestCancelFromParent()
      {
        for (std::size_t i = 0; i < this->steps_.size(); ++i)
        {
          this->steps_[i]->requestCancel();
        }
      }

      void clearCancelFromParent()
      {
        for (std::size_t i = 0; i < this->steps_.size(); ++i)
        {
          this->steps_[i]->clearCancel();
        }
      }

      // Only internal flow-level success continuation borrows a bracket.
      // Public run/resume and trigger entry create a fresh one after the hook.
      FlowRunResult runCoreFromIndex(std::size_t startIndex,
                                     flow_detail::RunBracket *continuation = 0) const
      {
        // Snapshot the hook fields once: begin/end stay paired even if the
        // hooks are cleared mid-run (e.g. the owning FlowSlot disowns the
        // flow while a shared-impl copy is executing).
        struct RunScope
        {
          explicit RunScope(const FlowChainImpl *impl)
              : beginFn_(impl->runBeginFn_),
                endFn_(impl->runEndFn_),
                hookFlow_(impl->runHookFlow_),
                hookUser_(impl->runHookUser_)
          {
            assert((this->beginFn_ == 0) == (this->endFn_ == 0)
                   && "FlowChainImpl::runFromIndex requires begin/end hooks to be paired");
            if (this->beginFn_ != 0)
            {
              assert(this->hookFlow_ != 0 && "FlowChainImpl::runFromIndex requires a hook flow");
              this->beginFn_(this->hookFlow_, this->hookUser_);
            }
          }

          ~RunScope()
          {
            if (this->beginFn_ != 0 && this->endFn_ != 0)
            {
              this->endFn_(this->hookFlow_, this->hookUser_);
            }
          }

          RunLifecycleFn beginFn_;
          RunLifecycleFn endFn_;
          void *hookFlow_;
          void *hookUser_;
        } runScope(this);

        if (startIndex >= this->steps_.size())
        {
          return FLOW_RUN_FAILED;
        }

        if (continuation)
          return this->runSegmentFromIndex(startIndex, true, *continuation);
        flow_detail::RunBracket bracket(this->tracker_);
        return this->runSegmentFromIndex(startIndex, false, bracket);
      }

      FlowRunResult runSegmentFromIndex(std::size_t startIndex, bool resumedSegment,
                                       flow_detail::RunBracket &bracket) const
      {
        const void *current = 0;
        if (startIndex == 0 && this->triggerInputBuffer_)
        {
          current = this->triggerInputBuffer_;
        }
        else if (startIndex > 0 && startIndex < this->steps_.size())
        {
          current = this->steps_[startIndex - 1]->outputPtr();
        }
        FlowError error;
        if (!resumedSegment)
        {
          if (this->loadingState_ != 0)
          {
            *this->loadingState_ = true;
          }
        }

        static const std::size_t MAX_ITERATIONS = 1024;
        std::size_t iterations = 0;
        for (std::size_t i = startIndex; i < this->steps_.size(); ++i)
        {
          if (this->cancelRequested_)
          {
            this->cancelRequested_ = false;
            this->settleCancelOnSteps();
            this->terminalCleanup(bracket);
            return FLOW_RUN_CANCELED;
          }
          if (++iterations > MAX_ITERATIONS)
          {
            this->terminalCleanup(bracket);
            return FLOW_RUN_FAILED;
          }
          bool stepHandled = false;
          int stepResumeStepId = -1;
          int stepSuccessResumeStepId = -1;
          StepRunStatus stepStatus =
              this->steps_[i]->run(current, error, stepHandled, stepResumeStepId, stepSuccessResumeStepId);
          if (stepStatus == FLOW_STEP_SUCCEEDED)
          {
            current = this->steps_[i]->outputPtr();

            if (stepSuccessResumeStepId >= 0)
            {
              std::size_t jumpIndex = 0;
              if (!this->findStepIndex(stepSuccessResumeStepId, jumpIndex))
              {
                this->terminalCleanup(bracket);
                return FLOW_RUN_FAILED;
              }
              i = (jumpIndex == 0) ? static_cast<std::size_t>(-1) : (jumpIndex - 1);
            }
            continue;
          }

          if (stepStatus == FLOW_STEP_PENDING)
          {
            bracket.release();
            return FLOW_RUN_PENDING;
          }

          bool flowHandled = false;
          int flowResumeStepId = -1;
          for (std::size_t k = 0; k < this->failureCallbacks_.size(); ++k)
          {
            const FlowFailureCallback &cb = this->failureCallbacks_[k];
            if (cb.matcher != 0 && cb.matcher(error, cb.user))
            {
              const bool handled = (cb.handler != 0 && cb.handler(error, cb.user) == FLOW_ERROR_HANDLED);
              flowHandled = handled;
              if (handled && cb.resumeStepId >= 0)
              {
                flowResumeStepId = cb.resumeStepId;
              }
              break; // first-match-wins
            }
          }

          const int jumpStepId = stepResumeStepId >= 0 ? stepResumeStepId : flowResumeStepId;
          if (jumpStepId >= 0)
          {
            std::size_t jumpIndex = 0;
            if (!this->findStepIndex(jumpStepId, jumpIndex))
            {
              this->terminalCleanup(bracket);
              return FLOW_RUN_FAILED;
            }
            i = (jumpIndex == 0) ? static_cast<std::size_t>(-1) : (jumpIndex - 1);
            continue;
          }

          this->terminalCleanup(bracket);
          return stepHandled || flowHandled ? FLOW_RUN_SUCCEEDED : FLOW_RUN_FAILED;
        }

        // Flow completed successfully — fire flow-level success callbacks.
        int flowSuccessResumeStepId = -1;
        for (std::size_t k = 0; k < this->successCallbacks_.size(); ++k)
        {
          this->successCallbacks_[k].fn(this->successCallbacks_[k].user);
          if (flowSuccessResumeStepId < 0 && this->successCallbacks_[k].resumeStepId >= 0)
          {
            flowSuccessResumeStepId = this->successCallbacks_[k].resumeStepId;
          }
        }

        if (flowSuccessResumeStepId >= 0)
        {
          std::size_t jumpIndex = 0;
          if (!this->findStepIndex(flowSuccessResumeStepId, jumpIndex))
          {
            this->terminalCleanup(bracket);
            return FLOW_RUN_FAILED;
          }
          // No terminalCleanup() here: the resumed segment continues the same
          // logical run, and its own terminal fires the flow-level finally,
          // tracker end, and loading=false exactly once.
          return this->runCoreFromIndex(jumpIndex, &bracket);
        }

        this->terminalCleanup(bracket);
        return FLOW_RUN_SUCCEEDED;
      }

      // --- Trigger support ---

      void setTrigger(loka::core::StateBase *state,
                      void *buffer,
                      void (*readFn)(void *, loka::core::StateBase *),
                      void (*deleteFn)(void *),
                      loka::core::StateBase::OnChangeFn callback)
      {
        this->clearTrigger();
        this->triggerState_ = state;
        this->triggerInputBuffer_ = buffer;
        this->triggerReadFn_ = readFn;
        this->triggerDeleteFn_ = deleteFn;
        this->triggerCallback_ = callback;
        this->triggerRunning_ = false;
        state->deferBind(callback, this);
      }

      /** Disconnect ingress without disturbing a step's pinned input buffer. */
      void withdrawTrigger()
      {
        if (this->triggerState_ && this->triggerCallback_)
          this->triggerState_->deferUnbind(this->triggerCallback_, this);
        this->triggerState_ = 0;
        this->triggerCallback_ = 0;
        this->triggerReadFn_ = 0;
      }

      void clearTrigger()
      {
        this->withdrawTrigger();
        if (this->triggerDeleteFn_ && this->triggerInputBuffer_)
        {
          this->triggerDeleteFn_(this->triggerInputBuffer_);
        }
        this->triggerState_ = 0;
        this->triggerInputBuffer_ = 0;
        this->triggerReadFn_ = 0;
        this->triggerDeleteFn_ = 0;
        this->triggerCallback_ = 0;
        this->triggerRunning_ = false;
      }

      static void OnTriggerChanged(void *userData)
      {
        FlowChainImpl *self = static_cast<FlowChainImpl *>(userData);
        if (self->triggerRunning_)
          return;
        RunPinScope runPinScope(self);
        self->triggerRunning_ = true;
        if (self->triggerReadFn_ && self->triggerInputBuffer_)
        {
          self->triggerReadFn_(self->triggerInputBuffer_, self->triggerState_);
        }
        // Trigger callbacks hold only the impl pointer, so lifetime still
        // needs pinning here; use the run pin rather than refs_ so callback
        // code can cancel the active impl without tripping copy-on-write.
        FlowRunResult result = self->runCoreFromIndex(0);
        if (result != FLOW_RUN_PENDING)
        {
          self->triggerRunning_ = false;
        }
      }

    private:
      void pinRun()
      {
        ++this->runPins_;
      }

      void unpinRun()
      {
        assert(this->runPins_ > 0 && "FlowChainImpl::unpinRun underflow");
        --this->runPins_;
        if (this->refs_ == 0 && this->runPins_ == 0)
        {
          delete this;
        }
      }

      struct RunPinScope
      {
        explicit RunPinScope(const FlowChainImpl *impl)
            : impl_(const_cast<FlowChainImpl *>(impl))
        {
          assert(this->impl_ != 0 && "FlowChainImpl::RunPinScope requires impl");
          this->impl_->pinRun();
        }

        ~RunPinScope()
        {
          this->impl_->unpinRun();
        }

        FlowChainImpl *impl_;

      private:
        RunPinScope(const RunPinScope &);
        RunPinScope &operator=(const RunPinScope &);
      };

      int refs_;
      int runPins_;
      FlowChainImpl(const FlowChainImpl &);
      FlowChainImpl &operator=(const FlowChainImpl &);
    };

    template <typename AdapterT> class RuntimeStep : public FlowChainImpl::IRuntimeStep
    {
    public:
      typedef StepSpec<AdapterT> Spec;
      typedef typename Spec::In In;
      typedef typename Spec::Out Out;

      explicit RuntimeStep(const Spec &spec)
          : spec_(spec),
            out_(),
            pendingCount_(0),
            inputOverride_(0),
            hasLastFailure_(false),
            lastFailure_(),
            producedOutput_(false)
      {
      }

      virtual FlowChainImpl::IRuntimeStep *clone() const
      {
        return new RuntimeStep<AdapterT>(this->spec_);
      }

      virtual int stepId() const
      {
        return this->spec_.id();
      }

      virtual void setInputOverride(const void *input)
      {
        this->inputOverride_ = input;
      }

      virtual void requestCancel()
      {
        flow_detail::RuntimeStepCancel<AdapterT>::request(this->spec_.adapterForRuntime());
      }

      virtual void clearCancel()
      {
        flow_detail::RuntimeStepCancel<AdapterT>::clear(this->spec_.adapterForRuntime());
      }

      virtual void settleCancel()
      {
        flow_detail::RuntimeStepCancel<AdapterT>::settle(this->spec_.adapterForRuntime());
      }

      virtual bool isPending() const
      {
        return this->pendingCount_ > 0;
      }

      virtual void resetRunObservations()
      {
        this->hasLastFailure_ = false;
        this->producedOutput_ = false;
      }

      virtual const void *producedOutputPtr() const
      {
        return this->producedOutput_ ? &this->out_ : 0;
      }

      virtual bool lastFailure(FlowError &errorOut) const
      {
        if (!this->hasLastFailure_)
        {
          return false;
        }
        errorOut = this->lastFailure_;
        return true;
      }

      virtual StepRunStatus
      run(const void *inputFromPrev, FlowError &error, bool &errorHandled, int &resumeStepId, int &successResumeStepId)
      {
        errorHandled = false;
        resumeStepId = -1;
        successResumeStepId = -1;
        const In *in = this->spec_.inputPtr();
        if (in == 0)
        {
          in = static_cast<const In *>(this->inputOverride_ ? this->inputOverride_ : inputFromPrev);
        }
        assert(in != 0);

        StepRunStatus status = this->spec_.adapter().run(*in, this->out_, error);
        if (status == FLOW_STEP_SUCCEEDED)
        {
          this->pendingCount_ = 0;
          this->inputOverride_ = 0;
          this->producedOutput_ = true;
          const std::vector<typename Spec::SuccessCallback> &callbacks = this->spec_.successCallbacks();
          for (std::size_t i = 0; i < callbacks.size(); ++i)
          {
            if (callbacks[i].fn != 0)
            {
              callbacks[i].fn(this->out_, callbacks[i].user);
            }
            if (successResumeStepId < 0 && callbacks[i].resumeStepId >= 0)
            {
              successResumeStepId = callbacks[i].resumeStepId;
            }
          }

          const std::vector<Out *> &states = this->spec_.successStates();
          for (std::size_t i = 0; i < states.size(); ++i)
          {
            if (states[i] != 0)
            {
              *states[i] = this->out_;
            }
          }

          const std::vector<MutableStateBinding> &bindings = this->spec_.mutableStateBindings();
          for (std::size_t i = 0; i < bindings.size(); ++i)
          {
            bindings[i].setter(bindings[i].statePtr, &this->out_, bindings[i].tracker);
          }

          const std::vector<MutableStateFieldBinding> &fields = this->spec_.fieldBindings();
          for (std::size_t i = 0; i < fields.size(); ++i)
          {
            fields[i].setter(fields[i].statePtr, &this->out_, fields[i].offset, fields[i].tracker);
          }

          if (this->spec_.finallyFn() != 0)
          {
            this->spec_.finallyFn()(this->spec_.finallyUser());
          }
          return FLOW_STEP_SUCCEEDED;
        }

        if (status == FLOW_STEP_PENDING)
        {
          ++this->pendingCount_;
          if (this->spec_.pendingTimeout() >= 0 && this->pendingCount_ > this->spec_.pendingTimeout())
          {
            error.kind = FLOW_ERROR_KIND_FLOW;
            error.code = FLOW_ERROR_CODE_STEP_PENDING_TIMEOUT;
            status = FLOW_STEP_FAILED;
          }
          else
          {
            return FLOW_STEP_PENDING;
          }
        }

        this->pendingCount_ = 0;
        this->inputOverride_ = 0;

        const std::vector<typename Spec::FailureCallback> &failureCallbacks = this->spec_.failureCallbacks();
        this->pendingCount_ = 0;
        for (std::size_t i = 0; i < failureCallbacks.size(); ++i)
        {
          const typename Spec::FailureCallback &cb = failureCallbacks[i];
          if (cb.matcher != 0 && cb.matcher(error, cb.user))
          {
            const bool handled = (cb.handler != 0 && cb.handler(error, cb.user) == FLOW_ERROR_HANDLED);
            errorHandled = handled;
            if (handled && cb.resumeStepId >= 0)
            {
              resumeStepId = cb.resumeStepId;
            }
            break; // first-match-wins
          }
        }

        if (this->spec_.finallyFn() != 0)
        {
          this->spec_.finallyFn()(this->spec_.finallyUser());
        }
        this->lastFailure_ = error;
        this->hasLastFailure_ = true;
        return FLOW_STEP_FAILED;
      }

      virtual const void *outputPtr() const
      {
        return &this->out_;
      }

    private:
      Spec spec_;
      Out out_;
      int pendingCount_;
      const void *inputOverride_;
      bool hasLastFailure_;
      FlowError lastFailure_;
      bool producedOutput_;
    };

    template <typename InT, typename OutT> class FlowChain
    {
    public:
      typedef InT In;
      typedef OutT Out;
      typedef bool (*ErrorMatcherFn)(const FlowError &, void *);
      typedef FlowHandleResult (*ErrorHandlerFn)(const FlowError &, void *);

      explicit FlowChain(FlowChainImpl *impl)
          : impl_(impl)
      {
      }

      typedef typename FlowChainImpl::RunLifecycleFn RunLifecycleFn;

      FlowChain(const FlowChain &other)
          : impl_(other.impl_)
      {
        this->impl_->retain();
      }

      ~FlowChain()
      {
        this->impl_->release();
      }

      FlowChain &operator=(const FlowChain &other)
      {
        if (this != &other)
        {
          this->impl_->release();
          this->impl_ = other.impl_;
          this->impl_->retain();
        }
        return *this;
      }

      template <typename NextAdapterT>
      FlowChain<InT, typename NextAdapterT::Out> operator|(const StepSpec<NextAdapterT> &step) const
      {
        typedef typename NextAdapterT::In NextIn;
        typedef typename NextAdapterT::Out NextOut;
        typedef char LokaFlowStepTypeMismatch[(flow_detail::IsSame<OutT, NextIn>::value) ? 1 : -1];
        (void)sizeof(LokaFlowStepTypeMismatch);

        FlowChainImpl *nextImpl = this->impl_->clone();
        nextImpl->steps_.push_back(new RuntimeStep<NextAdapterT>(step));
        return FlowChain<InT, NextOut>(nextImpl);
      }

      FlowChain &onSuccess(void (*fn)(void *), void *user)
      {
        this->detachIfShared();
        FlowChainImpl::FlowSuccessCallback cb;
        cb.fn = fn;
        cb.user = user;
        cb.resumeStepId = -1;
        this->impl_->successCallbacks_.push_back(cb);
        return *this;
      }

      FlowChain &onSuccess(void (*fn)(void *), void *user, int resumeStepId)
      {
        this->detachIfShared();
        FlowChainImpl::FlowSuccessCallback cb;
        cb.fn = fn;
        cb.user = user;
        cb.resumeStepId = resumeStepId;
        this->impl_->successCallbacks_.push_back(cb);
        return *this;
      }

      FlowChain &onFailure(ErrorMatcherFn matcher, ErrorHandlerFn handler, void *user)
      {
        this->detachIfShared();
        FlowChainImpl::FlowFailureCallback cb;
        cb.matcher = matcher;
        cb.handler = handler;
        cb.user = user;
        cb.resumeStepId = -1;
        this->impl_->failureCallbacks_.push_back(cb);
        return *this;
      }

      FlowChain &onFailure(ErrorMatcherFn matcher, ErrorHandlerFn handler, void *user, int resumeStepId)
      {
        this->detachIfShared();
        FlowChainImpl::FlowFailureCallback cb;
        cb.matcher = matcher;
        cb.handler = handler;
        cb.user = user;
        cb.resumeStepId = resumeStepId;
        this->impl_->failureCallbacks_.push_back(cb);
        return *this;
      }

      FlowChain &onFailure(ErrorHandlerFn handler, void *user)
      {
        return this->onFailure(&FlowChain::alwaysMatch, handler, user);
      }

      FlowChain &onFailure(ErrorHandlerFn handler, void *user, int resumeStepId)
      {
        return this->onFailure(&FlowChain::alwaysMatch, handler, user, resumeStepId);
      }

      FlowChain &onFinally(void (*fn)(void *), void *user)
      {
        this->detachIfShared();
        this->impl_->finallyFn_ = fn;
        this->impl_->finallyUser_ = user;
        return *this;
      }

      FlowChain &trackLoading(bool *loadingState)
      {
        this->detachIfShared();
        this->impl_->loadingState_ = loadingState;
        return *this;
      }

      FlowChain &withTracker(loka::core::StateTracker *tracker)
      {
        this->detachIfShared();
        this->impl_->tracker_ = tracker;
        return *this;
      }

      FlowChain &cancel()
      {
        this->detachIfShared();
        this->impl_->cancelRequested_ = true;
        this->impl_->requestCancelFromParent();
        return *this;
      }

      FlowChain &clearCancel()
      {
        this->detachIfShared();
        this->impl_->cancelRequested_ = false;
        this->impl_->clearCancelFromParent();
        return *this;
      }

      FlowChain &bindTrigger(loka::core::State<InT> *source)
      {
        this->detachIfShared();
        InT *buffer = new InT();
        this->impl_->setTrigger(
            source, buffer, &FlowChain::TriggerRead, &FlowChain::TriggerDelete, &FlowChainImpl::OnTriggerChanged);
        return *this;
      }

      bool run() const
      {
        const FlowRunResult result = this->impl_->runPinnedFromIndex(0);
        return result == FLOW_RUN_SUCCEEDED;
      }

      FlowRunResult runResult() const
      {
        return this->impl_->runPinnedFromIndex(0);
      }

      bool resume(int stepId) const
      {
        return this->resumeResult(stepId) == FLOW_RUN_SUCCEEDED;
      }

      FlowChain &setExecutionHooks(RunLifecycleFn beginFn, RunLifecycleFn endFn, void *user)
      {
        assert((beginFn == 0) == (endFn == 0) && "FlowChain::setExecutionHooks requires begin/end hooks to be paired");
        this->detachIfShared();
        this->impl_->runBeginFn_ = beginFn;
        this->impl_->runEndFn_ = endFn;
        this->impl_->runHookUser_ = user;
        this->impl_->runHookFlow_ = this;
        return *this;
      }

      /** Withdraw the actual participating implementation, without copy-on-write. */
      void withdraw() { this->impl_->withdrawTrigger(); }

      // Clears the hooks on the SHARED impl without detaching: used when the
      // hook owner (FlowSlot) disowns this flow, so wrappers that still share
      // the impl cannot fire hooks into the owner's freed run state.
      void clearExecutionHooks()
      {
        this->impl_->runBeginFn_ = 0;
        this->impl_->runEndFn_ = 0;
        this->impl_->runHookFlow_ = 0;
        this->impl_->runHookUser_ = 0;
      }

      FlowRunResult resumeResult(int stepId) const
      {
        FlowChainImpl *impl = this->impl_;
        FlowRunResult result = impl->runPinnedFromStepId(stepId);
        if (result != FLOW_RUN_PENDING)
        {
          impl->triggerRunning_ = false;
        }
        return result;
      }

    public:
      static bool alwaysMatch(const FlowError &, void *)
      {
        return true;
      }

      void detachIfShared()
      {
        if (!this->impl_->isShared())
        {
          return;
        }
        FlowChainImpl *current = this->impl_;
        FlowChainImpl *next = current->clone();
        const RunLifecycleFn runBeginFn = current->runBeginFn_;
        const RunLifecycleFn runEndFn = current->runEndFn_;
        void *runHookUser = current->runHookUser_;
        const bool preserveHooks = (runBeginFn != 0 || runEndFn != 0) && current->runHookFlow_ == this;
        current->release();
        this->impl_ = next;
        if (preserveHooks)
        {
          this->impl_->runBeginFn_ = runBeginFn;
          this->impl_->runEndFn_ = runEndFn;
          this->impl_->runHookUser_ = runHookUser;
          this->impl_->runHookFlow_ = this;
        }
      }

      FlowChainImpl *impl_;

    private:
      static void TriggerRead(void *buf, loka::core::StateBase *st)
      {
        *static_cast<InT *>(buf) = static_cast<loka::core::State<InT> *>(st)->get();
      }

      static void TriggerDelete(void *buf)
      {
        delete static_cast<InT *>(buf);
      }
    };

    class Flow
    {
    public:
      template <typename AdapterT>
      FlowChain<typename AdapterT::In, typename AdapterT::Out> operator|(const StepSpec<AdapterT> &step) const
      {
        FlowChainImpl *impl = new FlowChainImpl();
        impl->finallyFn_ = 0;
        impl->finallyUser_ = 0;
        impl->steps_.push_back(new RuntimeStep<AdapterT>(step));
        return FlowChain<typename AdapterT::In, typename AdapterT::Out>(impl);
      }
    };

    /** Implements one first-match-wins Flow step. Child chains are owned
        clones and advance only while the parent drives this adapter. */
    template <typename InT, typename OutT> class MatchAdapter
    {
    public:
      typedef InT In;
      typedef OutT Out;
      typedef bool (*MatcherFn)(const In &, void *);
      typedef StepRunStatus (*ValueHandlerFn)(const In &, Out &, FlowError &, void *);

      MatchAdapter()
          : arms_(),
            otherwise_(0),
            otherwiseUser_(0),
            selectedArm_(-1),
            pendingChildStepId_(-1),
            cancelForwarded_(false),
            matchedInput_()
      {
      }

      MatchAdapter(const MatchAdapter &other)
          : arms_(other.arms_),
            otherwise_(other.otherwise_),
            otherwiseUser_(other.otherwiseUser_),
            selectedArm_(-1),
            pendingChildStepId_(-1),
            cancelForwarded_(false),
            matchedInput_()
      {
      }

      MatchAdapter &operator=(const MatchAdapter &other)
      {
        if (this != &other)
        {
          this->arms_ = other.arms_;
          this->otherwise_ = other.otherwise_;
          this->otherwiseUser_ = other.otherwiseUser_;
          this->selectedArm_ = -1;
          this->pendingChildStepId_ = -1;
          this->cancelForwarded_ = false;
        }
        return *this;
      }

      void addValueArm(MatcherFn matcher, void *user, ValueHandlerFn handler)
      {
        this->arms_.push_back(Arm(matcher, user, handler));
      }

      template <typename ChildInT, typename ChildOutT>
      void addChildArm(MatcherFn matcher, void *user, const FlowChain<ChildInT, ChildOutT> &child)
      {
        typedef char LokaFlowMatchChildOutputTypeMismatch[
            (flow_detail::IsSame<Out, ChildOutT>::value) ? 1 : -1];
        (void)sizeof(LokaFlowMatchChildOutputTypeMismatch);
        this->arms_.push_back(Arm(matcher,
                                  user,
                                  child.impl_,
                                  flow_detail::IsSame<In, ChildInT>::value != 0));
      }

      void setOtherwise(ValueHandlerFn handler, void *user)
      {
        this->otherwise_ = handler;
        this->otherwiseUser_ = user;
      }

      StepRunStatus run(const In &in, Out &out, FlowError &error) const
      {
        if (this->cancelForwarded_)
        {
          this->clearChildCancel();
          this->resetRun();
        }

        // The value that selected the arm is captured once per fresh
        // selection and reused for every drive until the step settles: the
        // caller's `in` may point at a trigger buffer that a rebind replaces
        // while a value arm or a child is still pending.
        if (this->selectedArm_ < 0)
        {
          for (std::size_t i = 0; i < this->arms_.size(); ++i)
          {
            if (this->arms_[i].matcher != 0 && this->arms_[i].matcher(in, this->arms_[i].user))
            {
              this->selectedArm_ = static_cast<int>(i);
              break;
            }
          }

          if (this->selectedArm_ < 0)
          {
            if (this->otherwise_ == 0)
            {
              return FLOW_STEP_FAILED;
            }
            this->selectedArm_ = static_cast<int>(this->arms_.size());
          }
          this->matchedInput_ = in;
        }

        if (this->selectedArm_ == static_cast<int>(this->arms_.size()))
        {
          const StepRunStatus status = this->otherwise_(this->matchedInput_, out, error, this->otherwiseUser_);
          if (status != FLOW_STEP_PENDING)
          {
            this->resetRun();
          }
          return status;
        }

        Arm &arm = this->arms_[static_cast<std::size_t>(this->selectedArm_)];
        if (arm.valueHandler != 0)
        {
          const StepRunStatus status = arm.valueHandler(this->matchedInput_, out, error, arm.user);
          if (status != FLOW_STEP_PENDING)
          {
            this->resetRun();
          }
          return status;
        }

        // The child reads the matched value through a pointer it keeps across
        // pending resumes, so it points at storage this adapter owns; the
        // pointer is refreshed on every drive so a cloned adapter never hands
        // out its source's address.
        FlowRunResult childResult = FLOW_RUN_FAILED;
        if (this->pendingChildStepId_ >= 0)
        {
          if (arm.acceptsMatchInput)
          {
            arm.child->setFirstStepInputOverride(&this->matchedInput_);
          }
          childResult = arm.child->runPinnedFromStepId(this->pendingChildStepId_);
        }
        else
        {
          // A fresh arm start must not inherit a cancel request that an
          // earlier parent cancel() left on the child and a copy-on-write
          // clone carried over (the clone resets this adapter's own state).
          arm.child->cancelRequested_ = false;
          arm.child->clearCancelFromParent();
          if (arm.acceptsMatchInput)
          {
            childResult = arm.child->runPinnedWithInput(&this->matchedInput_);
          }
          else
          {
            childResult = arm.child->runPinnedFromIndex(0);
          }
        }

        if (childResult == FLOW_RUN_PENDING)
        {
          int pendingStepId = -1;
          if (!arm.child->pendingStepId(pendingStepId))
          {
            this->resetRun();
            return FLOW_STEP_FAILED;
          }
          this->pendingChildStepId_ = pendingStepId;
          return FLOW_STEP_PENDING;
        }

        if (childResult == FLOW_RUN_SUCCEEDED)
        {
          // A child whose own onFailure handled an error reports a
          // successful run without ever producing its final output. That is
          // not a value this arm can forward, so it is a Match failure.
          const Out *childOut = static_cast<const Out *>(arm.child->lastProducedOutputPtr());
          if (childOut != 0)
          {
            out = *childOut;
            this->resetRun();
            return FLOW_STEP_SUCCEEDED;
          }
          error.kind = FLOW_ERROR_KIND_FLOW;
          error.code = FLOW_ERROR_CODE_MATCH_CHILD_OUTPUT_MISSING;
        }
        else if (childResult == FLOW_RUN_FAILED)
        {
          FlowError childError;
          if (arm.child->lastFailure(childError))
          {
            error = childError;
          }
        }

        this->resetRun();
        return FLOW_STEP_FAILED;
      }

      // The parent consumed its cancellation: drive a pending child to its
      // own step boundary so its terminal cleanup (tracker end, onFinally,
      // loading state) runs exactly once, then forget the selection.
      void settleCancel() const
      {
        if (this->selectedArm_ >= 0 && this->selectedArm_ < static_cast<int>(this->arms_.size()))
        {
          Arm &arm = this->arms_[static_cast<std::size_t>(this->selectedArm_)];
          if (arm.child != 0 && this->pendingChildStepId_ >= 0)
          {
            arm.child->cancelRequested_ = true;
            arm.child->runPinnedFromStepId(this->pendingChildStepId_);
          }
        }
        this->resetRun();
      }

      void requestCancel() const
      {
        if (this->selectedArm_ < 0)
        {
          return;
        }
        this->cancelForwarded_ = true;
        if (this->selectedArm_ >= static_cast<int>(this->arms_.size()))
        {
          return;
        }
        Arm &arm = this->arms_[static_cast<std::size_t>(this->selectedArm_)];
        if (arm.child != 0)
        {
          arm.child->cancelRequested_ = true;
          arm.child->requestCancelFromParent();
        }
      }

      void clearCancel() const
      {
        if (!this->cancelForwarded_)
        {
          return;
        }
        if (this->selectedArm_ >= 0 && this->selectedArm_ < static_cast<int>(this->arms_.size()))
        {
          Arm &arm = this->arms_[static_cast<std::size_t>(this->selectedArm_)];
          if (arm.child != 0)
          {
            arm.child->cancelRequested_ = false;
            arm.child->clearCancelFromParent();
          }
        }
        this->cancelForwarded_ = false;
      }

    private:
      struct Arm
      {
        Arm(MatcherFn matcherValue, void *userValue, ValueHandlerFn handlerValue)
            : matcher(matcherValue),
              user(userValue),
              valueHandler(handlerValue),
              child(0),
              acceptsMatchInput(false)
        {
        }

        Arm(MatcherFn matcherValue, void *userValue, FlowChainImpl *childValue, bool acceptsInput)
            : matcher(matcherValue),
              user(userValue),
              valueHandler(0),
              child(childValue ? childValue->clone() : 0),
              acceptsMatchInput(acceptsInput)
        {
        }

        Arm(const Arm &other)
            : matcher(other.matcher),
              user(other.user),
              valueHandler(other.valueHandler),
              child(other.child ? other.child->clone() : 0),
              acceptsMatchInput(other.acceptsMatchInput)
        {
        }

        ~Arm()
        {
          if (this->child != 0)
          {
            this->child->release();
          }
        }

        Arm &operator=(const Arm &other)
        {
          if (this != &other)
          {
            FlowChainImpl *nextChild = other.child ? other.child->clone() : 0;
            if (this->child != 0)
            {
              this->child->release();
            }
            this->matcher = other.matcher;
            this->user = other.user;
            this->valueHandler = other.valueHandler;
            this->child = nextChild;
            this->acceptsMatchInput = other.acceptsMatchInput;
          }
          return *this;
        }

        MatcherFn matcher;
        void *user;
        ValueHandlerFn valueHandler;
        FlowChainImpl *child;
        bool acceptsMatchInput;
      };

      void clearChildCancel() const
      {
        if (this->selectedArm_ >= 0 && this->selectedArm_ < static_cast<int>(this->arms_.size()))
        {
          Arm &arm = this->arms_[static_cast<std::size_t>(this->selectedArm_)];
          if (arm.child != 0)
          {
            arm.child->cancelRequested_ = false;
          }
        }
      }

      void resetRun() const
      {
        this->selectedArm_ = -1;
        this->pendingChildStepId_ = -1;
        this->cancelForwarded_ = false;
      }

      mutable std::vector<Arm> arms_;
      ValueHandlerFn otherwise_;
      void *otherwiseUser_;
      mutable int selectedArm_;
      mutable int pendingChildStepId_;
      mutable bool cancelForwarded_;
      mutable In matchedInput_;
    };

    namespace flow_detail
    {
      template <typename InT, typename OutT>
      void RuntimeStepCancel<MatchAdapter<InT, OutT> >::request(MatchAdapter<InT, OutT> &adapter)
      {
        adapter.requestCancel();
      }

      template <typename InT, typename OutT>
      void RuntimeStepCancel<MatchAdapter<InT, OutT> >::clear(MatchAdapter<InT, OutT> &adapter)
      {
        adapter.clearCancel();
      }

      template <typename InT, typename OutT>
      void RuntimeStepCancel<MatchAdapter<InT, OutT> >::settle(MatchAdapter<InT, OutT> &adapter)
      {
        adapter.settleCancel();
      }
    } // namespace flow_detail

    /** Declares ordered value or child-Flow arms for a Match step. A child
        with the same In type receives the matched value; another ChildIn
        type must provide its first step's input explicitly. Every child Out
        must exactly match this Match's Out. */
    template <typename InT, typename OutT> class MatchSpec
    {
    public:
      typedef MatchAdapter<InT, OutT> Adapter;
      typedef typename Adapter::MatcherFn MatcherFn;
      typedef typename Adapter::ValueHandlerFn ValueHandlerFn;

      explicit MatchSpec(int id)
          : id_(id),
            adapter_()
      {
      }

      MatchSpec &arm(MatcherFn matcher, void *user, ValueHandlerFn handler)
      {
        this->adapter_.addValueArm(matcher, user, handler);
        return *this;
      }

      template <typename ChildInT, typename ChildOutT>
      MatchSpec &arm(MatcherFn matcher, void *user, const FlowChain<ChildInT, ChildOutT> &child)
      {
        this->adapter_.addChildArm(matcher, user, child);
        return *this;
      }

      MatchSpec &otherwise(ValueHandlerFn handler, void *user = 0)
      {
        this->adapter_.setOtherwise(handler, user);
        return *this;
      }

      int id() const
      {
        return this->id_;
      }

      const Adapter &adapter() const
      {
        return this->adapter_;
      }

    private:
      int id_;
      Adapter adapter_;
    };

    template <typename InT, typename OutT> MatchSpec<InT, OutT> Match(int id)
    {
      return MatchSpec<InT, OutT>(id);
    }

    template <typename FlowInT, typename CurrentOutT, typename MatchOutT>
    FlowChain<FlowInT, MatchOutT> operator|(const FlowChain<FlowInT, CurrentOutT> &chain,
                                            const MatchSpec<CurrentOutT, MatchOutT> &match)
    {
      return chain | Step(match.id(), match.adapter());
    }

    template <typename MatchInT, typename MatchOutT>
    FlowChain<MatchInT, MatchOutT> operator|(const Flow &flow, const MatchSpec<MatchInT, MatchOutT> &match)
    {
      return flow | Step(match.id(), match.adapter());
    }
  } // namespace dsl
} // namespace loka

#endif // LOKA_DSL_FLOW_HPP
