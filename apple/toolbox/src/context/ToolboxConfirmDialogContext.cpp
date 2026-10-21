#include "context/ToolboxConfirmDialogContext.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxApp.hpp"
#include "ToolboxDialogConstruction.hpp"
#include "platform/ToolboxPascalText.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include <Dialogs.h>
#include <Events.h>
#include <Memory.h>

namespace
{
  const Rect kSaveBounds = {100, 264, 120, 332};

  pascal Boolean ConfirmFilter(DialogPtr dialog, EventRecord *event, short *item)
  {
    if (event->what == updateEvt &&
        static_cast<unsigned long>(event->message) == reinterpret_cast<unsigned long>(dialog))
    {
      BeginUpdate(reinterpret_cast<WindowPtr>(dialog));
      DrawDialog(dialog);
      loka::toolbox::DrawDialogDefaultOutline(dialog, kSaveBounds);
      EndUpdate(reinterpret_cast<WindowPtr>(dialog));
      return false;
    }
    if (event->what == keyDown || event->what == autoKey)
    {
      const unsigned char key = event->message & charCodeMask;
      if (key == 13 || key == 3)
      {
        *item = 1;
        return true;
      }
      if (key == 27 || (key == '.' && (event->modifiers & cmdKey)))
      {
        *item = 2;
        return true;
      }
      // Command-D is Classic's key for Don't Save.
      if ((key == 'd' || key == 'D') && (event->modifiers & cmdKey))
      {
        *item = 3;
        return true;
      }
    }
    return false;
  }

  loka::app::ConfirmResult RunToolboxConfirmDialog(const loka::app::ConfirmDialogOptions &options)
  {
    using loka::app::ConfirmResult;
    // Same bounded native-script conversion as the Toolbox short-text path.
    Str255 message;
    if (!ToolboxEncodePascal(options.message(), message))
      return ConfirmResult::Error(memFullErr);
    unsigned char bytes[2 + (14 + 4) + (14 + 6) + (14 + 10) + (14 + 256) + (14 + 2)];
    unsigned char *out = bytes;
    loka::toolbox::PutDialogWord(out, 4); // count minus one
    loka::toolbox::PutDialogItem(out, kSaveBounds, ctrlItem + btnCtrl,
        reinterpret_cast<const unsigned char *>("Save"), 4);
    const Rect cancelBounds = {100, 176, 120, 244};
    loka::toolbox::PutDialogItem(out, cancelBounds, ctrlItem + btnCtrl,
        reinterpret_cast<const unsigned char *>("Cancel"), 6);
    const Rect discardBounds = {100, 20, 120, 112};
    loka::toolbox::PutDialogItem(out, discardBounds, ctrlItem + btnCtrl,
        reinterpret_cast<const unsigned char *>("Don't Save"), 10);
    // Icon and text sit where the OOM alert puts them (ToolboxOutOfMemory.cpp);
    // appending the icon keeps the button item numbers.
    const Rect textBounds = {20, 64, 80, 332};
    loka::toolbox::PutDialogItem(out, textBounds, statText + itemDisable, message + 1, message[0]);
    const Rect iconBounds = {20, 20, 52, 52};
    const unsigned char cautionIcon[] = {0, 2}; // System ICON resource 2
    loka::toolbox::PutDialogItem(out, iconBounds, iconItem + itemDisable, cautionIcon, 2);
    Handle items = NewHandle(out - bytes);
    if (!items)
      return ConfirmResult::Error(memFullErr);
    std::memcpy(*items, bytes, out - bytes);
    const Rect bounds = loka::toolbox::CenterDialogBounds(352, 140);
    const unsigned char title[] = {0};
    DialogPtr dialog = NewDialog(0, &bounds, title, true, dBoxProc,
                                reinterpret_cast<WindowPtr>(-1), false, 0, items);
    if (!dialog)
    {
      DisposeHandle(items);
      return ConfirmResult::Error(memFullErr);
    }
    // Same transfer as the OOM dialog: DisposeDialog owns the item-list handle.
    ModalFilterUPP filter = NewModalFilterUPP(ConfirmFilter);
    if (!filter)
    {
      DisposeDialog(dialog);
      return ConfirmResult::Error(memFullErr);
    }
    loka::toolbox::DrawDialogDefaultOutline(dialog, kSaveBounds);
    short item = 0;
    do { ModalDialog(filter, &item); } while (item < 1 || item > 3);
    DisposeModalFilterUPP(filter);
    DisposeDialog(dialog);
    switch (item)
    {
    case 1: return ConfirmResult::Save();
    case 2: return ConfirmResult::Cancel();
    case 3: return ConfirmResult::Discard();
    }
    return ConfirmResult::Error(paramErr);
  }

  class ToolboxConfirmDialogNodeHandler
      : public loka::app::scene::RetainedNodeHandler<ToolboxConfirmDialogNodeHandler,
          loka::app::ConfirmDialogNode, ToolboxConfirmDialogContext>
  {
  public:
    static loka::app::ConfirmDialogNode *cast(loka::app::scene::Node *node)
    { return node ? node->asConfirmDialogNode() : 0; }
    static ToolboxConfirmDialogContext *create(loka::app::ConfirmDialogNode *node,
        loka::app::scene::IPlatformController *controller, const loka::app::scene::LayoutState &state)
    {
      (void)state;
      ToolboxScenePlatformController *toolbox = static_cast<ToolboxScenePlatformController *>(controller);
      return new ToolboxConfirmDialogContext(node, &toolbox->pendingDialogs(), toolbox->dialogResults());
    }
    static void refresh(ToolboxConfirmDialogContext *context, const loka::app::scene::LayoutState &)
    { context->refresh(); }
  };
  ToolboxConfirmDialogNodeHandler gToolboxConfirmDialogNodeHandler;
}

void ToolboxConfirmDialogContext::present(ToolboxApp &app)
{
  ReturnPort port(this->registration());
  const loka::app::ConfirmDialogOptions options(this->props().options_);
  this->markPresented();
  const loka::app::ConfirmResult result = RunToolboxConfirmDialog(options);
  // Only stack values after native return: the modal may have destroyed this.
  app.cursorOwner().reconcile();
  port.seal(result);
}

bool RegisterToolboxConfirmDialogNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  return registry.registerHandler(&gToolboxConfirmDialogNodeHandler);
}
