#ifndef LOKA_WIN32_POPUP_MENU_CONTEXT_HPP
#define LOKA_WIN32_POPUP_MENU_CONTEXT_HPP

#include <windows.h>
#include "Win32RetirableContext.hpp"
#include "app/nodes/controls/PopupMenu.hpp"

namespace loka
{
  namespace app
  {
    namespace scene
    {
      class PlatformNodeHandlerRegistry;
    }
  } // namespace app
} // namespace loka

class Win32ScenePlatformController;

class Win32PopupMenuContext : public Win32RetirableContext
{
public:
  Win32PopupMenuContext(Win32ScenePlatformController *controller,
                        HWND parent,
                        int x,
                        int y,
                        int width,
                        int height,
                        loka::app::PopupMenuNode *node);
  virtual ~Win32PopupMenuContext();
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);
  /** Attach-time read (late-subscriber rule): presentation from the current
      fact, called by the installing handler right after setContext. */
  void readLifecycleFactOnAttach();

  void relayout(int x, int y, int width, int height);

protected:
  virtual void onNativeFact(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);

private:
  friend class Win32InputDoor;
  bool handleCommand(WPARAM wParam, LPARAM lParam);
  void applyAttachedPresentation();
  void applyDetachedPresentation();
  void bindSelection();
  void unbindSelection();
  void bindEnabled();
  void unbindEnabled();
  bool itemsMatchApplied() const;
  void applyItems();
  int dropHeight() const;
  void applyDropGeometry();
  void applySelection();
  void applyEnabled();
  void syncStateFromControl();

  static void SelectionChangedThunk(void *userData);
  static void EnabledChangedThunk(void *userData);

  loka::app::PopupMenuNode *node_;
  loka::core::State<int> *selectionState_;
  loka::app::scene::WriteSeat<int> selectionSeat_;
  loka::core::State<bool> *enabledState_;
  loka::Vector<loka::core::String> appliedItems_;
  bool applyingFromState_;
  bool updatingFromControl_;
  int baseHeight_;
  int baseWidth_;
  /** Completed outcome of the latest native submission, never a future promise. */
  loka::app::scene::PaintAnswer controlDelivery_;
};

void RegisterWin32PopupMenuNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_WIN32_POPUP_MENU_CONTEXT_HPP
