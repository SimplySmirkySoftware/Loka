#ifndef LOKA_WIN32_RIBBON_GROUP_EDGE_CONTEXT_HPP
#define LOKA_WIN32_RIBBON_GROUP_EDGE_CONTEXT_HPP

#include <windows.h>
#include "Win32RetirableContext.hpp"

namespace loka
{
  namespace app
  {
    class RibbonGroupEdgeNode;
    namespace scene
    {
      class PlatformNodeHandlerRegistry;
    }
  } // namespace app
} // namespace loka
class Win32ScenePlatformController;

class Win32RibbonGroupEdgeContext : public Win32RetirableContext
{
public:
  Win32RibbonGroupEdgeContext(Win32ScenePlatformController *controller,
                              HWND parent,
                              int x,
                              int y,
                              int width,
                              int height,
                              loka::app::RibbonGroupEdgeNode *node);
  virtual ~Win32RibbonGroupEdgeContext();
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);
  /** Attach-time read (late-subscriber rule): presentation from the current
      fact, called by the installing handler right after setContext. */
  void readLifecycleFactOnAttach();
  void relayout(int x, int y, int width, int height);

protected:
  virtual void onNativeFact(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);

private:
  void applyAttachedPresentation();
  void applyDetachedPresentation();
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  static void EnsureClassRegistered();
  void drawEdge(HDC hdc, const RECT &rect);

  loka::app::RibbonGroupEdgeNode *node_;
};

void RegisterWin32RibbonGroupEdgeNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_WIN32_RIBBON_GROUP_EDGE_CONTEXT_HPP
