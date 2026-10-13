#ifndef LOKA_DIALOG_RESULT_TRANSPORT_HPP
#define LOKA_DIALOG_RESULT_TRANSPORT_HPP

#include "app/OpenFileDialog.hpp"
#include "app/ConfirmDialog.hpp"
#include "app/core/DialogResultDelivery.hpp"
#include "core/LokaAlloc.hpp"
#include <new>

class Window;
namespace loka
{
  namespace app
  {
    namespace testing { class DialogResultTestAccess; }

    /** One Window-owned ledger. Entry types carry payloads; chains carry lifecycle.
        Context registrations own permission; native wakes own no storage. */
    class DialogResultTransport : public DialogResultDelivery
    {
    private:
      struct DialogRequest;
      template <class PropsT, class ResultT> struct TypedEntry;
      struct Chain
      {
        Chain() : head(0) {}
        DialogRequest *head;
      private:
        Chain(const Chain &);
        Chain &operator=(const Chain &);
      };

    public:
      template <class PropsT, class ResultT> class TypedReturnPort;
      /** Context-owned permission for exactly one payload type. */
      template <class PropsT, class ResultT> class TypedRegistration
      {
      public:
        ~TypedRegistration()
        {
          if (this->entry_)
            this->entry_->transport.cancel(*this->entry_);
        }
        bool matches(const PropsT &props) const
        {
          return this->binding_.result_.state() == props.result_.state()
              && this->binding_.onResult_ == props.onResult_
              && this->binding_.windowToAttach_ == props.windowToAttach_
              && this->binding_.options_.compare(props.options_) == 0;
        }
      private:
        explicit TypedRegistration(const PropsT &props) : binding_(props), entry_(0) {}
        PropsT binding_;
        TypedEntry<PropsT, ResultT> *entry_;
        friend class DialogResultTransport;
        friend class TypedReturnPort<PropsT, ResultT>;
        friend struct TypedEntry<PropsT, ResultT>;
        TypedRegistration(const TypedRegistration &);
        TypedRegistration &operator=(const TypedRegistration &);
      };

      /** Stack-owned, revocable return route. A mismatched payload cannot seal it. */
      template <class PropsT, class ResultT> class TypedReturnPort
      {
      public:
        explicit TypedReturnPort(TypedRegistration<PropsT, ResultT> *registration) : entry_(0)
        {
          if (registration && registration->entry_
              && registration->entry_->chain == &registration->entry_->transport.reserved_
              && !registration->entry_->returnPort)
          {
            this->entry_ = registration->entry_;
            this->entry_->returnPort = this;
          }
        }
        ~TypedReturnPort()
        {
          if (this->entry_)
            this->entry_->transport.cancel(*this->entry_);
        }
        Window *seal(const ResultT &result)
        {
          TypedEntry<PropsT, ResultT> *entry = this->entry_;
          if (!entry || !acceptsResult(result))
            return 0;
          this->entry_ = 0;
          entry->returnPort = 0;
          entry->result = result;
          entry->moveTo(entry->transport.pending_);
          return entry->transport.window_;
        }
      private:
        TypedEntry<PropsT, ResultT> *entry_;
        friend struct TypedEntry<PropsT, ResultT>;
        TypedReturnPort(const TypedReturnPort &);
        TypedReturnPort &operator=(const TypedReturnPort &);
      };

      typedef TypedRegistration<OpenFileDialogProps, FileChooserResult> Registration;
      typedef TypedReturnPort<OpenFileDialogProps, FileChooserResult> ReturnPort;
      typedef TypedRegistration<ConfirmDialogProps, ConfirmResult> ConfirmRegistration;
      typedef TypedReturnPort<ConfirmDialogProps, ConfirmResult> ConfirmReturnPort;

      DialogResultTransport();
      virtual ~DialogResultTransport();
      void open(Window &window);
      void close();
      /** Refuses closed enrollment or a channel without its owner tracker.
          Allocation refusal requests projection at the next App admission.
          The declaring scope must enclose the context and every invocation. */
      Registration *reserve(const OpenFileDialogProps &props);
      ConfirmRegistration *reserve(const ConfirmDialogProps &props);
      virtual bool hasRunnableWork() const;

    private:
      /** Neutral intrusive lifecycle. No payload identity or duplicate phase flags. */
      struct DialogRequest : Retirement
      {
        explicit DialogRequest(DialogResultTransport &owner);
        virtual ~DialogRequest() {}
        virtual void revoke() = 0;
        virtual void deliver() = 0;
        DialogResultTransport &transport;
        Chain *chain;
        DialogRequest *next;
        DialogRequest **previous;
        void unlink();
        void moveTo(Chain &destination);
      };
      template <class PropsT, class ResultT> struct TypedEntry : DialogRequest
      {
        explicit TypedEntry(DialogResultTransport &owner)
            : DialogRequest(owner), registration(0), returnPort(0), result() {}
        virtual void revoke()
        {
          if (this->registration)
          {
            this->registration->entry_ = 0;
            this->registration->binding_ = PropsT();
            this->registration = 0;
          }
          if (this->returnPort)
          {
            this->returnPort->entry_ = 0;
            this->returnPort = 0;
          }
        }
        virtual void deliver()
        {
          if (!this->registration)
            return;
          scene::NodeState<ResultT> channel = this->registration->binding_.result_;
          core::EmitterState *emitter = this->registration->binding_.onResult_;
          void *token = emitter ? emitter->retainExternalLifetimeToken() : 0;
          if (channel.isValid())
            channel.set(this->result, true);
          // set() may delete the registration, close the Window or destroy the emitter.
          // Never read the captured registration again after that callback.
          if (this->registration && emitter && core::StateBase::isExternalLifetimeTokenAlive(token))
            emitter->emit();
          if (token)
            core::StateBase::releaseExternalLifetimeToken(token);
        }
        TypedRegistration<PropsT, ResultT> *registration;
        TypedReturnPort<PropsT, ResultT> *returnPort;
        ResultT result;
      };
      /** Bounded invocation; active retirement precedes return. */
      class Invocation
      {
      public:
        explicit Invocation(DialogRequest &entry) : entry_(entry) {}
        ~Invocation();
        void deliver() { this->entry_.deliver(); }
      private:
        DialogRequest &entry_;
        Invocation(const Invocation &);
        Invocation &operator=(const Invocation &);
      };
      template <class PropsT, class ResultT>
      TypedRegistration<PropsT, ResultT> *reserveTyped(const PropsT &props)
      {
        // Owner-route validation, not transaction-local StateBase tracker membership.
        if (!this->window_ || (props.result_.isValid() && !props.result_.dangerouslyTracker()))
          return 0;
        TypedRegistration<PropsT, ResultT> *registration =
            new (std::nothrow) TypedRegistration<PropsT, ResultT>(props);
        if (!registration)
        {
          this->retryEnrollment();
          return 0;
        }
        typedef TypedEntry<PropsT, ResultT> Entry;
        void *storage = core::LokaAllocRaw(sizeof(Entry), entrySite());
        if (!storage)
        {
          delete registration;
          this->retryEnrollment();
          return 0;
        }
        Entry *entry = new (storage) Entry(*this);
        registration->entry_ = entry;
        entry->registration = registration;
        entry->moveTo(this->reserved_);
        return registration;
      }
      static const core::LokaAllocationSite &entrySite();
      static bool acceptsResult(const FileChooserResult &) { return true; }
      static bool acceptsResult(const ConfirmResult &result) { return result != ConfirmResult::none(); }
      void cancel(DialogRequest &entry);
      void retryEnrollment();
      virtual void deliver();
      virtual Retirement *retirementSnapshot() const;
      virtual void reclaim(Retirement *snapshot);

      Window *window_;
      Chain reserved_;
      Chain pending_;
      Chain active_;
      Chain retired_;
      friend class testing::DialogResultTestAccess;
      DialogResultTransport(const DialogResultTransport &);
      DialogResultTransport &operator=(const DialogResultTransport &);
    };
  } // namespace app
} // namespace loka
#endif
