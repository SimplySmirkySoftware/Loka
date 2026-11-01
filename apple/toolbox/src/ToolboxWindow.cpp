#include "ToolboxUpdateEvidence.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "ToolboxPaintGround.hpp"
#include "platform/ToolboxPascalText.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxWindow.hpp"
#include "ToolboxDeviceDepth.hpp"
#include "ToolboxApp.hpp"

#include <cstring>
#include <string>
#include <cstdio>
#include <Quickdraw.h>
#include "app/core/App.hpp"
#include "app/scene/Scene.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxWindowContext.hpp"
#include "core/String.hpp"
#include "platform/StringUTF8.hpp"

namespace
{
  Rect PrimaryWorkArea()
  {
    Rect work = qd.screenBits.bounds;
    work.top = static_cast<short>(work.top + GetMBarHeight());
    return work;
  }

  Rect ContentLimits(const ToolboxWindowChrome &chrome)
  {
    Rect limits = PrimaryWorkArea();
    limits.left = static_cast<short>(limits.left + chrome.left());
    limits.top = static_cast<short>(limits.top + chrome.top());
    limits.right = static_cast<short>(limits.right - chrome.right());
    limits.bottom = static_cast<short>(limits.bottom - chrome.bottom());
    return limits;
  }

  // Placement owns no rows: one content rectangle and its native chrome.
  void ClampStructureToScreen(Rect &content, const ToolboxWindowChrome &chrome)
  {
    const Rect limits = ContentLimits(chrome);
    short width = static_cast<short>(content.right - content.left);
    short height = static_cast<short>(content.bottom - content.top);
    if (width > limits.right - limits.left)
      width = static_cast<short>(limits.right - limits.left);
    if (height > limits.bottom - limits.top)
      height = static_cast<short>(limits.bottom - limits.top);
    if (content.left + width > limits.right)
      content.left = static_cast<short>(limits.right - width);
    if (content.top + height > limits.bottom)
      content.top = static_cast<short>(limits.bottom - height);
    if (content.left < limits.left)
      content.left = limits.left;
    if (content.top < limits.top)
      content.top = limits.top;
    content.right = static_cast<short>(content.left + width);
    content.bottom = static_cast<short>(content.top + height);
  }

  // The shipped convention, inherited from the original open(): the logical
  // frame is the CONTENT origin, with y measured from one row below the menu
  // bar (content top = y + menu height + 1). Every Toolbox scenario golden was
  // baked with it, so it stays (#712); nativeContentFrame() is the exact
  // inverse, and the chrome value only enters through the clamp.
  const short kContentTopBelowMenu = 1;

  Rect RequestedContentBounds(const loka::core::Frame &frame)
  {
    Rect content;
    const short left = static_cast<short>(frame.x);
    const short top = static_cast<short>(frame.y + GetMBarHeight() + kContentTopBelowMenu);
    SetRect(&content, left, top,
            static_cast<short>(left + frame.width), static_cast<short>(top + frame.height));
    return content;
  }
} // namespace

ToolboxWindowChrome::ToolboxWindowChrome(WindowPtr window)
{
  SetRect(&this->insets_, 0, 0, 0, 0);
  if (window)
  {
    const WindowPeek peek = reinterpret_cast<WindowPeek>(window);
    const Rect structure = (*peek->strucRgn)->rgnBBox;
    const Rect content = (*peek->contRgn)->rgnBBox;
    SetRect(&this->insets_, static_cast<short>(content.left - structure.left),
            static_cast<short>(content.top - structure.top),
            static_cast<short>(structure.right - content.right),
            static_cast<short>(structure.bottom - content.bottom));
  }
}

ToolboxWindow::ToolboxWindow(PlatformContext *context, const WindowProps &props)
    : Window(context, props),
      app_(0),
      window_(0),
      scenePlatformController_(0),
      context_(0),
      needsInvalidate_(false),
      pendingDebugDump_(false),
      pendingInvalidateRects_(),
      chrome_()
{
  window_ = 0;
  context_ = new ToolboxWindowContext(
#if !defined(LOKA_TOOLBOX_CLASSIC_6)
      ToolboxWindowContext::CAP_CONTROL_MANAGER | ToolboxWindowContext::CAP_TEXT_EDIT
#else
      ToolboxWindowContext::CAP_NONE
#endif
  );
  // Toolbox draws no edited mark by the #1245 ruling.
  this->observeNativeState(this->displayTitleState(), &ToolboxWindow::TitleChangedThunk, this);
  this->observeNativeState(this->frameState(), &ToolboxWindow::FrameChangedThunk, this);
}

ToolboxWindow::~ToolboxWindow()
{
  this->closeDialogResults();
  this->detachNativeStateObservers();
  needsInvalidate_ = false;
  pendingDebugDump_ = false;
  pendingInvalidateRects_.clear();
  teardownScene();
  delete context_;
  context_ = 0;
  if (window_)
  {
    DisposeWindow(window_);
    window_ = 0;
  }
}

void ToolboxWindow::setApp(App *app)
{
  app_ = app;
  if (app_)
  {
    app_->setActiveWindow(this);
  }
}

void ToolboxWindow::ensureSceneMounted()
{
  open();
  mountScene();
}

void ToolboxWindow::open()
{
  if (window_)
  {
    return;
  }
  const loka::core::Frame defaultFrame = Window::defaultFrame();
  const loka::core::Frame requested(
      this->hasPosition() ? this->positionX() : defaultFrame.x,
      this->hasPosition() ? this->positionY() : defaultFrame.y,
      this->hasSize() ? this->width() : defaultFrame.width,
      this->hasSize() ? this->height() : defaultFrame.height);
  // Chrome is not measurable until the window exists and is visible: the
  // Window Manager keeps strucRgn/contRgn empty for an invisible window, so a
  // hidden probe reads zero insets (measured on the maciix rig, #712). Create
  // at the requested bounds, measure, then let the thunk clamp: the window is
  // visible at the requested bounds for one event turn, as it was before #712.
  Rect bounds = RequestedContentBounds(requested);

  loka::core::String titleValue = this->displayTitleState().get();
  if (titleValue.empty())
  {
    titleValue = loka::core::String::Literal("Loka");
  }
  std::string title;
  if (!loka::platform::CollectUtf8(titleValue, title))
  {
    title = "Loka";
  }
#if LOKA_RETRO68_DIAGNOSTICS
  // Diagnostic profile only (#135): the counter summary in the title bar is
  // debug UI, and its formatting keeps sprintf resident in compact builds.
  if (this->scenePlatformController_)
  {
    title += this->scenePlatformController_->debugStatsSummary();
  }
#endif
  Str255 titleStr;
  if (!ToolboxEncodePascal(loka::core::String(title), titleStr))
    ToolboxEncodePascal(loka::core::String::Literal("Loka"), titleStr);

  window_ = NewWindow(0, &bounds, titleStr, true, documentProc, (WindowPtr)-1, true, 0);
  if (!this->window_)
  {
    return;
  }
  this->dialogResults_.open(*this);
  GrafPtr previousPort;
  GetPort(&previousPort);
  SetPort(this->window_);
  // Every plain EraseRect in this port (window draw, dirty replay, the
  // controller's text fast path, TextContext) restores the window ground.
  loka::toolbox::ToolboxApplyWindowGround();
  SetPort(previousPort);
  this->chrome_ = ToolboxWindowChrome(this->window_);
  FrameChangedThunk(this);
  TitleChangedThunk(this);
  this->storeCurrentNativeContentFrame();
}

#include "ToolboxWindowInvalidate.cpp"

void ToolboxWindow::preserveNativeContentPositionAfterMenuBarChange()
{
  if (!this->window_)
  {
    return;
  }

  Rect actualContentBounds = this->window_->portRect;
  GrafPtr oldPort;
  GetPort(&oldPort);
  SetPort(this->window_);
  LocalToGlobal(reinterpret_cast<Point *>(&actualContentBounds.top));
  LocalToGlobal(reinterpret_cast<Point *>(&actualContentBounds.bottom));
  SetPort(oldPort);

  MoveWindow(this->window_, actualContentBounds.left, actualContentBounds.top, false);
}

void ToolboxWindow::FrameChangedThunk(void *userData)
{
  ToolboxWindow *self = static_cast<ToolboxWindow *>(userData);
  if (!self || !self->window_)
  {
    return;
  }
  const loka::core::Frame frame = self->frameState().get();
  const loka::core::Frame actual = self->nativeContentFrame();
  const loka::core::Frame requested(frame.hasPosition() ? frame.x : actual.x,
                                    frame.hasPosition() ? frame.y : actual.y,
                                    frame.hasSize() ? frame.width : actual.width,
                                    frame.hasSize() ? frame.height : actual.height);
  Rect content = RequestedContentBounds(requested);
  ClampStructureToScreen(content, self->chrome_);
  const short width = static_cast<short>(content.right - content.left);
  const short height = static_cast<short>(content.bottom - content.top);
  if (actual.width != width || actual.height != height)
  {
    SizeWindow(self->window_, width, height, true);
  }
  MoveWindow(self->window_, content.left, content.top, false);
}

loka::core::Frame ToolboxWindow::nativeContentFrame() const
{
  if (!this->window_)
  {
    return loka::core::Frame();
  }
  const Rect portRect = this->window_->portRect;
  Point topLeft;
  topLeft.h = portRect.left;
  topLeft.v = portRect.top;
  GrafPtr oldPort;
  GetPort(&oldPort);
  SetPort(this->window_);
  LocalToGlobal(&topLeft);
  SetPort(oldPort);
  const short menuHeight = GetMBarHeight();
  // Exact inverse of RequestedContentBounds.
  return loka::core::Frame(topLeft.h,
                           static_cast<int>(topLeft.v) - menuHeight - kContentTopBelowMenu,
                           portRect.right - portRect.left,
                           portRect.bottom - portRect.top);
}

void ToolboxWindow::storeCurrentNativeContentFrame()
{
  if (this->window_)
  {
    this->storeNativeFrame(this->nativeContentFrame());
  }
}

void ToolboxWindow::TitleChangedThunk(void *userData)
{
  ToolboxWindow *self = static_cast<ToolboxWindow *>(userData);
  if (!self || !self->window_)
  {
    return;
  }
  loka::core::String titleValue = self->displayTitleState().get();
  if (titleValue.empty())
  {
    titleValue = loka::core::String::Literal("Loka");
  }
  std::string title;
  if (!loka::platform::CollectUtf8(titleValue, title))
  {
    title = "Loka";
  }
  Str255 titleStr;
  if (!ToolboxEncodePascal(loka::core::String(title), titleStr))
    ToolboxEncodePascal(loka::core::String::Literal("Loka"), titleStr);
  SetWTitle(self->window_, titleStr);
}

namespace
{
  const short kMinimumGrowWidth = 64;  // REALbasic 3.1 floor
  const short kMinimumGrowHeight = 64;
} // namespace

void ToolboxWindow::handleGrow(const Point &globalPoint)
{
  if (!window_)
  {
    return;
  }
  Rect sizeRect;
  // Rect is {top, left, bottom, right}; SetRect takes (left, top, right, bottom):
  // left/top = minimum width/height, right/bottom = maximum width/height.
  const Rect limits = ContentLimits(this->chrome_);
  const Rect content = RequestedContentBounds(this->nativeContentFrame());
  // GrowWindow keeps the top-left fixed, so bound the available remainder.
  const short maxWidth = static_cast<short>(limits.right - content.left);
  const short maxHeight = static_cast<short>(limits.bottom - content.top);
  if (maxWidth < kMinimumGrowWidth || maxHeight < kMinimumGrowHeight)
  {
    return;
  }
  SetRect(&sizeRect, kMinimumGrowWidth, kMinimumGrowHeight, maxWidth, maxHeight);
  const long grown = GrowWindow(window_, globalPoint, &sizeRect);
  if (this->app_)
    this->toolboxApp()->cursorOwner().reconcile();
  if (grown == 0)
  {
    return; // cancelled or unchanged: leave the size alone
  }
  const short width = static_cast<short>(grown & 0xFFFF);
  const short height = static_cast<short>((grown >> 16) & 0xFFFF);
  SizeWindow(window_, width, height, true);
  // SizeWindow(..., true) invalidates only the newly exposed area; the whole
  // content is laid out from portRect on the next draw, so invalidate it all.
  GrafPtr oldPort;
  GetPort(&oldPort);
  SetPort(window_);
  InvalRect(&window_->portRect);
  SetPort(oldPort);
  this->storeCurrentNativeContentFrame();
}

bool ToolboxWindow::isHilited() const
{
  return window_ && reinterpret_cast<WindowPeek>(window_)->hilited;
}

void ToolboxWindow::invalidateGrowIcon()
{
  if (!window_)
  {
    return;
  }
  GrafPtr oldPort;
  GetPort(&oldPort);
  SetPort(window_);
  const Rect corner = ToolboxWindowSizeBoxRect(window_->portRect);
  InvalRect(&corner);
  SetPort(oldPort);
}

bool ToolboxWindow::handleMouseDown(const Point &globalPoint, const EventRecord &event)
{
  if (!window_ || !scenePlatformController_)
  {
    return false;
  }
  GrafPtr oldPort;
  GetPort(&oldPort);
  SetPort(window_);
  Point localPoint = globalPoint;
  GlobalToLocal(&localPoint);
  bool handled = ToolboxInputDoor::mouseDown(*scenePlatformController_, localPoint, event);
  SetPort(oldPort);
  return handled;
}

bool ToolboxWindow::offerKeyEvent(const EventRecord &event)
{
  return this->scenePlatformController_ && ToolboxInputDoor::offerKeyEvent(*this->scenePlatformController_, event);
}

bool ToolboxWindow::handleKeyDown(char key)
{
  if (!scenePlatformController_)
  {
    return false;
  }
  return ToolboxInputDoor::keyDown(*scenePlatformController_, key);
}

bool ToolboxWindow::handleScrollKey(ToolboxScrollKey key)
{
  if (!scenePlatformController_)
    return false;
  return ToolboxInputDoor::scrollKey(*scenePlatformController_, key);
}

bool ToolboxWindow::handleTabKey(ToolboxTabDirection direction)
{
  if (!this->scenePlatformController_)
    return false;
  return ToolboxInputDoor::tabKey(*this->scenePlatformController_, direction);
}

#include "ToolboxWindowIdle.cpp"

ToolboxApp *ToolboxWindow::toolboxApp() const
{
  return static_cast<ToolboxApp *>(this->app_);
}

void ToolboxWindow::updateCursor(bool force)
{
  if (!this->window_ || this->window_ != FrontWindow() || !this->app_)
    return;
  GrafPtr oldPort;
  GetPort(&oldPort);
  SetPort(this->window_);
  Point localPoint;
  GetMouse(&localPoint);
  const bool inEdit = this->scenePlatformController_
      && this->scenePlatformController_->isPointInEdit(localPoint);
  SetPort(oldPort);
  this->toolboxApp()->cursorOwner().setHover(
      inEdit ? CursorOwner::HOVER_IBEAM : CursorOwner::HOVER_ARROW, force);
}

void ToolboxWindow::invalidateWindow()
{
  teardownScene();
}

void ToolboxWindow::drawForUpdate()
{
  if (!this->window_)
    return;
  if (this->scenePlatformController_)
    assert(!this->scenePlatformController_->erasedPass().isOpen());
  const Rect evidence =
      ToolboxUpdateEvidence(reinterpret_cast<WindowPeek>(this->window_)->updateRgn, this->window_->portBits.bounds);
  if (this->scenePlatformController_)
    this->scenePlatformController_->notePresentDamage(
        ToolboxUpdateDamage(reinterpret_cast<WindowPeek>(this->window_)->updateRgn, this->window_->portBits.bounds));
  BeginUpdate(this->window_);
  this->drawWhole(evidence);
  EndUpdate(this->window_);
}

void ToolboxWindow::drawForFlush()
{
  if (this->window_)
  {
    if (this->scenePlatformController_)
      this->scenePlatformController_->notePresentDamage(
          ToolboxUpdateDamage(reinterpret_cast<WindowPeek>(this->window_)->updateRgn, this->window_->portBits.bounds));
    this->drawWhole(this->window_->portRect);
  }
}

#include "ToolboxWindowPaint.cpp"

void ToolboxWindow::synchronizeScenePlatform()
{
  if (scenePlatformController_)
  {
    scenePlatformController_->synchronize();
  }
}

void ToolboxWindow::drainNativeRetirements()
{
  if (scenePlatformController_)
  {
    scenePlatformController_->drainNativeRetirements();
  }
}

bool ToolboxWindow::hasPendingScenePlatformSync() const
{
  return scenePlatformController_ ? scenePlatformController_->hasPendingSync() : false;
}

void ToolboxWindow::mountScene()
{
  if (this->scene() && !this->scenePlatformController_)
    this->mountReplacementScene(this->scene());
}

bool ToolboxWindow::mountReplacementScene(loka::app::scene::Scene *next)
{
  if (!this->window_)
    return true;
  if (!this->scenePlatformController_)
    this->scenePlatformController_ = new ToolboxScenePlatformController(this);
  if (!this->scenePlatformController_)
    return false;
  return next->mount(this->scenePlatformController_);
}

bool ToolboxWindow::dumpDebugStatsToTimestampedFile()
{
  return scenePlatformController_ ? scenePlatformController_->dumpDebugStatsToTimestampedFile() : false;
}

void ToolboxWindow::resetDebugStats()
{
  if (scenePlatformController_)
  {
    scenePlatformController_->resetDebugStats();
  }
}

void ToolboxWindow::requestDeferredDebugDump()
{
  pendingDebugDump_ = true;
}

void ToolboxWindow::flushDeferredDebugDump()
{
  if (!pendingDebugDump_)
  {
    return;
  }
  if (needsInvalidate_ || !pendingInvalidateRects_.empty())
  {
    return;
  }
  pendingDebugDump_ = false;
  dumpDebugStatsToTimestampedFile();
}

bool ToolboxWindow::queryDisplayScalePercent(int &out) const
{
  // Classic QuickDraw has exactly one density, and unscaled is a real answer
  // rather than a stand-in: 72 dpi is the coordinate system itself, not a guess
  // about the attached hardware.
  out = 100;
  return true;
}

bool ToolboxWindow::queryDisplayDepth(int &out) const
{
  if (!window_)
  {
    return false;
  }

  // A Classic window may straddle several screens, and depth genuinely differs
  // between them, so the answer is the depth of the device the window overlaps
  // most. That is the same rule the other backends follow, which is why the
  // choice lives here rather than in a resolver: the destination already knows
  // what it is drawn into.
  Rect globalBounds = window_->portRect;
  GrafPtr oldPort;
  GetPort(&oldPort);
  SetPort(window_);
  LocalToGlobal(reinterpret_cast<Point *>(&globalBounds.top));
  LocalToGlobal(reinterpret_cast<Point *>(&globalBounds.bottom));
  SetPort(oldPort);

  GDHandle best = 0;
  long bestArea = 0;
  // Only screenActive is worth filtering on: the Universal Interfaces mark
  // screenDevice itself as "1 if screen device [not used]" (Quickdraw.h:151).
  for (GDHandle device = GetDeviceList(); device; device = GetNextDevice(device))
  {
    if (!TestDeviceAttribute(device, screenActive))
    {
      continue;
    }
    Rect intersection;
    if (!SectRect(&globalBounds, &(*device)->gdRect, &intersection))
    {
      continue;
    }
    const long area = static_cast<long>(intersection.right - intersection.left)
                      * static_cast<long>(intersection.bottom - intersection.top);
    if (area > bestArea)
    {
      bestArea = area;
      best = device;
    }
  }
  if (!best)
  {
    // Dragged fully off the edge, or a screen was just detached. The main
    // device is where this window will be drawn next, so it is the answer, not
    // a substitute for one.
    best = GetMainDevice();
  }
  return loka::toolbox::queryDeviceDepth(best, out);
}

void ToolboxWindow::teardownScene()
{
  if (scenePlatformController_)
  {
    loka::app::scene::Scene *currentScene = this->scene();
    if (currentScene)
    {
      this->unmountSceneForTeardown(*currentScene);
    }
    delete scenePlatformController_;
    scenePlatformController_ = 0;
  }
}

#include "ToolboxWindowEditCommand.cpp"
