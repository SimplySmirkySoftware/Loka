#include "Win32ZStackContext.hpp"

#include <cassert>
#include <new>
#include "Win32ContainerWindow.hpp"
#include "../Win32ScenePlatformController.hpp"

Win32ZStackContext::Unit::Unit(Win32ScenePlatformController *controller, unsigned index)
    : next(0),
      window_(controller),
      index_(index)
{
}

bool Win32ZStackContext::Unit::ensure(HWND parent, const loka::win32::NativeRect &seat)
{
  if (this->hwnd())
    return true;
  EnsureClassRegistered();
  // As in ScrollView, the dialog manager needs CONTROLPARENT to enter the unit.
  return this->window_.createNativeChildWindow(WS_EX_CONTROLPARENT,
                                               L"LOKA_STACK_UNIT",
                                               L"",
                                               WS_CHILD | WS_VISIBLE,
                                               seat,
                                               parent,
                                               0,
                                               GetModuleHandleW(NULL),
                                               this)
         != 0;
}

void Win32ZStackContext::Unit::place(const loka::win32::NativeRect &seat)
{
  this->window_.positionNativeWindow(this->hwnd(), seat);
}

Win32ZStackContext::Win32ZStackContext(Win32ScenePlatformController *controller)
    : controller_(controller),
      units_(0)
{
}

Win32ZStackContext::~Win32ZStackContext()
{
  while (this->units_)
  {
    Unit *unit = this->units_;
    this->units_ = unit->next;
    assert(!unit->hwnd() && "terminal fact must queue every unit before context reclaim");
    delete unit;
  }
}

Win32ZStackContext::Unit *Win32ZStackContext::unitFor(unsigned index, Unit **&cursor)
{
  if (*cursor && (*cursor)->serves(index))
  {
    Unit *unit = *cursor;
    cursor = &unit->next;
    return unit;
  }
  Unit *unit = new (std::nothrow) Unit(this->controller_, index);
  if (!unit)
    return 0;
  unit->next = *cursor;
  *cursor = unit;
  cursor = &unit->next;
  return unit;
}

void Win32ZStackContext::orderUnits()
{
  HWND previous = 0;
  bool ordered = true;
  for (Unit *unit = this->units_; unit; unit = unit->next)
  {
    if (!unit->hwnd())
      continue;
    if (previous && GetWindow(previous, GW_HWNDPREV) != unit->hwnd())
      ordered = false;
    previous = unit->hwnd();
  }
  if (ordered && (!previous || !GetWindow(previous, GW_HWNDPREV)))
    return;
  for (Unit *unit = this->units_; unit; unit = unit->next)
    unit->raise();
}

void Win32ZStackContext::retireUnits()
{
  for (Unit *unit = this->units_; unit; unit = unit->next)
    unit->retire();
}

void Win32ZStackContext::onFactChanged(loka::app::scene::NodeLifecycleFact, loka::app::scene::NodeLifecycleFact next)
{
  for (Unit *unit = this->units_; unit; unit = unit->next)
    if (unit->hwnd())
      ShowWindow(unit->hwnd(), next == loka::app::scene::NODE_FACT_ATTACHED ? SW_SHOW : SW_HIDE);
  if (next == loka::app::scene::NODE_FACT_RETIRED)
    this->retireUnits();
}

void Win32ZStackContext::Unit::EnsureClassRegistered()
{
  WNDCLASSW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.lpszClassName = L"LOKA_STACK_UNIT";
  // Registration is idempotent; an already registered class needs no new state.
  RegisterClassW(&wc);
}

LRESULT CALLBACK Win32ZStackContext::Unit::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  Unit *self = reinterpret_cast<Unit *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE)
  {
    CREATESTRUCTW *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
    self = static_cast<Unit *>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  LRESULT result;
  if (loka::win32::Win32AnswerContainerMessage(
          hwnd, msg, wParam, lParam, self ? self->window_.controller() : 0, result))
    return result;
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}
