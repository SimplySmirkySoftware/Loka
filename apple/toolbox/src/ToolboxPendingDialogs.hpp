#ifndef LOKA_TOOLBOX_PENDING_DIALOGS_HPP
#define LOKA_TOOLBOX_PENDING_DIALOGS_HPP

#include "app/OpenFileDialog.hpp"

class ToolboxDialogPresenter;
namespace loka { namespace testing { struct ToolboxDialogTestAccess; } }

/** Context-owned enrollment. Links alone encode pending membership. */
class ToolboxDialogEnrollment
{
public:
  explicit ToolboxDialogEnrollment(ToolboxDialogPresenter *owner);
  ~ToolboxDialogEnrollment();
  void unlink();

private:
  friend class ToolboxPendingDialogs;
  friend struct loka::testing::ToolboxDialogTestAccess;
  ToolboxDialogEnrollment(const ToolboxDialogEnrollment &);
  ToolboxDialogEnrollment &operator=(const ToolboxDialogEnrollment &);
  ToolboxDialogEnrollment *prev_;
  ToolboxDialogEnrollment *next_;
  ToolboxDialogPresenter *const owner_;
};

/** Window-owned FIFO, consumed only by the foreground App present pass. */
class ToolboxPendingDialogs
{
public:
  ToolboxPendingDialogs();
  ~ToolboxPendingDialogs();
  void enroll(ToolboxDialogEnrollment &row);
  ToolboxDialogPresenter *take();
  bool empty() const { return this->sentinel_.next_ == &this->sentinel_; }

private:
  friend struct loka::testing::ToolboxDialogTestAccess;
  ToolboxPendingDialogs(const ToolboxPendingDialogs &);
  ToolboxPendingDialogs &operator=(const ToolboxPendingDialogs &);
  ToolboxDialogEnrollment sentinel_;
};

/** Native modal body; holds no context or controller reference. */
loka::app::FileChooserResult RunToolboxFileDialog(const loka::app::FileDialogOptions &options);
#endif
