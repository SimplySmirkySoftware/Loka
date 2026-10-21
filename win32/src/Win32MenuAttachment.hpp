#ifndef LOKA_WIN32_MENU_ATTACHMENT_HPP
#define LOKA_WIN32_MENU_ATTACHMENT_HPP

#include "app/Menu.hpp"
#include "app/MenuProjectionRecord.hpp"
#include <windows.h>
#include <vector>
#include <string>

class Win32Window;
namespace loka { namespace app { namespace scene { class Scene; } } }

/** Owns one Window's native menu, accelerator table, and endpoint borrows. Scene detach makes
    the installed menu inert until replacement or native window destruction. */
class Win32MenuAttachment
{
public:
  explicit Win32MenuAttachment(Win32Window &window);
  ~Win32MenuAttachment();
  /** Refusal preserves the installed projection for retry. Unchanged identity
      skips merge and native work; success commits the offered identity. */
  enum ProjectResult
  {
    PROJECT_APPLIED,
    PROJECT_UNCHANGED,
    PROJECT_REFUSED
  };
  ProjectResult project(const loka::app::MenuSources &sources);
  void disconnect();
  void releaseFrom(const loka::app::scene::Scene *source);
  bool dispatch(int commandId);
  /** Consumes a declared shortcut through this Window's native command route. */
  bool translateAccelerator(MSG &msg);

private:
  friend class Win32Window;
  Win32MenuAttachment(const Win32MenuAttachment &);
  Win32MenuAttachment &operator=(const Win32MenuAttachment &);
  enum DetachMode
  {
    DETACH_PRESERVING_CONTENT_FRAME,
    DETACH_FOR_TEARDOWN
  };
  bool reset(DetachMode mode);
  bool resetForTeardown();
  struct MenuCommand
  {
    int commandId;
    loka::app::MenuActionType action;
    loka::core::EmitterState *emitter;
  };

  struct MenuBinding
  {
    explicit MenuBinding(const std::wstring &suffix) : shortcutSuffix(suffix) {}
    const std::wstring shortcutSuffix;
    const wchar_t *emptyTitle;
    loka::core::State<loka::core::String> *titleState;
    HMENU menu;
    // Win32 addresses a menu item either by command id or by position: a
    // leaf carries its command id with MF_BYCOMMAND, a popup title has no
    // command and carries its appended index with MF_BYPOSITION.
    UINT item;
    UINT byFlags;
    HWND hwnd;
    loka::core::State<bool> *enabledState;
    bool invertEnabled;
    loka::core::State<bool> *checkedState;
  };

  void clearMenuBindings();
  static void MenuTitleChangedThunk(void *userData);
  static void MenuEnabledChangedThunk(void *userData);
  static void MenuCheckedChangedThunk(void *userData);
  void bindMenuItemStates(HMENU menu,
                          UINT item,
                          UINT byFlags,
                          const loka::app::MenuItemDefinition *itemDef,
                          HWND hwnd, const std::wstring &shortcutSuffix);
  bool buildMenuItem(HMENU menu,
                     const loka::app::MenuItemDefinition *itemDef,
                     HWND hwnd,
                     std::vector<ACCEL> &accelerators,
                     bool suppressQuit,
                     bool &pendingSeparator);
  bool buildMenuItems(HMENU menu,
                      const loka::app::MenuItemDefinition *itemsHead,
                      HWND hwnd,
                      std::vector<ACCEL> &accelerators,
                      bool suppressQuit,
                      const loka::app::MenuItemDefinition *tail = 0);

  Win32Window &window_;
  HMENU menu_;
  HACCEL accel_;
  int nextCommandId_;
  std::vector<MenuCommand> commands_;
  std::vector<MenuBinding *> bindings_;
  loka::app::MenuProjectionRecord record_;
};
#endif
