#ifndef LOKA_WIN32_CELL_CONTEXT_HPP
#define LOKA_WIN32_CELL_CONTEXT_HPP

#include <windows.h>
#include "Win32RetirableContext.hpp"
#include "core/String.hpp"
#include <string>

namespace loka
{
  namespace core
  {
    template <typename T> class State;
  }
} // namespace loka

namespace loka
{
  namespace app
  {
    class CellNode;
    namespace scene
    {
      class PlatformNodeHandlerRegistry;
    }
  } // namespace app
} // namespace loka

namespace loka
{
  namespace testing
  {
    class Win32CellPaintAccess;
  }
}

class Win32ScenePlatformController;

class Win32CellContext : public Win32RetirableContext
{
public:
  Win32CellContext(Win32ScenePlatformController *controller,
                   HWND parent,
                   int x,
                   int y,
                   int width,
                   int height,
                   loka::app::CellNode *node);
  virtual ~Win32CellContext();
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);
  /** Attach-time read (late-subscriber rule): presentation from the current
      fact, called by the installing handler right after setContext. */
  void readLifecycleFactOnAttach();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                             loka::app::scene::NodeLifecycleFact next);
  void relayout(int x, int y, int width, int height);

private:
  friend class Win32InputDoor;
  friend class loka::testing::Win32CellPaintAccess;
  void handleClick();
  void applyAttachedPresentation();
  void applyDetachedPresentation();
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  static void EnsureClassRegistered();
  void bindText();
  void unbindText();
  void applyText();
  void drawCell(HDC hdc, const RECT &rect);
  static void TextChangedThunk(void *userData);

  loka::app::CellNode *node_;
  HWND hwnd_;
  loka::core::State<loka::core::String> *textState_;
  std::wstring text_;
};

void RegisterWin32CellNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_WIN32_CELL_CONTEXT_HPP
