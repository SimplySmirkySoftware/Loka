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

  static NSString *MenuItemTitleFromString(const loka::core::String &title)
  {
    return MenuTitleFromString(title, "Menu Item");
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
  NSString *title = MenuItemTitleFromString(binding->titleState->get());
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
    const loka::app::MenuItemDefinition *itemDef, bool allowQuit)
{
  NSMenu *menu = (NSMenu *)nativeMenu;
  if (!itemDef)
    return 0;
  if (!itemDef->isVisibleInitial())
    return 0;
  if (!allowQuit && itemDef->action == loka::app::MENU_ACTION_QUIT_APP)
    return 0;
  if (itemDef->isSeparator)
  {
    [menu addItem:[NSMenuItem separatorItem]];
    return 1;
  }

  NSString *title = MenuItemTitleFromString(itemDef->titleState ? itemDef->titleState->get() : itemDef->title);
  NSString *shortcut = MenuShortcutForAction(itemDef);
  NSMenuItem *menuItem = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:shortcut];

  if (itemDef->hasChildren())
  {
    NSMenu *subMenu = [[NSMenu alloc] initWithTitle:title];
    if (this->BuildMenuItems(subMenu, itemDef->childrenHead(), allowQuit) == 0)
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

  [menu addItem:menuItem];
  [menuItem release];
  return 1;
}

std::size_t MacMenuAttachment::BuildMenuItems(void *nativeMenu,
    const loka::app::MenuItemDefinition *itemsHead, bool allowQuit)
{
  NSMenu *menu = (NSMenu *)nativeMenu;
  std::size_t added = 0;
  const loka::app::MenuItemDefinition *itemDef = itemsHead;
  while (itemDef)
  {
    added += this->BuildMenuItem(menu, itemDef, allowQuit);
    itemDef = itemDef->nextInComposition;
  }
  return added;
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
  bool hasAppMenu = false;
  loka::dsl::CompositionCursor<loka::app::MenuDefinition> appMenuScan(menuBar->menusHead(), menuBar->menusCount());
  for (loka::app::MenuDefinition *menuDef = appMenuScan.next(); menuDef; menuDef = appMenuScan.next())
  {
    if (menuDef->isAppMenu)
    {
      hasAppMenu = true;
      break;
    }
  }

  NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@""];
  loka::dsl::CompositionCursor<loka::app::MenuDefinition> it(menuBar->menusHead(), menuBar->menusCount());
  for (loka::app::MenuDefinition *menuDef = it.next(); menuDef; menuDef = it.next())
  {
    const char *fallback = menuDef->isAppMenu ? "Loka" : "Menu";
    NSString *menuTitle = MenuTitleFromString(menuDef->title, fallback);
    NSMenu *subMenu = [[NSMenu alloc] initWithTitle:menuTitle];
    bool allowQuit = !menuDef->isAppMenu;
    if (menuDef->isAppMenu)
    {
      allowQuit = true;
    }
    else if (hasAppMenu)
    {
      allowQuit = false;
    }
    if (this->BuildMenuItems(subMenu, menuDef->itemsHead(), allowQuit) == 0)
    {
      [subMenu release];
      continue;
    }

    NSMenuItem *menuItem = [[NSMenuItem alloc] initWithTitle:menuTitle action:nil keyEquivalent:@""];
    [menuItem setSubmenu:subMenu];
    [mainMenu addItem:menuItem];
    [menuItem release];
    [subMenu release];
  }
  this->menu_ = (void *)mainMenu;
  [NSApp setMainMenu:mainMenu];
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
