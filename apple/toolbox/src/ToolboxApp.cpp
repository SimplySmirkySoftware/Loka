#include "platform/ToolboxPascalText.hpp"
#include "ToolboxApp.hpp"
#include "ToolboxIdleClock.hpp"
#include "ToolboxOutOfMemory.hpp"
#include "ToolboxUnicodeStartup.hpp"
#include "ToolboxWindow.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxInputDoor.hpp"

#include <cassert>
#include <climits>
#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Menus.h>
#include <Quickdraw.h>
#include <Sound.h>
#include <Script.h>
#include <TextEdit.h>
#include <Windows.h>
#include <Devices.h>
#include "platform/StringUTF8.hpp"

namespace
{
  ToolboxWindow *FindToolboxWindow(AppComponentGroup *group, WindowPtr target)
  {
    if (!group || !target)
    {
      return 0;
    }
    const AppComponentGroup::Components components = group->getComponents();
    for (std::size_t i = 0; i < components.size(); ++i)
    {
      Window *window = components[i]->asWindow();
      ToolboxWindow *toolboxWindow = window ? window->asToolboxWindow() : 0;
      if (toolboxWindow && toolboxWindow->window() == target)
      {
        return toolboxWindow;
      }
    }
    return 0;
  }
} // namespace

CursorOwner::CursorOwner(ToolboxApp &app)
    : app_(app),
      hoverCursor_(HOVER_ARROW),
      busyDepth_(0),
      batchPhase_(BATCH_NONE),
      lastApplied_(UNKNOWN),
      nativeApplies_(0),
      outerEntries_(0),
      outerExits_(0),
      iBeam_(),
      watch_()
{
}

void CursorOwner::CachedCursor::load(short resourceId)
{
  CursHandle resource = GetCursor(resourceId);
  this->available_ = resource && *resource;
  if (this->available_)
    this->value_ = **resource;
}

void CursorOwner::initialize()
{
  this->iBeam_.load(iBeamCursor);
  this->watch_.load(watchCursor);
}

void CursorOwner::setHover(HoverCursor cursor, bool force)
{
  this->hoverCursor_ = cursor;
  this->apply(force);
}

void CursorOwner::apply(bool force)
{
  if (this->app_.activationPhase_ != ACTIVATION_FOREGROUND)
  {
    this->lastApplied_ = UNKNOWN;
    return;
  }
  const AppliedCursor effective = this->busyDepth_ > 0 ? WATCH
      : (this->hoverCursor_ == HOVER_IBEAM ? IBEAM : ARROW);
  if (!force && effective == this->lastApplied_)
    return;
  const Cursor *cursor = 0;
  switch (effective)
  {
  case ARROW: cursor = &qd.arrow; break;
  case IBEAM: cursor = this->iBeam_.get(); break;
  case WATCH: cursor = this->watch_.get(); break;
  case UNKNOWN: break;
  }
  if (!cursor)
  {
    // Match the old optional I-beam resource path: leave the native cursor alone.
    this->lastApplied_ = UNKNOWN;
    return;
  }
  SetCursor(const_cast<Cursor *>(cursor));
  ++this->nativeApplies_;
  this->lastApplied_ = effective;
}

void CursorOwner::reconcile()
{
  this->lastApplied_ = UNKNOWN;
  this->app_.sampleHover(true);
}

// Deliberate transition twin: tests/toolbox/host/ToolboxHost.hpp.
bool CursorOwner::openBatch()
{
  if (this->batchPhase_ != BATCH_NONE || this->busyDepth_ != 0)
    return false;
  this->batchPhase_ = BATCH_OPEN;
  return true;
}

void CursorOwner::closeBatch()
{
  if (this->batchPhase_ == BATCH_HELD)
  {
    assert(this->busyDepth_ == 1);
    --this->busyDepth_;
    ++this->outerExits_;
    this->reconcile();
  }
  this->batchPhase_ = BATCH_NONE;
}

void CursorOwner::enterBusy()
{
  if (this->batchPhase_ == BATCH_OPEN && this->busyDepth_ == 0)
  {
    this->batchPhase_ = BATCH_HELD;
    ++this->busyDepth_;
    ++this->outerEntries_;
    this->apply(true);
  }
  assert(this->busyDepth_ < SHRT_MAX);
  ++this->busyDepth_;
  if (this->busyDepth_ == 1)
  {
    ++this->outerEntries_;
    this->apply(true);
  }
}

void CursorOwner::exitBusy()
{
  assert(this->busyDepth_ > 0);
  --this->busyDepth_;
  if (this->busyDepth_ == 0)
  {
    ++this->outerExits_;
    this->reconcile();
  }
}

void CursorOwner::assertIdle() const
{
  assert(this->batchPhase_ == BATCH_NONE);
  assert(this->busyDepth_ == 0);
}

void ToolboxApp::sampleHover(bool force)
{
  ToolboxWindow *front = FindToolboxWindow(this->group_, FrontWindow());
  if (front)
    front->updateCursor(force);
  else
    this->cursorOwner_.setHover(CursorOwner::HOVER_ARROW, force);
}

ToolboxApp::ToolboxApp(AppConfigurable *config)
    : App(config),
      activationPhase_(ACTIVATION_FOREGROUND),
      cursorOwner_(*this),
      menuBarDrawDeferred_(false),
      menuAttachment_(*this),
      running_(false)
{
}

bool ToolboxApp::windowAdopted(Window *window)
{
  ToolboxWindow *toolboxWindow = window ? window->asToolboxWindow() : 0;
  if (!toolboxWindow)
    return true;
  toolboxWindow->setApp(this);
  toolboxWindow->open();
  toolboxWindow->ensureSceneMounted();
  if (toolboxWindow->scene() && toolboxWindow->scene()->composeRefusedForMemory())
    return false;
  // Startup has no turn boundary yet: report a reserve lent during this
  // window's open and mount before the next window adds more work.
  loka::toolbox::QuitIfOutOfMemoryReserveSpent();
  return true;
}

void ToolboxApp::bootstrapWindowRefused(Window *)
{
  loka::toolbox::QuitForOutOfMemory();
}

void ToolboxApp::bootstrapWindowless()
{
  loka::toolbox::QuitForOutOfMemory();
}

void ToolboxApp::run()
{
  InitGraf(&qd.thePort);
  InitFonts();
  InitWindows();
  InitMenus();
  TEInit();
  InitDialogs(0);
#if LOKA_TOOLBOX_UNICODE
  loka::toolbox::RequireUnicodeSystem();
#endif
  loka::toolbox::UnicodeText::Session unicodeText;
  loka::toolbox::ArmOutOfMemoryReserve();
  InitCursor();
  this->cursorOwner_.initialize();
  // Blocking Toolbox work with no path to this app (file reads, image decodes)
  // borrows the watch through the process registration while the loop runs.
  const ToolboxBusyOwnerRegistration busyRegistration(this->cursorOwner_);

  App::run();
  if (group_)
  {
    const AppComponentGroup::Components comps = group_->getComponents();
    ToolboxWindow *firstWindow = 0;
    for (std::size_t i = 0; i < comps.size(); ++i)
    {
      Window *w = comps[i]->asWindow();
      ToolboxWindow *toolboxWindow = w ? w->asToolboxWindow() : 0;
      if (toolboxWindow)
      {
        firstWindow = toolboxWindow;
        break;
      }
    }
    if (!activeWindow() && firstWindow)
    {
      setActiveWindow(firstWindow);
    }
  }
  this->cursorOwner_.assertIdle();
  ToolboxIdleClock clock(TickCount());
  activationPhase_ = ACTIVATION_FOREGROUND;
  running_ = true;
  // The run loop is the clock regime: a joining write without an Operation
  // turn is a defect here and a legacy fallback everywhere else (#1057 C5).
  {
    loka::core::Operation::Regime regime;
    while (running_)
    {
      EventRecord record;
      const bool gotEvent = WaitNextEvent(everyEvent, &record, 1, 0);
      this->iterate(clock, gotEvent, record);
    }
  }
  // Teardown runs outside the Regime (tracked writes in detach hooks) and
  // inside the Unicode session, like the macOS rail; the destructor's second
  // retireComponents() is harmless.
  this->retireComponents();
}

#include "ToolboxIteration.cpp"
#include "ToolboxPresent.cpp"

void ToolboxApp::quit()
{
  running_ = false;
}

void ToolboxApp::requestMenuBarDraw()
{
  if (activationPhase_ == ACTIVATION_FOREGROUND)
  {
    DrawMenuBar();
    return;
  }
  menuBarDrawDeferred_ = true;
}
