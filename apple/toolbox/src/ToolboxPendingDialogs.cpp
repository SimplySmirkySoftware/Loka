#include "ToolboxPendingDialogs.hpp"
#include "ToolboxDialogPresenter.hpp"
#include <cassert>

ToolboxDialogEnrollment::ToolboxDialogEnrollment(ToolboxDialogPresenter *owner)
    : prev_(0), next_(0), owner_(owner)
{
}

ToolboxDialogEnrollment::~ToolboxDialogEnrollment()
{
  this->unlink();
}

void ToolboxDialogEnrollment::unlink()
{
  if (!this->next_)
    return;
  this->prev_->next_ = this->next_;
  this->next_->prev_ = this->prev_;
  this->prev_ = this->next_ = 0;
}

ToolboxPendingDialogs::ToolboxPendingDialogs() : sentinel_(0)
{
  this->sentinel_.prev_ = this->sentinel_.next_ = &this->sentinel_;
}

ToolboxPendingDialogs::~ToolboxPendingDialogs()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  const bool wasEmpty = this->sentinel_.next_ == &this->sentinel_;
#endif
  while (this->sentinel_.next_ != &this->sentinel_)
    this->sentinel_.next_->unlink();
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(wasEmpty && "pending dialogs must detach before controller teardown");
  (void)wasEmpty;
#endif
}

void ToolboxPendingDialogs::enroll(ToolboxDialogEnrollment &row)
{
  if (row.next_)
    return;
  row.prev_ = this->sentinel_.prev_;
  row.next_ = &this->sentinel_;
  row.prev_->next_ = &row;
  this->sentinel_.prev_ = &row;
}

ToolboxDialogPresenter *ToolboxPendingDialogs::take()
{
  if (this->empty())
    return 0;
  ToolboxDialogEnrollment *row = this->sentinel_.next_;
  if (!row->owner_->attached())
    return 0;
  ToolboxDialogPresenter *owner = row->owner_;
  row->unlink();
  return owner;
}
