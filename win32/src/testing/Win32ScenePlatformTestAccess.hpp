#ifndef LOKA_WIN32_SCENE_PLATFORM_TEST_ACCESS_HPP
#define LOKA_WIN32_SCENE_PLATFORM_TEST_ACCESS_HPP

#include "../Win32ScenePlatformController.hpp"
#include "../Win32BitmapCapture.hpp"

namespace loka
{
  namespace dsl
  {
    namespace testing
    {
      class Win32ScenePlatformTestAccess
      {
      public:
        static bool captureWindowClientBitmap(PlatformContext &context, HWND hwnd, ::loka::core::resource::Image &out)
        {
          return ::loka::win32::CaptureWindowClientBitmap(&context, hwnd, out);
        }

        struct PendingInvalidationSnapshot
        {
          PendingInvalidationSnapshot()
              : hwnd(0),
                eraseBackground(FALSE),
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

        typedef ::Win32ScenePlatformController::RedrawStats RedrawStats;

        static const RedrawStats &redrawStats(const ::Win32ScenePlatformController &controller)
        {
          return controller.redrawStats_;
        }

        // Match synchronize's invalidation flags, but let the message pump
        // paint after flushing so synchronize's stats reset cannot hide it.
        static void flushPendingInvalidations(::Win32ScenePlatformController &controller)
        {
          controller.flushPendingInvalidations(false);
        }

        static void resetRedrawStats(::Win32ScenePlatformController &controller)
        {
          controller.redrawStats_.reset();
        }

        static int onChangeCalls(const ::Win32ScenePlatformController &controller)
        {
          return controller.redrawStats_.onChangeCalls;
        }

        static int onBoundaryApplyCalls(const ::Win32ScenePlatformController &controller)
        {
          return controller.redrawStats_.onBoundaryApplyCalls;
        }

        static ::loka::app::scene::NodeDirtyFlags lastOnChangeFlags(const ::Win32ScenePlatformController &controller)
        {
          return controller.redrawStats_.lastOnChangeFlags;
        }

        static bool lastOnChangeRequiredLayout(const ::Win32ScenePlatformController &controller)
        {
          return controller.redrawStats_.lastOnChangeRequiredLayout;
        }

        static bool lastOnChangeFullRebuild(const ::Win32ScenePlatformController &controller)
        {
          return controller.redrawStats_.lastOnChangeFullRebuild;
        }

        static int queuedFullWindowInvalidates(const ::Win32ScenePlatformController &controller)
        {
          return controller.redrawStats_.queuedFullWindowInvalidates;
        }

        static int queuedRectInvalidates(const ::Win32ScenePlatformController &controller)
        {
          return controller.redrawStats_.queuedRectInvalidates;
        }

        static int queuedLayoutBoundsInvalidates(const ::Win32ScenePlatformController &controller)
        {
          return controller.redrawStats_.queuedLayoutBoundsInvalidates;
        }

        static int queuedPaintBoundsInvalidates(const ::Win32ScenePlatformController &controller)
        {
          return controller.redrawStats_.queuedPaintBoundsInvalidates;
        }

        static int queuedMissingBoundsInvalidates(const ::Win32ScenePlatformController &controller)
        {
          return controller.redrawStats_.queuedMissingBoundsInvalidates;
        }

        static int queuedCompositedInvalidates(const ::Win32ScenePlatformController &controller)
        {
          return controller.redrawStats_.queuedCompositedInvalidates;
        }

        static int queuedOpaquePaintInvalidates(const ::Win32ScenePlatformController &controller)
        {
          return controller.redrawStats_.queuedOpaquePaintInvalidates;
        }

        static int queuedGenericPaintInvalidates(const ::Win32ScenePlatformController &controller)
        {
          return controller.redrawStats_.queuedGenericPaintInvalidates;
        }

        static int queuedPaintInvalidates(const ::Win32ScenePlatformController &controller)
        {
          return queuedCompositedInvalidates(controller) + queuedOpaquePaintInvalidates(controller)
                 + queuedGenericPaintInvalidates(controller);
        }

        /** The layout door a WM_SIZE would reach: the display-font retry runs here. */
        static void performLayout(::Win32ScenePlatformController &controller, int clientWidth, int clientHeight)
        {
          controller.performLayout(clientWidth, clientHeight);
        }

        /** Test-owned guards exercise frames before a rail creates clip parents. */
        static ::loka::app::scene::ProjectionParentScopeStack &
        projectionParentScopes(::Win32ScenePlatformController &controller)
        {
          return controller.projectionParentScopes_;
        }

        static int layoutNode(::Win32ScenePlatformController &controller,
                              ::loka::app::scene::Node *node,
                              const ::Win32ScenePlatformController::LayoutState &state)
        {
          return controller.layoutNode(node, state);
        }

        static int clientWidth(const ::Win32ScenePlatformController &controller)
        {
          return controller.clientWidth_;
        }

        static int clientHeight(const ::Win32ScenePlatformController &controller)
        {
          return controller.clientHeight_;
        }

        static bool queryPendingInvalidation(const ::Win32ScenePlatformController &controller,
                                             std::size_t index,
                                             PendingInvalidationSnapshot &out)
        {
          if (index >= controller.pendingInvalidations_.size())
          {
            return false;
          }
          const ::Win32ScenePlatformController::PendingInvalidate &entry = controller.pendingInvalidations_[index];
          out.hwnd = entry.hwnd;
          out.rect = entry.rect;
          out.eraseBackground = entry.eraseBackground;
          out.fullWindow = entry.fullWindow;
          out.includeChildren = entry.includeChildren;
          return true;
        }
      };
    } // namespace testing
  } // namespace dsl
} // namespace loka

#endif // LOKA_WIN32_SCENE_PLATFORM_TEST_ACCESS_HPP
