#ifndef LOKA_WIN32_RECT_SURFACE_CONTEXT_HPP
#define LOKA_WIN32_RECT_SURFACE_CONTEXT_HPP

#include <windows.h>
#include "app/RectSurface.hpp"
#include "Win32RetirableContext.hpp"
#include "app/scene/projection/PaintFact.hpp"

namespace loka
{
  namespace app
  {
    class RectSurfaceNode;
  }
} // namespace loka

class Win32ScenePlatformController;

class Win32RectSurfaceContext : public Win32RetirableContext
{
public:
  Win32RectSurfaceContext(Win32ScenePlatformController *controller,
                          HWND parent,
                          int x,
                          int y,
                          int width,
                          int height,
                          loka::app::RectSurfaceNode *node);
  virtual ~Win32RectSurfaceContext();
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;
  /** Attach-time read (late-subscriber rule): presentation from the current
      fact, called by the installing handler right after setContext. */
  void readLifecycleFactOnAttach();

  virtual void onPropsApplied();
  void relayout(int x, int y, int width, int height);
  /** False when the native child window could not be created: the context
      then holds no surface and must not be installed on the node. */
  virtual HWND paintHwnd() const
  {
    return this->hwnd();
  }
  bool hasNativeSurface() const
  {
    return this->hwnd() != 0;
  }

protected:
  virtual void onNativeFact(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);

private:
  void applyAttachedPresentation();
  void applyDetachedPresentation();
  static void EnsureClassRegistered();
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  static void ModelChangedThunk(void *userData);
  void bindModel();
  void unbindModel();
  void applyModel();
  bool queryBoundsInParent(HWND &parent, RECT &rect) const;
  void draw(HDC hdc, const RECT &rect);

  loka::app::scene::PaintFact<loka::app::RectSurfaceModel> presented_;
  loka::app::RectSurfaceNode *node_;
  loka::core::State<loka::app::RectSurfaceModel> *modelState_;
};

#endif
