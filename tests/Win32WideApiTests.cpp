#include "context/Win32ZStackContext.hpp"
#include "Win32WideApiTests.hpp"
#include "support/TestVerify.hpp"

#include <cassert>
#include <cstdio>
#include <cwchar>
#include <windows.h>

#include "Win32ScenePlatformController.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "context/Win32CellContext.hpp"
#include "context/Win32RibbonGroupEdgeContext.hpp"
#include "context/Win32CardBandContext.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "context/Win32AttributedTextContext.hpp"
#include "context/Win32ImageViewContext.hpp"
#include "context/Win32RectSurfaceContext.hpp"
#include "context/Win32ScrollViewContext.hpp"
#include "core/State.hpp"

namespace
{
  struct ClassWindowQuery
  {
    explicit ClassWindowQuery(const wchar_t *className)
        : className_(className), hwnd_(0)
    {
    }

    const wchar_t *className_;
    HWND hwnd_;
  };

  BOOL CALLBACK FindClassWindowThunk(HWND hwnd, LPARAM lParam)
  {
    ClassWindowQuery *query = reinterpret_cast<ClassWindowQuery *>(lParam);
    wchar_t className[64];
    const int length = GetClassNameW(hwnd, className, sizeof(className) / sizeof(className[0]));
    if (length > 0 && std::wcscmp(className, query->className_) == 0)
    {
      query->hwnd_ = hwnd;
      return FALSE;
    }
    return TRUE;
  }

  HWND findChildWindowByClass(HWND root, const wchar_t *className)
  {
    ClassWindowQuery query(className);
    EnumChildWindows(root, FindClassWindowThunk, reinterpret_cast<LPARAM>(&query));
    return query.hwnd_;
  }

  void verifyWideChildWindow(HWND root, const wchar_t *className)
  {
    HWND child = findChildWindowByClass(root, className);
    LOKA_VERIFY(child && "the context must materialize its named child window");
    LOKA_VERIFY(IsWindowUnicode(child) &&
                "RegisterClass, CreateWindowEx, and DefWindowProc must use the same wide API family");
  }
} // namespace

void testWin32CustomWindowClassesUseWideApiFamily()
{
  std::printf("\n==== [testWin32CustomWindowClassesUseWideApiFamily] start ====\n");
  HWND root = CreateWindowExW(
      0, L"STATIC", L"wide-api-host", WS_OVERLAPPED, 0, 0, 320, 240, NULL, NULL, GetModuleHandleW(NULL), NULL);
  assert(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));

    loka::app::CellProps cellProps;
    loka::app::CellNode cellNode(cellProps);
    Win32CellContext cell(&controller, root, 0, 0, 80, 24, &cellNode);
    verifyWideChildWindow(root, L"LOKA_CELL");

    loka::app::RibbonGroupEdgeNode edgeNode((loka::app::RibbonGroupEdgeProps()));
    Win32RibbonGroupEdgeContext edge(&controller, root, 85, 0, 1, 24, &edgeNode);
    verifyWideChildWindow(root, L"LOKA_RIBBON_GROUP_EDGE");
    HWND edgeWindow = findChildWindowByClass(root, L"LOKA_RIBBON_GROUP_EDGE");
    const wchar_t edgeTitle[] = {0xff21, 0xd83d, 0xde00, 0};
    LOKA_VERIFY(SetWindowTextW(edgeWindow, edgeTitle));
    wchar_t edgeRoundTrip[8];
    LOKA_VERIFY(GetWindowTextW(edgeWindow, edgeRoundTrip, 8) == 3);
    LOKA_VERIFY(std::wcscmp(edgeRoundTrip, edgeTitle) == 0);

    loka::app::CardBandNode bandNode((loka::app::CardBandProps()));
    Win32CardBandContext band(&controller, root, 100, 0, 40, 3, &bandNode);
    verifyWideChildWindow(root, L"LOKA_CARD_BAND");
    LOKA_VERIFY(SetWindowTextW(band.hwnd(), edgeTitle));
    LOKA_VERIFY(GetWindowTextW(band.hwnd(), edgeRoundTrip, 8) == 3);
    LOKA_VERIFY(std::wcscmp(edgeRoundTrip, edgeTitle) == 0);

    loka::app::ImageViewProps imageProps;
    loka::app::ImageViewNode imageNode(imageProps);
    Win32ImageViewContext image(&controller, root, 0, 30, 80, 60, &imageNode);
    verifyWideChildWindow(root, L"LOKA_IMAGE_VIEW");

    loka::core::MutableState<loka::app::RectSurfaceModel> model((loka::app::RectSurfaceModel()));
    loka::app::RectSurfaceProps surfaceProps;
    surfaceProps.model(&model).size(80, 60);
    loka::app::RectSurfaceNode surfaceNode(surfaceProps);
    Win32RectSurfaceContext surface(&controller, root, 0, 100, 80, 60, &surfaceNode);
    verifyWideChildWindow(root, L"LOKA_RECT_SURFACE");

    loka::app::AttributedTextNode attributedNode((loka::app::AttributedTextProps()));
    Win32AttributedTextContext attributed(&controller, root, 0, 160, 80, 30, &attributedNode);
    verifyWideChildWindow(root, L"LOKA_ATTRIBUTED_TEXT");
    // Default handling must preserve UTF-16 too, including a surrogate pair.
    const wchar_t payload[] = {0xff21, 0xd83d, 0xde00, 0};
    LOKA_VERIFY(SetWindowTextW(attributed.paintHwnd(), payload));
    wchar_t roundTrip[8];
    LOKA_VERIFY(GetWindowTextW(attributed.paintHwnd(), roundTrip, 8) == 3);
    LOKA_VERIFY(std::wcscmp(roundTrip, payload) == 0);

    loka::app::ScrollViewNode scrollViewNode((loka::app::ScrollViewProps()));
    Win32ScrollViewContext scrollView(
        &controller, root, 90, 0, 80, 60, &scrollViewNode);
    verifyWideChildWindow(root, L"LOKA_SCROLL_VIEW");
    verifyWideChildWindow(root, L"LOKA_SCROLL_FRAME");

    Win32ZStackContext units(&controller);
    Win32ZStackContext::Unit **cursor = units.unitsBegin();
    Win32ZStackContext::Unit *unit = units.unitFor(1, cursor);
    LOKA_VERIFY(unit && unit->ensure(root, controller.displayScale().projectFrame(loka::core::Frame(0, 0, 80, 30))));
    verifyWideChildWindow(root, L"LOKA_STACK_UNIT");
    units.onFactChanged(loka::app::scene::NODE_FACT_ATTACHED, loka::app::scene::NODE_FACT_RETIRED);

    attributed.onFactChanged(loka::app::scene::NODE_FACT_ATTACHED, loka::app::scene::NODE_FACT_RETIRED);
    band.onFactChanged(loka::app::scene::NODE_FACT_ATTACHED, loka::app::scene::NODE_FACT_RETIRED);
    edge.onFactChanged(loka::app::scene::NODE_FACT_ATTACHED, loka::app::scene::NODE_FACT_RETIRED);
    cell.onFactChanged(loka::app::scene::NODE_FACT_ATTACHED, loka::app::scene::NODE_FACT_RETIRED);
    image.onFactChanged(loka::app::scene::NODE_FACT_ATTACHED, loka::app::scene::NODE_FACT_RETIRED);
    surface.onFactChanged(loka::app::scene::NODE_FACT_ATTACHED, loka::app::scene::NODE_FACT_RETIRED);
    scrollView.onFactChanged(loka::app::scene::NODE_FACT_ATTACHED,
                             loka::app::scene::NODE_FACT_RETIRED);
    controller.drainNativeRetirements();
  }
  DestroyWindow(root);
  std::printf("==== [testWin32CustomWindowClassesUseWideApiFamily] PASSED ====\n");
}
