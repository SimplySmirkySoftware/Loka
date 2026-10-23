#include "MacMenuAttachment.hpp"
#include "app/MenuComposition.hpp"
#include "core/util/OwnedDef.hpp"
#include "MacApp.hpp"
#include "MacObjCCompat.hpp"
#include "core/Operation.hpp"
#include "platform/StringUTF8.hpp"
#include <AppKit/AppKit.h>
#include <cassert>

@interface LokaMenuTarget : NSObject
{
  MacApp *owner_;
}
@property(nonatomic, assign) MacApp *owner;
@end

@implementation LokaMenuTarget
@synthesize owner = owner_;
- (void)handleMenuAction:(id)sender
{
  if (self.owner)
  {
    loka::core::Operation turn;
    NSInteger tag = [sender tag];
    self.owner->dispatchNativeMenuCommand(static_cast<int>(tag));
    turn.close();
  }
}
@end

namespace
{
  static NSString *MenuTitleFromString(const loka::core::String &title, const char *fallback)
  {
    std::string utf8;
    if (loka::platform::CollectUtf8(title, utf8) && !utf8.empty())
    {
      return [NSString stringWithUTF8String:utf8.c_str()];
    }
    return [NSString stringWithUTF8String:fallback];
  }

  // Deliberate platform-seam twin of Classic (#1535) and Win32 (#1536).
  static const char *MenuEmptyTitle(loka::app::MenuActionType action)
  {
    if (action == loka::app::MENU_ACTION_ABOUT_APP)
      return "About";
    if (action == loka::app::MENU_ACTION_QUIT_APP)
      return "Quit";
    return "Menu Item";
  }

  // AppKit can retain an item after its graph leaves the main menu. Unlike
  // Toolbox, clearing command lookup alone cannot revoke that native callback.
  static void StripMenuActions(NSMenu *menu)
  {
    for (NSInteger i = 0; i < [menu numberOfItems]; ++i)
    {
      NSMenuItem *item = [menu itemAtIndex:i];
      [item setTarget:nil];
      [item setAction:NULL];
      [item setTag:0];
      StripMenuActions([item submenu]);
    }
  }
}

MacMenuAttachment::MacMenuAttachment(MacApp &app)
    : app_(app), nextCommandId_(1), target_(0), menu_(0), record_()
{
  LokaMenuTarget *target = [[LokaMenuTarget alloc] init];
  [target setOwner:&app];
  this->target_ = (void *)target;
  assert(this->target_);
}

MacMenuAttachment::~MacMenuAttachment()
{
  this->reset();
  [(LokaMenuTarget *)this->target_ setOwner:0];
  [(id)this->target_ release];
}

void MacMenuAttachment::disconnect()
{
  this->clearMenuBindings();
  this->record_.clear();
  StripMenuActions((NSMenu *)this->menu_);
}

void MacMenuAttachment::releaseFrom(const loka::app::scene::Scene *scene)
{
  if (this->record_.release(scene))
  {
    this->clearMenuBindings();
    StripMenuActions((NSMenu *)this->menu_);
  }
}

void MacMenuAttachment::reset()
{
  this->disconnect();
  if (!this->menu_)
    return;
  if ([NSApp mainMenu] == (NSMenu *)this->menu_)
    [NSApp setMainMenu:nil];
  [(id)this->menu_ release];
  this->menu_ = 0;
}

// Deliberately mirrors the Toolbox title binding at the native projection seam.
void MacMenuAttachment::MenuTitleChangedThunk(void *userData)
{
  MacMenuAttachment::MenuBinding *binding = static_cast<MacMenuAttachment::MenuBinding *>(userData);
  if (!binding || !binding->menuItem || !binding->titleState)
    return;
  NSMenuItem *item = (NSMenuItem *)binding->menuItem;
  NSString *title = MenuTitleFromString(binding->titleState->get(), binding->emptyTitle);
  [item setTitle:title];
  [[item submenu] setTitle:title];
}

void MacMenuAttachment::MenuEnabledChangedThunk(void *userData)
{
  MacMenuAttachment::MenuBinding *binding = static_cast<MacMenuAttachment::MenuBinding *>(userData);
  if (!binding || !binding->menuItem || !binding->enabledState)
    return;
  NSMenuItem *item = (NSMenuItem *)binding->menuItem;
  bool enabled = binding->enabledState->get();
  if (binding->invertEnabled)
  {
    enabled = !enabled;
  }
  [item setEnabled:enabled ? YES : NO];
}

void MacMenuAttachment::MenuCheckedChangedThunk(void *userData)
{
  MacMenuAttachment::MenuBinding *binding = static_cast<MacMenuAttachment::MenuBinding *>(userData);
  if (!binding || !binding->menuItem || !binding->checkedState)
    return;
  NSMenuItem *item = (NSMenuItem *)binding->menuItem;
  [item setState:binding->checkedState->get() ? LOKA_MAC_CONTROL_STATE_ON : LOKA_MAC_CONTROL_STATE_OFF];
}

void MacMenuAttachment::clearMenuBindings()
{
  for (size_t i = 0; i < this->bindings_.size(); ++i)
  {
    MenuBinding *binding = this->bindings_[i];
    if (binding && binding->titleState)
    {
      binding->titleState->deferUnbind(&MacMenuAttachment::MenuTitleChangedThunk, binding);
    }
    if (binding && binding->enabledState)
    {
      binding->enabledState->deferUnbind(&MacMenuAttachment::MenuEnabledChangedThunk, binding);
    }
    if (binding && binding->checkedState)
    {
      binding->checkedState->deferUnbind(&MacMenuAttachment::MenuCheckedChangedThunk, binding);
    }
    delete binding;
  }
  this->bindings_.clear();
  this->commands_.clear();
  this->nextCommandId_ = 1;
}

static NSString *MenuShortcutForAction(const loka::app::MenuItemDefinition *itemDef)
{
  if (!itemDef)
    return @"";
  if (itemDef->hasShortcut && itemDef->shortcutKey)
  {
    char buf[2] = {itemDef->shortcutKey, 0};
    return [NSString stringWithUTF8String:buf];
  }
  switch (itemDef->action)
  {
  case loka::app::MENU_ACTION_QUIT_APP:
    return @"q";
  case loka::app::MENU_ACTION_ABOUT_APP:
  case loka::app::MENU_ACTION_SHOW_COLOR_PICKER:
  case loka::app::MENU_ACTION_NONE:
    return @"";
  }
  return @"";
}

std::size_t MacMenuAttachment::BuildMenuItem(void *nativeMenu,
                                             const loka::app::MenuItemDefinition *itemDef,
                                             bool suppressQuit,
                                             bool &pendingSeparator)
{
  NSMenu *menu = (NSMenu *)nativeMenu;
  NSString *title = MenuTitleFromString(itemDef->titleState ? itemDef->titleState->get() : itemDef->title,
                                        MenuEmptyTitle(itemDef->action));
  NSString *shortcut = MenuShortcutForAction(itemDef);
  NSMenuItem *menuItem = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:shortcut];

  if (itemDef->hasChildren())
  {
    NSMenu *subMenu = [[NSMenu alloc] initWithTitle:title];
    if (this->BuildMenuItems(subMenu, itemDef->childrenHead(), suppressQuit) == 0)
    {
      [subMenu release];
      [menuItem release];
      return 0;
    }
    [menuItem setSubmenu:subMenu];
    [subMenu release];
  }
  else
  {
    [menuItem setTarget:(id)this->target_];
    [menuItem setAction:@selector(handleMenuAction:)];
    int commandId = this->nextCommandId_++;
    [menuItem setTag:commandId];
    MacMenuAttachment::MenuCommand command;
    command.commandId = commandId;
    command.action = itemDef->action;
    command.emitter = itemDef->onClickState;
    this->commands_.push_back(command);
  }

  if (!itemDef->isEnabledInitial())
  {
    [menuItem setEnabled:NO];
  }
  [menuItem setState:itemDef->isCheckedInitial() ? LOKA_MAC_CONTROL_STATE_ON : LOKA_MAC_CONTROL_STATE_OFF];

  loka::core::State<bool> *enabledBindingState = itemDef->enabledBindingState();
  loka::core::State<bool> *checkedBindingState = itemDef->checkedBindingState();
  if (itemDef->titleState || enabledBindingState || checkedBindingState)
  {
    if (enabledBindingState)
    {
      [menuItem setEnabled:itemDef->isEnabledInitial() ? YES : NO];
    }
    MacMenuAttachment::MenuBinding *binding = new MacMenuAttachment::MenuBinding();
    binding->menuItem = (void *)menuItem;
    binding->emptyTitle = MenuEmptyTitle(itemDef->action);
    binding->titleState = itemDef->titleState;
    binding->enabledState = enabledBindingState;
    binding->invertEnabled = itemDef->enabledBindingInvert();
    binding->checkedState = checkedBindingState;
    if (binding->titleState)
    {
      binding->titleState->deferBind(&MacMenuAttachment::MenuTitleChangedThunk, binding);
    }
    if (enabledBindingState)
    {
      enabledBindingState->deferBind(&MacMenuAttachment::MenuEnabledChangedThunk, binding);
    }
    if (checkedBindingState)
    {
      checkedBindingState->deferBind(&MacMenuAttachment::MenuCheckedChangedThunk, binding);
    }
    this->bindings_.push_back(binding);
  }

  if (pendingSeparator && [menu numberOfItems] > 0)
    [menu addItem:[NSMenuItem separatorItem]];
  pendingSeparator = false;
  [menu addItem:menuItem];
  [menuItem release];
  return 1;
}

// Both menu builders call this once per unselected row. Filtering and pending
// separators share one door; Color Picker uses the existing item payload path.
void MacMenuAttachment::BuildMenuRow(void *nativeMenu,
                                     const loka::app::MenuItemDefinition *item,
                                     bool suppressQuit,
                                     bool &pendingSeparator)
{
  if (!item->isVisibleInitial())
    return;
  if (item->isSeparator)
  {
    pendingSeparator = true;
    return;
  }
  if (suppressQuit && !item->hasChildren() && item->action == loka::app::MENU_ACTION_QUIT_APP)
    return;
  this->BuildMenuItem(nativeMenu, item, suppressQuit, pendingSeparator);
}

// Called once per ordinary menu or submenu during a changed projection. Walks
// only that menu's rows; recursive calls own their pending separator.
std::size_t
MacMenuAttachment::BuildMenuItems(void *nativeMenu, const loka::app::MenuItemDefinition *itemsHead, bool suppressQuit)
{
  bool pendingSeparator = false;
  for (const loka::app::MenuItemDefinition *item = itemsHead; item; item = item->nextInComposition)
    this->BuildMenuRow(nativeMenu, item, suppressQuit, pendingSeparator);
  return static_cast<std::size_t>([(NSMenu *)nativeMenu numberOfItems]);
}

// Called once for the merged app container per changed projection. Selection
// and middle construction each walk its rows; selected leaves bypass the row
// helper so their visibility is read only during selection.
std::size_t
MacMenuAttachment::BuildAppMenu(void *nativeMenu, const loka::app::MenuItemDefinition *itemsHead, bool &quitPlaced)
{
  NSMenu *menu = (NSMenu *)nativeMenu;
  const loka::app::MenuItemDefinition *about = 0;
  const loka::app::MenuItemDefinition *quit = 0;
  for (const loka::app::MenuItemDefinition *item = itemsHead; item; item = item->nextInComposition)
  {
    if (item->isSeparator || item->hasChildren())
      continue;
    if (!about && item->action == loka::app::MENU_ACTION_ABOUT_APP && item->isVisibleInitial())
      about = item;
    if (!quit && item->action == loka::app::MENU_ACTION_QUIT_APP && item->isVisibleInitial())
      quit = item;
  }

  bool pendingSeparator = false;
  if (about)
    this->BuildMenuItem(menu, about, false, pendingSeparator);

  pendingSeparator = [menu numberOfItems] > 0;
  for (const loka::app::MenuItemDefinition *item = itemsHead; item; item = item->nextInComposition)
  {
    if (item != about && item != quit)
      this->BuildMenuRow(menu, item, false, pendingSeparator);
  }

  pendingSeparator = [menu numberOfItems] > 0;
  if (quit)
    this->BuildMenuItem(menu, quit, false, pendingSeparator);
  quitPlaced = quit != 0;
  return static_cast<std::size_t>([menu numberOfItems]);
}

bool MacMenuAttachment::project(const loka::app::MenuSources &sources)
{
  if (sources.absent())
  {
    this->record_.clear();
    // An absent offer with no installed graph is unchanged.
    if (!this->menu_)
      return false;
    this->reset();
    return true;
  }
  if (this->record_.unchanged(sources))
    return false;
  loka::core::OwnedDef<loka::app::MenuBarDefinition> merged(
      loka::app::MergeMenuBars(sources.base, sources.overlay));
  if (!merged.isSet())
    return false;
  const loka::app::MenuBarDefinition *menuBar = merged.get();
  this->reset();
  NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@""];
  NSMenu *appMenu = nil;
  bool quitPlaced = false;
  // Two bounded passes over the merged bar put the app container first without
  // changing the declaration order of ordinary menus or retaining definitions.
  for (int pass = 0; pass < 2; ++pass)
  {
    loka::dsl::CompositionCursor<loka::app::MenuDefinition> it(menuBar->menusHead(), menuBar->menusCount());
    for (loka::app::MenuDefinition *menuDef = it.next(); menuDef; menuDef = it.next())
    {
      if (menuDef->isAppMenu != (pass == 0))
        continue;
      const char *fallback = menuDef->isAppMenu ? "Loka" : "Menu";
      NSString *menuTitle = MenuTitleFromString(menuDef->title, fallback);
      NSMenu *subMenu = [[NSMenu alloc] initWithTitle:menuTitle];
      const std::size_t added = menuDef->isAppMenu ? this->BuildAppMenu(subMenu, menuDef->itemsHead(), quitPlaced)
                                                   : this->BuildMenuItems(subMenu, menuDef->itemsHead(), quitPlaced);
      if (added == 0)
      {
        [subMenu release];
        continue;
      }
      NSMenuItem *menuItem = [[NSMenuItem alloc] initWithTitle:menuTitle action:nil keyEquivalent:@""];
      [menuItem setSubmenu:subMenu];
      [mainMenu addItem:menuItem];
      if (menuDef->isAppMenu)
        appMenu = subMenu;
      [menuItem release];
      [subMenu release];
    }
  }
  this->menu_ = (void *)mainMenu;
  [NSApp setMainMenu:mainMenu];
  // A main menu built in code names its application menu explicitly. Legacy
  // AppKit (observed on 10.4.11) otherwise synthesizes its own (process name,
  // Quit) and shows the first menu beside it under its own title (#1565);
  // Tahoe takes the first submenu either way. setAppleMenu: is unpublished, so it is sent by
  // selector and skipped where the running AppKit lacks it.
  if (appMenu && [NSApp respondsToSelector:@selector(setAppleMenu:)])
    [NSApp performSelector:@selector(setAppleMenu:) withObject:appMenu];
  this->record_.commit(sources);
  return true;
}

bool MacMenuAttachment::dispatch(int commandId)
{
  for (size_t i = 0; i < this->commands_.size(); ++i)
  {
    if (this->commands_[i].commandId != commandId)
      continue;
    // Emission may disconnect or replace the attachment. Never revisit its table.
    const MenuCommand command = this->commands_[i];
    switch (command.action)
    {
    case loka::app::MENU_ACTION_ABOUT_APP:
      [NSApp orderFrontStandardAboutPanel:nil];
      return true;
    case loka::app::MENU_ACTION_SHOW_COLOR_PICKER:
      [NSApp orderFrontColorPanel:nil];
      return true;
    case loka::app::MENU_ACTION_QUIT_APP:
      this->app_.requestQuit();
      return true;
    case loka::app::MENU_ACTION_NONE:
      break;
    }
    if (command.emitter)
    {
      command.emitter->emit();
      // State writes/adoptions leave pending invalidation for handleFlush's App clock.
      // Keep replacement and relayout out of this native menu callback.
    }
    return true;
  }
  return false;
}
