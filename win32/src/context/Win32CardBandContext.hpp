#ifndef LOKA_WIN32_CARD_BAND_CONTEXT_HPP
#define LOKA_WIN32_CARD_BAND_CONTEXT_HPP

#include <windows.h>
#include "Win32RetirableContext.hpp"

namespace loka
{
  namespace app
  {
    class CardBandNode;
    namespace scene
    {
      class PlatformNodeHandlerRegistry;
    }
  } // namespace app
} // namespace loka
class Win32ScenePlatformController;

/** Transparent native band; the base owns and retires its single HWND. */
class Win32CardBandContext : public Win32RetirableContext
{
public:
  Win32CardBandContext(Win32ScenePlatformController *controller,
                       HWND parent,
                       int x,
                       int y,
                       int width,
                       int height,
                       loka::app::CardBandNode *node);
  virtual ~Win32CardBandContext();
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);
  /** Attach-time read (late-subscriber rule): presentation from the current
      fact, called by the installing handler right after adoptContext. */
  void readLifecycleFactOnAttach();
  virtual void onPropsApplied();
  void relayout(int x, int y, int width, int height);

protected:
  virtual void onNativeFact(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);

private:
  void applyAttachedPresentation();
  void applyDetachedPresentation();
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  static void EnsureClassRegistered();
  void drawBand(HDC hdc, const RECT &rect);

  loka::app::CardBandNode *node_;
  loka::core::Frame frame_;
};

void RegisterWin32CardBandNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_WIN32_CARD_BAND_CONTEXT_HPP
