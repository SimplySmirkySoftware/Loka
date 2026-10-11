#include "support/WindowAdmissionTestApp.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "Win32PropsReconciliationTests.hpp"
#include "support/PropsReconciliation.hpp"
#include "Win32Window.hpp"
#include "Win32ScenePlatformController.hpp"
#include "app/nodes/Text.hpp"
#include "app/RectSurface.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "testing/Win32ScenePlatformTestAccess.hpp"
#include <cwchar>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using namespace PropsReconciliationSupport;
  typedef loka::dsl::testing::Win32ScenePlatformTestAccess Access;

  void show(Win32Window &window)
  {
    {
      StateTrackerGuard guard(window.getTracker());
      window.visibilityState().set(true);
    }
    WindowAdmissionTestApp admission(window);
    admission.flush();
  }

  void textIs(HWND hwnd, const wchar_t *expected)
  {
    wchar_t text[64];
    const int length = GetWindowTextW(hwnd, text, 64);
    assert(length == static_cast<int>(std::wcslen(expected)));
    assert(std::wcscmp(text, expected) == 0);
  }

  /** Scoped native observation; restores the procedure before scene unmount. */
  class TextWrites
  {
  public:
    explicit TextWrites(HWND hwnd)
        : hwnd_(hwnd),
          previous_(0),
          count_(0)
    {
      LOKA_VERIFY(SetPropW(hwnd, L"Loka604TextWrites", this));
      this->previous_ = reinterpret_cast<WNDPROC>(
          SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&TextWrites::observe)));
      assert(this->previous_);
    }
    ~TextWrites()
    {
      SetWindowLongPtrW(this->hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(this->previous_));
      RemovePropW(this->hwnd_, L"Loka604TextWrites");
    }
    unsigned count() const
    {
      return this->count_;
    }

  private:
    static LRESULT CALLBACK observe(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
      TextWrites *self = static_cast<TextWrites *>(GetPropW(hwnd, L"Loka604TextWrites"));
      assert(self);
      if (message == WM_SETTEXT)
        ++self->count_;
      return CallWindowProcW(self->previous_, hwnd, message, wParam, lParam);
    }
    HWND hwnd_;
    WNDPROC previous_;
    unsigned count_;
    TextWrites(const TextWrites &);
    TextWrites &operator=(const TextWrites &);
  };

  RectSurfaceModel sprite(short x)
  {
    RectSurfaceModel model;
    model.rectCount = 1;
    model.rects[0] = RectSprite(x, 4, 8, 8);
    return model;
  }
} // namespace

void testWin32RetainedTextRebindsToNewState()
{
  NullPlatformContext platform;
  WindowProps windowProps;
  windowProps.frame(40, 40, 320, 240).visible(false);
  Win32Window window(&platform, windowProps);
  show(window);
  assert(window.hwnd());
  Win32ScenePlatformController controller(window.hwnd(), loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
  MutableState<String> a(String::Literal("same")), b(String::Literal("same"));
  Text declaration(&a);
  Scene scene((Boundary<Tree<Text> >(Props<Text>(&declaration))));
  scene.mount(&controller);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  settle(scene);
  TextNode *text = root(scene)->childrenHead()->asTextNode();
  assert(text);
  NodeContext *context = text->getContext();
  assert(context);
  HWND hwnd = FindWindowExW(window.hwnd(), NULL, L"STATIC", L"same");
  assert(hwnd);
  {
    TextWrites writes(hwnd);
    declaration = Text(&b);
    applyProps(scene, declaration);
    assert(root(scene)->childrenHead() == text && text->getContext() == context);
    assert(text->props.text_ == &b);
    textIs(hwnd, L"same");
    assert(writes.count() == 1);
    // Props-owned text: a literal is applied as a snapshot (no subscription), and
    // a literal-to-literal apply rewrites the same owned State without notifying,
    // so the door itself must re-apply it.
    declaration = Text("lit1");
    applyProps(scene, declaration);
    assert(root(scene)->childrenHead() == text && text->getContext() == context);
    assert(text->props.ownsText());
    textIs(hwnd, L"lit1");
    assert(writes.count() == 2);
    declaration = Text("lit2");
    applyProps(scene, declaration);
    textIs(hwnd, L"lit2");
    assert(writes.count() == 3);
    {
      StateTrackerGuard guard(root(scene)->tracker());
      b.set(String::Literal("ignored B"));
    }
    settle(scene);
    textIs(hwnd, L"lit2");
    assert(writes.count() == 3);
    // Back to the borrowed State B: subscribed again, value follows B.
    declaration = Text(&b);
    applyProps(scene, declaration);
    textIs(hwnd, L"ignored B");
    assert(writes.count() == 4);
    {
      StateTrackerGuard guard(root(scene)->tracker());
      b.set(String::Literal("new B"));
    }
    settle(scene);
    textIs(hwnd, L"new B");
    assert(writes.count() == 5);
    {
      StateTrackerGuard guard(root(scene)->tracker());
      a.set(String::Literal("old A"));
    }
    settle(scene);
    textIs(hwnd, L"new B");
    assert(writes.count() == 5);
  }
  loka::dsl::testing::SceneTestAccess::unmount(scene);
  controller.drainNativeRetirements();
}

void testWin32RetainedRectSurfaceRebindsToNewModel()
{
  NullPlatformContext platform;
  WindowProps windowProps;
  windowProps.frame(40, 40, 320, 240).visible(false);
  Win32Window window(&platform, windowProps);
  show(window);
  assert(window.hwnd());
  Win32ScenePlatformController controller(window.hwnd(), loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
  MutableState<RectSurfaceModel> a(sprite(0)), b(sprite(0));
  RectSurface declaration = RectSurface(&a).size(100, 60);
  Scene scene((Boundary<Tree<RectSurface> >(Props<RectSurface>(&declaration))));
  scene.mount(&controller);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  settle(scene);
  RectSurfaceNode *surface = root(scene)->childrenHead()->asRectSurfaceNode();
  assert(surface);
  NodeContext *context = surface->getContext();
  assert(context);
  HWND hwnd = FindWindowExW(window.hwnd(), NULL, L"LOKA_RECT_SURFACE", NULL);
  assert(hwnd);
  declaration = RectSurface(&b).size(100, 60);
  applyProps(scene, declaration);
  assert(root(scene)->childrenHead() == surface && surface->getContext() == context);
  assert(surface->props.model_ == &b);
  Access::flushPendingInvalidations(controller);
  Access::PendingInvalidationSnapshot pending;
  assert(!Access::queryPendingInvalidation(controller, 0, pending)); // loka-assert-ok: pure query; output unused
  {
    StateTrackerGuard guard(root(scene)->tracker());
    b.set(sprite(4));
  }
  // Inspect the context's subscription before a Scene flush can add or drain damage.
  LOKA_VERIFY(Access::queryPendingInvalidation(controller, 0, pending));
  assert(pending.hwnd == hwnd && pending.fullWindow && !pending.includeChildren);
  assert(pending.eraseBackground == FALSE);
  assert(!Access::queryPendingInvalidation(controller, 1, pending)); // loka-assert-ok: pure query; output unused
  settle(scene);
  Access::flushPendingInvalidations(controller);
  {
    StateTrackerGuard guard(root(scene)->tracker());
    a.set(sprite(8));
  }
  assert(!Access::queryPendingInvalidation(controller, 0, pending)); // loka-assert-ok: pure query; output unused
  settle(scene);
  assert(!Access::queryPendingInvalidation(controller, 0, pending)); // loka-assert-ok: pure query; output unused
  loka::dsl::testing::SceneTestAccess::unmount(scene);
  controller.drainNativeRetirements();
}
