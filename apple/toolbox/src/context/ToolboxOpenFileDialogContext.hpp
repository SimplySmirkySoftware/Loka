#ifndef LOKA_TOOLBOX_OPEN_FILE_DIALOG_CONTEXT_HPP
#define LOKA_TOOLBOX_OPEN_FILE_DIALOG_CONTEXT_HPP
#include "context/ToolboxDialogContext.hpp"

class ToolboxOpenFileDialogContext
    : public ToolboxTypedDialogContext<loka::app::OpenFileDialogProps, loka::app::FileChooserResult>
{
public:
  ToolboxOpenFileDialogContext(loka::app::OpenFileDialogNode *node, ToolboxPendingDialogs *pending,
                               loka::app::DialogResultTransport *transport)
      : ToolboxTypedDialogContext<loka::app::OpenFileDialogProps, loka::app::FileChooserResult>(node, pending, transport) {}
  virtual void present(ToolboxApp &app);
};

bool RegisterToolboxOpenFileDialogNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);
#endif
