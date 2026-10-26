#include "app/EditMenuDispatch.hpp"
#include "app/MenuComposition.hpp"
#include "ToolboxMenuAttachment.hpp"
#include "ToolboxApp.hpp"
#include "ToolboxWindow.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "platform/ToolboxPascalText.hpp"
#include <Devices.h>
#include <Sound.h>

namespace
{
  const unsigned char kSeparator[] = {1, '-'};
  const unsigned char kPlaceholder[] = {1, ' '};

  // Deliberate platform-seam twin of the title fallback tables in
  // MacMenuAttachment.mm and Win32MenuAttachment.cpp: Classic uses About / Quit.
  const unsigned char *EmptyMenuTitle(loka::app::MenuActionType action)
  {
    static const unsigned char about[] = {5, 'A', 'b', 'o', 'u', 't'};
    static const unsigned char quit[] = {4, 'Q', 'u', 'i', 't'};
    static const unsigned char undo[] = {4, 'U', 'n', 'd', 'o'};
    static const unsigned char cut[] = {3, 'C', 'u', 't'};
    static const unsigned char copy[] = {4, 'C', 'o', 'p', 'y'};
    static const unsigned char paste[] = {5, 'P', 'a', 's', 't', 'e'};
    static const unsigned char select_all[] = {10, 'S', 'e', 'l', 'e', 'c', 't', ' ', 'A', 'l', 'l'};
    switch (action)
    {
    case loka::app::MENU_ACTION_ABOUT_APP:
      return about;
    case loka::app::MENU_ACTION_QUIT_APP:
      return quit;
    case loka::app::MENU_ACTION_EDIT_UNDO:
      return undo;
    case loka::app::MENU_ACTION_EDIT_CUT:
      return cut;
    case loka::app::MENU_ACTION_EDIT_COPY:
      return copy;
    case loka::app::MENU_ACTION_EDIT_PASTE:
      return paste;
    case loka::app::MENU_ACTION_EDIT_SELECT_ALL:
      return select_all;
    case loka::app::MENU_ACTION_COUNT:
    case loka::app::MENU_ACTION_NONE:
    case loka::app::MENU_ACTION_SHOW_COLOR_PICKER:
      return 0;
    }
    return 0;
  }

  void SetMenuTitle(MenuHandle menu, short item, const loka::core::String &value, const unsigned char *emptyTitle)
  {
    Str255 title;
    const bool encoded = ToolboxEncodePascal(value, title);
    SetMenuItemText(menu, item, encoded && title[0] == 0 && emptyTitle ? emptyTitle : title);
  }

  // Each candidate item write, including insertion before DAs or an opener,
  // must grow the native menu by exactly one. No installation occurs here.
  bool AddMenuItem(MenuHandle menu, const unsigned char *text, short after = -1)
  {
    const short before = CountMenuItems(menu);
    if (after < 0)
      AppendMenu(menu, text);
    else
      InsertMenuItem(menu, text, after);
    return CountMenuItems(menu) == before + 1;
  }

  // ClearMenuBar also deletes every entry of the menu color table, which
  // InitMenus filled from 'mctb' 0 at startup. Keep the table across the
  // clear; without a copy (no memory) the colors fall back to the defaults.
  void ClearMenuBarKeepingColors()
  {
    MCTableHandle colors = GetMCInfo();
    ClearMenuBar();
    if (colors)
    {
      SetMCInfo(colors);
      DisposeMCInfo(colors);
    }
  }
}

ToolboxMenuAttachment::ToolboxMenuAttachment(ToolboxApp &app)
    : app_(app),
      record_()
{
}

ToolboxMenuAttachment::~ToolboxMenuAttachment()
{
  ClearMenuBar();
}

void ToolboxMenuAttachment::disconnect()
{
  this->graph_.disconnect();
  this->record_.clear();
}

void ToolboxMenuAttachment::releaseFrom(const loka::app::scene::Scene *scene)
{
  if (this->record_.release(scene))
    this->graph_.disconnect();
}

void ToolboxMenuAttachment::MenuTitleChangedThunk(void *userData)
{
  ToolboxMenuAttachment::MenuBinding *binding = static_cast<ToolboxMenuAttachment::MenuBinding *>(userData);
  if (!binding || !binding->menu || !binding->titleState)
    return;
  SetMenuTitle(binding->menu, binding->itemIndex, binding->titleState->get(), binding->emptyTitle);
  binding->app->requestMenuBarDraw();
}

void ToolboxMenuAttachment::MenuEnabledChangedThunk(void *userData)
{
  ToolboxMenuAttachment::MenuBinding *binding = static_cast<ToolboxMenuAttachment::MenuBinding *>(userData);
  if (!binding || !binding->menu || !binding->enabledState)
    return;
  bool enabled = binding->enabledState->get();
  if (binding->invertEnabled)
  {
    enabled = !enabled;
  }
  if (enabled)
  {
    EnableItem(binding->menu, binding->itemIndex);
  }
  else
  {
    DisableItem(binding->menu, binding->itemIndex);
  }
  // Enable/DisableItem mutate app-owned menu data and are phase-free.
  // Projection and binding screen writes share the App's draw gate.
  binding->app->requestMenuBarDraw();
}

void ToolboxMenuAttachment::MenuCheckedChangedThunk(void *userData)
{
  ToolboxMenuAttachment::MenuBinding *binding = static_cast<ToolboxMenuAttachment::MenuBinding *>(userData);
  if (!binding || !binding->menu || !binding->checkedState)
    return;
  CheckItem(binding->menu, binding->itemIndex, binding->checkedState->get());
  binding->app->requestMenuBarDraw();
}

void ToolboxMenuAttachment::ApplyMenuItemStates(ToolboxApp *app,
                                MenuHandle menu,
                                short itemIndex,
                                const loka::app::MenuItemDefinition *itemDef,
                                std::vector<ToolboxMenuAttachment::MenuBinding *> &bindings)
{
  const unsigned char *emptyTitle = EmptyMenuTitle(itemDef->action);
  SetMenuTitle(menu, itemIndex, itemDef->titleState ? itemDef->titleState->get() : itemDef->title, emptyTitle);

  // Openers previously bypassed bool bindings; retain that policy while binding titles.
  if (!itemDef->hasChildren() && !itemDef->isEnabledInitial())
  {
    DisableItem(menu, itemIndex);
  }
  if (!itemDef->hasChildren())
    CheckItem(menu, itemIndex, itemDef->isCheckedInitial());

  loka::core::State<bool> *enabledState = itemDef->hasChildren() ? 0 : itemDef->enabledBindingState();
  loka::core::State<bool> *checkedState = itemDef->hasChildren() ? 0 : itemDef->checkedBindingState();
  if (!itemDef->titleState && !enabledState && !checkedState)
  {
    return;
  }

  ToolboxMenuAttachment::MenuBinding *binding = new ToolboxMenuAttachment::MenuBinding();
  binding->app = app;
  binding->menu = menu;
  binding->itemIndex = itemIndex;
  binding->titleState = itemDef->titleState;
  binding->emptyTitle = emptyTitle;
  binding->enabledState = enabledState;
  binding->invertEnabled = itemDef->enabledBindingInvert();
  binding->checkedState = checkedState;
  if (binding->titleState)
  {
    binding->titleState->deferBind(&ToolboxMenuAttachment::MenuTitleChangedThunk, binding);
  }
  if (enabledState)
  {
    enabledState->deferBind(&ToolboxMenuAttachment::MenuEnabledChangedThunk, binding);
  }
  if (checkedState)
  {
    checkedState->deferBind(&ToolboxMenuAttachment::MenuCheckedChangedThunk, binding);
  }
  bindings.push_back(binding);
}

void ToolboxMenuAttachment::ToolboxMenuGraph::disconnect()
{
  for (size_t i = 0; i < this->bindings.size(); ++i)
  {
    MenuBinding *binding = this->bindings[i];
    if (binding)
    {
      if (binding->titleState)
      {
        binding->titleState->deferUnbind(&ToolboxMenuAttachment::MenuTitleChangedThunk, binding);
      }
      if (binding->enabledState)
      {
        binding->enabledState->deferUnbind(&ToolboxMenuAttachment::MenuEnabledChangedThunk, binding);
      }
      if (binding->checkedState)
      {
        binding->checkedState->deferUnbind(&ToolboxMenuAttachment::MenuCheckedChangedThunk, binding);
      }
      binding->menu = 0;
      binding->titleState = 0;
      binding->enabledState = 0;
      binding->checkedState = 0;
    }
    delete binding;
  }
  this->bindings.clear();
  this->commands.clear();
}

ToolboxMenuAttachment::ToolboxMenuGraph::~ToolboxMenuGraph()
{
  this->disconnect();
  for (size_t i = 0; i < this->owned_.size(); ++i)
    DisposeMenu(this->owned_[i]);
}

MenuHandle ToolboxMenuAttachment::ToolboxMenuGraph::create(short id, const unsigned char *title)
{
  MenuHandle menu = NewMenu(id, title);
  if (menu)
    this->owned_.push_back(menu);
  return menu;
}

void ToolboxMenuAttachment::ToolboxMenuGraph::prune(MenuHandle menu)
{
  for (size_t i = 0; i < this->owned_.size(); ++i)
    if (this->owned_[i] == menu)
    {
      DisposeMenu(menu);
      this->owned_.erase(this->owned_.begin() + i);
      return;
    }
}

void ToolboxMenuAttachment::ToolboxMenuGraph::recordInsertion(MenuHandle menu, short mode)
{
  const Insertion entry = {menu, mode};
  this->insertions_.push_back(entry);
}

void ToolboxMenuAttachment::ToolboxMenuGraph::install() const
{
  ClearMenuBarKeepingColors();
  for (size_t i = 0; i < this->insertions_.size(); ++i)
    InsertMenu(this->insertions_[i].menu, this->insertions_[i].mode);
}

void ToolboxMenuAttachment::ToolboxMenuGraph::swap(ToolboxMenuGraph &other)
{
  this->owned_.swap(other.owned_);
  this->insertions_.swap(other.insertions_);
  this->commands.swap(other.commands);
  this->bindings.swap(other.bindings);
}

// Deliberate platform-seam twin of MacMenuAttachment.mm:141 MenuShortcutForAction;
// Win32's twin deliberately has no default key for Quit.
static char MenuShortcutForAction(const loka::app::MenuItemDefinition *itemDef)
{
  if (itemDef->hasShortcut && itemDef->shortcutKey)
  {
    const char key = itemDef->shortcutKey;
    // The Classic cmd byte is overloaded: 0x1B marks a submenu, 0x1C-0x1E
    // script/icon markers, other control bytes are reserved. Only a printable
    // ASCII key may be written; anything else projects no shortcut.
    if (key < 0x20 || key >= 0x7F)
      return 0;
    return key >= 'a' && key <= 'z' ? static_cast<char>(key - 'a' + 'A') : key;
  }
  switch (itemDef->action)
  {
  case loka::app::MENU_ACTION_EDIT_UNDO:
    return 'Z';
  case loka::app::MENU_ACTION_EDIT_CUT:
    return 'X';
  case loka::app::MENU_ACTION_EDIT_COPY:
    return 'C';
  case loka::app::MENU_ACTION_EDIT_PASTE:
    return 'V';
  case loka::app::MENU_ACTION_EDIT_SELECT_ALL:
    return 'A';
  case loka::app::MENU_ACTION_QUIT_APP:
    return 'Q';
  case loka::app::MENU_ACTION_ABOUT_APP:
  case loka::app::MENU_ACTION_SHOW_COLOR_PICKER:
  case loka::app::MENU_ACTION_COUNT:
  case loka::app::MENU_ACTION_NONE:
    return 0;
  }
  return 0;
}

void ToolboxMenuAttachment::ApplyMenuLeaf(ToolboxApp *app,
                                          MenuHandle menu,
                                          short menuId,
                                          short itemIndex,
                                          const loka::app::MenuItemDefinition *itemDef,
                                          ToolboxMenuGraph &graph)
{
  const char key = MenuShortcutForAction(itemDef);
  if (key)
    SetItemCmd(menu, itemIndex, key);
  const MenuCommand command = {menuId, itemIndex, itemDef->action, itemDef->onClickState};
  graph.commands.push_back(command);
  ApplyMenuItemStates(app, menu, itemIndex, itemDef, graph.bindings);
}

bool ToolboxMenuAttachment::BuildMenuItems(ToolboxApp *app,
                                           MenuHandle menu,
                                           const loka::app::MenuItemDefinition *itemsHead,
                                           short menuId,
                                           short &nextMenuId,
                                           ToolboxMenuGraph &graph,
                                           bool suppressQuit,
                                           const loka::app::MenuItemDefinition *tail)
{
  bool pendingSeparator = false;
  for (const loka::app::MenuItemDefinition *itemDef = itemsHead; itemDef; itemDef = itemDef->nextInComposition)
  {
    if (!itemDef->isVisibleInitial())
      continue;
    if (itemDef->isSeparator)
    {
      pendingSeparator = CountMenuItems(menu) != 0;
      continue;
    }
    if (itemDef->action == loka::app::MENU_ACTION_SHOW_COLOR_PICKER
        || (suppressQuit && !itemDef->hasChildren() && itemDef->action == loka::app::MENU_ACTION_QUIT_APP))
      continue;

    // Reserve the item before descending, as before. A separator waits until
    // the submenu survives filtering, then is inserted before this reservation.
    if (!AddMenuItem(menu, kPlaceholder))
      return false;
    short itemIndex = CountMenuItems(menu);
    short subMenuId = 0;
    if (itemDef->hasChildren())
    {
      Str255 title;
      ToolboxEncodePascal(itemDef->title, title);
      if (nextMenuId > 235)
        return false;
      subMenuId = nextMenuId++;
      MenuHandle subMenu = graph.create(subMenuId, title);
      if (!subMenu)
        return false;
      if (!BuildMenuItems(app, subMenu, itemDef->childrenHead(), subMenuId, nextMenuId, graph, suppressQuit))
        return false;
      if (CountMenuItems(subMenu) == 0)
      {
        graph.prune(subMenu);
        DeleteMenuItem(menu, itemIndex);
        continue;
      }
      graph.recordInsertion(subMenu, kInsertHierarchicalMenu);
    }
    if (pendingSeparator)
    {
      if (!AddMenuItem(menu, kSeparator, itemIndex - 1))
        return false;
      ++itemIndex;
    }
    if (itemDef->hasChildren())
    {
      // A declared shortcut is ignored: Classic uses the cmd field as the submenu marker.
      SetItemCmd(menu, itemIndex, hMenuCmd);
#if defined(TARGET_API_MAC_CARBON) && TARGET_API_MAC_CARBON
      OSErr hierErr = SetMenuItemHierarchicalID(menu, itemIndex, subMenuId);
      if (hierErr != noErr)
      {
        SetItemMark(menu, itemIndex, static_cast<CharParameter>(subMenuId & 0xFF));
      }
#else
      SetItemMark(menu, itemIndex, static_cast<CharParameter>(subMenuId & 0xFF));
#endif
      ApplyMenuItemStates(app, menu, itemIndex, itemDef, graph.bindings);
    }
    else
    {
      ApplyMenuLeaf(app, menu, menuId, itemIndex, itemDef, graph);
    }
    pendingSeparator = false;
  }
  if (tail)
  {
    if (CountMenuItems(menu) && !AddMenuItem(menu, kSeparator))
      return false;
    if (!AddMenuItem(menu, kPlaceholder))
      return false;
    ApplyMenuLeaf(app, menu, menuId, CountMenuItems(menu), tail, graph);
  }
  return true;
}

bool ToolboxMenuAttachment::project(const loka::app::MenuSources &sources)
{
  if (sources.absent())
  {
    this->record_.clear();
    if (this->graph_.empty())
      return false;
    ClearMenuBarKeepingColors();
    {
      ToolboxMenuGraph old;
      this->graph_.swap(old);
    }
    this->app_.requestMenuBarDraw();
    return true;
  }
  if (this->record_.unchanged(sources))
    return false;

  loka::core::OwnedDef<loka::app::MenuBarDefinition> merged(
      loka::app::MergeMenuBars(sources.base, sources.overlay));
  if (!merged.isSet())
    return false;

  // Construction assumes no release, disconnect, nested projection or State
  // source destruction reentry, as before. InsertMenu refusal (list full),
  // SetMenuItemText failure and AppendResMenu completeness remain out of scope.
  // Candidate bindings may request a harmless redraw of the installed bar.
  {
    ToolboxMenuGraph candidate;
    if (!this->build(*merged.get(), candidate))
      return false;
    candidate.install();
    this->graph_.swap(candidate);
  }
  this->record_.commit(sources);
  this->app_.requestMenuBarDraw();
  return true;
}

bool ToolboxMenuAttachment::build(const loka::app::MenuBarDefinition &menuBar, ToolboxMenuGraph &graph)
{
  const size_t menuCount = menuBar.menusCount();
  const loka::app::MenuItemDefinition *about = 0;
  const loka::app::MenuItemDefinition *quit = 0;
  const loka::app::MenuDefinition *file = 0;
  loka::dsl::CompositionCursor<loka::app::MenuDefinition> scan(menuBar.menusHead(), menuCount);
  for (loka::app::MenuDefinition *menuDef = scan.next(); menuDef; menuDef = scan.next())
  {
    if (!menuDef->isAppMenu)
    {
      if (!file && menuDef->title.equals(loka::core::String::Literal("File")))
        file = menuDef;
      continue;
    }
    for (const loka::app::MenuItemDefinition *item = menuDef->itemsHead(); item; item = item->nextInComposition)
    {
      if (item->hasChildren() || item->isSeparator)
        continue;
      if (!about && item->action == loka::app::MENU_ACTION_ABOUT_APP && item->isVisibleInitial())
        about = item;
      if (!quit && item->action == loka::app::MENU_ACTION_QUIT_APP && item->isVisibleInitial())
        quit = item;
    }
  }

  const unsigned char appleTitle[] = {1, 0x14};
  MenuHandle apple = graph.create(128, appleTitle);
  if (!apple)
    return false;
  // Leave desk-accessory rows exactly as the system supplies them, including their separator.
  AppendResMenu(apple, 'DRVR');
  if (about)
  {
    // Baseline includes the DA entries; insert About above them.
    if (!AddMenuItem(apple, kPlaceholder, 0))
      return false;
    ApplyMenuLeaf(&this->app_, apple, 128, 1, about, graph);
  }
  graph.recordInsertion(apple, 0);
  short nextMenuId = 129;
  if (quit && !file)
  {
    const unsigned char fileTitle[] = {4, 'F', 'i', 'l', 'e'};
    const short menuId = nextMenuId++;
    MenuHandle menu = graph.create(menuId, fileTitle);
    if (!menu)
      return false;
    if (!BuildMenuItems(&this->app_, menu, 0, menuId, nextMenuId, graph, true, quit))
      return false;
    graph.recordInsertion(menu, 0);
  }

  loka::dsl::CompositionCursor<loka::app::MenuDefinition> it(menuBar.menusHead(), menuCount);
  for (loka::app::MenuDefinition *menuDef = it.next(); menuDef; menuDef = it.next())
  {
    if (menuDef->isAppMenu)
    {
      continue;
    }
    Str255 title;
    ToolboxEncodePascal(menuDef->title, title);
    if (title[0] == 0)
    {
      ToolboxEncodePascal(loka::core::String::Literal("Menu"), title);
    }
    // Take this menu's ID before its submenus take theirs (they draw from the
    // same counter), so a submenu never shares its parent's ID.
    const short menuId = nextMenuId++;
    MenuHandle menu = graph.create(menuId, title);
    if (!menu)
      return false;
    if (!BuildMenuItems(
            &this->app_, menu, menuDef->itemsHead(), menuId, nextMenuId, graph, quit != 0, menuDef == file ? quit : 0))
      return false;
    if (CountMenuItems(menu) == 0)
    {
      graph.prune(menu);
      continue;
    }
    graph.recordInsertion(menu, 0);
  }
  return true;
}

bool ToolboxMenuAttachment::dispatch(short menuId, short item)
{
  if (menuId == 0 || item == 0)
    return false;
  for (size_t i = 0; i < this->graph_.commands.size(); ++i)
  {
    if (this->graph_.commands[i].menuId != menuId || this->graph_.commands[i].itemIndex != item)
      continue;
    // Emission may disconnect or replace this attachment. Keep the whole
    // command on the stack; never revisit the table after calling user code.
    const MenuCommand command = this->graph_.commands[i];
    switch (command.action)
    {
    case loka::app::MENU_ACTION_ABOUT_APP:
    case loka::app::MENU_ACTION_SHOW_COLOR_PICKER:
      SysBeep(1);
      return true;
    case loka::app::MENU_ACTION_QUIT_APP:
      this->app_.requestQuit();
      return true;
    case loka::app::MENU_ACTION_EDIT_UNDO:
    case loka::app::MENU_ACTION_EDIT_CUT:
    case loka::app::MENU_ACTION_EDIT_COPY:
    case loka::app::MENU_ACTION_EDIT_PASTE:
    case loka::app::MENU_ACTION_EDIT_SELECT_ALL:
    {
      loka::app::EditVerb verb;
      if (loka::app::menuEditVerb(command.action, verb))
        loka::app::dispatchEditMenuCommand(this->app_.activeWindow(), verb, command.emitter);
      return true;
    }
    case loka::app::MENU_ACTION_COUNT:
    case loka::app::MENU_ACTION_NONE:
      break;
    }
    if (command.emitter)
      command.emitter->emit();
    return true;
  }
  return false;
}

ToolboxApp::~ToolboxApp()
{
  this->retireComponents();
}

void ToolboxApp::projectMenu(Window *activeWindow, const loka::app::MenuSources &sources)
{
  if (activeWindow != this->activeWindow())
    return;
  if (this->menuAttachment_.project(sources) && activeWindow && activeWindow->asToolboxWindow())
    activeWindow->asToolboxWindow()->preserveNativeContentPositionAfterMenuBarChange();
}

void ToolboxApp::handleMenuSelection(short menuId, short item)
{
  if (!menuId || !item)
    return;
  if (!this->menuAttachment_.dispatch(menuId, item) && menuId == 128)
  {
    Str255 deskAccName;
    GetMenuItemText(GetMenuHandle(menuId), item, deskAccName);
    OpenDeskAcc(deskAccName);
  }
}

void ToolboxScenePlatformController::releaseMenu()
{
  ToolboxApp *app = this->window_ ? this->window_->toolboxApp() : 0;
  if (app)
    app->menuAttachment().releaseFrom(this->window_->scene());
}
