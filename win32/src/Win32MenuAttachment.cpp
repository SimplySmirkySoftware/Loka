#include "Win32MenuAttachment.hpp"
#include "app/MenuComposition.hpp"
#include "core/util/OwnedDef.hpp"
#include "Win32Window.hpp"
#include "Win32App.hpp"
#include "platform/Win32String.hpp"
#include <algorithm>

namespace
{
  // Deliberate platform-seam twin of ToolboxMenuAttachment.cpp's
  // MenuShortcutForAction and MacMenuAttachment.mm:141: Win32 has NO default
  // key for MENU_ACTION_QUIT_APP. Alt+F4 closes a window, not the application.
  WORD MenuShortcutForAction(const loka::app::MenuItemDefinition *item)
  {
    if (!item->hasShortcut || !item->shortcutKey)
      return 0;
    unsigned char key = static_cast<unsigned char>(item->shortcutKey);
    if (key >= 'a' && key <= 'z')
      key = static_cast<unsigned char>(key - 'a' + 'A');
    return (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') ? key : 0;
  }

  // Deliberate platform-seam twin of ToolboxMenuAttachment.cpp and
  // MacMenuAttachment.mm: Win32 calls an empty-title Quit "Exit".
  const wchar_t *MenuEmptyTitle(loka::app::MenuActionType action)
  {
    if (action == loka::app::MENU_ACTION_QUIT_APP)
      return L"Exit";
    if (action == loka::app::MENU_ACTION_ABOUT_APP)
      return L"About";
    return L"";
  }

  bool AppendPendingSeparator(HMENU menu, bool &pending)
  {
    if (pending && GetMenuItemCount(menu) > 0 && !AppendMenuW(menu, MF_SEPARATOR, 0, NULL))
      return false;
    pending = false;
    return true;
  }

  // False when the title cannot be materialized: the caller keeps what it has
  // (a build refuses, an update leaves the installed label) rather than
  // committing an empty or suffix-only label (bot P2 on #1129).
  bool MenuItemLabel(const loka::core::String &title,
                     const std::wstring &shortcutSuffix,
                     const wchar_t *emptyTitle,
                     std::wstring &label)
  {
    if (!loka::win32::MaterializeWideString(title, label))
      return false;
    if (label.empty())
      label = emptyTitle;
    label += shortcutSuffix;
    return true;
  }

  bool ApplyWindowMenuPreservingContentFrame(Win32Window *window, HMENU menu)
  {
    if (!window || !window->hwnd())
    {
      return false;
    }
    loka::core::Frame contentFrame;
    if (!window->queryNativeContentFrame(contentFrame))
    {
      return false;
    }
    if (!SetMenu(window->hwnd(), menu))
    {
      return false;
    }
    window->applyNativeContentFrame(contentFrame);
    window->storeCurrentNativeContentFrame();
    DrawMenuBar(window->hwnd());
    return true;
  }

} // namespace

Win32MenuAttachment::Win32MenuAttachment(Win32Window &window)
    : window_(window), menu_(NULL), accel_(NULL), nextCommandId_(1000), record_()
{
}

Win32MenuAttachment::~Win32MenuAttachment()
{
  this->resetForTeardown();
}

bool Win32MenuAttachment::reset(DetachMode mode)
{
  if (this->menu_)
  {
    if (!this->window_.hwnd())
    {
      // DestroyWindow already released its attached menu.
      this->menu_ = NULL;
    }
    else
    {
      if (GetMenu(this->window_.hwnd()) == this->menu_)
      {
        const bool detached = mode == DETACH_PRESERVING_CONTENT_FRAME
            ? ApplyWindowMenuPreservingContentFrame(&this->window_, NULL)
            : this->window_.detachMenuForTeardown(this->menu_);
        if (!detached)
          return false;
      }
      DestroyMenu(this->menu_);
      this->menu_ = NULL;
    }
  }
  if (this->accel_)
  {
    DestroyAcceleratorTable(this->accel_);
    this->accel_ = NULL;
  }
  return true;
}

bool Win32MenuAttachment::resetForTeardown()
{
  this->disconnect();
  return this->reset(DETACH_FOR_TEARDOWN);
}

void Win32MenuAttachment::disconnect()
{
  this->clearMenuBindings();
  this->record_.clear();
}

void Win32MenuAttachment::releaseFrom(const loka::app::scene::Scene *source)
{
  if (this->record_.release(source))
    this->clearMenuBindings();
}

bool Win32MenuAttachment::translateAccelerator(MSG &msg)
{
  return this->accel_ && TranslateAcceleratorW(this->window_.hwnd(), this->accel_, &msg) != 0;
}

bool Win32MenuAttachment::dispatch(int commandId)
{
  for (size_t i = 0; i < this->commands_.size(); ++i)
  {
    if (this->commands_[i].commandId != commandId)
      continue;
    // emit() may disconnect or replace the table synchronously.
    const MenuCommand command = this->commands_[i];
    Win32App *app = this->window_.win32App();
    if (command.action == loka::app::MENU_ACTION_QUIT_APP)
    {
      if (app)
        app->requestQuit();
      return true;
    }
    if (command.emitter)
      command.emitter->emit();
    return true;
  }
  return false;
}

// Deliberate twin of ToolboxMenuAttachment's title binding: Win32 preserves
// the shortcut suffix and changes only MIIM_STRING, leaving id/submenu intact.
void Win32MenuAttachment::MenuTitleChangedThunk(void *userData)
{
  MenuBinding *binding = static_cast<MenuBinding *>(userData);
  if (!binding || !binding->titleState || !binding->menu)
    return;
  std::wstring label;
  if (!MenuItemLabel(binding->titleState->get(), binding->shortcutSuffix, binding->emptyTitle, label))
    return;
  MENUITEMINFOW info = {};
  info.cbSize = sizeof(info);
  info.fMask = MIIM_STRING;
  info.dwTypeData = const_cast<wchar_t *>(label.c_str());
  // SetMenuItemInfoW's contract: redraw the owning window's menu bar after
  // every change, whichever menu the item lives in.
  if (SetMenuItemInfoW(binding->menu, binding->item, binding->byFlags == MF_BYPOSITION, &info)
      && binding->hwnd)
  {
    DrawMenuBar(binding->hwnd);
  }
}

void Win32MenuAttachment::MenuEnabledChangedThunk(void *userData)
{
  MenuBinding *binding = static_cast<MenuBinding *>(userData);
  if (!binding || !binding->enabledState || !binding->menu)
    return;
  bool enabled = binding->enabledState->get();
  if (binding->invertEnabled)
  {
    enabled = !enabled;
  }
  EnableMenuItem(binding->menu, binding->item, binding->byFlags | (enabled ? MF_ENABLED : MF_GRAYED));
  if (binding->hwnd)
  {
    DrawMenuBar(binding->hwnd);
  }
}

void Win32MenuAttachment::MenuCheckedChangedThunk(void *userData)
{
  MenuBinding *binding = static_cast<MenuBinding *>(userData);
  if (!binding || !binding->checkedState || !binding->menu)
    return;
  const bool checked = binding->checkedState->get();
  CheckMenuItem(binding->menu, binding->item, binding->byFlags | (checked ? MF_CHECKED : MF_UNCHECKED));
  if (binding->hwnd)
  {
    DrawMenuBar(binding->hwnd);
  }
}

void Win32MenuAttachment::clearMenuBindings()
{
  for (size_t i = 0; i < bindings_.size(); ++i)
  {
    MenuBinding *binding = bindings_[i];
    if (binding && binding->titleState)
    {
      binding->titleState->deferUnbind(&Win32MenuAttachment::MenuTitleChangedThunk, binding);
    }
    if (binding && binding->enabledState)
    {
      binding->enabledState->deferUnbind(&Win32MenuAttachment::MenuEnabledChangedThunk, binding);
    }
    if (binding && binding->checkedState)
    {
      binding->checkedState->deferUnbind(&Win32MenuAttachment::MenuCheckedChangedThunk, binding);
    }
    delete binding;
  }
  bindings_.clear();
  commands_.clear();
  nextCommandId_ = 1000;
}

bool Win32MenuAttachment::buildMenuItem(HMENU menu,
                                        const loka::app::MenuItemDefinition *itemDef,
                                        HWND hwnd,
                                        std::vector<ACCEL> &accelerators,
                                        bool suppressQuit,
                                        bool &pendingSeparator)
{
  const WORD key = itemDef->hasChildren() ? 0 : MenuShortcutForAction(itemDef);
  std::wstring shortcutSuffix;
  if (key)
  {
    shortcutSuffix = L"\tCtrl+";
    shortcutSuffix += static_cast<wchar_t>(key);
  }
  std::wstring titleWide;
  if (!MenuItemLabel(itemDef->titleState ? itemDef->titleState->get() : itemDef->title,
                     shortcutSuffix,
                     MenuEmptyTitle(itemDef->action),
                     titleWide))
    return false;
  UINT flags = MF_STRING;
  if (!itemDef->isEnabledInitial())
  {
    flags |= MF_GRAYED;
  }
  if (itemDef->isCheckedInitial())
  {
    flags |= MF_CHECKED;
  }

  if (itemDef->hasChildren())
  {
    HMENU subMenu = CreatePopupMenu();
    if (!subMenu)
      return false;
    if (!this->buildMenuItems(subMenu, itemDef->childrenHead(), hwnd, accelerators, suppressQuit))
    {
      DestroyMenu(subMenu);
      return false;
    }
    if (GetMenuItemCount(subMenu) == 0)
    {
      DestroyMenu(subMenu);
      return true;
    }
    if (!AppendPendingSeparator(menu, pendingSeparator))
    {
      DestroyMenu(subMenu);
      return false;
    }
    // A popup title has no command id; its live state is addressed by the
    // position it is appended at.
    const int popupPosition = GetMenuItemCount(menu);
    if (!AppendMenuW(menu, flags | MF_POPUP, reinterpret_cast<UINT_PTR>(subMenu), titleWide.c_str()))
    {
      DestroyMenu(subMenu);
      return false;
    }
    bindMenuItemStates(menu, static_cast<UINT>(popupPosition), MF_BYPOSITION, itemDef, hwnd, shortcutSuffix);
    return true;
  }

  if (!AppendPendingSeparator(menu, pendingSeparator))
    return false;

  // ACCEL.cmd and WM_COMMAND both carry a 16-bit command id.
  if (this->nextCommandId_ > 0xffff)
    return false;
  const int commandId = this->nextCommandId_++;
  if (key)
  {
    ACCEL accelerator = {FCONTROL | FVIRTKEY, key, static_cast<WORD>(commandId)};
    accelerators.push_back(accelerator);
  }
  if (!AppendMenuW(menu, flags, static_cast<UINT_PTR>(commandId), titleWide.c_str()))
    return false;
  Win32MenuAttachment::MenuCommand command;
  command.commandId = commandId;
  command.action = itemDef->action;
  command.emitter = itemDef->onClickState;
  commands_.push_back(command);
  bindMenuItemStates(menu, static_cast<UINT>(commandId), MF_BYCOMMAND, itemDef, hwnd, shortcutSuffix);
  return true;
}

void Win32MenuAttachment::bindMenuItemStates(HMENU menu,
                                  UINT item,
                                  UINT byFlags,
                                  const loka::app::MenuItemDefinition *itemDef,
                                  HWND hwnd, const std::wstring &shortcutSuffix)
{
  loka::core::State<bool> *enabledBindingState = itemDef->enabledBindingState();
  loka::core::State<bool> *checkedBindingState = itemDef->checkedBindingState();
  if (!itemDef->titleState && !enabledBindingState && !checkedBindingState)
  {
    return;
  }
  Win32MenuAttachment::MenuBinding *binding = new Win32MenuAttachment::MenuBinding(shortcutSuffix);
  binding->emptyTitle = MenuEmptyTitle(itemDef->action);
  binding->titleState = itemDef->titleState;
  binding->menu = menu;
  binding->item = item;
  binding->byFlags = byFlags;
  binding->hwnd = hwnd;
  binding->enabledState = enabledBindingState;
  binding->invertEnabled = itemDef->enabledBindingInvert();
  binding->checkedState = checkedBindingState;
  if (binding->titleState)
  {
    binding->titleState->deferBind(&Win32MenuAttachment::MenuTitleChangedThunk, binding);
  }
  if (enabledBindingState)
  {
    enabledBindingState->deferBind(&Win32MenuAttachment::MenuEnabledChangedThunk, binding);
  }
  if (checkedBindingState)
  {
    checkedBindingState->deferBind(&Win32MenuAttachment::MenuCheckedChangedThunk, binding);
  }
  bindings_.push_back(binding);
}

bool Win32MenuAttachment::buildMenuItems(HMENU menu,
                                         const loka::app::MenuItemDefinition *itemsHead,
                                         HWND hwnd,
                                         std::vector<ACCEL> &accelerators,
                                         bool suppressQuit,
                                         const loka::app::MenuItemDefinition *tail)
{
  bool pendingSeparator = false;
  for (const loka::app::MenuItemDefinition *item = itemsHead; item; item = item->nextInComposition)
  {
    if (!item->isVisibleInitial())
      continue;
    if (item->isSeparator)
    {
      pendingSeparator = true;
      continue;
    }
    if (item->action == loka::app::MENU_ACTION_SHOW_COLOR_PICKER
        || (suppressQuit && !item->hasChildren() && item->action == loka::app::MENU_ACTION_QUIT_APP))
      continue;
    if (!this->buildMenuItem(menu, item, hwnd, accelerators, suppressQuit, pendingSeparator))
      return false;
  }
  if (tail)
  {
    // The pre-scan already read visibility. Carry the selected leaf intact.
    pendingSeparator = true;
    if (!this->buildMenuItem(menu, tail, hwnd, accelerators, suppressQuit, pendingSeparator))
      return false;
  }
  return true;
}

Win32MenuAttachment::ProjectResult Win32MenuAttachment::project(const loka::app::MenuSources &sources)
{
  if (!this->window_.hwnd())
    return PROJECT_REFUSED;
  if (sources.absent())
  {
    this->record_.clear();
    // An absent offer with no installed HMENU is unchanged.
    if (!this->menu_)
      return PROJECT_UNCHANGED;
    if (!this->reset(DETACH_PRESERVING_CONTENT_FRAME))
      return PROJECT_REFUSED;
    this->disconnect();
    return PROJECT_APPLIED;
  }

  if (this->record_.unchanged(sources))
    return PROJECT_UNCHANGED;

  loka::core::OwnedDef<loka::app::MenuBarDefinition> merged(
      loka::app::MergeMenuBars(sources.base, sources.overlay));
  if (!merged.isSet())
    return PROJECT_REFUSED;
  const loka::app::MenuBarDefinition *bar = merged.get();

  const loka::app::MenuItemDefinition *quit = 0;
  const loka::app::MenuDefinition *file = 0;
  loka::dsl::CompositionCursor<loka::app::MenuDefinition> scan(bar->menusHead(), bar->menusCount());
  for (loka::app::MenuDefinition *menu = scan.next(); menu; menu = scan.next())
  {
    if (!menu->isAppMenu)
    {
      if (!file && menu->title.equals(loka::core::String::Literal("File")))
        file = menu;
      continue;
    }
    for (const loka::app::MenuItemDefinition *item = menu->itemsHead(); item; item = item->nextInComposition)
    {
      if (!quit && !item->isSeparator && !item->hasChildren() && item->action == loka::app::MENU_ACTION_QUIT_APP
          && item->isVisibleInitial())
        quit = item;
    }
  }

  // The same owner shape holds the candidate until the native swap succeeds.
  // Its destructor disposes either a refused candidate or the replaced handle.
  Win32MenuAttachment next(this->window_);
  next.menu_ = CreateMenu();
  if (!next.menu_)
    return PROJECT_REFUSED;
  HWND hwnd = this->window_.hwnd();
  std::vector<ACCEL> accelerators;
  if (quit && !file)
  {
    HMENU subMenu = CreatePopupMenu();
    if (!subMenu)
      return PROJECT_REFUSED;
    if (!next.buildMenuItems(subMenu, 0, hwnd, accelerators, true, quit)
        || !AppendMenuW(next.menu_, MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(subMenu), L"File"))
    {
      DestroyMenu(subMenu);
      return PROJECT_REFUSED;
    }
  }
  loka::dsl::CompositionCursor<loka::app::MenuDefinition> it(bar->menusHead(), bar->menusCount());
  for (loka::app::MenuDefinition *menuDef = it.next(); menuDef; menuDef = it.next())
  {
    if (menuDef->isAppMenu)
      continue;
    std::wstring titleWide;
    loka::win32::MaterializeWideString(menuDef->title, titleWide);
    if (titleWide.empty())
      titleWide = L"Menu";
    HMENU subMenu = CreatePopupMenu();
    if (!subMenu)
      return PROJECT_REFUSED;
    if (!next.buildMenuItems(subMenu, menuDef->itemsHead(), hwnd, accelerators, quit != 0, menuDef == file ? quit : 0))
    {
      DestroyMenu(subMenu);
      return PROJECT_REFUSED;
    }
    if (GetMenuItemCount(subMenu) == 0)
    {
      DestroyMenu(subMenu);
      continue;
    }
    if (!AppendMenuW(next.menu_, MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(subMenu), titleWide.c_str()))
    {
      DestroyMenu(subMenu);
      return PROJECT_REFUSED;
    }
  }

  // AppMenu is platform-reserved. An empty bar must not consume a menu row.
  if (GetMenuItemCount(next.menu_) == 0)
  {
    DestroyMenu(next.menu_);
    next.menu_ = NULL;
  }
  if (!accelerators.empty())
  {
    next.accel_ = CreateAcceleratorTableW(&accelerators[0], static_cast<int>(accelerators.size()));
    if (!next.accel_)
      return PROJECT_REFUSED;
  }
  if (!ApplyWindowMenuPreservingContentFrame(&this->window_, next.menu_))
    return PROJECT_REFUSED;
  this->disconnect();
  std::swap(this->menu_, next.menu_);
  std::swap(this->accel_, next.accel_);
  this->commands_.swap(next.commands_);
  this->bindings_.swap(next.bindings_);
  std::swap(this->nextCommandId_, next.nextCommandId_);
  this->record_.commit(sources);
  return PROJECT_APPLIED;
}
