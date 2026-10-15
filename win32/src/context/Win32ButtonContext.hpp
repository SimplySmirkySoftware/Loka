#ifndef LOKA_WIN32_BUTTON_CONTEXT_HPP
#define LOKA_WIN32_BUTTON_CONTEXT_HPP

#include <windows.h>
#include "Win32RetirableContext.hpp"
#include "core/String.hpp"

namespace loka
{
  namespace core
  {
    template <typename T> class State;
    class EmitterState;
  } // namespace core
} // namespace loka

namespace loka
{
  namespace app
  {
    class ButtonNode;
    namespace scene
    {
      class PlatformNodeHandlerRegistry;
    }
  } // namespace app
} // namespace loka

class Win32ScenePlatformController;

class Win32ButtonContext : public Win32RetirableContext, public loka::app::scene::ICapturableBitmap
{
public:
  Win32ButtonContext(Win32ScenePlatformController *controller,
                     HWND parent,
                     int x,
                     int y,
                     int width,
                     int height,
                     loka::app::ButtonNode *node);
  virtual ~Win32ButtonContext();
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;
  virtual loka::app::scene::ICapturableBitmap *asCapturableBitmap()
  {
    return this;
  }
  virtual const loka::app::scene::ICapturableBitmap *asCapturableBitmap() const
  {
    return this;
  }
  virtual bool captureBitmap(loka::core::resource::Image &out) const;
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
  void bindEnabled();
  void unbindEnabled();
  void applyText();
  void applyEnabled();
  static void TextChangedThunk(void *userData);
  static void EnabledChangedThunk(void *userData);

  loka::app::ButtonNode *node_;
  loka::core::State<loka::core::String> *textState_;
  loka::core::State<bool> *enabledState_;
};

void RegisterWin32ButtonNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_WIN32_BUTTON_CONTEXT_HPP
