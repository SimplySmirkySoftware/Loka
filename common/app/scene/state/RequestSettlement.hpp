#ifndef LOKA_APP_SCENE_STATE_REQUEST_SETTLEMENT_HPP
#define LOKA_APP_SCENE_STATE_REQUEST_SETTLEMENT_HPP
#include "app/scene/Node.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "app/scene/state/Request.hpp"
#ifdef TEST_BUILD
#include <climits>
#endif
namespace loka
{
  namespace app
  {
    namespace scene
    {
#ifdef TEST_BUILD
      template <class Fact> class RequestSettlement;
#endif
      /** The stimulus whose outer operation is completing. */
      enum Settlement
      {
        SETTLE_ATTACH,
        SETTLE_PROPS,
        SETTLE_INPUT,
        SETTLE_RESTORE,
        SETTLE_DEFERRED
      };
      /** Rail decisions; phase changes happen synchronously, arms happen at completion. */
      enum FollowUp
      {
        FOLLOW_NONE,
        RESTORE_QUEUED,
        SCHEDULE_RESTORE,
        SCHEDULE_HIGHLIGHTS,
        OWNER_FOLLOWS,
        SCROLL_CLEANUP,
        REPAINT
      };
      /** Result of arming the settle's follow-up, never retained by the rail. */
      enum FollowUpResult
      {
        FOLLOW_UP_ARMED,
        FOLLOW_UP_FAILED,
        FOLLOW_UP_NONE
      };
      /** Immutable set of returned decisions. Rails resolve scheduling supersession from
          their live phase in finishSettle; independent repaint intent is never lost. */
      class FollowUps
      {
      public:
        FollowUps()
            : bits_(0)
        {
        }
        FollowUps including(FollowUp next) const
        {
          return FollowUps(this->bits_ | (next == FOLLOW_NONE ? 0u : 1u << next));
        }
        bool contains(FollowUp action) const
        {
          return (this->bits_ & (1u << action)) != 0;
        }

      private:
        explicit FollowUps(unsigned bits)
            : bits_(bits)
        {
        }
        unsigned bits_;
      };
      enum Admission
      {
        ADMISSION_EMPTY,
        ADMISSION_DEFERRED,
        ADMISSION_TAKE
      };
      /** A native application result, independent of the seam's acceptance. */
      template <typename T, typename Reason> class RequestApplication
      {
      public:
        RequestApplication(const T &value, Reason result, FollowUp follow = FOLLOW_NONE)
            : value_(value),
              result_(result),
              follow_(follow)
        {
        }
        const T &value() const
        {
          return this->value_;
        }
        Reason result() const
        {
          return this->result_;
        }
        FollowUp followUp() const
        {
          return this->follow_;
        }

      private:
        T value_;
        Reason result_;
        FollowUp follow_;
      };
      /** Stack seat policy. The driver supplies a live Node; each seat borrows
          its own request binding. validate/report return the seam's result. */
      template <class Request, class Fact> class SeatOperation
      {
      public:
        virtual ~SeatOperation() {}
        /** Supply the binding even when deferring, for failed-arm refusal. */
        virtual Admission admit(Node &, RequestBinding<Request> &) = 0;
        virtual typename RequestTraits<Request>::Reason resolve(Node &, const RequestBinding<Request> &) = 0;
        virtual typename RequestTraits<Request>::Reason validate(Node &, const Request &) = 0;
        virtual RequestApplication<Fact, typename RequestTraits<Request>::Reason> apply(Node &, const Request &) = 0;
        virtual typename RequestTraits<Request>::Reason report(Node &, const Fact &) = 0;
        virtual bool current(Node &, const RequestBinding<Request> &) = 0;
        /** Preserve native application and seam outcomes separately for repair. */
        virtual FollowUp finishTake(Node &,
                                    const Reply<Request> &,
                                    const RequestApplication<Fact, typename RequestTraits<Request>::Reason> &) = 0;
      };
      /** Borrows the projection for the whole native entry, independently of Fact. */
      class SettleOwnerBase
      {
      public:
        Node *node() const { return this->node_; }
        /** Identity only; a parked context may still match. */
        bool hasSameContext() const
        {
          return this->node_ && this->node_->getContext() == this->identity_;
        }
      protected:
        SettleOwnerBase(IPlatformController &controller, Node *node, const NodeContext *identity)
            : scope_(controller), node_(node), identity_(identity) {}
#ifdef TEST_BUILD
        SettleOwnerBase(BorrowPhase &phase, Node *node, const NodeContext *identity)
            : scope_(phase), node_(node), identity_(identity) {}
#endif
      private:
        SettleOwnerBase(const SettleOwnerBase &);
        SettleOwnerBase &operator=(const SettleOwnerBase &);
        BorrowScope scope_;
        Node *const node_;
        const NodeContext *const identity_;
      };
      /** One completion owner for all seats in an entry operation. */
      template <class Fact> class SettleOwner : public SettleOwnerBase
      {
      public:
        SettleOwner(IPlatformController &controller, Node *node, const NodeContext *identity)
            : SettleOwnerBase(controller, node, identity) {}
#ifdef TEST_BUILD
        SettleOwner(BorrowPhase &phase, Node *node, const NodeContext *identity)
            : SettleOwnerBase(phase, node, identity) {}
#endif
        virtual ~SettleOwner() {}
        virtual FollowUpResult finishSettle(Node &, const FollowUps &) = 0;
#ifdef TEST_BUILD
        virtual Fact fact(Node &) const = 0;
#endif
      };
      /** Combined single-seat policy retained by the existing rails. */
      template <typename T> class RailOperation : public SeatOperation<T, T>, public SettleOwner<T>
      {
      public:
        RailOperation(IPlatformController &controller, Node *node, const NodeContext *identity)
            : SettleOwner<T>(controller, node, identity) {}
#ifdef TEST_BUILD
        RailOperation(BorrowPhase &phase, Node *node, const NodeContext *identity)
            : SettleOwner<T>(phase, node, identity) {}
#endif
        virtual ~RailOperation() {}
      };

      /** Different effect types acknowledge the request without a clamped payload. */
      template <class Request, class Fact>
      Reply<Request> formReply(const Request &pending, const Fact &, typename RequestTraits<Request>::Reason result)
      {
        return result != RequestTraits<Request>::accepted() ? Reply<Request>::Refused(pending, result)
                                                            : Reply<Request>::Granted(pending);
      }
      /** Same-type effects retain the existing Granted/Clamped distinction. */
      template <class T>
      Reply<T> formReply(const T &pending, const T &applied, typename RequestTraits<T>::Reason result)
      {
        return result != RequestTraits<T>::accepted() ? Reply<T>::Refused(pending, result)
               : pending != applied                   ? Reply<T>::Clamped(pending, applied)
                                                      : Reply<T>::Granted(applied);
      }
    } // namespace scene
#ifdef TEST_BUILD
    namespace testing
    {
      template <class Fact> class SettleTraceCapture;
      /** Emitted-row ordering across every trace of one fact type.
          Only the walk advances it; a capture reset starts a new history.
          Saturation marks the capture incomplete instead of wrapping. */
      template <class Fact> class SettleTraceClock
      {
        friend class scene::RequestSettlement<Fact>;
        friend class SettleTraceCapture<Fact>;
        static unsigned current() { return sequence_; }
        static void advance()
        {
          if (sequence_ != UINT_MAX)
            ++sequence_;
        }
        static void clear() { sequence_ = 0; }
        static bool overwritten() { return sequence_ == UINT_MAX; }
        static unsigned sequence_;
      };
      template <class Fact> unsigned SettleTraceClock<Fact>::sequence_ = 0;

      /** One reset/overflow boundary for the static histories of a fact type. */
      template <class Fact> class SettleTraceCapture
      {
      public:
        // Public because SettleTrace names it as a base: GCC 4.0 refused the
        // private class there even to a befriended SettleTrace (Tiger UB1).
        // The protected constructor still limits it to derived histories.
        /** Intrusive registration follows the static trace's lifetime. */
        class Trace
        {
          friend class SettleTraceCapture<Fact>;
        public:
          virtual void clear() = 0;
          virtual unsigned overwritten() const = 0;
        protected:
          Trace() : next_(traces()) { traces() = this; }
          virtual ~Trace()
          {
            Trace **link = &traces();
            while (*link != this)
              link = &(*link)->next_;
            *link = this->next_;
          }
        private:
          Trace(const Trace &);
          Trace &operator=(const Trace &);
          Trace *next_;
        };

      private:
        static Trace *&traces()
        {
          static Trace *head = 0;
          return head;
        }
      public:
        static void clear()
        {
          for (Trace *trace = traces(); trace; trace = trace->next_)
            trace->clear();
          SettleTraceClock<Fact>::clear();
        }
        static bool overwritten()
        {
          bool lost = SettleTraceClock<Fact>::overwritten();
          for (Trace *trace = traces(); trace; trace = trace->next_)
            lost = trace->overwritten() != 0 || lost;
          return lost;
        }
      };
      /** Bounded diagnostic history. Overflow overwrites the oldest row explicitly. */
      template <class Request, class Fact = Request> struct SettleTraceRow
      {
        scene::Settlement stimulus;
        scene::Admission admission[2];
        // Two ordinary takes and at most one follow-up-failure refusal.
        scene::Reply<Request> takes[3];
        typename scene::RequestTraits<Request>::Reason seam[3];
        unsigned count;
        unsigned seq;
        Fact before, after;
        SettleTraceRow(scene::Settlement kind, const Fact &fact)
            : stimulus(kind),
              count(0),
              seq(0),
              before(fact),
              after(fact)
        {
          admission[0] = admission[1] = scene::ADMISSION_EMPTY;
          seam[0] = seam[1] = seam[2] = scene::RequestTraits<Request>::accepted();
        }
      };
      template <class Request, class Fact = Request> class SettleTrace : private SettleTraceCapture<Fact>::Trace
      {
      public:
        enum
        {
          CAPACITY = 64
        };
        static SettleTrace &instance()
        {
          static SettleTrace trace;
          return trace;
        }
        /** Clear only this history; the capture owns resetting the shared clock. */
        virtual void clear()
        {
          this->begin_ = this->size_ = this->overwritten_ = 0;
        }
        unsigned size() const
        {
          return this->size_;
        }
        virtual unsigned overwritten() const
        {
          return this->overwritten_;
        }
        const SettleTraceRow<Request, Fact> &at(unsigned index) const
        {
          assert(index < this->size_);
          return this->rows_[(this->begin_ + index) % CAPACITY];
        }
        /** Return whether a row was emitted, so only emitted rows advance the clock. */
        bool append(const SettleTraceRow<Request, Fact> &row)
        {
          if (!row.count && !(row.before != row.after))
            return false;
          if (this->size_ == CAPACITY)
          {
            this->begin_ = (this->begin_ + 1) % CAPACITY;
            --this->size_;
            ++this->overwritten_;
          }
          static_cast<SettleTraceRow<Request, Fact> &>(this->rows_[(this->begin_ + this->size_++) % CAPACITY]) = row;
          return true;
        }

      private:
        // Rows are values; no trace retains an owner, node, context or state handle.
        struct Row : SettleTraceRow<Request, Fact>
        {
          Row()
              : SettleTraceRow<Request, Fact>(scene::SETTLE_ATTACH, Fact())
          {
          }
        };
        SettleTrace()
            : begin_(0),
              size_(0),
              overwritten_(0)
        {
        }
        Row rows_[CAPACITY];
        unsigned begin_, size_, overwritten_;
      };
    } // namespace testing
#endif
    namespace scene
    {
      /** Synchronous driver-stack seat. No runner escapes the settle call. */
      template <class Fact> class SeatRunnerBase
      {
      public:
        virtual ~SeatRunnerBase() {}
        virtual bool take(unsigned ordinal) = 0;
        virtual bool refuseFailed() = 0;
#ifdef TEST_BUILD
        /** Close this row and return its entry fact for the preceding row. */
        virtual Fact finalize(const Fact &after) = 0;
        virtual bool append(unsigned seq) = 0;
#endif
      };
      /** Owns the binding snapshot and diagnostic row for one borrowed seat. */
      template <class Request, class Fact> class SeatRunner : public SeatRunnerBase<Fact>
      {
      public:
        SeatRunner(SettleOwner<Fact> &owner,
                   SeatOperation<Request, Fact> &op,
                   FollowUps &follow,
                   Settlement stimulus
#ifdef TEST_BUILD
                   ,
                   const Fact &before,
                   SettleOwner<Fact> *entryFact = 0
#endif
                   )
            : owner_(owner),
              op_(op),
              follow_(follow),
              binding_()
#ifdef TEST_BUILD
              ,
              row_(stimulus, before),
              entryFact_(entryFact)
#endif
        {
#ifndef TEST_BUILD
          (void)stimulus;
#endif
        }
        virtual bool take(unsigned ordinal)
        {
#ifdef TEST_BUILD
          // Seat zero keeps the caller's pre-entry snapshot. Later seats sample
          // only when entered, after the preceding seat's publications.
          if (ordinal == 0 && this->entryFact_)
            this->row_.before = this->entryFact_->fact(*this->owner_.node());
#else
          (void)ordinal;
#endif
          this->binding_ = RequestBinding<Request>();
          const Admission admission = this->op_.admit(*this->owner_.node(), this->binding_);
#ifdef TEST_BUILD
          this->row_.admission[ordinal] = admission;
#endif
          if (admission != ADMISSION_TAKE)
            return true;
          const Request pending = this->binding_.consume();
          if (!this->owner_.hasSameContext())
            return false;
          typename RequestTraits<Request>::Reason result = this->op_.resolve(*this->owner_.node(), this->binding_);
          if (result == RequestTraits<Request>::accepted())
            result = this->op_.validate(*this->owner_.node(), pending);
          RequestApplication<Fact, typename RequestTraits<Request>::Reason> applied(Fact(), result);
          if (result == RequestTraits<Request>::accepted())
            applied = this->op_.apply(*this->owner_.node(), pending);
          if (!this->owner_.hasSameContext())
            return false;
          this->follow_ = this->follow_.including(applied.followUp());
          result = applied.result();
          if (result == RequestTraits<Request>::accepted())
            result = this->op_.report(*this->owner_.node(), applied.value());
          if (!this->owner_.hasSameContext())
            return false;
          const Reply<Request> reply = formReply(pending, applied.value(), result);
          // A binding discard is not a reply to a different recipient.
          if (this->op_.current(*this->owner_.node(), this->binding_))
            this->binding_.reply_.set(reply, true);
          if (!this->owner_.hasSameContext())
            return false;
#ifdef TEST_BUILD
          this->row_.takes[this->row_.count] = reply;
          this->row_.seam[this->row_.count++] = result;
#endif
          const FollowUp completed = this->op_.finishTake(*this->owner_.node(), reply, applied);
          if (!this->owner_.hasSameContext())
            return false;
          this->follow_ = this->follow_.including(completed);
          return true;
        }
        /** Refuse the last admitted binding without reopening native admission. */
        virtual bool refuseFailed()
        {
          if (!this->binding_.isValid() || !this->op_.current(*this->owner_.node(), this->binding_)
              || this->binding_.state()->get().isNone())
            return true;
          const Request pending = this->binding_.consume();
          if (!this->owner_.hasSameContext())
            return false;
          const Reply<Request> reply = Reply<Request>::Refused(pending, RequestTraits<Request>::unavailable());
          // A clear subscriber can discard the binding without retiring the context.
          if (this->op_.current(*this->owner_.node(), this->binding_))
            this->binding_.reply_.set(reply, true);
          if (!this->owner_.hasSameContext())
            return false;
#ifdef TEST_BUILD
          this->row_.takes[this->row_.count] = reply;
          this->row_.seam[this->row_.count++] = RequestTraits<Request>::unavailable();
#endif
          return true;
        }
#ifdef TEST_BUILD
        virtual Fact finalize(const Fact &after)
        {
          if (!this->contributesTrace())
            return after;
          this->row_.after = after;
          return this->row_.before;
        }
        virtual bool append(unsigned seq)
        {
          if (!this->contributesTrace())
            return false;
          this->row_.seq = seq;
          return loka::app::testing::SettleTrace<Request, Fact>::instance().append(this->row_);
        }
#endif
      private:
        SettleOwner<Fact> &owner_;
        SeatOperation<Request, Fact> &op_;
        FollowUps &follow_;
        RequestBinding<Request> binding_;
#ifdef TEST_BUILD
        bool contributesTrace() const
        {
          return !this->entryFact_ || this->row_.count != 0;
        }
        loka::app::testing::SettleTraceRow<Request, Fact> row_;
        SettleOwner<Fact> *const entryFact_;
#endif
      };
      /** One completion owner, two ordinary takes per seat, and at most one
          refusal-only take per seat on arm failure. Runners and follow-ups are
          local to the driver; retirement suppresses completion and trace. */
      template <class Fact> class RequestSettlement
      {
      public:
        static FollowUpResult settle(RailOperation<Fact> &op,
                                     Settlement stimulus
#ifdef TEST_BUILD
                                     ,
                                     const Fact &before
#endif
        )
        {
          FollowUps follow;
          SeatRunner<Fact, Fact> runner(op,
                                        op,
                                        follow,
                                        stimulus
#ifdef TEST_BUILD
                                        ,
                                        before
#endif
          );
          SeatRunnerBase<Fact> *seats[1] = {&runner};
          return walk(op, follow, seats, 1);
        }
        /** Complete one request seat whose effect may have a different type. */
        template <class R0>
        static FollowUpResult settle(SettleOwner<Fact> &owner,
                                     SeatOperation<R0, Fact> &seat0,
                                     Settlement stimulus
#ifdef TEST_BUILD
                                     ,
                                     const Fact &before
#endif
        )
        {
          FollowUps follow;
          SeatRunner<R0, Fact> runner(owner,
                                      seat0,
                                      follow,
                                      stimulus
#ifdef TEST_BUILD
                                      ,
                                      before
#endif
          );
          SeatRunnerBase<Fact> *seats[1] = {&runner};
          return walk(owner, follow, seats, 1);
        }
        template <class R0, class R1>
        static FollowUpResult settle(SettleOwner<Fact> &owner,
                                     SeatOperation<R0, Fact> &seat0,
                                     SeatOperation<R1, Fact> &seat1,
                                     Settlement stimulus
#ifdef TEST_BUILD
                                     ,
                                     const Fact &before
#endif
        )
        {
          FollowUps follow;
          SeatRunner<R0, Fact> runner0(owner,
                                       seat0,
                                       follow,
                                       stimulus
#ifdef TEST_BUILD
                                       ,
                                       before
#endif
          );
          SeatRunner<R1, Fact> runner1(owner,
                                       seat1,
                                       follow,
                                       stimulus
#ifdef TEST_BUILD
                                       ,
                                       before,
                                       &owner
#endif
          );
          SeatRunnerBase<Fact> *seats[2] = {&runner0, &runner1};
          return walk(owner, follow, seats, 2);
        }

      private:
        static FollowUpResult walk(SettleOwner<Fact> &owner,
                                   FollowUps &follow,
                                   SeatRunnerBase<Fact> **seats,
                                   unsigned count)
        {
          for (unsigned i = 0; i < count; ++i)
          {
            if (!owner.hasSameContext())
              return FOLLOW_UP_NONE;
            if (!seats[i]->take(0) || !seats[i]->take(1))
              return FOLLOW_UP_NONE;
          }
          const FollowUpResult armed = owner.finishSettle(*owner.node(), follow);
          if (!owner.hasSameContext())
            return FOLLOW_UP_NONE;
          switch (armed)
          {
          case FOLLOW_UP_FAILED:
            for (unsigned i = 0; i < count; ++i)
              if (!seats[i]->refuseFailed())
                return FOLLOW_UP_NONE;
            break;
          case FOLLOW_UP_ARMED:
          case FOLLOW_UP_NONE:
            break;
          }
#ifdef TEST_BUILD
          Fact after = owner.fact(*owner.node());
          // Close each preceding row at the next seat's captured entry fact.
          // Finalization and publication both wait for the epilogue and tail.
          for (unsigned i = count; i != 0; --i)
            after = seats[i - 1]->finalize(after);
          for (unsigned i = 0; i < count; ++i)
            if (seats[i]->append(loka::app::testing::SettleTraceClock<Fact>::current()))
              loka::app::testing::SettleTraceClock<Fact>::advance();
#endif
          return armed;
        }
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
