#include "MacWindow.hpp"
#include "MacInputDoor.hpp"
#include "MacApp.hpp"
#include "MacDisplayAppearance.hpp"
#include "MacObjCCompat.hpp"
#include "MacScenePlatformController.hpp"
#include "Utf8String.hpp"
#include <AppKit/AppKit.h>
#include "app/scene/Scene.hpp"
#include "platform/StringUTF8.hpp"

@interface LokaFlippedView : NSView
{
  MacWindow *owner_;
}
- (void)setOwner:(MacWindow *)owner;
@end

@implementation LokaFlippedView
- (BOOL)isFlipped
{
  return YES;
}

- (BOOL)acceptsFirstResponder
{
  return YES;
}

// The window's own keys land here while no field is focused, but Tab passes
// over it: a stop here leaves the loop with no focus ring (#1461).
- (BOOL)canBecomeKeyView
{
  return NO;
}

- (void)setOwner:(MacWindow *)owner
{
  owner_ = owner;
}

- (void)keyDown:(NSEvent *)event
{
  loka::core::Operation turn;
  if (owner_)
  {
    NSString *characters = [event charactersIgnoringModifiers];
    if (characters && [characters length] > 0)
    {
      const unichar value = [characters characterAtIndex:0];
      if (value <= 0x7f && owner_->handleKeyPress(static_cast<char>(value)))
      {
        return;
      }
    }
  }
  [super keyDown:event];
}
@end

@interface LokaWindowDelegate : NSObject
{
  MacWindow *owner_;
}
@property(nonatomic, assign) MacWindow *owner;
@end

@implementation LokaWindowDelegate
@synthesize owner = owner_;
- (BOOL)windowShouldClose:(id)sender
{
  (void)sender;
  if (self.owner)
  {
    loka::core::Operation turn;
    const bool asked = self.owner->handleWindowShouldClose();
    turn.close();
    return asked ? NO : YES;
  }
  return YES;
}

- (void)windowWillClose:(NSNotification *)notification
{
  (void)notification;
  if (self.owner)
  {
    loka::core::Operation turn;
    self.owner->handleWindowWillClose();
    turn.close();
  }
}

- (void)windowDidResize:(NSNotification *)notification
{
  (void)notification;
  if (self.owner)
  {
    MacInputDoor::windowResize(*[self owner]);
  }
}

- (void)windowDidMove:(NSNotification *)notification
{
  (void)notification;
  if (self.owner)
  {
    MacInputDoor::windowMove(*[self owner]);
  }
}

- (void)windowDidBecomeKey:(NSNotification *)notification
{
  (void)notification;
  if (self.owner)
  {
    self.owner->handleWindowDidBecomeKey();
  }
}
@end

MacWindow::MacWindow(PlatformContext *context, const WindowProps &props)
    : Window(context, props),
      window_(0),
      contentView_(0),
      delegate_(0),
      app_(0),
      closing_(false),
      scenePlatformController_(0)
{
  // Initial visibility is a seed, not a request. Keep creation and mount
  // synchronous; subsequent visibility changes enter through App admission.
  this->applyNativeVisibility();
  this->observeNativeState(this->displayTitleState(), &MacWindow::TitleChangedThunk, this);
  this->observeNativeState(this->editedState(), &MacWindow::EditedChangedThunk, this);
  this->observeNativeState(this->frameState(), &MacWindow::FrameChangedThunk, this);
}

MacWindow::~MacWindow()
{
  this->dialogResults().close();
  this->detachNativeStateObservers();
  teardownScene();
  NSWindow *window = (NSWindow *)window_;
  if (window)
  {
    [window setDelegate:nil];
    [window close];
  }
  if (contentView_)
  {
    [(id)contentView_ release];
    contentView_ = 0;
  }
  if (window_)
  {
    [(id)window_ release];
    window_ = 0;
  }
  if (delegate_)
  {
    [(id)delegate_ release];
    delegate_ = 0;
  }
}

MacWindow *MacWindow::fromRootView(void *rootView)
{
  // Deliberate counterpart of Win32's root HWND / GWLP_USERDATA lookup.
  return [(LokaWindowDelegate *)[[(NSView *)rootView window] delegate] owner];
}

void MacWindow::setApp(MacApp *app)
{
  app_ = app;
  if (app_)
  {
    app_->setActiveWindow(this);
  }
}

// Deliberate counterpart of Win32Window's native-identity comparison.
bool MacWindow::hasPendingNativeVisibility() const
{
  return this->visibility_->get() != (this->window_ != nil);
}

void MacWindow::applyNativeVisibility()
{
  if (!this->hasPendingNativeVisibility())
  {
    return;
  }
  if (this->visibility_->get())
  {
    this->createNativeWindow();
    if (this->window_)
    {
      this->onShow();
    }
  }
  else
  {
    this->onHide();
    this->destroyNativeWindow();
  }
}

void MacWindow::EditedChangedThunk(void *userData)
{
  MacWindow *self = static_cast<MacWindow *>(userData);
  if (!self || !self->window_)
    return;
  NSWindow *window = (NSWindow *)self->window_;
  [window setDocumentEdited:self->editedState().get() ? YES : NO];
}

void MacWindow::TitleChangedThunk(void *userData)
{
  MacWindow *self = static_cast<MacWindow *>(userData);
  if (!self || !self->window_)
  {
    return;
  }
  NSWindow *window = (NSWindow *)self->window_;
  std::string utf8;
  if (loka::platform::CollectUtf8(self->displayTitleState().get(), utf8))
  {
    [window setTitle:loka::macos::CreateNSStringFromUtf8(utf8)];
  }
  else
  {
    [window setTitle:@""];
  }
}

namespace
{
  static CGFloat VisibleTopForScreen(NSScreen *screen)
  {
    if (!screen)
    {
      return 0.0;
    }
    NSRect screenFrame = [screen frame];
    NSRect visibleFrame = [screen visibleFrame];
    CGFloat top = visibleFrame.origin.y + visibleFrame.size.height;
    if (visibleFrame.size.height >= screenFrame.size.height - 1.0)
    {
      CGFloat menuHeight = [[NSStatusBar systemStatusBar] thickness];
      top = screenFrame.origin.y + screenFrame.size.height - menuHeight;
    }
    return top;
  }

  static NSRect
  FrameRectForContent(CGFloat x, CGFloat y, CGFloat width, CGFloat height, NSUInteger style, NSScreen *screen)
  {
    NSRect contentRect = NSMakeRect(0.0, 0.0, width, height);
    NSRect frameRect = [NSWindow frameRectForContentRect:contentRect styleMask:style];
    CGFloat top = VisibleTopForScreen(screen);
    frameRect.origin.x = x;
    frameRect.origin.y = top - y - frameRect.size.height;
    return frameRect;
  }

  // Deliberate counterpart of Win32's ClampOuterRectToWorkArea and Toolbox's
  // ClampStructureToScreen. AppKit's constraint only keeps the title bar
  // reachable; declared placement must fit the entire frame, including chrome.
  static NSRect ClampFrameToVisibleFrame(NSRect frame, NSScreen *screen)
  {
    if (!screen)
    {
      return frame;
    }
    const NSRect visible = [screen visibleFrame];
    if (frame.size.width > visible.size.width)
    {
      frame.size.width = visible.size.width;
    }
    if (frame.size.height > visible.size.height)
    {
      frame.size.height = visible.size.height;
    }
    if (NSMaxX(frame) > NSMaxX(visible))
    {
      frame.origin.x = NSMaxX(visible) - frame.size.width;
    }
    // AppKit is bottom-up: correct right/bottom before left/top.
    if (NSMinY(frame) < NSMinY(visible))
    {
      frame.origin.y = NSMinY(visible);
    }
    if (NSMinX(frame) < NSMinX(visible))
    {
      frame.origin.x = NSMinX(visible);
    }
    if (NSMaxY(frame) > NSMaxY(visible))
    {
      frame.origin.y = NSMaxY(visible) - frame.size.height;
    }
    return frame;
  }

  static loka::core::Frame NativeContentFrame(NSWindow *window,
                                              const loka::macos::MacProjection &projection)
  {
    if (!window)
    {
      return loka::core::Frame();
    }
    const NSRect frameRect = [window frame];
    const NSRect contentRect = [window contentRectForFrameRect:frameRect];
    NSScreen *screen = [window screen];
    if (!screen)
    {
      screen = [NSScreen mainScreen];
    }
    // The origin is the exact inverse of FrameRectForContent: x and y place
    // the outer frame (y is its top's offset below the visible top), as the
    // intent does, so frameState().set(nativeFrame().get()) does not move the
    // window (#526). The size is the content's; only it crosses into lu.
    return loka::core::Frame(static_cast<int>(frameRect.origin.x),
                             static_cast<int>(VisibleTopForScreen(screen)
                                              - (frameRect.origin.y + frameRect.size.height)),
                             projection.clientCapacityToLu(contentRect.size.width),
                             projection.clientCapacityToLu(contentRect.size.height));
  }
} // namespace

void MacWindow::FrameChangedThunk(void *userData)
{
  MacWindow *self = static_cast<MacWindow *>(userData);
  if (!self || !self->window_)
  {
    return;
  }
  NSWindow *window = (NSWindow *)self->window_;
  loka::core::Frame frame = self->frameState().get();
  if (!frame.hasSize())
  {
    return;
  }
  NSRect currentFrame = [window frame];
  NSRect currentContent = [window contentRectForFrameRect:currentFrame];
  CGFloat x = frame.x >= 0 ? frame.x : currentContent.origin.x;
  CGFloat y = frame.y >= 0 ? frame.y : currentContent.origin.y;
  const loka::macos::MacRect intent = self->projection().projectClientSize(frame.width, frame.height);
  CGFloat width = frame.width > 0 ? intent.r.size.width : currentContent.size.width;
  CGFloat height = frame.height > 0 ? intent.r.size.height : currentContent.size.height;
  NSScreen *screen = [window screen];
  if (!screen)
  {
    screen = [NSScreen mainScreen];
  }
  const bool sameContentSize = currentContent.size.width == intent.r.size.width
                               && currentContent.size.height == intent.r.size.height;
  bool samePosition = !frame.hasPosition();
  if (frame.hasPosition())
  {
    NSRect mappedCurrentFrame = FrameRectForContent(frame.x,
                                                    frame.y,
                                                    currentContent.size.width,
                                                    currentContent.size.height,
                                                    [window styleMask],
                                                    screen);
    samePosition = currentFrame.origin.x == mappedCurrentFrame.origin.x
                   && currentFrame.origin.y == mappedCurrentFrame.origin.y;
  }
  if (sameContentSize && samePosition)
  {
    return;
  }
  NSRect nextFrame = FrameRectForContent(x, y, width, height, [window styleMask], screen);
  if (frame.x < 0)
  {
    nextFrame.origin.x = currentFrame.origin.x;
  }
  if (frame.y < 0)
  {
    nextFrame.origin.y = currentFrame.origin.y;
  }
  nextFrame = ClampFrameToVisibleFrame(nextFrame, screen);
  [window setFrame:nextFrame display:YES];
}

loka::macos::MacProjection MacWindow::projection() const
{
  return this->scenePlatformController_ ? this->scenePlatformController_->projection()
                                       : loka::macos::MacProjection(this->contentView_, loka::macos::DefaultRailMetrics());
}

void MacWindow::createNativeWindow()
{
  if (window_)
  {
    return;
  }
  closing_ = false;
  const loka::core::Frame defaultFrame = Window::defaultFrame();
  CGFloat x = this->hasPosition() ? this->positionX() : defaultFrame.x;
  CGFloat y = this->hasPosition() ? this->positionY() : defaultFrame.y;
  const loka::macos::MacRect content = this->projection().projectClientSize(
      this->hasSize() ? this->width() : defaultFrame.width,
      this->hasSize() ? this->height() : defaultFrame.height);
  const CGFloat width = content.r.size.width;
  const CGFloat height = content.r.size.height;
  NSUInteger style = LOKA_MAC_WINDOW_STYLE_TITLED | LOKA_MAC_WINDOW_STYLE_CLOSABLE
                     | LOKA_MAC_WINDOW_STYLE_RESIZABLE | LOKA_MAC_WINDOW_STYLE_MINIATURIZABLE;
  NSWindow *window = [[NSWindow alloc] initWithContentRect:content.r
                                                 styleMask:style
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
  // MacWindow owns the allocation; hide and destruction release after close.
  [window setReleasedWhenClosed:NO];
  // Tab follows AppKit's geometric key-view loop, recomputed as Show, Keyed and
  // LazyView add or remove fields; a chain kept by the rail went stale (129d353a).
  [window setAutorecalculatesKeyViewLoop:YES];
  NSScreen *screen = [NSScreen mainScreen];
  NSRect frame = FrameRectForContent(x, y, width, height, style, screen);
  frame = ClampFrameToVisibleFrame(frame, screen);
  [window setFrame:frame display:NO];
  std::string utf8;
  if (loka::platform::CollectUtf8(this->displayTitleState().get(), utf8))
  {
    [window setTitle:loka::macos::CreateNSStringFromUtf8(utf8)];
  }
  else
  {
    [window setTitle:@""];
  }

  [window setDocumentEdited:this->editedState().get() ? YES : NO];

  LokaFlippedView *contentView = [[LokaFlippedView alloc] initWithFrame:content.r];
  [window setContentView:contentView];
  [contentView setOwner:this];
  [window makeFirstResponder:contentView];

  LokaWindowDelegate *delegate = [[LokaWindowDelegate alloc] init];
  delegate.owner = this;
  [window setDelegate:(id)delegate];

  window_ = (void *)window;
  contentView_ = (void *)contentView;
  delegate_ = (void *)delegate;
  this->storeNativeFrame(NativeContentFrame(window, this->projection()));

  [window makeKeyAndOrderFront:nil];
  this->dialogResults().open(*this);
  this->onCreate();
  this->mountScene();
}

bool MacWindow::handleKeyPress(char key)
{
  return app_ ? app_->handleKeyPress(key) : false;
}

void MacWindow::destroyNativeWindow()
{
  this->dialogResults().close();
  NSWindow *window = (NSWindow *)window_;
  if (!window)
  {
    return;
  }
  this->teardownScene();
  // Like Win32's cleared GWLP_USERDATA, detach callback access before close.
  [window setDelegate:nil];
  [(LokaWindowDelegate *)this->delegate_ setOwner:0];
  [(LokaFlippedView *)this->contentView_ setOwner:0];
  // Like Win32's hide path, retire the active identity. Clear before close so
  // a replacement window's key notification can establish the new active one.
  if (this->app_ && this->app_->activeWindow() == this)
  {
    this->app_->setActiveWindow(0);
  }
  [window close];
  // Match ~MacWindow's release order; the no-release policy belongs only to
  // handleWindowWillClose while AppKit is unwinding its delegate callback.
  [(id)this->contentView_ release];
  this->contentView_ = 0;
  [(id)this->window_ release];
  this->window_ = 0;
  [(id)this->delegate_ release];
  this->delegate_ = 0;
}

bool MacWindow::queryDisplayScalePercent(int &out) const
{
  NSWindow *window = (NSWindow *)window_;
  if (!window)
  {
    return false;
  }
  // backingScaleFactor is 10.7 and later, while library/core targets Tiger
  // through Snow Leopard. Asked by selector rather than by OS version so the
  // path is capability-based (docs/TODO.md:97); MacObjCCompat.hpp declares the
  // selector under older SDKs so this stays one code path rather than two.
  // On the systems that lack it there is no HiDPI to report, and unscaled is
  // the density, not a fallback.
  double scale = 1.0;
  if ([window respondsToSelector:@selector(backingScaleFactor)])
  {
    scale = (double)[window backingScaleFactor];
  }
  if (scale <= 0.0)
  {
    return false;
  }
  // The float stops here: the seam carries an integer percentage so no other
  // target has to deal with one.
  out = (int)(scale * 100.0 + 0.5);
  return true;
}

bool MacWindow::queryDisplayDepth(int &out) const
{
  NSWindow *window = (NSWindow *)window_;
  if (!window)
  {
    return false;
  }
  // The window reports the screen it overlaps most, which is the same rule the
  // other backends follow. A window that is offscreen has no screen at all, so
  // there is nothing to report rather than something to guess.
  NSScreen *screen = [window screen];
  if (!screen)
  {
    return false;
  }
  const NSInteger bitsPerPixel = NSBitsPerPixelFromDepth([screen depth]);
  if (bitsPerPixel <= 0)
  {
    return false;
  }
  out = (int)bitsPerPixel;
  return true;
}

bool MacWindow::queryDisplayAppearance(DisplayAppearance &out) const
{
  NSWindow *window = (NSWindow *)window_;
  if (!window)
  {
    return false;
  }
  // effectiveAppearance is 10.9 and later. Appearance is per-window, which is
  // the reason this is asked of a window rather than of the application. The
  // borrowed appearance decides whether it has the later light/dark matching
  // capability; on older systems the honest answer is absent.
  if (![window respondsToSelector:@selector(effectiveAppearance)])
  {
    return false;
  }
  id appearance = [window effectiveAppearance];
  return loka::macos::TryReadDisplayAppearance((void *)appearance, out);
}

void MacWindow::onCreate()
{
  // Native creation is entered from the seed or App admission; it reads the
  // application's intent and never writes it back (no forced re-notify).
  Window::onCreate();
}

void MacWindow::onShow()
{
  NSWindow *window = (NSWindow *)window_;
  if (window)
  {
    [window makeKeyAndOrderFront:nil];
  }
}

void MacWindow::onHide() {}

void MacWindow::synchronizeScenePlatform()
{
  if (scenePlatformController_)
  {
    scenePlatformController_->synchronize();
  }
}

void MacWindow::drainNativeRetirements()
{
  if (this->scenePlatformController_)
  {
    this->scenePlatformController_->drainNativeRetirements();
  }
}

bool MacWindow::hasPendingScenePlatformSync() const
{
  return scenePlatformController_ ? scenePlatformController_->hasPendingSync() : false;
}

bool MacWindow::handleWindowShouldClose()
{
  // Same request/terminal split as NullWindow::simulateCloseAsked.
  return this->app_ && this->app_->closeAsked(static_cast<Window *>(this));
}

void MacWindow::handleWindowWillClose()
{
  this->dialogResults().close();
  if (closing_)
  {
    return;
  }
  closing_ = true;
  MacApp *app = app_;
  NSWindow *window = (NSWindow *)window_;
  id contentView = (id)contentView_;
  id delegate = (id)delegate_;
  // Revoke the menu borrow while its route exists. The deferred Scene teardown
  // finds the attachment through the delegate cleared below (#1242).
  if (app)
    app->menuAttachment().releaseFrom(this->scene());
  if (window)
  {
    [window setDelegate:nil];
  }
  if (delegate)
  {
    [(LokaWindowDelegate *)delegate setOwner:0];
  }
  if (contentView)
  {
    [(LokaFlippedView *)contentView setOwner:0];
  }
  // Closed fact only: App reclamation tears down the scene in ~MacWindow.
  this->onDestroy();
  window_ = 0;
  contentView_ = 0;
  delegate_ = 0;
  app_ = 0;
  // On 10.6 the window-close shutdown path can still crash inside NSWindow
  // dealloc/free while AppKit is unwinding this callback. Keep the native
  // objects detached from our C++ side here and let process teardown reclaim
  // them instead of trying to release them during close.
  (void)delegate;
  (void)contentView;
  (void)window;
  if (app)
  {
    app->requestWindowClose(static_cast<Window *>(this));
  }
}

void MacWindow::handleWindowDidResize()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(!this->scenePlatformController_ || this->scenePlatformController_->borrowPhase().open());
#endif
  NSWindow *window = (NSWindow *)window_;
  NSView *view = (NSView *)contentView_;
  if (!window)
  {
    return;
  }
  this->storeNativeFrame(NativeContentFrame(window, this->projection()));
  if (!scenePlatformController_ || !contentView_)
  {
    return;
  }
  const NSRect contentBounds = [view bounds];
  scenePlatformController_->relayout(this->projection().clientCapacityToLu(contentBounds.size.width),
                                     this->projection().clientCapacityToLu(contentBounds.size.height));
}

void MacWindow::handleWindowDidMove()
{
#ifdef LOKA_LIFECYCLE_AUDIT
  assert(!this->scenePlatformController_ || this->scenePlatformController_->borrowPhase().open());
#endif
  NSWindow *window = (NSWindow *)window_;
  if (window)
  {
    this->storeNativeFrame(NativeContentFrame(window, this->projection()));
  }
}

void MacWindow::handleWindowDidBecomeKey()
{
  if (app_)
  {
    app_->setActiveWindow(this);
  }
}

void MacWindow::mountScene()
{
  if (this->scene() && !this->scenePlatformController_)
    this->mountReplacementScene(this->scene());
}

bool MacWindow::mountReplacementScene(loka::app::scene::Scene *next)
{
  if (!this->window_ || !this->contentView_)
    return true;
  if (!this->scenePlatformController_)
    this->scenePlatformController_ = new MacScenePlatformController(this->contentView_, loka::macos::DefaultRailMetrics(), this->context());
  if (!this->scenePlatformController_)
    return false;
  return next->mount(this->scenePlatformController_);
}

void MacWindow::teardownScene()
{
  loka::app::scene::Scene *currentScene = this->scene();
  if (currentScene)
  {
    this->unmountSceneForTeardown(*currentScene);
  }
  if (scenePlatformController_)
  {
    delete scenePlatformController_;
    scenePlatformController_ = 0;
  }
}
