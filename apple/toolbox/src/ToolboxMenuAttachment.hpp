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
      native menus were projected; build or merge refusal preserves native and record. */
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
    const unsigned char *emptyTitle;
    loka::core::State<bool> *checkedState;
  };
  /** Owns each native handle once; insertion rows only borrow those handles.
      Build-local until install, then attachment-owned. Never clears the bar. */
  class ToolboxMenuGraph
  {
  public:
    ToolboxMenuGraph() {}
    ~ToolboxMenuGraph();
    MenuHandle create(short id, const unsigned char *title);
    void prune(MenuHandle menu);
    void recordInsertion(MenuHandle menu, short mode);
    void install() const;
    bool empty() const
    {
      return this->insertions_.empty();
    }
    void disconnect();
    void swap(ToolboxMenuGraph &other);
    std::vector<MenuCommand> commands;
    std::vector<MenuBinding *> bindings;

  private:
    ToolboxMenuGraph(const ToolboxMenuGraph &);
    ToolboxMenuGraph &operator=(const ToolboxMenuGraph &);
    struct Insertion
    {
      MenuHandle menu;
      short mode;
    };
    std::vector<MenuHandle> owned_;
    std::vector<Insertion> insertions_;
  };
  static void MenuTitleChangedThunk(void *userData);
  static void MenuEnabledChangedThunk(void *userData);
  static void MenuCheckedChangedThunk(void *userData);
  static void ApplyMenuItemStates(ToolboxApp *app, MenuHandle menu, short itemIndex,
                                 const loka::app::MenuItemDefinition *item,
                                 std::vector<MenuBinding *> &bindings);
  static void ApplyMenuLeaf(ToolboxApp *app,
                            MenuHandle menu,
                            short menuId,
                            short itemIndex,
                            const loka::app::MenuItemDefinition *item,
                            ToolboxMenuGraph &graph);
  static bool BuildMenuItems(ToolboxApp *app,
                             MenuHandle menu,
                             const loka::app::MenuItemDefinition *items,
                             short menuId,
                             short &nextMenuId,
                             ToolboxMenuGraph &graph,
                             bool suppressQuit,
                             const loka::app::MenuItemDefinition *tail = 0);
  bool build(const loka::app::MenuBarDefinition &menuBar, ToolboxMenuGraph &graph);

  ToolboxApp &app_;
  ToolboxMenuGraph graph_;
  loka::app::MenuProjectionRecord record_;
};
#endif
