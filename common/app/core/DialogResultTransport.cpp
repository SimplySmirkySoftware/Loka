#include "app/core/DialogResultTransport.hpp"
#include <new>
#include <cassert>
#include "app/core/Window.hpp"
#include "core/LokaAlloc.hpp"

namespace loka
{
  namespace app
  {
    const core::LokaAllocationSite &DialogResultTransport::entrySite()
    {
      static const core::LokaAllocationSite site("DialogResultTransport", "Entry");
      return site;
    }

    DialogResultTransport::DialogRequest::DialogRequest(DialogResultTransport &owner)
        : transport(owner),
          chain(0),
          next(0),
          previous(0)
    {
    }

    void DialogResultTransport::DialogRequest::unlink()
    {
      if (!this->previous)
        return;
      *this->previous = this->next;
      if (this->next)
        this->next->previous = this->previous;
      this->chain = 0;
      this->next = 0;
      this->previous = 0;
    }

    void DialogResultTransport::DialogRequest::moveTo(Chain &destination)
    {
      this->unlink();
      this->chain = &destination;
      this->next = destination.head;
      this->previous = &destination.head;
      if (this->next)
        this->next->previous = &this->next;
      destination.head = this;
    }

    DialogResultTransport::DialogResultTransport()
        : window_(0)
    {
    }
    DialogResultTransport::~DialogResultTransport()
    {
      this->close();
      assert(!this->active_.head && "Window destruction must follow App admission return");
      this->reclaim(this->retired_.head);
    }

    void DialogResultTransport::open(Window &window)
    {
      assert(!this->window_ || this->window_ == &window);
      this->window_ = &window;
    }

    DialogResultTransport::Registration *DialogResultTransport::reserve(const OpenFileDialogProps &props)
    {
      return this->reserveTyped<OpenFileDialogProps, FileChooserResult>(props);
    }

    DialogResultTransport::ConfirmRegistration *DialogResultTransport::reserve(const ConfirmDialogProps &props)
    {
      if (!props.result_.isValid())
        return 0;
      return this->reserveTyped<ConfirmDialogProps, ConfirmResult>(props);
    }

    void DialogResultTransport::retryEnrollment()
    {
      if (this->window_->scene())
      {
        // Native enrollment runs in platform apply, after the refresh drain.
        // The white flag prevents CHILD relaxation from dropping the retry;
        // invalidation admits the Window again without flushing this callback.
        this->window_->scene()->noteComposeAllocationFailure();
        this->window_->scene()->requestInvalidate(scene::NODE_DIRTY_CHILD);
      }
    }

    void DialogResultTransport::cancel(DialogRequest &entry)
    {
      entry.revoke();
      if (entry.chain != &this->active_)
        entry.moveTo(this->retired_);
    }

    void DialogResultTransport::close()
    {
      this->window_ = 0;
      while (this->reserved_.head)
        this->cancel(*this->reserved_.head);
      while (this->pending_.head)
        this->cancel(*this->pending_.head);
      for (DialogRequest *entry = this->active_.head; entry; entry = entry->next)
        entry->revoke();
    }

    bool DialogResultTransport::hasRunnableWork() const
    {
      return this->pending_.head || this->retired_.head;
    }

    void DialogResultTransport::deliver()
    {
      assert(!this->active_.head);
      // Capture a finite batch. Reversing the pending stack preserves seal order.
      while (this->pending_.head)
        this->pending_.head->moveTo(this->active_);
      while (this->active_.head)
      {
        Invocation invocation(*this->active_.head);
        invocation.deliver();
      }
    }

    DialogResultTransport::Invocation::~Invocation()
    {
      this->entry_.revoke();
      this->entry_.moveTo(this->entry_.transport.retired_);
    }

    DialogResultDelivery::Retirement *DialogResultTransport::retirementSnapshot() const
    {
      return this->retired_.head;
    }

    void DialogResultTransport::reclaim(Retirement *retired)
    {
      DialogRequest *snapshot = static_cast<DialogRequest *>(retired);
      while (snapshot)
      {
        DialogRequest *next = snapshot->next;
        snapshot->unlink();
        snapshot->~DialogRequest();
        loka::core::LokaFreeRaw(snapshot, entrySite());
        snapshot = next;
      }
    }
  } // namespace app
} // namespace loka
