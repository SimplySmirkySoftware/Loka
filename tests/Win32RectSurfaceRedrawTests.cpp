#include "Win32PaintGround.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "context/Win32CellContext.hpp"
#include "support/ContrastRatio.hpp"
#include "Win32InputDoor.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "context/Win32EditTextContext.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "context/Win32PopupMenuContext.hpp"
#include "support/PropsReconciliation.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/scene/state/WriteSeat.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "Win32BuiltInSupport.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "Win32RectSurfaceRedrawTests.hpp"
#include "support/TestVerify.hpp"
#include <cstdio>
#include <windows.h>
#include "Win32ScenePlatformController.hpp"
#include "Win32Window.hpp"
#include "app/nodes/Text.hpp"
#include "context/Win32TextContext.hpp"
#include "context/Win32AttributedTextContext.hpp"
#include "context/Win32ImageViewContext.hpp"
#include "context/Win32ScrollViewContext.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "app/RectSurface.hpp"
#include "context/Win32RectSurfaceContext.hpp"
#include "core/State.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "testing/Win32ScenePlatformTestAccess.hpp"

namespace
{
  LRESULT CALLBACK countHostPaint(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
  {
    if (message == WM_PAINT || message == WM_ERASEBKGND)
    {
      Win32ScenePlatformController::noteNativePaint(
          hwnd, Win32ScenePlatformController::NATIVE_PAINT_ROOT, message == WM_ERASEBKGND);
    }
    WNDPROC original = reinterpret_cast<WNDPROC>(GetClassLongPtrW(hwnd, GCLP_WNDPROC));
    return CallWindowProcW(original, hwnd, message, wParam, lParam);
  }

  void pumpMessages()
  {
    MSG message;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
    {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
}

void testWin32RectSurfaceTicksRepaintOnlySurface()
{
  typedef loka::dsl::testing::Win32ScenePlatformTestAccess Access;
  HWND root = CreateWindowExW(0, L"STATIC", L"rect-surface-redraw-host", WS_OVERLAPPED,
                              0, 0, 320, 240, NULL, NULL, GetModuleHandleW(NULL), NULL);
  LOKA_VERIFY(root != NULL);
  const LONG_PTR original = SetWindowLongPtrW(root, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&countHostPaint));
  LOKA_VERIFY(original != 0);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    loka::core::PushStateTracker tracker;
    loka::core::MutableState<loka::app::RectSurfaceModel> model((loka::app::RectSurfaceModel()));
    tracker.addState(&model);
    loka::app::RectSurfaceProps props;
    props.model(&model).size(100, 60);
    loka::app::RectSurfaceNode node(props);
    Win32RectSurfaceContext context(&controller, root, 10, 20, 100, 60, &node);
    LOKA_VERIFY(context.hasNativeSurface());
    ShowWindow(root, SW_SHOWNOACTIVATE);
    Access::flushPendingInvalidations(controller);
    pumpMessages();
    for (int tick = 1; tick <= 5; ++tick)
    {
      const Access::RedrawStats before = Access::redrawStats(controller);
      loka::app::RectSurfaceModel next;
      next.rectCount = 1;
      next.rects[0] = loka::app::RectSprite(static_cast<short>(tick * 4), 8, 10, 10);
      {
        loka::core::StateTrackerGuard guard(&tracker);
        model.set(next);
      }
      Access::flushPendingInvalidations(controller);
      pumpMessages();
      const Access::RedrawStats after = Access::redrawStats(controller);
      const int rootErase = after.rootEraseCount - before.rootEraseCount;
      const int rootPaint = after.rootPaintCount - before.rootPaintCount;
      const int surfaceErase = after.rectSurfaceEraseCount - before.rectSurfaceEraseCount;
      const int surfacePaint = after.rectSurfacePaintCount - before.rectSurfacePaintCount;
      std::printf("#597 tick %d: root erase=%d paint=%d; surface erase=%d paint=%d\n",
                  tick, rootErase, rootPaint, surfaceErase, surfacePaint);
      LOKA_VERIFY(rootErase == 0);
      LOKA_VERIFY(rootPaint == 0);
      LOKA_VERIFY(surfaceErase == 0);
      LOKA_VERIFY(surfacePaint == 1);
    }
    context.onFactChanged(loka::app::scene::NODE_FACT_ATTACHED, loka::app::scene::NODE_FACT_RETIRED);
    controller.drainNativeRetirements();
  }
  SetWindowLongPtrW(root, GWLP_WNDPROC, original);
  DestroyWindow(root);
}

static void
exercisePaintOnlyChangeUnderScrollView(bool refused, bool removeSprites = false, bool paintBeforeApply = false)
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace PropsReconciliationSupport;
  typedef loka::dsl::testing::Win32ScenePlatformTestAccess Access;
  typedef loka::dsl::testing::SceneTestAccess SceneAccess;

  NullPlatformContext platform;
  WindowProps windowProps;
  windowProps.frame(40, 40, 320, 240).visible(false);
  Win32Window window(&platform, windowProps);
  {
    loka::core::StateTrackerGuard guard(window.getTracker());
    window.visibilityState().set(true);
  }
  WindowAdmissionTestApp admission(window);
  admission.flush();
  HWND rootHwnd = window.hwnd();
  LOKA_VERIFY(rootHwnd != NULL);
  Win32ScenePlatformController controller(rootHwnd, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
  RegisterWin32BuiltInSupport(controller);

  // RectSurface pairs DOCUMENT ground and its text-role sprites. Keep B black
  // while adding or removing A's sprite.
  RectSurfaceModel black;
  black.rectCount = 1;
  black.rects[0] = RectSprite(0, 0, 100, 60);
  loka::core::MutableState<RectSurfaceModel> a(removeSprites ? black : RectSurfaceModel());
  loka::core::MutableState<RectSurfaceModel> b(black);
  loka::core::MutableState<loka::core::String> editText(loka::core::String::Literal("input"));
  loka::core::MutableState<int> selection(0);
  loka::core::MutableState<bool> enabled(true);
  const char *items[] = {"one", "two"};
  // Both native controls must be handled in the same Boundary as the surfaces.
  VStack content = VStack() << RectSurface(&a).size(100, 60) << Box().size(100, 20)
                            << RectSurface(&b).size(100, 60)
                            << PopupMenu(items, 2).selectedIndex(loka::app::scene::WriteSeat<int>(&selection)).enabled(&enabled) << EditText(loka::app::scene::WriteSeat<loka::core::String>(&editText));
  ScrollView declaration = ScrollView() << content;
  Scene scene((Boundary<Tree<ScrollView> >(Props<ScrollView>(&declaration))));
  scene.mount(&controller);
  SceneAccess::updateAttached(scene, true);
  settle(scene);
  BoundaryNode *boundary = SceneAccess::rootBoundary(scene);
  LOKA_VERIFY(boundary != 0);
  controller.onChange(boundary, NODE_DIRTY_NONE, false);
  controller.relayout(320, 240);
  settle(scene);
  Access::flushPendingInvalidations(controller);
  pumpMessages();
  UpdateWindow(rootHwnd);

  HWND viewport = FindWindowExW(rootHwnd, NULL, L"LOKA_SCROLL_VIEW", NULL);
  LOKA_VERIFY(viewport != NULL);
  // #1199: the viewport paints beneath transparent children; native parentage
  // still clips projected children to the viewport's client area.
  LOKA_VERIFY((GetWindowLongPtrW(viewport, GWL_STYLE) & WS_CLIPCHILDREN) == 0);
  LOKA_VERIFY(FindWindowExW(viewport, NULL, L"COMBOBOX", NULL) != NULL);
  LOKA_VERIFY(FindWindowExW(viewport, NULL, L"EDIT", NULL) != NULL);
  HWND first = FindWindowExW(viewport, NULL, L"LOKA_RECT_SURFACE", NULL);
  LOKA_VERIFY(first != NULL);
  HWND second = FindWindowExW(viewport, first, L"LOKA_RECT_SURFACE", NULL);
  LOKA_VERIFY(second != NULL);
  RECT firstRect, secondRect;
  LOKA_VERIFY(GetWindowRect(first, &firstRect));
  LOKA_VERIFY(GetWindowRect(second, &secondRect));
  // Native z-order need not match declaration order; A is the upper surface.
  const RECT aRect = firstRect.top < secondRect.top ? firstRect : secondRect;
  const RECT bRect = firstRect.top < secondRect.top ? secondRect : firstRect;
  LOKA_VERIFY(aRect.bottom < bRect.top);
  POINT aPoint = {(aRect.left + aRect.right) / 2, (aRect.top + aRect.bottom) / 2};
  POINT bPoint = {(bRect.left + bRect.right) / 2, (bRect.top + bRect.bottom) / 2};
  LOKA_VERIFY(ScreenToClient(rootHwnd, &aPoint));
  LOKA_VERIFY(ScreenToClient(rootHwnd, &bPoint));

  for (int phase = 0; phase < 2; ++phase)
  {
    if (phase == 1)
    {
      Access::resetRedrawStats(controller);
      // Same RectSurfaceModel write as the no-LAYOUT PaintBaseline pin.
      {
        loka::core::StateTrackerGuard guard(boundary->tracker());
        a.set(removeSprites ? RectSurfaceModel() : black);
      }
      // Submit the observer's HWND request without painting. The later queue
      // row must therefore come from the Boundary's answer translation.
      Access::flushPendingInvalidations(controller);
      if (paintBeforeApply)
      {
        // Native expose before a deferred Boundary apply clears the removed
        // sprite and certifies the model, so the later answer is empty EXACT.
        HWND surface = firstRect.top < secondRect.top ? first : second;
        RedrawWindow(surface, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
      }
      if (refused)
      {
        // Invalidate B's presentation without changing its model or pixels.
        // The missing history must force the Boundary's broad fallback.
        HWND sibling = firstRect.top < secondRect.top ? second : first;
        SendMessageW(sibling, WM_SIZE, 0, MAKELPARAM(100, 60));
      }
      settle(scene);
      const PlatformApplyPlan &plan = SceneAccess::lastApplyPlan(scene);
      LOKA_VERIFY(plan.hasPaintWork());
      LOKA_VERIFY(!plan.hasLayoutWork());
      LOKA_VERIFY(Access::onBoundaryApplyCalls(controller) > 0);
      LOKA_VERIFY(!Access::lastOnChangeRequiredLayout(controller));
      Access::PendingInvalidationSnapshot request;
      const bool hasRequest = Access::queryPendingInvalidation(controller, 0, request);
      Access::flushPendingInvalidations(controller);
      if (refused)
        LOKA_VERIFY(Access::queuedPaintInvalidates(controller) == 1);
      else
        LOKA_VERIFY(Access::queuedPaintInvalidates(controller) == 0);
      LOKA_VERIFY(hasRequest == (refused || !paintBeforeApply));
      if (refused)
      {
        LOKA_VERIFY(request.hwnd == rootHwnd && request.includeChildren);
      }
      else if (!paintBeforeApply)
      {
        LOKA_VERIFY(request.hwnd == (firstRect.top < secondRect.top ? first : second));
        LOKA_VERIFY(!request.fullWindow && !request.eraseBackground && !request.includeChildren);
        RECT client;
        LOKA_VERIFY(GetClientRect(request.hwnd, &client));
        LOKA_VERIFY(EqualRect(&request.rect, &client));
      }
      pumpMessages();
      UpdateWindow(rootHwnd);
      // The refused path must replay both surfaces after the root fills its
      // ground; the exact path must repaint A without requiring that replay.
      // An expose before the apply already painted A's ground and certified
      // the new model; the apply cycle restarts the counters
      // (beginApplyCycle), and its empty EXACT answer asks for no repaint.
      const int surfacePaints = Access::redrawStats(controller).rectSurfacePaintCount;
      if (refused)
        LOKA_VERIFY(surfacePaints >= 2);
      else if (paintBeforeApply)
        LOKA_VERIFY(surfacePaints == 0);
      else
        LOKA_VERIFY(surfacePaints >= 1);
    }
    NullPlatformContext captureOwner;
    loka::core::resource::Image capture;
    LOKA_VERIFY(Access::captureWindowClientBitmap(captureOwner, rootHwnd, capture));
    HDC pixels = CreateCompatibleDC(NULL);
    LOKA_VERIFY(pixels != NULL);
    HGDIOBJ previous = SelectObject(pixels, static_cast<HBITMAP>(capture.nativeHandle()));
    LOKA_VERIFY(previous != NULL && previous != HGDI_ERROR);
    const COLORREF aPixel = GetPixel(pixels, aPoint.x, aPoint.y);
    const COLORREF bPixel = GetPixel(pixels, bPoint.x, bPoint.y);
    SelectObject(pixels, previous);
    DeleteDC(pixels);
    const Access::RedrawStats &stats = Access::redrawStats(controller);
    std::printf("#725 phase %d: A=%08lX B=%08lX window=%08lX; root paint=%d erase=%d surface paint=%d\n",
                phase,
                static_cast<unsigned long>(aPixel),
                static_cast<unsigned long>(bPixel),
                static_cast<unsigned long>(GetSysColor(COLOR_WINDOW)),
                stats.rootPaintCount,
                stats.rootEraseCount,
                stats.rectSurfacePaintCount);
    std::fflush(stdout);
    LOKA_VERIFY(bPixel == GetSysColor(COLOR_WINDOWTEXT) && "paint-only root delivery must preserve the sibling under ScrollView");
    const bool blackA = removeSprites ? phase == 0 : phase == 1;
    const COLORREF expectedA = blackA ? GetSysColor(COLOR_WINDOWTEXT) : GetSysColor(COLOR_WINDOW);
    LOKA_VERIFY(aPixel == expectedA);
  }
  SceneAccess::unmount(scene);
  controller.drainNativeRetirements();
}

void testWin32PaintOnlyChangeUnderScrollViewKeepsSiblingPixels()
{
  exercisePaintOnlyChangeUnderScrollView(false);
}

void testWin32RefusedAnswerKeepsBroadFallback()
{
  exercisePaintOnlyChangeUnderScrollView(true);
}

void testWin32RemovedSpritesRestoreGround()
{
  exercisePaintOnlyChangeUnderScrollView(false, true);
  exercisePaintOnlyChangeUnderScrollView(false, true, true);
}

void testWin32PaintAnswerContracts()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  typedef loka::dsl::testing::Win32ScenePlatformTestAccess Access;
  HWND root = CreateWindowExW(
      0, L"STATIC", L"paint-answer-contracts", WS_OVERLAPPED, 0, 0, 320, 240, NULL, NULL, GetModuleHandleW(NULL), NULL);
  LOKA_VERIFY(root != NULL);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    loka::core::PushStateTracker tracker;
    loka::core::MutableState<RectSurfaceModel> model((RectSurfaceModel()));
    loka::core::MutableState<loka::core::String> label(loka::core::String::Literal("before"));
    tracker.addState(&model);
    tracker.addState(&label);
    RectSurfaceProps props;
    props.model(&model).size(100, 60);
    RectSurfaceNode node(props);
    Win32RectSurfaceContext surface(&controller, root, 0, 0, 100, 60, &node);
    TextNode textNode((TextProps(&label)));
    Win32TextContext text(&controller, root, 0, 80, 100, 24, &textNode);
    PopupMenuNode popupNode((PopupMenuProps()));
    Win32PopupMenuContext popup(&controller, root, 0, 120, 100, 24, &popupNode);
    const PaintQuery query = {Win32RetirableContext::paintScope(), PLACEMENT_ELIGIBLE};
    const PaintQuery pending = {query.scope, PLACEMENT_PENDING};
    LOKA_VERIFY(surface.queryPaintDamage(query).reason == PAINT_REFUSED_HISTORY_UNKNOWN);
    LOKA_VERIFY(popup.queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
    LOKA_VERIFY(popup.queryPaintDamage(query).damage.width == 0);
    ShowWindow(root, SW_SHOWNOACTIVATE);
    Access::flushPendingInvalidations(controller);
    pumpMessages();
    UpdateWindow(root);
    // Establish a full-client presentation, independent of expose clipping.
    RedrawWindow(surface.paintHwnd(), NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    PaintAnswer answer = surface.queryPaintDamage(query);
    LOKA_VERIFY(answer.kind == PAINT_ANSWER_EXACT);
    LOKA_VERIFY(answer.damage.width == 0 && answer.damage.height == 0);
    RectSurfaceModel black;
    black.rectCount = 1;
    black.rects[0] = RectSprite(0, 0, 100, 60);
    {
      loka::core::StateTrackerGuard guard(&tracker);
      model.set(black);
      label.set(loka::core::String::Literal("after"));
    }
    answer = surface.queryPaintDamage(query);
    LOKA_VERIFY(answer.kind == PAINT_ANSWER_EXACT);
    LOKA_VERIFY(answer.damage.scope == query.scope);
    LOKA_VERIFY(answer.damage.x == 0 && answer.damage.y == 0);
    LOKA_VERIFY(answer.damage.width == 100 && answer.damage.height == 60);
    LOKA_VERIFY(answer.damage.coverage == PAINT_COVERAGE_PAINT_ONLY);
    LOKA_VERIFY(surface.queryPaintDamage(pending).reason == PAINT_REFUSED_PLACEMENT_UNSETTLED);
    LOKA_VERIFY(text.queryPaintDamage(query).kind == PAINT_ANSWER_NATIVE_SCHEDULED);
    // A read-only query must not consume the delivery fact.
    LOKA_VERIFY(text.queryPaintDamage(query).kind == PAINT_ANSWER_NATIVE_SCHEDULED);
    {
      loka::core::StateTrackerGuard guard(&tracker);
      label.set(loka::core::String::Literal("after"), true);
    }
    LOKA_VERIFY(text.queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
    LOKA_VERIFY(text.queryPaintDamage(query).damage.width == 0);
    {
      loka::core::StateTrackerGuard guard(&tracker);
      label.set(loka::core::String::Literal("later")); // Same length, different text.
    }
    LOKA_VERIFY(text.queryPaintDamage(query).kind == PAINT_ANSWER_NATIVE_SCHEDULED);
    const char *longText = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                           "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                           "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                           "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    {
      loka::core::StateTrackerGuard guard(&tracker);
      label.set(loka::core::String::Literal(longText));
    }
    LOKA_VERIFY(text.queryPaintDamage(query).kind == PAINT_ANSWER_NATIVE_SCHEDULED);
    {
      loka::core::StateTrackerGuard guard(&tracker);
      label.set(loka::core::String::Literal(longText), true);
    }
    // 256 characters cannot fit with a terminator: no unbounded read/allocation.
    LOKA_VERIFY(text.queryPaintDamage(query).kind == PAINT_ANSWER_NATIVE_SCHEDULED);
    Access::flushPendingInvalidations(controller);
    pumpMessages();
    RedrawWindow(surface.paintHwnd(), NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    LOKA_VERIFY(surface.queryPaintDamage(query).damage.width == 0);
    const RECT partial = {0, 0, 10, 10};
    RedrawWindow(surface.paintHwnd(), &partial, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    LOKA_VERIFY(surface.queryPaintDamage(query).reason == PAINT_REFUSED_HISTORY_UNKNOWN);
    RedrawWindow(surface.paintHwnd(), NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    LOKA_VERIFY(surface.queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
    // A completed clearing paint certifies the current model; applying props
    // still invalidates that presentation before the next native paint.
    LOKA_VERIFY(surface.queryPaintDamage(query).damage.coverage == PAINT_COVERAGE_PAINT_ONLY);
    surface.onPropsApplied();
    LOKA_VERIFY(surface.queryPaintDamage(query).reason == PAINT_REFUSED_HISTORY_UNKNOWN);
    surface.relayout(0, 0, 90, 60);
    // relayout may synchronously repaint; an explicit resize invalidates history.
    SendMessageW(surface.paintHwnd(), WM_SIZE, 0, MAKELPARAM(90, 60));
    LOKA_VERIFY(surface.queryPaintDamage(query).reason == PAINT_REFUSED_HISTORY_UNKNOWN);
    surface.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
    LOKA_VERIFY(surface.queryPaintDamage(query).reason == PAINT_REFUSED_HISTORY_UNKNOWN);
    text.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    surface.onFactChanged(NODE_FACT_DETACHED_RETAINED, NODE_FACT_RETIRED);
    popup.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    LOKA_VERIFY(surface.queryPaintDamage(query).reason == PAINT_REFUSED_NO_CONTEXT);
    LOKA_VERIFY(text.queryPaintDamage(query).reason == PAINT_REFUSED_NO_CONTEXT);
    controller.drainNativeRetirements();
    // Foreign contexts cannot replace kinds that the paint visitor casts.
    RefusedNodeHandler foreign(NodeTypeToken<TextNode>());
    LOKA_VERIFY(!controller.registerNodeHandler(&foreign));
  }
  DestroyWindow(root);
}

void testWin32EditTextPaintDelivery()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  HWND root = CreateWindowExW(
      0, L"STATIC", L"edit-delivery", WS_OVERLAPPED, 0, 0, 320, 240, NULL, NULL, GetModuleHandleW(NULL), NULL);
  LOKA_VERIFY(root != NULL);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    loka::core::PushStateTracker tracker;
    loka::core::MutableState<loka::core::String> value(loka::core::String::Literal("before"));
    loka::core::MutableState<loka::core::String> replacement(loka::core::String::Literal("foreign"));
    tracker.addState(&value);
    EditTextNode node((EditTextProps(loka::app::scene::WriteSeat<loka::core::String>(&value))));
    Win32EditTextContext context(&controller, root, 0, 0, 100, 24, &node);
    const PaintQuery query = {Win32RetirableContext::paintScope(), PLACEMENT_ELIGIBLE};
    {
      loka::core::StateTrackerGuard guard(&tracker);
      value.set(loka::core::String::Literal("before"), true);
    }
    PaintAnswer answer = context.queryPaintDamage(query);
    LOKA_VERIFY(answer.kind == PAINT_ANSWER_EXACT && answer.damage.width == 0 && answer.damage.height == 0);
    LOKA_VERIFY(answer.damage.scope == query.scope);
    {
      loka::core::StateTrackerGuard guard(&tracker);
      value.set(loka::core::String::Literal("after"));
    }
    LOKA_VERIFY(context.queryPaintDamage(query).kind == PAINT_ANSWER_NATIVE_SCHEDULED);
    LOKA_VERIFY(context.queryPaintDamage(query).kind == PAINT_ANSWER_NATIVE_SCHEDULED);
    // A native echo is already represented in the EDIT and owes no new damage.
    LOKA_VERIFY(SetWindowTextW(context.hwnd(), L"native"));
    {
      loka::core::StateTrackerGuard guard(&tracker);
      LOKA_VERIFY(Win32InputDoor::editTextCommand(context, MAKEWPARAM(0, EN_CHANGE), 0));
    }
    LOKA_VERIFY(value.get().equals(loka::core::String::Literal("native")));
    LOKA_VERIFY(context.queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
    node.props.text(loka::app::scene::WriteSeat<loka::core::String>(&replacement));
    LOKA_VERIFY(context.queryPaintDamage(query).reason == PAINT_REFUSED_PROPS_UNRECONCILED);
    node.props.text(loka::app::scene::WriteSeat<loka::core::String>(&value));
    context.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
    LOKA_VERIFY(context.queryPaintDamage(query).reason == PAINT_REFUSED_HISTORY_UNKNOWN);
    context.onFactChanged(NODE_FACT_DETACHED_RETAINED, NODE_FACT_RETIRED);
    LOKA_VERIFY(context.queryPaintDamage(query).reason == PAINT_REFUSED_NO_CONTEXT);
    EditTextNode unboundNode((EditTextProps()));
    Win32EditTextContext unbound(&controller, root, 0, 30, 100, 24, &unboundNode);
    LOKA_VERIFY(unbound.queryPaintDamage(query).reason == PAINT_REFUSED_HISTORY_UNKNOWN);
    unbound.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    controller.drainNativeRetirements();
  }
  DestroyWindow(root);
}

void testWin32PopupMenuPaintDelivery()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  HWND root = CreateWindowExW(
      0, L"STATIC", L"popup-delivery", WS_OVERLAPPED, 0, 0, 320, 240, NULL, NULL, GetModuleHandleW(NULL), NULL);
  LOKA_VERIFY(root != NULL);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    loka::core::PushStateTracker tracker;
    loka::core::MutableState<int> selection(0);
    loka::core::MutableState<int> replacement(1);
    loka::core::MutableState<bool> enabled(true);
    loka::core::MutableState<bool> replacementEnabled(false);
    tracker.addState(&selection);
    tracker.addState(&enabled);
    const char *items[] = {"one", "two"};
    PopupMenuProps props;
    props.items(items, 2).selectedIndex(loka::app::scene::WriteSeat<int>(&selection)).enabled(&enabled);
    PopupMenuNode node(props);
    Win32PopupMenuContext context(&controller, root, 0, 0, 100, 24, &node);
    const PaintQuery query = {Win32RetirableContext::paintScope(), PLACEMENT_ELIGIBLE};
    {
      loka::core::StateTrackerGuard guard(&tracker);
      selection.set(0, true);
    }
    PaintAnswer answer = context.queryPaintDamage(query);
    LOKA_VERIFY(answer.kind == PAINT_ANSWER_EXACT && answer.damage.width == 0 && answer.damage.height == 0);
    LOKA_VERIFY(answer.damage.scope == query.scope);
    {
      loka::core::StateTrackerGuard guard(&tracker);
      selection.set(1);
    }
    LOKA_VERIFY(context.queryPaintDamage(query).kind == PAINT_ANSWER_NATIVE_SCHEDULED);
    LOKA_VERIFY(SendMessageW(context.hwnd(), CB_GETCURSEL, 0, 0) == 1);
    LOKA_VERIFY(context.queryPaintDamage(query).kind == PAINT_ANSWER_NATIVE_SCHEDULED);
    {
      loka::core::StateTrackerGuard guard(&tracker);
      enabled.set(false);
    }
    LOKA_VERIFY(context.queryPaintDamage(query).kind == PAINT_ANSWER_NATIVE_SCHEDULED);
    const bool nativeEnabled = IsWindowEnabled(context.hwnd()) != FALSE;
    LOKA_VERIFY(!nativeEnabled);
    {
      loka::core::StateTrackerGuard guard(&tracker);
      enabled.set(false, true);
    }
    LOKA_VERIFY(context.queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
    {
      loka::core::StateTrackerGuard guard(&tracker);
      selection.set(-1);
    }
    LOKA_VERIFY(context.queryPaintDamage(query).kind == PAINT_ANSWER_NATIVE_SCHEDULED);
    LOKA_VERIFY(SendMessageW(context.hwnd(), CB_GETCURSEL, 0, 0) == CB_ERR);
    {
      loka::core::StateTrackerGuard guard(&tracker);
      selection.set(-1, true);
    }
    LOKA_VERIFY(context.queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
    node.props.selectedIndex(loka::app::scene::WriteSeat<int>(&replacement));
    LOKA_VERIFY(context.queryPaintDamage(query).reason == PAINT_REFUSED_PROPS_UNRECONCILED);
    node.props.selectedIndex(loka::app::scene::WriteSeat<int>(&selection)).enabled(&replacementEnabled);
    LOKA_VERIFY(context.queryPaintDamage(query).reason == PAINT_REFUSED_PROPS_UNRECONCILED);
    node.props.enabled(&enabled);
    context.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
    LOKA_VERIFY(context.queryPaintDamage(query).reason == PAINT_REFUSED_HISTORY_UNKNOWN);
    context.onFactChanged(NODE_FACT_DETACHED_RETAINED, NODE_FACT_RETIRED);
    LOKA_VERIFY(context.queryPaintDamage(query).reason == PAINT_REFUSED_NO_CONTEXT);
    controller.drainNativeRetirements();
  }
  DestroyWindow(root);
}

void testWin32TextOverlapPinsSiblingRepaint()
{
  typedef loka::dsl::testing::Win32ScenePlatformTestAccess Access;
  BOOL fontSmoothing = FALSE;
  UINT fontSmoothingType = 0;
  LOKA_VERIFY(SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &fontSmoothing, 0));
  LOKA_VERIFY(SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE, 0, &fontSmoothingType, 0));
  // Use the production root so its STATIC background and ground-fill policies
  // participate in the pin, along with the two native projection contexts.
  NullPlatformContext platform;
  WindowProps windowProps;
  windowProps.frame(40, 40, 320, 240).visible(false);
  Win32Window window(&platform, windowProps);
  {
    loka::core::StateTrackerGuard guard(window.getTracker());
    window.visibilityState().set(true);
  }
  WindowAdmissionTestApp admission(window);
  admission.flush();
  HWND root = window.hwnd();
  LOKA_VERIFY(root != NULL);
  ShowWindow(root, SW_HIDE);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    loka::app::RectSurfaceModel initial;
    initial.rectCount = 1;
    initial.rects[0] = loka::app::RectSprite(0, 40, 160, 40);
    loka::core::PushStateTracker tracker;
    loka::core::MutableState<loka::app::RectSurfaceModel> model(initial);
    tracker.addState(&model);
    loka::app::RectSurfaceProps props;
    props.model(&model).size(160, 80);
    loka::app::RectSurfaceNode node(props);
    Win32RectSurfaceContext surface(&controller, root, 0, 0, 160, 80, &node);
    LOKA_VERIFY(surface.hasNativeSurface());
    loka::app::TextNode textNode((loka::app::TextProps("*")));
    Win32TextContext text(&controller, root, 0, 0, 160, 24, &textNode);
    HWND textHwnd = FindWindowExW(root, NULL, L"STATIC", L"*");
    LOKA_VERIFY(textHwnd != NULL);

    ShowWindow(root, SW_SHOWNOACTIVATE);
    Access::flushPendingInvalidations(controller);
    pumpMessages();
    UpdateWindow(root);

    for (int phase = 0; phase < 2; ++phase)
    {
      if (phase == 1)
      {
        loka::app::RectSurfaceModel next;
        next.rectCount = 1;
        next.rects[0] = loka::app::RectSprite(0, 44, 160, 36);
        {
          loka::core::StateTrackerGuard guard(&tracker);
          model.set(next);
        }
        Access::flushPendingInvalidations(controller);
        pumpMessages();
        UpdateWindow(root);
      }
      NullPlatformContext captureOwner;
      loka::core::resource::Image capture;
      LOKA_VERIFY(Access::captureWindowClientBitmap(captureOwner, root, capture));
      HDC pixels = CreateCompatibleDC(NULL);
      LOKA_VERIFY(pixels != NULL);
      HGDIOBJ previous = SelectObject(pixels, static_cast<HBITMAP>(capture.nativeHandle()));
      LOKA_VERIFY(previous != NULL && previous != HGDI_ERROR);
      const COLORREF below = GetPixel(pixels, 80, 60);
      const COLORREF overlap = GetPixel(pixels, 150, 12);
      const COLORREF ground = GetPixel(pixels, 200, 100);
      // Font smoothing can render the glyph without pure text-role pixels (#829).
      int inkPixels = 0;
      for (int y = 0; y < 24; ++y)
      {
        for (int x = 0; x < 160; ++x)
        {
          if (GetPixel(pixels, x, y) != GetSysColor(COLOR_WINDOW))
          {
            ++inkPixels;
          }
        }
      }
      SelectObject(pixels, previous);
      DeleteDC(pixels);
      std::printf("#598 ZStack capture %d: below=%08lX overlap=%08lX ground=%08lX; window=%08lX ink=%d; SPI_GETFONTSMOOTHING=%d SPI_GETFONTSMOOTHINGTYPE=%u\n",
                  phase, static_cast<unsigned long>(below), static_cast<unsigned long>(overlap),
                  static_cast<unsigned long>(ground), static_cast<unsigned long>(GetSysColor(COLOR_BTNFACE)), inkPixels,
                  static_cast<int>(fontSmoothing), static_cast<unsigned int>(fontSmoothingType));
      std::fflush(stdout);
      LOKA_VERIFY(below == GetSysColor(COLOR_WINDOWTEXT));
      // Win32 sibling-repaint path pin, not a cross-rail overlap promise.
      LOKA_VERIFY(overlap == GetSysColor(COLOR_WINDOW));
      LOKA_VERIFY(inkPixels > 0);
      LOKA_VERIFY(ground == GetSysColor(COLOR_BTNFACE));
    }

    text.onFactChanged(loka::app::scene::NODE_FACT_ATTACHED, loka::app::scene::NODE_FACT_RETIRED);
    surface.onFactChanged(loka::app::scene::NODE_FACT_ATTACHED, loka::app::scene::NODE_FACT_RETIRED);
    controller.drainNativeRetirements();
  }
}

namespace
{
  typedef loka::dsl::testing::Win32ScenePlatformTestAccess RedrawAccess;

  WindowProps transparentPaintWindowProps()
  {
    WindowProps props;
    props.frame(40, 40, 320, 240).visible(false);
    return props;
  }

  // Same production root and admission as testWin32TextOverlapPinsSiblingRepaint.
  struct TransparentPaintWindow
  {
    NullPlatformContext platform;
    Win32Window window;
    WindowAdmissionTestApp admission;

    TransparentPaintWindow()
        : window(&this->platform, transparentPaintWindowProps()), admission(this->window)
    {
      {
        loka::core::StateTrackerGuard guard(this->window.getTracker());
        this->window.visibilityState().set(true);
      }
      this->admission.flush();
      LOKA_VERIFY(this->window.hwnd() != NULL);
      ShowWindow(this->window.hwnd(), SW_HIDE);
    }
  };

  void flushTransparentPaint(Win32ScenePlatformController &controller, HWND root)
  {
    RedrawAccess::flushPendingInvalidations(controller);
    pumpMessages();
    UpdateWindow(root);
  }

  COLORREF windowPixel(HWND hwnd, int x, int y)
  {
    // Read existing pixels; do not issue PrintWindow or an extra invalidation
    // that could hide a missing parent repaint in the change path.
    HDC dc = GetDC(hwnd);
    LOKA_VERIFY(dc != NULL);
    const COLORREF pixel = GetPixel(dc, x, y);
    ReleaseDC(hwnd, dc);
    LOKA_VERIFY(pixel != CLR_INVALID);
    return pixel;
  }

  void releaseTestBitmap(void *handle, void *)
  {
    DeleteObject(static_cast<HBITMAP>(handle));
  }

  loka::core::resource::Image blackTestImage(HWND root)
  {
    HDC windowDC = GetDC(root);
    LOKA_VERIFY(windowDC != NULL);
    HDC dc = CreateCompatibleDC(windowDC);
    HBITMAP bitmap = CreateCompatibleBitmap(windowDC, 32, 16);
    LOKA_VERIFY(dc != NULL && bitmap != NULL);
    HGDIOBJ previous = SelectObject(dc, bitmap);
    LOKA_VERIFY(previous != NULL && previous != HGDI_ERROR);
    const RECT rect = {0, 0, 32, 16};
    LOKA_VERIFY(FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))));
    SelectObject(dc, previous);
    DeleteDC(dc);
    ReleaseDC(root, windowDC);
    const loka::core::resource::Image image =
        loka::core::resource::Image::FromNative(bitmap, 32, 16, &releaseTestBitmap, NULL);
    LOKA_VERIFY(image.isValid());
    return image;
  }
} // namespace

void testWin32AttributedTextTransparentOverSprite()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  TransparentPaintWindow host;
  HWND root = host.window.hwnd();
  Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
  RectSurfaceModel initial;
  initial.rectCount = 1;
  initial.rects[0] = RectSprite(0, 0, 160, 80);
  loka::core::PushStateTracker tracker;
  loka::core::MutableState<RectSurfaceModel> model(initial);
  tracker.addState(&model);
  RectSurfaceProps props;
  props.model(&model).size(160, 80);
  RectSurfaceNode node(props);
  Win32RectSurfaceContext surface(&controller, root, 0, 0, 160, 80, &node);
  LOKA_VERIFY(surface.hasNativeSurface());
  AttributedTextNode textNode((AttributedTextProps(Styled("*", FontSize<12>()))));
  Win32AttributedTextContext text(&controller, root, 0, 0, 160, 24, &textNode);
  LOKA_VERIFY(text.paintHwnd() != NULL);
  LayoutState state;
  state.width = 160;
  text.layout(&controller, state);
  LOKA_VERIFY(state.height > 4);
  ShowWindow(root, SW_SHOWNOACTIVATE);
  flushTransparentPaint(controller, root);
  // Native ZStack arrangement, just like the Text pin above. This is a Win32
  // sibling-path pin, not a promise of cross-rail RectSurface overlays.
  LOKA_VERIFY(windowPixel(text.paintHwnd(), 150, 2) == GetSysColor(COLOR_WINDOWTEXT));
  {
    loka::core::StateTrackerGuard guard(&tracker);
    model.set(RectSurfaceModel());
  }
  flushTransparentPaint(controller, root);
  LOKA_VERIFY(windowPixel(text.paintHwnd(), 150, 2) == GetSysColor(COLOR_WINDOW));
  {
    loka::core::StateTrackerGuard guard(&tracker);
    model.set(initial);
  }
  flushTransparentPaint(controller, root);
  LOKA_VERIFY(windowPixel(text.paintHwnd(), 150, 2) == GetSysColor(COLOR_WINDOWTEXT));
  text.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
  surface.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
  controller.drainNativeRetirements();
}

namespace
{
  void exerciseAttributedTextShrink(Win32ScenePlatformController &controller, HWND parent)
  {
    using namespace loka::app;
    using namespace loka::app::scene;
    HWND root = controller.rootHwnd();
    loka::core::PushStateTracker tracker;
    loka::core::MutableState<AttributedString> value(Styled("MMMMMMMMMMMMMMMM", FontSize<12>()));
    tracker.addState(&value);
    AttributedTextNode node((AttributedTextProps(&value)));
    Win32AttributedTextContext text(&controller, parent, 0, 0, 240, 24, &node);
    LOKA_VERIFY(text.paintHwnd() != NULL);
    LOKA_VERIFY(GetParent(text.paintHwnd()) == parent);
    LayoutState state;
    state.width = 240;
    text.layout(&controller, state);
    const short height = state.height;
    LOKA_VERIFY(height > 0);
    ShowWindow(root, SW_SHOWNOACTIVATE);
    flushTransparentPaint(controller, root);
    // Locate real glyph ink, so smoothing/font metrics cannot make the probe vacuous.
    POINT oldInk = {-1, -1};
    HDC dc = GetDC(text.paintHwnd());
    LOKA_VERIFY(dc != NULL);
    for (int y = 0; y < height && oldInk.x < 0; ++y)
      for (int x = 80; x < 240 && oldInk.x < 0; ++x)
      {
        const COLORREF pixel = GetPixel(dc, x, y);
        LOKA_VERIFY(pixel != CLR_INVALID);
        if (pixel != GetSysColor(COLOR_BTNFACE))
        {
          oldInk.x = x;
          oldInk.y = y;
        }
      }
    ReleaseDC(text.paintHwnd(), dc);
    LOKA_VERIFY(oldInk.x >= 80);
    {
      loka::core::StateTrackerGuard guard(&tracker);
      value.set(Styled(".", FontSize<12>()));
    }
    // Model the live-content layout input without a scene-wide repaint or a
    // props replacement that collapses the HWND and accidentally erases old ink.
    state.inputs = static_cast<NodeDirtyFlags>(NODE_DIRTY_PROPS | NODE_DIRTY_LAYOUT);
    text.layout(&controller, state);
    LOKA_VERIFY(state.height == height);
    flushTransparentPaint(controller, root);
    LOKA_VERIFY(windowPixel(text.paintHwnd(), oldInk.x, oldInk.y) == GetSysColor(COLOR_BTNFACE));
    text.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    controller.drainNativeRetirements();
  }

  void exerciseImageTransparency(Win32ScenePlatformController &controller, HWND parent, bool removeImage)
  {
    using namespace loka::app;
    using namespace loka::app::scene;
    HWND root = controller.rootHwnd();
    loka::core::PushStateTracker tracker;
    loka::core::MutableState<loka::core::resource::Image> image(blackTestImage(root));
    tracker.addState(&image);
    ImageViewProps props;
    props.image(&image).size(160, 80).attr(ImageViewAttr().fit(IMAGE_FIT_NONE));
    ImageViewNode node(props);
    Win32ImageViewContext context(&controller, parent, 0, 0, 160, 80, &node);
    HWND child = FindWindowExW(parent, NULL, L"LOKA_IMAGE_VIEW", NULL);
    LOKA_VERIFY(child != NULL);
    ShowWindow(root, SW_SHOWNOACTIVATE);
    flushTransparentPaint(controller, root);
    LOKA_VERIFY(windowPixel(child, 16, 8) == RGB(0, 0, 0));
    if (removeImage)
    {
      {
        loka::core::StateTrackerGuard guard(&tracker);
        image.set(loka::core::resource::Image::Empty());
      }
      flushTransparentPaint(controller, root);
      LOKA_VERIFY(windowPixel(child, 16, 8) == GetSysColor(COLOR_BTNFACE));
    }
    else
    {
      LOKA_VERIFY(windowPixel(child, 120, 60) == GetSysColor(COLOR_BTNFACE));
      // A fit change uses relayout, independently of the image observer.
      node.props.attr_.fit(IMAGE_FIT_STRETCH);
      context.relayout(0, 0, 160, 80);
      flushTransparentPaint(controller, root);
      LOKA_VERIFY(windowPixel(child, 120, 60) == RGB(0, 0, 0));
      node.props.attr_.fit(IMAGE_FIT_NONE);
      context.relayout(0, 0, 160, 80);
      flushTransparentPaint(controller, root);
      LOKA_VERIFY(windowPixel(child, 120, 60) == GetSysColor(COLOR_BTNFACE));
    }
    context.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    controller.drainNativeRetirements();
  }
} // namespace

void testWin32ImageViewTransparentLetterbox()
{
  TransparentPaintWindow host;
  Win32ScenePlatformController controller(
      host.window.hwnd(), loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
  exerciseImageTransparency(controller, host.window.hwnd(), false);
}

void testWin32ImageViewRemovalRestoresGround()
{
  TransparentPaintWindow host;
  Win32ScenePlatformController controller(
      host.window.hwnd(), loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
  exerciseImageTransparency(controller, host.window.hwnd(), true);
}

void testWin32AttributedTextShrinkRestoresGround()
{
  TransparentPaintWindow host;
  Win32ScenePlatformController controller(
      host.window.hwnd(), loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
  exerciseAttributedTextShrink(controller, host.window.hwnd());
}

void testWin32ScrollViewAttributedTextShrinkRestoresGround()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  TransparentPaintWindow host;
  Win32ScenePlatformController controller(host.window.hwnd(), loka::win32::Win32DisplayScale(96, RailMetrics()));
  ScrollViewNode node((ScrollViewProps()));
  Win32ScrollViewContext viewport(&controller, host.window.hwnd(), 10, 10, 280, 120, &node);
  LOKA_VERIFY(viewport.isValid());
  exerciseAttributedTextShrink(controller, viewport.hwnd());
  viewport.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
  controller.drainNativeRetirements();
}

void testWin32ScrollViewImageRemovalRestoresGround()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  TransparentPaintWindow host;
  Win32ScenePlatformController controller(host.window.hwnd(), loka::win32::Win32DisplayScale(96, RailMetrics()));
  ScrollViewNode node((ScrollViewProps()));
  Win32ScrollViewContext viewport(&controller, host.window.hwnd(), 10, 10, 280, 120, &node);
  LOKA_VERIFY(viewport.isValid());
  exerciseImageTransparency(controller, viewport.hwnd(), true);
  viewport.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
  controller.drainNativeRetirements();
}

namespace loka
{
  namespace testing
  {
    /** Test-only access to the existing Cell drawer; no production getter. */
    class Win32CellPaintAccess
    {
    public:
      static void draw(Win32CellContext &cell, HDC dc, const RECT &rect)
      {
        cell.drawCell(dc, rect);
      }
    };
  }
}

namespace
{
  void verifyStaticColors(HWND parent, HWND child)
  {
    HDC dc = CreateCompatibleDC(NULL);
    LOKA_VERIFY(dc != NULL);
    const COLORREF sentinel = RGB(1, 2, 3);
    if (sentinel == GetSysColor(COLOR_BTNTEXT))
      std::printf("[skip] STATIC sentinel equals live BTNTEXT; text mutation not discriminated\n");
    SetTextColor(dc, sentinel);
    SetBkMode(dc, OPAQUE);
    const HBRUSH direct = loka::win32::Win32StaticTextColors(dc);
    LOKA_VERIFY(direct == GetStockObject(NULL_BRUSH));
    LOKA_VERIFY(GetTextColor(dc) == GetSysColor(COLOR_BTNTEXT));
    LOKA_VERIFY(GetBkMode(dc) == TRANSPARENT);
    // Reset before dispatch: the direct helper call cannot mask missing wiring.
    SetTextColor(dc, sentinel);
    SetBkMode(dc, OPAQUE);
    const LRESULT brush = SendMessageW(parent, WM_CTLCOLORSTATIC,
        reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(child));
    LOKA_VERIFY(reinterpret_cast<HBRUSH>(brush) == GetStockObject(NULL_BRUSH));
    LOKA_VERIFY(GetTextColor(dc) == GetSysColor(COLOR_BTNTEXT));
    LOKA_VERIFY(GetBkMode(dc) == TRANSPARENT);
    DeleteDC(dc);
  }

  void noteEqualSystemColors(int first, int second, const char *pin)
  {
    if (GetSysColor(first) == GetSysColor(second))
      std::printf("[skip] %s: system indices %d/%d equal; substitution not discriminated\n", pin, first, second);
  }
}

void testWin32WindowAndViewportGroundRoles()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  TransparentPaintWindow host;
  HWND root = host.window.hwnd();
  Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
  ScrollViewNode scrollNode((ScrollViewProps()));
  Win32ScrollViewContext viewport(&controller, root, 10, 10, 180, 120, &scrollNode);
  LOKA_VERIFY(viewport.isValid());
  TextNode rootText((TextProps("root")));
  TextNode viewportText((TextProps("viewport")));
  Win32TextContext first(&controller, root, 0, 150, 80, 24, &rootText);
  Win32TextContext second(&controller, viewport.hwnd(), 0, 0, 80, 24, &viewportText);
  HWND firstHwnd = FindWindowExW(root, NULL, L"STATIC", L"root");
  HWND secondHwnd = FindWindowExW(viewport.hwnd(), NULL, L"STATIC", L"viewport");
  LOKA_VERIFY(firstHwnd != NULL && secondHwnd != NULL);
  ShowWindow(root, SW_SHOWNOACTIVATE);
  flushTransparentPaint(controller, root);
  noteEqualSystemColors(COLOR_BTNFACE, COLOR_WINDOW, "root/viewport ground");
  LOKA_VERIFY(windowPixel(root, 220, 100) == GetSysColor(COLOR_BTNFACE));
  LOKA_VERIFY(windowPixel(viewport.hwnd(), 120, 60) == GetSysColor(COLOR_BTNFACE));
  verifyStaticColors(root, firstHwnd);
  verifyStaticColors(viewport.hwnd(), secondHwnd);
  first.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
  second.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
  viewport.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
  controller.drainNativeRetirements();
}

void testWin32CellGroundAndTextRoles()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  TransparentPaintWindow host;
  HWND root = host.window.hwnd();
  Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
  CellNode node(CellProps().text("MMMM"));
  Win32CellContext cell(&controller, root, 0, 0, 160, 48, &node);
  HWND child = FindWindowExW(root, NULL, L"LOKA_CELL", NULL);
  LOKA_VERIFY(child != NULL);
  ShowWindow(root, SW_SHOWNOACTIVATE);
  flushTransparentPaint(controller, root);
  LOKA_VERIFY(windowPixel(child, 4, 4) == GetSysColor(COLOR_BTNFACE));

  HDC source = GetDC(root);
  LOKA_VERIFY(source != NULL);
  HDC actual = CreateCompatibleDC(source), expected = CreateCompatibleDC(source);
  HBITMAP actualBitmap = CreateCompatibleBitmap(source, 160, 48);
  HBITMAP expectedBitmap = CreateCompatibleBitmap(source, 160, 48);
  LOKA_VERIFY(actual && expected && actualBitmap && expectedBitmap);
  HGDIOBJ oldActual = SelectObject(actual, actualBitmap);
  HGDIOBJ oldExpected = SelectObject(expected, expectedBitmap);
  LOKA_VERIFY(oldActual && oldActual != HGDI_ERROR && oldExpected && oldExpected != HGDI_ERROR);
  const RECT rect = {0, 0, 160, 48};
  const COLORREF sentinel = RGB(1, 2, 3);
  if (sentinel == GetSysColor(COLOR_BTNTEXT))
    std::printf("[skip] Cell sentinel equals live BTNTEXT; text mutation not discriminated\n");
  SetTextColor(actual, sentinel);
  SetBkMode(actual, OPAQUE);
  loka::testing::Win32CellPaintAccess::draw(cell, actual, rect);
  LOKA_VERIFY(GetTextColor(actual) == sentinel);
  LOKA_VERIFY(GetBkMode(actual) == OPAQUE);
  LOKA_VERIFY(FillRect(expected, &rect, GetSysColorBrush(COLOR_BTNFACE)));
  HGDIOBJ oldFont = NULL;
  if (controller.displayFont())
    oldFont = SelectObject(expected, controller.displayFont());
  SetTextColor(expected, GetSysColor(COLOR_BTNTEXT));
  SetBkMode(expected, TRANSPARENT);
  RECT label = rect;
  LOKA_VERIFY(DrawTextW(expected, L"MMMM", 4, &label,
      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX));
  int ink = 0;
  // Compare GDI's same-font reference, including antialiasing. Exclude the
  // Cell's black frame: it is explicitly a non-role color (#1208).
  for (int y = 1; y < 47; ++y)
    for (int x = 1; x < 159; ++x)
    {
      const COLORREF reference = GetPixel(expected, x, y);
      LOKA_VERIFY(reference != CLR_INVALID);
      LOKA_VERIFY(GetPixel(actual, x, y) == reference);
      if (reference != GetSysColor(COLOR_BTNFACE))
        ++ink;
    }
  noteEqualSystemColors(COLOR_BTNTEXT, COLOR_BTNFACE, "Cell glyph visibility");
  if (GetSysColor(COLOR_BTNTEXT) != GetSysColor(COLOR_BTNFACE))
    LOKA_VERIFY(ink > 0);
  if (oldFont) SelectObject(expected, oldFont);
  SelectObject(actual, oldActual);
  SelectObject(expected, oldExpected);
  DeleteObject(actualBitmap);
  DeleteObject(expectedBitmap);
  DeleteDC(actual);
  DeleteDC(expected);
  ReleaseDC(root, source);
  cell.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
  controller.drainNativeRetirements();
}

void testWin32RectSurfaceGroundAndSpriteRoles()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  TransparentPaintWindow host;
  HWND root = host.window.hwnd();
  Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
  RectSurfaceModel value;
  value.rectCount = 1;
  value.rects[0] = RectSprite(8, 8, 16, 16);
  loka::core::MutableState<RectSurfaceModel> model(value);
  RectSurfaceProps props;
  props.model(&model).size(100, 60);
  RectSurfaceNode node(props);
  Win32RectSurfaceContext context(&controller, root, 0, 0, 100, 60, &node);
  HWND child = FindWindowExW(root, NULL, L"LOKA_RECT_SURFACE", NULL);
  LOKA_VERIFY(child != NULL);
  ShowWindow(root, SW_SHOWNOACTIVATE);
  flushTransparentPaint(controller, root);
  noteEqualSystemColors(COLOR_WINDOW, COLOR_BTNFACE, "RectSurface ground");
  noteEqualSystemColors(COLOR_WINDOWTEXT, COLOR_BTNTEXT, "RectSurface sprite text role");
  // A shipped light theme cannot distinguish the old stock brushes. Report
  // this VM coverage limit explicitly; the host brush-identity pin kills
  // both stock-brush reversions independently of the live theme.
  LOGBRUSH oldGround, oldSprite;
  LOKA_VERIFY(GetObjectW(GetStockObject(WHITE_BRUSH), sizeof(oldGround), &oldGround) != 0);
  LOKA_VERIFY(GetObjectW(GetStockObject(BLACK_BRUSH), sizeof(oldSprite), &oldSprite) != 0);
  if (GetSysColor(COLOR_WINDOW) == oldGround.lbColor)
    std::printf("[skip] RectSurface stock WHITE_BRUSH reversion equals live WINDOW\n");
  if (GetSysColor(COLOR_WINDOWTEXT) == oldSprite.lbColor)
    std::printf("[skip] RectSurface stock BLACK_BRUSH reversion equals live WINDOWTEXT\n");
  LOKA_VERIFY(windowPixel(child, 60, 40) == GetSysColor(COLOR_WINDOW));
  LOKA_VERIFY(windowPixel(child, 12, 12) == GetSysColor(COLOR_WINDOWTEXT));
  context.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
  controller.drainNativeRetirements();
}

void testWin32LiveGroundLegibility()
{
  using namespace loka::app;
  using namespace loka::win32;
  // Only the live theme's fact. High contrast/user-composed colors (page 3)
  // are outside the shipped-light-theme acceptance run.
  const SurfaceGround roles[] = {SURFACE_GROUND_WINDOW, SURFACE_GROUND_DOCUMENT, SURFACE_GROUND_CONTROL};
  for (unsigned g = 0; g < sizeof(roles) / sizeof(roles[0]); ++g)
    for (unsigned t = 0; t < sizeof(roles) / sizeof(roles[0]); ++t)
    {
      int index = -1;
      LOKA_VERIFY(QueryWin32GroundColor(roles[g], index));
      const int text = Win32TextRoleColor(roles[t]);
      const COLORREF bg = GetSysColor(index), fg = GetSysColor(text);
      const double ratio = loka_test::ContrastRatio(
          loka_test::RelativeLuminance(GetRValue(bg) / 255.0, GetGValue(bg) / 255.0, GetBValue(bg) / 255.0),
          loka_test::RelativeLuminance(GetRValue(fg) / 255.0, GetGValue(fg) / 255.0, GetBValue(fg) / 255.0));
      std::printf("live Win32 text=%d ground=%d contrast=%.3f\n", text, index, ratio);
      LOKA_VERIFY(ratio >= 4.5);
    }
}
