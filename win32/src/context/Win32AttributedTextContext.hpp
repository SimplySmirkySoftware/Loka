#ifndef LOKA_WIN32_ATTRIBUTED_TEXT_CONTEXT_HPP
#define LOKA_WIN32_ATTRIBUTED_TEXT_CONTEXT_HPP
#include "Win32RetirableContext.hpp"
#include "../Win32TextEnvironment.hpp"
#include "Win32LinkPress.hpp"
#include "app/nodes/AttributedText.hpp"
#include "app/scene/projection/PaintFact.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"

/** Completed text presentation at its drawn client size. */
struct Win32PresentedText
{
  Win32PresentedText()
      : seatWidth(0),
        seatHeight(0)
  {
  }
  Win32PresentedText(const loka::app::AttributedString &value, const RECT &seat)
      : text(value),
        seatWidth(seat.right - seat.left),
        seatHeight(seat.bottom - seat.top)
  {
  }
  loka::app::AttributedString text;
  int seatWidth, seatHeight;
};

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
  HitAnswer prepareLinkHit(POINT client, LinkHit &out);
  bool queryEligibleRun(const LinkHit &hit, Win32PressedRun &out) const;
  void handleLinkButton(UINT, LPARAM);
  friend class Win32InputDoor;
  void clearMeasurement();
  void draw(HDC, const RECT &);
  void relayout(int, int, int, int);
  loka::app::AttributedTextNode *node_;
  Win32LinkPressTable links_;
  loka::app::scene::PaintFact<Win32PresentedText> presented_;
};
void RegisterWin32AttributedTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &);
#endif
