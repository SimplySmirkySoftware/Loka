#ifndef LOKA_WIN32_EDIT_TEXT_CONTEXT_HPP
#define LOKA_WIN32_EDIT_TEXT_CONTEXT_HPP

#include <windows.h>
#include "Win32RetirableContext.hpp"
#include "core/String.hpp"
#include "app/scene/state/WriteSeat.hpp"

namespace loka
{
  namespace core
  {
    template <typename T> class State;
    template <typename T> class MutableState;
  } // namespace core
} // namespace loka

namespace loka
{
  namespace app
  {
    class EditTextNode;
    namespace scene
    {
      class PlatformNodeHandlerRegistry;
    }
  } // namespace app
} // namespace loka

class Win32ScenePlatformController;

class Win32EditTextContext : public Win32RetirableContext
{
public:
  Win32EditTextContext(Win32ScenePlatformController *controller,
                       HWND parent,
                       int x,
                       int y,
                       int width,
                       int height,
                       loka::app::EditTextNode *node);
  virtual ~Win32EditTextContext();
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
  void bindText();
  void unbindText();
  void applyText();
  void syncStateFromControl();
  static void TextChangedThunk(void *userData);

  loka::app::EditTextNode *node_;
  loka::core::State<loka::core::String> *textState_;
  loka::app::scene::WriteSeat<loka::core::String> textSeat_;
  bool applyingFromState_;
  bool updatingFromControl_;
  /** Completed outcome of the latest native submission, never a future promise. */
  loka::app::scene::PaintAnswer textDelivery_;
};

void RegisterWin32EditTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_WIN32_EDIT_TEXT_CONTEXT_HPP
