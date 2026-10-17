#ifndef LOKA_WIN32_ZSTACK_CONTEXT_HPP
#define LOKA_WIN32_ZSTACK_CONTEXT_HPP

#include "Win32OwnedChildWindow.hpp"
#include "app/scene/projection/NativeNodeContext.hpp"

/** Owns the opaque native units of one ZStack composition. CompositionList's
    resident child list is fixed after declaration; Show changes its own subtree,
    not this list, so empty units stay and no stale-unit sweep is needed. */
class Win32ZStackContext : public loka::app::scene::NativeNodeContext
{
public:
  class Unit
  {
  public:
    Unit(Win32ScenePlatformController *controller, unsigned index);
    bool serves(unsigned index) const
    {
      return this->index_ == index;
    }
    bool ensure(HWND parent, const loka::win32::NativeRect &seat);
    void place(const loka::win32::NativeRect &seat);
    HWND hwnd() const
    {
      return this->window_.hwnd();
    }
    void retire()
    {
      this->window_.retireWindow();
    }
    void raise()
    {
      this->window_.raise();
    }

  private:
    friend class Win32ZStackContext;
    Unit *next;
    static void EnsureClassRegistered();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    Win32OwnedChildWindow window_;
    const unsigned index_;
  };

  explicit Win32ZStackContext(Win32ScenePlatformController *controller);
  virtual ~Win32ZStackContext();
  /** Layout calls in composition order with a pass-local cursor: each owned
      row is visited once, and one unit may be allocated for a new child. */
  Unit *unitFor(unsigned index, Unit **&cursor);
  Unit **unitsBegin()
  {
    return &this->units_;
  }
  /** One own-list order check per layout pass; only a mismatch adds a second
      walk raising units. Correct order issues no SetWindowPos. */
  void orderUnits();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);

private:
  void retireUnits();
  Win32ScenePlatformController *controller_;
  Unit *units_;
  Win32ZStackContext(const Win32ZStackContext &);
  Win32ZStackContext &operator=(const Win32ZStackContext &);
};
#endif
