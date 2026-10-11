#include "MacWindowFrameClampTests.hpp"
#include "support/TestVerify.hpp"

#include <AppKit/AppKit.h>

#include "MacWindow.hpp"
#include "MacScenePlatformController.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "testing/MacWindowTestAccess.hpp"

namespace
{
  typedef loka::dsl::testing::MacWindowTestAccess NativeAccess;

  void verifyPlacedFrame(MacWindow &window, NSSize expectedContentSize)
  {
    NSWindow *native = (NSWindow *)NativeAccess::nativeWindow(window);
    LOKA_VERIFY(native != nil);
    LOKA_VERIFY([native isVisible]);
    NSScreen *screen = [native screen];
    LOKA_VERIFY(screen != nil);
    const NSRect frame = [native frame];
    LOKA_VERIFY(NSContainsRect([screen visibleFrame], frame));
    const NSRect content = [native contentRectForFrameRect:frame];
    LOKA_VERIFY(NSEqualSizes(content.size, expectedContentSize));

    // Deliberately mirror NativeContentFrame / VisibleTopForScreen in
    // MacWindow.mm, including the menu-bar fallback and integer truncation.
    const NSRect screenFrame = [screen frame];
    const NSRect visibleFrame = [screen visibleFrame];
    CGFloat top = visibleFrame.origin.y + visibleFrame.size.height;
    if (visibleFrame.size.height >= screenFrame.size.height - 1.0)
    {
      top = screenFrame.origin.y + screenFrame.size.height
            - [[NSStatusBar systemStatusBar] thickness];
    }
    const loka::macos::MacProjection projection(
        NativeAccess::contentView(window), loka::macos::DefaultRailMetrics());
    // The origin places the outer frame, the exact inverse of the intent (#526).
    const loka::core::Frame actual(static_cast<int>(frame.origin.x),
                                   static_cast<int>(top - (frame.origin.y + frame.size.height)),
                                   projection.clientCapacityToLu(content.size.width),
                                   projection.clientCapacityToLu(content.size.height));
    LOKA_VERIFY(window.nativeFrame().get() == actual);
  }
}

void testMacWindowDeclaredFrameStaysInsideVisibleFrame()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  {
    NullPlatformContext context;
    NSScreen *screen = [NSScreen mainScreen];
    LOKA_VERIFY(screen != nil);
    const NSRect visible = [screen visibleFrame];
    WindowProps props;
    // FrameRectForContent treats y as a downward offset from the visible top
    // to the outer frame's top; this request crosses the right and bottom edges.
    props.frame(static_cast<int>(NSMaxX(visible)) - 50,
                static_cast<int>(NSMaxY(visible)) - 50, 257, 163);
    props.visible(true);
    // Like the admission sibling's initial seed, construction enters the
    // visibility path and makeKeyAndOrderFront: synchronously.
    MacWindow window(&context, props);
    verifyPlacedFrame(window, loka::macos::MacProjection(
        NativeAccess::contentView(window), loka::macos::DefaultRailMetrics()).projectClientSize(257, 163).r.size);
    LOKA_VERIFY(window.nativeFrame().get().width == 257);
    LOKA_VERIFY(window.nativeFrame().get().height == 163);

    const loka::core::Frame first = window.nativeFrame().get();
    {
      loka::core::StateTrackerGuard guard(window.getTracker());
      // Still off-screen horizontally, but a different y makes a missing
      // frame observer distinguishable from preserving the first placement.
      window.frameState().set(loka::core::Frame(
          static_cast<int>(NSMaxX(visible)) + 100, 40, 257, 163));
    }
    verifyPlacedFrame(window, loka::macos::MacProjection(
        NativeAccess::contentView(window), loka::macos::DefaultRailMetrics()).projectClientSize(257, 163).r.size);
    LOKA_VERIFY(window.nativeFrame().get().width == 257);
    LOKA_VERIFY(window.nativeFrame().get().height == 163);
    LOKA_VERIFY(window.nativeFrame().get() != first);
  }
  {
    NullPlatformContext context;
    NSScreen *screen = [NSScreen mainScreen];
    LOKA_VERIFY(screen != nil);
    const NSRect visible = [screen visibleFrame];
    const int width = static_cast<int>(visible.size.width) + 100;
    const int height = static_cast<int>(visible.size.height) + 100;
    WindowProps props;
    props.frame(static_cast<int>(NSMinX(visible)), 0, width, height);
    props.visible(true);
    MacWindow window(&context, props);
    NSWindow *native = (NSWindow *)NativeAccess::nativeWindow(window);
    LOKA_VERIFY(native != nil);
    // An oversized request fills the work area with the outer frame; content
    // must leave room for the native chrome rather than retaining its request.
    const NSRect expectedContent = [native contentRectForFrameRect:visible];
    verifyPlacedFrame(window, expectedContent.size);
    LOKA_VERIFY(window.nativeFrame().get().width < width);
    LOKA_VERIFY(window.nativeFrame().get().height < height);
  }
  [pool drain];
}
