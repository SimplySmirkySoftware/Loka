#ifndef LOKA_APP_SCENE_STATE_REQUEST_HPP
#define LOKA_APP_SCENE_STATE_REQUEST_HPP
#include "app/scene/state/Reported.hpp"
#include "app/scene/state/RequestRing.hpp"
namespace loka
{
  namespace app
  {
    namespace scene
    {
      template <class Request, class Fact> class SeatRunner;
      template <typename T> class RequestBinding;
      template <typename T> struct RequestTraits;
      /** Plain slots are only meaningful for explicitly coalescable values. */
      template <typename T> struct RequestDeclarationWall
      {
#if __cplusplus >= 201103L
        static_assert(RequestTraits<T>::coalescable, "Use RequestQueue for non-coalescable requests");
#endif
        typedef char Wall[RequestTraits<T>::coalescable ? 1 : -1];
      };
      /** Admission to an endpoint; refused posts change neither slot nor ring. */
      enum PostResult
      {
        POST_ACCEPTED,
        POST_QUEUE_FULL,
        POST_INVALID
      };
      /** Result of one take, not a promise for every post. */
      template <typename T> class Reply
      {
      public:
        enum Kind
        {
          NO_REPLY,
          GRANTED,
          CLAMPED,
          REFUSED
        };
        Reply()
            : kind_(NO_REPLY),
              requested_(),
              applied_(),
              reason_(RequestTraits<T>::accepted())
        {
        }
        static Reply Granted(const T &applied)
        {
          return Reply(GRANTED, applied, applied, RequestTraits<T>::accepted());
        }
        static Reply Clamped(const T &requested, const T &applied)
        {
          return Reply(CLAMPED, requested, applied, RequestTraits<T>::accepted());
        }
        static Reply Refused(const T &requested, typename RequestTraits<T>::Reason reason)
        {
          return Reply(REFUSED, requested, T(), reason);
        }
        Kind kind() const
        {
          return this->kind_;
        }
        const T &requested() const
        {
          assert(this->kind_ != NO_REPLY);
          return this->requested_;
        }
        const T &applied() const
        {
          assert(this->kind_ == GRANTED || this->kind_ == CLAMPED);
          return this->applied_;
        }
        typename RequestTraits<T>::Reason reason() const
        {
          assert(this->kind_ == REFUSED);
          return this->reason_;
        }
        bool operator!=(const Reply &other) const
        {
          return this->kind_ != other.kind_ || this->requested_ != other.requested_ || this->applied_ != other.applied_
                 || this->reason_ != other.reason_;
        }

      private:
        Reply(Kind kind, const T &requested, const T &applied, typename RequestTraits<T>::Reason reason)
            : kind_(kind),
              requested_(requested),
              applied_(applied),
              reason_(reason)
        {
        }
        Kind kind_;
        T requested_;
        T applied_;
        typename RequestTraits<T>::Reason reason_;
      };
      template <typename T> class RequestWithReply;
      /** App-owned declaration handle. Borrow by reference; None means no request. */
      template <typename T> class Request
      {
      public:
        Request()
            : seat_()
        {
        }
        bool isValid() const
        {
          return this->seat_.isValid();
        }
        core::State<T> *state() const
        {
          return this->seat_.state();
        }
        T get() const
        {
          return this->seat_.get();
        }
        void set(const T &value)
        {
          this->seat_.set(value);
        }

      private:
        friend class ComposableNode;
        friend class StateBatchBase;
        template <class PropsT> friend struct NodePropsBase;
        Request(const Request &);
        Request &operator=(const Request &);
        NodeState<T> seat_;
      };
      /** Opt-in per-take replies. The reference conversion is a posting view;
          pass this typed handle to Props to include the reply capability.
          Declaration handles cannot be copied or sliced into a plain request. */
      template <typename T> class RequestWithReply
      {
      public:
        RequestWithReply()
            : request_(),
              reply_()
        {
        }
        operator Request<T> &()
        {
          return this->request_;
        }
        bool isValid() const
        {
          return this->request_.isValid();
        }
        core::State<T> *state() const
        {
          return this->request_.state();
        }
        T get() const
        {
          return this->request_.get();
        }
        void set(const T &value)
        {
          this->request_.set(value);
        }
        const Reported<Reply<T> > &reply() const
        {
          return this->reply_;
        }

      private:
        friend class ComposableNode;
        friend class StateBatchBase;
        template <class PropsT> friend struct NodePropsBase;
        RequestWithReply(const RequestWithReply &);
        RequestWithReply &operator=(const RequestWithReply &);
        Request<T> request_;
        Reported<Reply<T> > reply_;
      };
      /** App-owned FIFO endpoint. Borrow by reference without extending its lifetime.
          POST_ACCEPTED promises one reply per taken post. Binding change or detach
          may discard pending posts, without replies or an exact drop count.
          TextEditor admits caret before command; a nonempty caret slot at every
          command admission can delay commands indefinitely.
          Storage is supplied by RequestQueue; no queue operation allocates. */
      template <typename T> class RequestQueueBase
      {
      public:
        PostResult post(const T &value)
        {
          if (value.isNone())
            return POST_INVALID;
          if (this->seat_.get().isNone() && this->ring_.count() == 0)
          {
            this->seat_.set(value);
            return POST_ACCEPTED;
          }
          if (!this->ring_.pushBack(value))
            return POST_QUEUE_FULL;
          return POST_ACCEPTED;
        }
        /** Current ring occupancy, excluding the published slot. */
        unsigned pending() const
        {
          return this->ring_.count();
        }
        const Reported<Reply<T> > &reply() const { return this->reply_; }
        bool isValid() const { return this->seat_.isValid(); }
        core::State<T> *state() const { return this->seat_.state(); }

      protected:
        RequestQueueBase(T *ring, unsigned char capacity)
            : seat_(),
              reply_(),
              ring_(ring, capacity)
        {
        }

      private:
        friend class RequestBinding<T>;
        friend class ComposableNode;
        friend class StateBatchBase;
        template <class PropsT> friend struct NodePropsBase;
        RequestQueueBase(const RequestQueueBase &);
        RequestQueueBase &operator=(const RequestQueueBase &);
        bool advance(T &next)
        {
          return this->ring_.advance(next);
        }
        void clearRing()
        {
          this->ring_.clear();
        }
        NodeState<T> seat_;
        Reported<Reply<T> > reply_;
        RequestRing<T> ring_;
      };
      /** Fixed ring of 1..255 waiting values, plus the endpoint's published slot. */
      template <typename T, unsigned N> class RequestQueue : public RequestQueueBase<T>
      {
        typedef typename RequestRingCapacity<N>::Wall CapacityWall;

      public:
        RequestQueue() : RequestQueueBase<T>(this->storage_, N)
        {
          (void)sizeof(CapacityWall);
        }
      private:
        T storage_[N];
      };
      /** Internal borrow extracted at the Props door; consume/discard are friend-only doors. */
      template <typename T> class RequestBinding
      {
      public:
        RequestBinding()
            : request_(),
              reply_(),
              source_(0)
        {
        }
        explicit RequestBinding(const WriteSeat<T> &request,
                                const WriteSeat<Reply<T> > &reply = WriteSeat<Reply<T> >())
            : request_(request),
              reply_(reply),
              source_(0)
        {
          (void)sizeof(typename RequestDeclarationWall<T>::Wall);
        }
        /** Props supplies both seats from the same borrowed queue. */
        explicit RequestBinding(const WriteSeat<T> &request,
                                const WriteSeat<Reply<T> > &reply,
                                RequestQueueBase<T> &source)
            : request_(request),
              reply_(reply),
              source_(&source)
        {
        }
        bool isValid() const
        {
          return this->request_.isValid();
        }
        core::State<T> *state() const
        {
          return this->request_.state();
        }
        bool usesTracker(const core::StateTracker *tracker) const
        {
          return this->request_.usesTracker(tracker) && (!this->reply_.isValid() || this->reply_.usesTracker(tracker));
        }
        bool same(const RequestBinding &other) const
        {
          return this->state() == other.state() && this->reply_.state() == other.reply_.state()
                 && this->source_ == other.source_;
        }
        bool operator<(const RequestBinding &other) const
        {
          if (this->state() != other.state())
            return this->state() < other.state();
          if (this->reply_.state() != other.reply_.state())
            return this->reply_.state() < other.reply_.state();
          return this->source_ < other.source_;
        }

      private:
        template <class Request, class Fact> friend class SeatRunner;
        friend struct PendingRequests;
        T consume() const
        {
          const T taken = this->request_.state()->get();
          T next;
          if (this->source_ && this->source_->advance(next))
            this->request_.set(next, true);
          else
            this->request_.set(T::None());
          return taken;
        }
        /** First cancellation pass: ring storage only, with no publication. */
        void clearRing() const
        {
          if (this->source_)
            this->source_->clearRing();
        }
        /** Second pass: preserve a changed slot, otherwise promote new queued
            work or publish None. A plain cross-post equal to snapshot is cleared. */
        void cancelFrom(const T &snapshot) const
        {
          if (!this->request_.isValid() || this->request_.state()->get() != snapshot)
            return;
          (void)this->consume();
        }
        WriteSeat<T> request_;
        WriteSeat<Reply<T> > reply_;
        RequestQueueBase<T> *source_;
      };
      /** Cancels pending work only in the seats selected by the caller.
          Copy all bindings, clear their rings, then snapshot all valid slots
          (None for invalid bindings) before cancelling in argument order.
          Stack copies keep callback-driven binding replacement from retargeting
          cancellation; changed slots preserve subscriber reposts. */
      struct PendingRequests
      {
        template <class A> static void cancel(const RequestBinding<A> &a)
        {
          const RequestBinding<A> first = a;
          first.clearRing();
          const A snapshot = first.isValid() ? first.state()->get() : A::None();
          first.cancelFrom(snapshot);
        }
        template <class A, class B> static void cancel(const RequestBinding<A> &a, const RequestBinding<B> &b)
        {
          const RequestBinding<A> first = a;
          const RequestBinding<B> second = b;
          first.clearRing();
          second.clearRing();
          const A snapshotA = first.isValid() ? first.state()->get() : A::None();
          const B snapshotB = second.isValid() ? second.state()->get() : B::None();
          first.cancelFrom(snapshotA);
          second.cancelFrom(snapshotB);
        }
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
