#include "context/ToolboxOpenFileDialogContext.hpp"
#include "ToolboxFileChoice.hpp"
#include "platform/ToolboxPascalText.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxApp.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include <StandardFile.h>
#include <cassert>

namespace
{
  class ToolboxOpenFileDialogNodeHandler
      : public loka::app::scene::RetainedNodeHandler<ToolboxOpenFileDialogNodeHandler,
                                                     loka::app::OpenFileDialogNode,
                                                     ToolboxOpenFileDialogContext>
  {
  public:
    static loka::app::OpenFileDialogNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asOpenFileDialogNode() : 0;
    }

    static ToolboxOpenFileDialogContext *create(loka::app::OpenFileDialogNode *node,
                                                loka::app::scene::IPlatformController *controller,
                                                const loka::app::scene::LayoutState &state)
    {
      ToolboxScenePlatformController *toolbox = static_cast<ToolboxScenePlatformController *>(controller);
      (void)state;
      // Keep the installation ordering at the shared ritual; see RetainedNodeHandler.
      return new ToolboxOpenFileDialogContext(node, &toolbox->pendingDialogs(), toolbox->dialogResults());
    }
    static void refresh(ToolboxOpenFileDialogContext *context, const loka::app::scene::LayoutState &)
    { context->refresh(); }
  };

  ToolboxOpenFileDialogNodeHandler gToolboxOpenFileDialogNodeHandler;
} // namespace

loka::app::FileChooserResult RunToolboxFileDialog(const loka::app::FileDialogOptions &options)
{
  StandardFileReply reply;
  reply.sfGood = false;
  loka::app::FileChooserResult result = loka::app::FileChooserResult::Canceled();
  switch (options.purpose())
  {
  case loka::app::FILE_DIALOG_OPEN:
    // Both policies admit all files on Toolbox, preserving the OPEN behavior.
    StandardGetFile(0, -1, 0, &reply);
    break;
  case loka::app::FILE_DIALOG_SAVE:
  {
    const unsigned char prompt[] = {8, 'S', 'a', 'v', 'e', ' ', 'a', 's', ':'};
    Str255 defaultName;
    loka::core::String roundTrip;
    // The short-label door can substitute or cap text. A filename must survive
    // its round trip exactly, in addition to fitting the HFS byte limit.
    if (!ToolboxEncodePascal(options.defaultName(), defaultName)
        || defaultName[0] > 31
        || !ToolboxDecodeNative(defaultName + 1, defaultName[0], roundTrip)
        || !roundTrip.equals(options.defaultName()))
    {
      result = loka::app::FileChooserResult::Error(paramErr);
      break;
    }
    StandardPutFile(prompt, defaultName, &reply);
    break;
  }
  }

  if (reply.sfGood)
  {
    if (options.purpose() == loka::app::FILE_DIALOG_SAVE
        && (reply.sfFile.name[0] == 0 || reply.sfFile.name[0] > 31))
      result = loka::app::FileChooserResult::Error(paramErr);
    else
    {
      loka::file::File file;
      result = ToolboxCaptureChosenFile(reply.sfFile, file)
          ? loka::app::FileChooserResult::File(file)
          : loka::app::FileChooserResult::Error(memFullErr);
    }
  }

  return result;
}

bool RegisterToolboxOpenFileDialogNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  return registry.registerHandler(&gToolboxOpenFileDialogNodeHandler);
}

void ToolboxOpenFileDialogContext::present(ToolboxApp &app)
{
  ReturnPort port(this->registration());
  const loka::app::FileDialogOptions options(this->props().options_);
  this->markPresented();
  const loka::app::FileChooserResult result = RunToolboxFileDialog(options);
  // Deliberate confirm twin: no context access after the modal can destroy it.
  app.cursorOwner().reconcile();
  port.seal(result);
}
