#ifndef LOKA_TOOLBOX_DIALOG_CONTEXT_HPP
#define LOKA_TOOLBOX_DIALOG_CONTEXT_HPP

#include "ToolboxPendingDialogs.hpp"
#include "app/core/DialogResultTransport.hpp"
#include "ToolboxDialogPresenter.hpp"

/** Context owns cancellation; Window owns storage. File and confirm share the
    attach/refresh/revoke ritual, while only the native body differs. */
template <class PropsT, class ResultT>
class ToolboxTypedDialogContext : public ToolboxDialogPresenter
{
public:
  typedef loka::app::DialogResultTransport::TypedRegistration<PropsT, ResultT> Registration;
  typedef loka::app::DialogResultTransport::TypedReturnPort<PropsT, ResultT> ReturnPort;
  ToolboxTypedDialogContext(typename PropsT::NodeType *node, ToolboxPendingDialogs *pending,
                            loka::app::DialogResultTransport *transport)
      : ToolboxDialogPresenter(node), pending_(pending), transport_(transport),
        enrollment_(this), registration_(0), presentation_() {}
  virtual ~ToolboxTypedDialogContext() { this->revoke(); }

  void readLifecycleFactOnAttach() { this->refresh(); }
  void refresh()
  {
    if (!this->attached() || !this->pending_ || !this->transport_
        || this->presentation_.value != loka::app::OPEN_FILE_DIALOG_PRESENTATION_PENDING_ATTACH)
      return;
    if (!this->registration_)
      this->registration_ = this->transport_->reserve(this->props());
    if (this->registration_)
      this->pending_->enroll(this->enrollment_);
  }
  virtual void onPropsApplied()
  {
    // Deliberate Win32 twin: retarget revokes the old permission. Toolbox may
    // refresh a not-yet-presented enrollment; a consumed modal waits for reattach.
    if (this->registration_ && !this->registration_->matches(this->props()))
      this->revoke();
    this->refresh();
  }
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                             loka::app::scene::NodeLifecycleFact next)
  {
    (void)previous;
    if (next == loka::app::scene::NODE_FACT_ATTACHED)
      this->refresh();
    else
    {
      this->revoke();
      this->presentation_.markDetached();
    }
  }

protected:
  const PropsT &props() const { return static_cast<typename PropsT::NodeType *>(this->owner())->props; }
  Registration *registration() const { return this->registration_; }
  void markPresented() { this->presentation_.markPresented(); }

private:
  void revoke()
  {
    this->enrollment_.unlink();
    Registration *registration = this->registration_;
    this->registration_ = 0;
    delete registration;
  }
  ToolboxPendingDialogs *pending_;
  loka::app::DialogResultTransport *transport_;
  ToolboxDialogEnrollment enrollment_;
  Registration *registration_;
  loka::app::OpenFileDialogPresentationPhase presentation_;
};
#endif
