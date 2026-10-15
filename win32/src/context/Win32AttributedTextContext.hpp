#ifndef LOKA_WIN32_ATTRIBUTED_TEXT_CONTEXT_HPP
#define LOKA_WIN32_ATTRIBUTED_TEXT_CONTEXT_HPP
#include "Win32RetirableContext.hpp"
#include "../Win32TextEnvironment.hpp"
#include "Win32AttributedTextTable.hpp"
#include "app/nodes/AttributedText.hpp"
#include "app/scene/projection/PaintFact.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"

/** RectSurface-shaped PER_RUN projection. Retained detach hides its HWND;
    terminal delivery drops derived rows before queuing native destruction. */
class Win32AttributedTextContext : public Win32RetirableContext, public Win32TextEnvironment::Listener
{
public:
  Win32AttributedTextContext(Win32ScenePlatformController *, HWND, int, int, int, int, loka::app::AttributedTextNode *);
  virtual ~Win32AttributedTextContext();
  static void *operator new(std::size_t) throw();
  static void operator delete(void *) throw();
  void readLifecycleFactOnAttach();
  virtual void onPropsApplied();
  virtual void onTextEnvironmentChanged();
  virtual short layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &);
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &) const;
  virtual HWND paintHwnd() const
  {
    return this->hwnd();
  }

protected:
  virtual void onNativeFact(loka::app::scene::NodeLifecycleFact, loka::app::scene::NodeLifecycleFact);

private:
  Win32TextEnvironment::Subscription textEnvironmentSubscription_;
  friend class loka::testing::Win32AttributedTextAccess;
  static void EnsureClassRegistered();
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  void clearMeasurement();
  void draw(HDC, const RECT &);
  void relayout(int, int, int, int);
  loka::app::AttributedTextNode *node_;
  Win32AttributedTextTable table_;
  loka::app::scene::PaintFact<loka::app::AttributedString> presented_;
};
void RegisterWin32AttributedTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &);
#endif
