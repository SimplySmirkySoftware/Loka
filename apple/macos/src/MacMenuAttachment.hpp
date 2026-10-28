#ifndef LOKA_MAC_MENU_ATTACHMENT_HPP
#define LOKA_MAC_MENU_ATTACHMENT_HPP

#include "app/Menu.hpp"
#include "app/MenuProjectionRecord.hpp"
#include <vector>

#ifdef __OBJC__
@class NSMenuItem;
#else
class NSMenuItem;
#endif

class MacApp;
namespace loka { namespace app { namespace scene { class Scene; } } }

/** Owns the global AppKit menu projection and its revocable endpoint borrows.
    Scene detach leaves installed and tracking-retained native items inert. */
class MacMenuAttachment
{
public:
  explicit MacMenuAttachment(MacApp &app);
  ~MacMenuAttachment();
  /** Called per admission offer, active window only. Unchanged input costs
      three pointer comparisons, no allocations or row walks. Changed input
      merges once and fully rebuilds the merged bar's rows. Returns true when
      native menus were projected; merge refusal preserves native and record. */
  bool project(const loka::app::MenuSources &sources);
  void disconnect();
  void releaseFrom(const loka::app::scene::Scene *scene);
  bool dispatch(int commandId);
  bool dispatch(int commandId, void *nativeItem);
  bool validate(NSMenuItem *item);

private:
  MacMenuAttachment(const MacMenuAttachment &);
  MacMenuAttachment &operator=(const MacMenuAttachment &);
  struct MenuCommand
  {
    int commandId;
    loka::app::MenuActionType action;
    loka::core::EmitterState *emitter;
    bool declaredEnabled;
  };
  struct MenuBinding
  {
    MacMenuAttachment *owner;
    void *menuItem;
    const char *emptyTitle;
    loka::core::State<loka::core::String> *titleState;
    loka::core::State<bool> *enabledState;
    bool invertEnabled;
    loka::core::State<bool> *checkedState;
  };
  /** The only writer of declaredEnabled. Stores the declaration on the row
      (when the item has one) and projects it to the native item. */
  void applyDeclaredEnabled(NSMenuItem *item, MenuCommand *row, bool enabled);
  MenuCommand *rowForTag(int tag);
  static void MenuTitleChangedThunk(void *userData);
  static void MenuEnabledChangedThunk(void *userData);
  static void MenuCheckedChangedThunk(void *userData);
  std::size_t
  BuildMenuItem(void *menu, const loka::app::MenuItemDefinition *item, bool suppressQuit, bool &pendingSeparator);
  void BuildMenuRow(void *menu, const loka::app::MenuItemDefinition *item, bool suppressQuit, bool &pendingSeparator);
  std::size_t BuildMenuItems(void *menu, const loka::app::MenuItemDefinition *items, bool suppressQuit);
  std::size_t BuildAppMenu(void *menu, const loka::app::MenuItemDefinition *items, bool &quitPlaced);
  void clearMenuBindings();
  void reset();

  MacApp &app_;
  int nextCommandId_;
  std::vector<MenuCommand> commands_;
  std::vector<MenuBinding *> bindings_;
  void *target_;
  void *menu_;
  loka::app::MenuProjectionRecord record_;
};
#endif
