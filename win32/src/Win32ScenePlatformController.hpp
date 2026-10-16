#ifndef LOKA_WIN32_SCENE_PLATFORM_CONTROLLER_HPP
#define LOKA_WIN32_SCENE_PLATFORM_CONTROLLER_HPP

#include <windows.h>
#include "Win32TextEnvironment.hpp"
#include "app/layout/TextShaping.hpp"
#include "app/layout/NaturalWidthSource.hpp"
#include <vector>
#include "app/layout/RowLayout.hpp"
#include "app/RectSurface.hpp"
#include "app/scene/projection/ProjectionParentScope.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "app/scene/projection/PlatformLayoutHandler.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"
#include "platform/Win32DisplayFont.hpp"
#include "platform/Win32DisplayScale.hpp"

class PlatformContext;

class Win32ButtonContext;
class Win32EditTextContext;
class Win32NativeLayoutPass;
class Win32PopupMenuContext;
class Win32RetirableContext;
class Win32TextContext;
class Win32AttributedTextContext;
class Win32ScrollViewContext;

namespace loka
{
  namespace win32
  {
    /** Candidate rail values; shared by scene and pre-scene window projection. */
    app::RailMetrics DefaultRailMetrics();
  }

  namespace core
  {
    namespace scene
    {
      class Node;
    }
  } // namespace core

  namespace app
  {
    class BoxNode;
    class RectSurfaceNode;
    class ScrollViewNode;
  } // namespace app

  namespace dsl
  {
    namespace testing
    {
      class Win32ScenePlatformTestAccess;
    }
  } // namespace dsl

  namespace app
  {
    namespace scene
    {
      class Win32PlatformLayoutTraversal;
    }
  } // namespace app
} // namespace loka

#ifdef TEST_BUILD
namespace loka
{
  namespace win32
  {
    namespace testing
    {
      void failNativeChildWindowCreations(unsigned count);
      bool refuseNativeChildWindowCreation();
    } // namespace testing
  } // namespace win32
} // namespace loka
#endif

class Win32ScenePlatformController : public loka::app::scene::IPlatformController,
    public loka::app::layout::INaturalWidthSource
{
public:
  virtual bool queryNaturalWidth(loka::app::scene::Node *child, short &width) const;
  /** Retire door for a RectSurface context: takes back that surface's pending
      seat rows so a surface reclaimed during delivery publishes nothing. */
  void cancelRectSurfaceExtent(loka::app::RectSurfaceNode *surface)
  {
    this->rectSurfaceExtentLedger_.cancel(surface);
  }
  enum NativePaintKind
  {
    NATIVE_PAINT_ROOT = 0,
    NATIVE_PAINT_CELL = 1,
    NATIVE_PAINT_IMAGE = 2,
    NATIVE_PAINT_RECT_SURFACE = 3
  };

  /** A layout offer in lu. Geometry moves along a pass; the carried gap is fixed
      when the offer is made (root seed, common-to-native bridge, or the dispatch
      entry's resolution), so the constructor is its only writer. Deliberate twin of
      MacScenePlatformController::LayoutState. */
  struct LayoutState
  {
    typedef int Coordinate;

    LayoutState(int xValue, int yValue, int widthValue, int heightValue, int spacingValue)
        : x(xValue),
          y(yValue),
          width(widthValue),
          height(heightValue),
          spacing_(spacingValue)
    {
    }
    int spacing() const { return this->spacing_; }

    int x;
    int y;
    int width;
    int height;

  private:
    int spacing_;
  };

  struct LayoutNodeResult
  {
    LayoutNodeResult()
        : boundaryWidth(0),
          resultY(0)
    {
    }
    LayoutNodeResult(int width, int y)
        : boundaryWidth(width),
          resultY(y)
    {
    }

    int boundaryWidth;
    int resultY;
  };

  Win32ScenePlatformController(HWND rootHwnd,
                               const loka::win32::Win32DisplayScale &displayScale,
                               PlatformContext *platformContext = 0);
  virtual ~Win32ScenePlatformController();
  /** Capture borrows the Window's ancestor context; standalone controllers
      without that owner refuse. No Image retains this controller. */
  bool captureWindowClientBitmap(HWND hwnd, loka::core::resource::Image &out) const;

  static void requestDirtyRect(HWND targetHwnd, const RECT *rect, BOOL eraseBackground);
  static void requestDirtySubtree(HWND targetHwnd, const RECT *rect, BOOL eraseBackground);
  /** The repaint path of a transparent child (#1199): its parent repaints the
      child's rectangle, ground and overlapping siblings included, before the
      child draws its foreground. Answers false when the child has no parent
      or no rectangle, and requests nothing then. */
  static bool requestTransparentChildRepaint(HWND child);
  /** Shared root/viewport ground for WM_PAINT, including paint-only damage. */
  static void paintWindowGround(HDC dc, const RECT &rect);
  static void redrawDirtySubtreeNow(HWND targetHwnd, const RECT *rect, BOOL eraseBackground);
  static void noteNativePaint(HWND targetHwnd, NativePaintKind kind, bool eraseBackground);

  virtual void onChange(loka::app::scene::Node *rootNode, loka::app::scene::NodeDirtyFlags flags, bool fullRebuild);
  virtual void onBoundaryApply(loka::app::scene::Node *rootNode,
                               loka::app::scene::BoundaryNode *boundary,
                               const loka::app::scene::BoundaryLocalApplyInfo &info,
                               const loka::app::scene::PlatformApplyPlan &plan);
  virtual bool canSkipGlobalChangeForBoundaryLocalPaint() const
  {
    return true;
  }
  virtual bool readNativeFocus(loka::app::scene::NodeContext *&out);
  virtual bool applyNativeFocus(loka::app::scene::NodeContext &ctx);
  virtual void beginApplyCycle();
  virtual void synchronize();
  virtual bool hasPendingSync() const;
  virtual void drainNativeRetirements();
  virtual void destroy();
  virtual void releaseMenu();
  virtual void releaseNodeContexts(loka::app::scene::Node *node);
  virtual bool prepareProjectedLayout(loka::app::scene::Node *node, loka::app::scene::LayoutState &state);
  virtual bool registerNodeHandler(loka::app::scene::IPlatformNodeHandler *handler);

  bool handleCommand(WPARAM wParam, LPARAM lParam);
  /** Relayout entry for WM_SIZE and other device-pixel client readings. */
  void relayoutNativeClientPixels(int clientWidth, int clientHeight);
  void relayout(int clientWidth, int clientHeight);
  /** Replaces the projection fact and makes the native subtree's DPI-derived
      font retryably converge without exposing an unowned font handle. */
  void updateDisplayScale(const loka::win32::Win32DisplayScale &displayScale);
  const loka::win32::Win32DisplayScale &displayScale() const
  {
    return this->displayScale_;
  }
  HFONT textFont(const loka::app::TextStyle &style) const
  {
    return this->displayFont_.find(style);
  }
  /** Schedule through the existing WM_SIZE layout path, never reenter layout. */
  void requestRelayout();
  loka::app::TextShaping textShaping() const { return this->textShaping_; }
  /** One construction-time height for Row alignment and Button layout;
      a declined native measurement keeps the fallback. */
  const loka::app::layout::RowLayoutMetrics &rowLayoutMetrics() const
  {
    return this->rowLayoutMetrics_;
  }

  HFONT displayFont() const
  {
    return this->displayFont_.get();
  }
  HWND rootHwnd() const
  {
    return rootHwnd_;
  }
  /** Native parent for a projected child. The root remains the default;
      an active ScrollView scope supplies its viewport HWND. */
  HWND projectionParentHwnd() const
  {
    return static_cast<HWND>(this->projectionParentScopes_.current().nativeParent);
  }
  void queueNativeRetirement(HWND hwnd);

private:
  /** Display-font text extent plus title insets, returned in logical layout units. */
  bool measurePushButtonNaturalWidth(const loka::core::String &label, short &width) const;

  friend class ::loka::dsl::testing::Win32ScenePlatformTestAccess;
  friend class ::loka::app::scene::Win32PlatformLayoutTraversal;
  friend class ::Win32NativeLayoutPass;
  friend class ::Win32RetirableContext;
  friend class ::Win32TextContext;
  friend class ::Win32AttributedTextContext;
  friend void RegisterWin32BuiltInSupport(Win32ScenePlatformController &controller);

  struct RedrawStats
  {
    RedrawStats()
        : onChangeCalls(0),
          onBoundaryApplyCalls(0),
          lastOnChangeFlags(loka::app::scene::NODE_DIRTY_NONE),
          lastOnChangeRequiredLayout(false),
          lastOnChangeFullRebuild(false),
          queuedFullWindowInvalidates(0),
          queuedRectInvalidates(0),
          queuedLayoutBoundsInvalidates(0),
          queuedPaintBoundsInvalidates(0),
          queuedMissingBoundsInvalidates(0),
          queuedCompositedInvalidates(0),
          queuedOpaquePaintInvalidates(0),
          queuedGenericPaintInvalidates(0),
          rootEraseCount(0),
          rootPaintCount(0),
          cellEraseCount(0),
          cellPaintCount(0),
          imageEraseCount(0),
          imagePaintCount(0),
          rectSurfaceEraseCount(0),
          rectSurfacePaintCount(0)
    {
    }

    void reset()
    {
      onChangeCalls = 0;
      onBoundaryApplyCalls = 0;
      lastOnChangeFlags = loka::app::scene::NODE_DIRTY_NONE;
      lastOnChangeRequiredLayout = false;
      lastOnChangeFullRebuild = false;
      queuedFullWindowInvalidates = 0;
      queuedRectInvalidates = 0;
      queuedLayoutBoundsInvalidates = 0;
      queuedPaintBoundsInvalidates = 0;
      queuedMissingBoundsInvalidates = 0;
      queuedCompositedInvalidates = 0;
      queuedOpaquePaintInvalidates = 0;
      queuedGenericPaintInvalidates = 0;
      rootEraseCount = 0;
      rootPaintCount = 0;
      cellEraseCount = 0;
      cellPaintCount = 0;
      imageEraseCount = 0;
      imagePaintCount = 0;
      rectSurfaceEraseCount = 0;
      rectSurfacePaintCount = 0;
    }

    int onChangeCalls;
    int onBoundaryApplyCalls;
    loka::app::scene::NodeDirtyFlags lastOnChangeFlags;
    bool lastOnChangeRequiredLayout;
    bool lastOnChangeFullRebuild;
    int queuedFullWindowInvalidates;
    int queuedRectInvalidates;
    int queuedLayoutBoundsInvalidates;
    int queuedPaintBoundsInvalidates;
    int queuedMissingBoundsInvalidates;
    int queuedCompositedInvalidates;
    int queuedOpaquePaintInvalidates;
    int queuedGenericPaintInvalidates;
    int rootEraseCount;
    int rootPaintCount;
    int cellEraseCount;
    int cellPaintCount;
    int imageEraseCount;
    int imagePaintCount;
    int rectSurfaceEraseCount;
    int rectSurfacePaintCount;
  };

  struct PendingInvalidate
  {
    PendingInvalidate()
        : hwnd(0),
          eraseBackground(TRUE),
          fullWindow(false),
          includeChildren(false)
    {
      rect.left = rect.top = rect.right = rect.bottom = 0;
    }
    HWND hwnd;
    RECT rect;
    BOOL eraseBackground;
    bool fullWindow;
    bool includeChildren;
  };

  static LayoutNodeResult DispatchProjectedLayout(
      Win32ScenePlatformController *controller,
      loka::app::scene::Node *node,
      const LayoutState &state);

  static int layoutContainerChild(void *context, loka::app::scene::Node *child, const LayoutState &state);
  int layoutNodeFromSceneState(loka::app::scene::Node *node, const loka::app::scene::LayoutState &state);
  int layoutNode(loka::app::scene::Node *node, const LayoutState &state);
  LayoutNodeResult computeLayoutResult(loka::app::scene::Node *node, const LayoutState &state);
  LayoutNodeResult layoutScrollViewNode(loka::app::ScrollViewNode *scrollView, const LayoutState &state);
  int applyBoundaryLayoutResult(loka::app::scene::BoundaryNode *boundary, int x, int y, const LayoutNodeResult &result);
  LayoutNodeResult layoutRectSurfaceNode(loka::app::RectSurfaceNode *surface, const LayoutState &state);
  bool narrowLayoutState(const LayoutState &state,
                         loka::app::scene::LayoutState &narrowed,
                         bool applyProjection);
  bool refuseNarrowingInScrollScope(int resultY);
  void refuseScrollViewShortRange();
  void performLayout(int clientWidth, int clientHeight);
  /** Position one projected HWND. A root layout pass suppresses per-child
      repaint and presents the completed native layout once at the pass end. */
  void positionNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry);
  void resizeNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry);
  HWND createNativeChildWindow(DWORD exStyle,
                               LPCWSTR className,
                               LPCWSTR windowName,
                               DWORD style,
                               const loka::win32::NativeRect &geometry,
                               HWND parent,
                               HMENU menu,
                               HINSTANCE instance,
                               void *createParameter);
  void applyDisplayFontToNativeSubtree(const loka::win32::Win32DisplayFont &replacement);
  Win32TextEnvironment textEnvironment_;
  void ensureDisplayFont();
  void clearContexts();
  void clearNodeContexts(loka::app::scene::Node *node);
  int measureClientWidth(int requestedWidth) const;
  void queueDirtyRect(HWND targetHwnd, const loka::win32::NativeRect *rect, BOOL eraseBackground, bool includeChildren);
  static UINT pendingInvalidationFlags(const PendingInvalidate &entry);
  /** Flush native damage and replay intersecting later siblings without erasing. */
  void flushPendingInvalidations(bool updateNow);
  void dumpRedrawStatsIfNeeded();

  PlatformContext *const platformContext_; // Ancestor outlives this controller and its contexts.
  HWND rootHwnd_;
  Win32NativeLayoutPass *activeNativeLayoutPass_;
  loka::app::RectSurfaceExtentLedger rectSurfaceExtentLedger_;
  loka::app::scene::ProjectionParentScopeStack projectionParentScopes_;
  loka::app::scene::PlatformLayoutHandlerRegistry layoutHandlerRegistry_;
  loka::app::scene::PlatformNodeHandlerRegistry nodeHandlerRegistry_;
  loka::app::scene::Node *rootNode_;
  int clientWidth_;
  int clientHeight_;
  const loka::app::RailMetrics railMetrics_;
  const loka::app::TextShaping textShaping_;
  loka::win32::Win32DisplayScale displayScale_;
  loka::win32::Win32DisplayFont displayFont_;
  // Keep the construction measurement across DPI changes: Stack owns a copy.
  // Font and inset scale together; remeasurement could split Row/Button heights.
  const loka::app::layout::RowLayoutMetrics rowLayoutMetrics_;
  std::vector<PendingInvalidate> pendingInvalidations_;
  std::vector<HWND> retiredWindows_;
  RedrawStats redrawStats_;
};

#endif // LOKA_WIN32_SCENE_PLATFORM_CONTROLLER_HPP
