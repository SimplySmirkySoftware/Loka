#ifndef LOKA_TOOLBOX_CONFIRM_DIALOG_CONTEXT_HPP
#define LOKA_TOOLBOX_CONFIRM_DIALOG_CONTEXT_HPP
#include "context/ToolboxDialogContext.hpp"

class ToolboxConfirmDialogContext
    : public ToolboxTypedDialogContext<loka::app::ConfirmDialogProps, loka::app::ConfirmResult>
{
public:
  ToolboxConfirmDialogContext(loka::app::ConfirmDialogNode *node, ToolboxPendingDialogs *pending,
                              loka::app::DialogResultTransport *transport)
      : ToolboxTypedDialogContext<loka::app::ConfirmDialogProps, loka::app::ConfirmResult>(node, pending, transport) {}
  virtual void present(ToolboxApp &app);
};

bool RegisterToolboxConfirmDialogNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);
#endif
