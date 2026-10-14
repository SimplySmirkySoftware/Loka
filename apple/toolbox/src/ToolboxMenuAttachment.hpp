#ifndef LOKA_TOOLBOX_MENU_ATTACHMENT_HPP
#define LOKA_TOOLBOX_MENU_ATTACHMENT_HPP

#include "app/Menu.hpp"
#include "app/MenuProjectionRecord.hpp"
#include <Menus.h>
#include <vector>

class ToolboxApp;
namespace loka { namespace app { namespace scene { class Scene; } } }

/** Owns the global Classic menu projection. Scene detach revokes all endpoint
    borrows synchronously; native handles remain inert until replacement/disposal. */
class ToolboxMenuAttachment
{
public:
  explicit ToolboxMenuAttachment(ToolboxApp &app);
  ~ToolboxMenuAttachment();
  /** Called per admission offer, active window only. Unchanged input costs
      three pointer comparisons, no allocations or row walks. Changed input
      merges once and fully rebuilds the merged bar's rows. Returns true when
      native menus were projected; merge refusal preserves native and record. */
  bool project(const loka::app::MenuSources &sources);
  void disconnect();
  void releaseFrom(const loka::app::scene::Scene *scene);
  bool dispatch(short menuId, short item);

private:
  ToolboxMenuAttachment(const ToolboxMenuAttachment &);
  ToolboxMenuAttachment &operator=(const ToolboxMenuAttachment &);
  struct MenuCommand
  {
    short menuId;
    short itemIndex;
    loka::app::MenuActionType action;
    loka::core::EmitterState *emitter;
  };
  struct MenuBinding
  {
    ToolboxApp *app;
    MenuHandle menu;
    short itemIndex;
    loka::core::State<loka::core::String> *titleState;
    loka::core::State<bool> *enabledState;
    bool invertEnabled;
    loka::core::State<bool> *checkedState;
  };
  static void MenuTitleChangedThunk(void *userData);
  static void MenuEnabledChangedThunk(void *userData);
  static void MenuCheckedChangedThunk(void *userData);
  static void ApplyMenuItemStates(ToolboxApp *app, MenuHandle menu, short itemIndex,
                                 const loka::app::MenuItemDefinition *item,
                                 std::vector<MenuBinding *> &bindings);
  static void BuildMenuItems(ToolboxApp *app, MenuHandle menu,
                            const loka::app::MenuItemDefinition *items, short menuId,
                            short &nextMenuId, std::vector<MenuCommand> &commands,
                            std::vector<MenuBinding *> &bindings,
                            std::vector<MenuHandle> &hierarchicalMenus);
  void clearMenuBindings();
  void resetMenuState();
  void disposeMenuEntries();
  void disposeHierarchicalMenus();

  ToolboxApp &app_;
  short nextMenuId_;
  std::vector<MenuCommand> commands_;
  std::vector<MenuBinding *> bindings_;
  std::vector<MenuHandle> menuEntries_;
  std::vector<MenuHandle> hierarchicalMenus_;
  loka::app::MenuProjectionRecord record_;
};
#endif
