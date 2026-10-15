#ifndef LOKA_WIN32_IMAGE_VIEW_CONTEXT_HPP
#define LOKA_WIN32_IMAGE_VIEW_CONTEXT_HPP

#include <windows.h>
#include "Win32RetirableContext.hpp"
#include "app/nodes/ImageView.hpp"
#include "core/State.hpp"
#include "core/resource/Image.hpp"

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

class Win32ImageViewContext : public Win32RetirableContext
{
public:
  Win32ImageViewContext(Win32ScenePlatformController *controller,
                        HWND parent,
                        int x,
                        int y,
                        int width,
                        int height,
                        loka::app::ImageViewNode *node);
  virtual ~Win32ImageViewContext();
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);
  /** Attach-time read (late-subscriber rule): presentation from the current
      fact, called by the installing handler right after setContext. */
  void readLifecycleFactOnAttach();

  static void EnsureClassRegistered();
  void relayout(int x, int y, int width, int height);

protected:
  virtual void onNativeFact(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);

private:
  void applyAttachedPresentation();
  void applyDetachedPresentation();
  void bindImage();
  void unbindImage();
  void applyImage();
  void drawImage(HDC hdc, const RECT &rect);

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  static void ImageChangedThunk(void *userData);

  loka::app::ImageViewNode *node_;
  loka::core::State<loka::core::resource::Image> *imageState_;
  loka::core::resource::Image image_;
};

void RegisterWin32ImageViewNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_WIN32_IMAGE_VIEW_CONTEXT_HPP
