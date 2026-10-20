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
  Str255 title;
  ToolboxEncodePascal(binding->titleState->get(), title);
  SetMenuItemText(binding->menu, binding->itemIndex, title);
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
  Str255 title;
  ToolboxEncodePascal(itemDef->titleState ? itemDef->titleState->get() : itemDef->title, title);
  SetMenuItemText(menu, itemIndex, title);

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
  case loka::app::MENU_ACTION_QUIT_APP:
    return 'Q';
  case loka::app::MENU_ACTION_ABOUT_APP:
  case loka::app::MENU_ACTION_SHOW_COLOR_PICKER:
  case loka::app::MENU_ACTION_NONE:
    return 0;
  }
  return 0;
}

bool ToolboxMenuAttachment::BuildMenuItems(ToolboxApp *app,
                                           MenuHandle menu,
                                           const loka::app::MenuItemDefinition *itemsHead,
                                           short menuId,
                                           short &nextMenuId,
                                           ToolboxMenuGraph &graph)
{
  const loka::app::MenuItemDefinition *itemDef = itemsHead;
  while (itemDef)
  {
    if (!itemDef->isVisibleInitial())
    {
      itemDef = itemDef->nextInComposition;
      continue;
    }
    if (itemDef->isSeparator)
    {
      const short before = CountMenuItems(menu);
      AppendMenu(menu, kSeparator);
      if (CountMenuItems(menu) != before + 1)
        return false;
      itemDef = itemDef->nextInComposition;
      continue;
    }
    if (itemDef->action == loka::app::MENU_ACTION_SHOW_COLOR_PICKER)
    {
      itemDef = itemDef->nextInComposition;
      continue;
    }
    const short before = CountMenuItems(menu);
    AppendMenu(menu, kPlaceholder);
    if (CountMenuItems(menu) != before + 1)
      return false;
    short itemIndex = CountMenuItems(menu);
    if (itemDef->hasChildren())
    {
      Str255 title;
      ToolboxEncodePascal(itemDef->title, title);
      if (nextMenuId > 235)
        return false;
      short subMenuId = nextMenuId++;
      MenuHandle subMenu = graph.create(subMenuId, title);
      if (!subMenu)
        return false;
      if (!BuildMenuItems(app, subMenu, itemDef->childrenHead(), subMenuId, nextMenuId, graph))
        return false;
      if (CountMenuItems(subMenu) == 0)
      {
        graph.prune(subMenu);
        DeleteMenuItem(menu, itemIndex);
        itemDef = itemDef->nextInComposition;
        continue;
      }
      graph.recordInsertion(subMenu, kInsertHierarchicalMenu);
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
      itemDef = itemDef->nextInComposition;
      continue;
    }
    const char key = MenuShortcutForAction(itemDef);
    if (key)
      SetItemCmd(menu, itemIndex, key);
    ToolboxMenuAttachment::MenuCommand command;
    command.menuId = menuId;
    command.itemIndex = itemIndex;
    command.action = itemDef->action;
    command.emitter = itemDef->onClickState;
    graph.commands.push_back(command);
    ApplyMenuItemStates(app, menu, itemIndex, itemDef, graph.bindings);
    itemDef = itemDef->nextInComposition;
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
  short nextMenuId = 128;
  bool hasAppMenu = false;
  loka::dsl::CompositionCursor<loka::app::MenuDefinition> appMenuScan(menuBar.menusHead(), menuCount);
  for (loka::app::MenuDefinition *menuDef = appMenuScan.next(); menuDef; menuDef = appMenuScan.next())
  {
    if (menuDef->isAppMenu)
    {
      hasAppMenu = true;
      break;
    }
  }

  if (hasAppMenu)
  {
    Str255 title;
    title[0] = 1;
    title[1] = 0x14;
    MenuHandle menu = graph.create(128, title);
    if (!menu)
      return false;
    std::vector<const loka::app::MenuItemDefinition *> aboutItems;
    loka::dsl::CompositionCursor<loka::app::MenuDefinition> appMenuIt(menuBar.menusHead(), menuCount);
    for (loka::app::MenuDefinition *menuDef = appMenuIt.next(); menuDef; menuDef = appMenuIt.next())
    {
      if (!menuDef->isAppMenu)
        continue;
      const loka::app::MenuItemDefinition *itemDef = menuDef->itemsHead();
      while (itemDef)
      {
        if (itemDef->action == loka::app::MENU_ACTION_ABOUT_APP)
        {
          if (itemDef->isVisibleInitial())
          {
            aboutItems.push_back(itemDef);
          }
        }
        itemDef = itemDef->nextInComposition;
      }
    }
    if (!aboutItems.empty())
    {
      AppendResMenu(menu, 'DRVR');
      const short before = CountMenuItems(menu);
      const loka::app::MenuItemDefinition *itemDef = aboutItems[0];
      InsertMenuItem(menu, kPlaceholder, 0);
      if (CountMenuItems(menu) != before + 1)
        return false;
      short aboutIndex = 1;
      // Separator not needed; desk accessories already have one.
      ToolboxMenuAttachment::MenuCommand command;
      command.menuId = 128;
      command.itemIndex = aboutIndex;
      command.action = itemDef->action;
      command.emitter = itemDef->onClickState;
      graph.commands.push_back(command);
      ApplyMenuItemStates(&this->app_, menu, aboutIndex, itemDef, graph.bindings);
      graph.recordInsertion(menu, 0);
    }
    else
      graph.prune(menu);
    nextMenuId = 129;
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
    if (!BuildMenuItems(&this->app_, menu, menuDef->itemsHead(), menuId, nextMenuId, graph))
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
