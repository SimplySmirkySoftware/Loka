#include "Win32OpenFileDialogContext.hpp"
#include "../Win32ScenePlatformController.hpp"
#include "../Win32Window.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include <commdlg.h>
#include <string>
#include "Win32ThreadModalDialogScope.hpp"
#include "Win32DialogResultWake.hpp"
#include "platform/Win32PathBridge.hpp"

namespace
{
  class Win32OpenFileDialogNodeHandler
      : public loka::app::scene::RetainedNodeHandler<Win32OpenFileDialogNodeHandler,
                                                     loka::app::OpenFileDialogNode,
                                                     Win32OpenFileDialogContext>
  {
  public:
    static loka::app::OpenFileDialogNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asOpenFileDialogNode() : 0;
    }

    static Win32OpenFileDialogContext *create(loka::app::OpenFileDialogNode *dialog,
                                              loka::app::scene::IPlatformController *controller,
                                              const loka::app::scene::LayoutState &state)
    {
      (void)state;
      Win32ScenePlatformController *win32 = static_cast<Win32ScenePlatformController *>(controller);
      Win32Window *window = win32->rootHwnd()
                           ? reinterpret_cast<Win32Window *>(GetWindowLongPtr(win32->rootHwnd(), GWLP_USERDATA))
                           : 0;
      return new Win32OpenFileDialogContext(win32->rootHwnd(), dialog, window);
    }

    static void refresh(Win32OpenFileDialogContext *ctx, const loka::app::scene::LayoutState &)
    {
      ctx->presentIfNeeded();
    }

    static void afterAttach(Win32OpenFileDialogContext *ctx)
    {
      // Keep presentation in the shared after-attach slot; see RetainedNodeHandler.
      ctx->presentIfNeeded();
    }
  };

  Win32OpenFileDialogNodeHandler gWin32OpenFileDialogNodeHandler;
} // namespace

UINT Win32OpenFileDialogContext::deferredResultMessage()
{
  return loka::win32::DialogResultWakeMessage();
}

bool Win32OpenFileDialogContext::handlePostedResultMessage(UINT message, WPARAM, LPARAM)
{
  if (message != loka::win32::DialogResultWakeMessage())
  {
    return false;
  }
  // A wake carries no entry identity and never invokes application code.
  return true;
}

Win32OpenFileDialogContext::Win32OpenFileDialogContext(HWND parent, loka::app::OpenFileDialogNode *node,
                                                     Win32Window *window)
    : parent_(parent), node_(node), transport_(window ? &window->dialogResults() : 0),
      presentation_(), registration_(0)
{
}

// Lifecycle/props sequencing deliberately mirrors Win32ConfirmDialogContext.
void Win32OpenFileDialogContext::onPropsApplied()
{
  if (this->registration_ && this->node_ && !this->registration_->matches(this->node_->props))
    this->detachOwnedDialog();
}

Win32OpenFileDialogContext::~Win32OpenFileDialogContext()
{
  this->detachOwnedDialog();
}

void Win32OpenFileDialogContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void Win32OpenFileDialogContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                               loka::app::scene::NodeLifecycleFact next)
{
  (void)previous;
  if (next == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
  else
  {
    // Retained detach abandons the old operation just like terminal retirement.
    this->applyDetachedPresentation();
  }
}

void Win32OpenFileDialogContext::applyAttachedPresentation()
{
  // Record intent here; afterAttach/refresh owns the presentation attempt.
  this->presentation_.markDetached();
}

void Win32OpenFileDialogContext::applyDetachedPresentation()
{
  this->presentation_.markDetached();
  this->detachOwnedDialog();
}

void Win32OpenFileDialogContext::presentIfNeeded()
{
  if (this->registration_ || !this->transport_ || !this->node_
      || this->node_->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
    return;
  if (!this->presentation_.beginPresent())
    return;
  this->registration_ = this->transport_->reserve(this->node_->props);
  if (!this->registration_)
  {
    this->presentation_.markDetached();
    return;
  }
  this->presentDialog();
}

bool Win32OpenFileDialogContext::configureSaveDialog(const loka::app::FileDialogOptions &options,
                                                     wchar_t (&buffer)[MAX_PATH], OPENFILENAMEW &dialog)
{
  ZeroMemory(buffer, sizeof(buffer));
  std::wstring defaultName;
  if (!loka::win32::MaterializeWideString(options.defaultName(), defaultName)
      || defaultName.size() >= MAX_PATH)
    return false;

  // A default is a basename, never a path. Empty lets the user supply a name.
  // Native validation still owns reserved device names and destination validity.
  for (std::size_t i = 0; i < defaultName.size(); ++i)
  {
    const wchar_t unit = defaultName[i];
    if (unit < L' ' || unit == L'<' || unit == L'>' || unit == L':' || unit == L'"'
        || unit == L'/' || unit == L'\\' || unit == L'|' || unit == L'?' || unit == L'*')
      return false;
  }
  if (!defaultName.empty()
      && (defaultName[defaultName.size() - 1] == L'.' || defaultName[defaultName.size() - 1] == L' '))
    return false;

  defaultName.copy(buffer, defaultName.size());
  dialog.lpstrFile = buffer;
  dialog.nMaxFile = MAX_PATH;
  dialog.nFilterIndex = 1;
  dialog.lpstrFilter = L"All Files\0*.*\0";
  dialog.lpstrDefExt = 0;
  switch (options.filterPolicy())
  {
  case loka::app::FILE_DIALOG_FILTER_DEFAULT:
    break;
  case loka::app::FILE_DIALOG_FILTER_ALL_FILES_TEXT:
    dialog.lpstrFilter = L"Text\0*.txt\0All Files\0*.*\0";
    dialog.lpstrDefExt = L"txt";
    break;
  }
  dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
  return true;
}

void Win32OpenFileDialogContext::presentDialog()
{
  loka::app::DialogResultTransport::ReturnPort port(this->registration_);
  const HWND parent = this->parent_;
  const loka::app::FileDialogOptions options = this->node_->props.options_;
  this->presentation_.markPresented();

  // The W dialog, not the A one: GetOpenFileNameA returns the path in the
  // process ANSI code page, and handing those bytes to loka::core::String --
  // which reads them as UTF-8 -- destroys a full-width path before anything
  // tries to open it (#15). Same failure family as the ANSI EDIT control in
  // #160: the logical String must be built from UTF-16, never from ANSI bytes.
  wchar_t buffer[MAX_PATH];
  buffer[0] = L'\0';

  OPENFILENAMEW ofn;
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = parent;
  ofn.lpstrFile = buffer;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0All Files\0*.*\0";
  if (options.filterPolicy() == loka::app::FILE_DIALOG_FILTER_ALL_FILES_TEXT)
    ofn.lpstrFilter = L"All Files\0*.*\0Text\0*.txt\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

  if (options.purpose() == loka::app::FILE_DIALOG_SAVE
      && !configureSaveDialog(options, buffer, ofn))
  {
    queueDeferredResult(port, loka::app::FileChooserResult::Error(FNERR_INVALIDFILENAME));
    return;
  }

  BOOL accepted;
  {
    loka::win32::ThreadModalDialogScope threadModal(parent);
    accepted = options.purpose() == loka::app::FILE_DIALOG_SAVE
        ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
  }

  loka::app::FileChooserResult result;
  if (accepted)
  {
    const std::wstring selected(buffer);
    loka::file::File file = loka::win32::FileFromWidePath(selected.c_str(), selected.size());
    result = loka::app::FileChooserResult::File(file);
  }
  else
  {
    DWORD error = CommDlgExtendedError();
    if (error != 0)
    {
      result = loka::app::FileChooserResult::Error(static_cast<int>(error));
    }
    else
    {
      result = loka::app::FileChooserResult::Canceled();
    }
  }

  // Native return has only the revocable stack port; the context may be gone.
  queueDeferredResult(port, result);
}

void Win32OpenFileDialogContext::queueDeferredResult(loka::app::DialogResultTransport::ReturnPort &port,
                                                     const loka::app::FileChooserResult &result)
{
  loka::win32::QueueDeferredDialogResult(port, result);
}

void Win32OpenFileDialogContext::detachOwnedDialog()
{
  loka::app::DialogResultTransport::Registration *registration = this->registration_;
  this->registration_ = 0;
  delete registration;
}

void RegisterWin32OpenFileDialogNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gWin32OpenFileDialogNodeHandler);
}
