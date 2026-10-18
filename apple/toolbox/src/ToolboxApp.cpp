#include "platform/ToolboxPascalText.hpp"
#include "ToolboxApp.hpp"
#include "ToolboxOutOfMemory.hpp"
#include "ToolboxWindow.hpp"
#include "ToolboxScenePlatformController.hpp"

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
  const unsigned long kOsEventMessageMask = 0xFF000000UL;
#if defined(LOKA_TOOLBOX_MULTIVERSAL_INTERFACES)
  const unsigned long kSuspendResumeEventMessage = SUSPENDRESUMEBITS;
  const unsigned long kResumeEventFlag = RESUME;
#else
  const unsigned long kSuspendResumeEventMessage =
      static_cast<unsigned long>(suspendResumeMessage) << 24;
  const unsigned long kResumeEventFlag = resumeFlag;
#endif

  /** The keyboard script owns byte boundaries, including committed IM text. */
  bool ToolboxIsLeadByte(unsigned char byte, short script)
  {
    return script != smRoman && CharacterByteType(reinterpret_cast<Ptr>(&byte), 0, script) == smFirstByte;
  }

  bool IsSuspendResumeEvent(const EventRecord &event)
  {
    return (static_cast<unsigned long>(event.message) & kOsEventMessageMask) ==
           kSuspendResumeEventMessage;
  }

  bool IsResumeEvent(const EventRecord &event)
  {
    return (static_cast<unsigned long>(event.message) & kResumeEventFlag) != 0;
  }

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
    : app_(app), hoverCursor_(HOVER_ARROW), busyDepth_(0), lastApplied_(UNKNOWN),
      nativeApplies_(0), outerEntries_(0), outerExits_(0), iBeam_(), watch_()
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

void CursorOwner::enterBusy()
{
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
  unsigned long lastTick = TickCount();
  activationPhase_ = ACTIVATION_FOREGROUND;
  running_ = true;
  // The run loop is the clock regime: a joining write without an Operation
  // turn is a defect here and a legacy fallback everywhere else (#1057 C5).
  loka::core::Operation::Regime regime;
  while (running_)
  {
    EventRecord event;
    WaitNextEvent(everyEvent, &event, 1, 0);
    loka::core::Operation turn;
    // Consume the OS foreground fact before idle work or hover can write the
    // shared cursor on the iteration that delivers a suspend event.
    if (event.what == osEvt && IsSuspendResumeEvent(event))
    {
      this->activationPhase_ =
          IsResumeEvent(event) ? ACTIVATION_FOREGROUND : ACTIVATION_BACKGROUND;
      this->cursorOwner_.reconcile();
    }
    // TODO: Re-enable invalidation once Classic update flow is stable.
    if (group_)
    {
      const AppComponentGroup::Components comps = group_->getComponents();
      for (std::size_t i = 0; i < comps.size(); ++i)
      {
        Window *w = comps[i]->asWindow();
        ToolboxWindow *toolboxWindow = w ? w->asToolboxWindow() : 0;
        if (toolboxWindow)
        {
          toolboxWindow->idleControls(activationPhase_);
        }
      }
    }
    if (activationPhase_ == ACTIVATION_FOREGROUND)
      this->sampleHover(false);
    if (event.what == updateEvt)
    {
      // An OS update event arrives in every phase -- another window exposing
      // part of a background Loka window still needs that region repainted,
      // and skipping BeginUpdate/EndUpdate would leave the update region
      // pending so WaitNextEvent returns the same event forever. Mutation-driven
      // window presentation also runs in every phase (#503); present() keeps
      // pending dialogs and focus reconciliation foreground-only.
      WindowPtr target = reinterpret_cast<WindowPtr>(event.message);
      if (target && group_)
      {
        const AppComponentGroup::Components comps = group_->getComponents();
        for (std::size_t i = 0; i < comps.size(); ++i)
        {
          Window *w = comps[i]->asWindow();
          ToolboxWindow *toolboxWindow = w ? w->asToolboxWindow() : 0;
          if (toolboxWindow && toolboxWindow->window() == target)
          {
            if (toolboxWindow->scenePlatformController())
            {
              toolboxWindow->scenePlatformController()->noteWindowUpdateEvtDraw();
            }
            // An OS update event is the presentation itself, so draw while
            // its update clip is active. Mutation-driven paints stay deferred.
            BeginUpdate(target);
            toolboxWindow->draw();
            EndUpdate(target);
            break;
          }
        }
      }
    }
    else if (event.what == mouseDown)
    {
      WindowPtr target = 0;
      short part = FindWindow(event.where, &target);
      if (part == inMenuBar)
      {
        long choice = MenuSelect(event.where);
        this->cursorOwner_.reconcile();
        if (choice != 0)
        {
          short menuId = static_cast<short>(choice >> 16);
          short item = static_cast<short>(choice & 0xFFFF);
          this->handleMenuSelection(menuId, item);
          HiliteMenu(0);
        }
      }
      if (part == inGoAway && target)
      {
        if (TrackGoAway(target, event.where))
        {
          ToolboxWindow *closing = 0;
          if (group_)
          {
            const AppComponentGroup::Components comps = group_->getComponents();
            for (std::size_t i = 0; i < comps.size(); ++i)
            {
              Window *w = comps[i]->asWindow();
              ToolboxWindow *toolboxWindow = w ? w->asToolboxWindow() : 0;
              if (toolboxWindow && toolboxWindow->window() == target)
              {
                closing = toolboxWindow;
                break;
              }
            }
          }
          // Same request/terminal split as NullWindow::simulateCloseAsked.
          if (closing && !this->closeAsked(static_cast<Window *>(closing)))
          {
            requestWindowClose(closing);
          }
        }
      }
      else if (part == inContent && target)
      {
        if (target != FrontWindow())
        {
          // The first content click selects a back window without reaching its content.
          SelectWindow(target);
        }
        else
        {
          ToolboxWindow *clicked = 0;
          if (group_)
          {
            const AppComponentGroup::Components comps = group_->getComponents();
            for (std::size_t i = 0; i < comps.size(); ++i)
            {
              Window *w = comps[i]->asWindow();
              ToolboxWindow *toolboxWindow = w ? w->asToolboxWindow() : 0;
              if (toolboxWindow && toolboxWindow->window() == target)
              {
                clicked = toolboxWindow;
                break;
              }
            }
          }
          if (clicked)
          {
            if (!clicked->hasPendingInvalidate())
            {
              clicked->handleMouseDown(event.where);
              this->cursorOwner_.reconcile();
            }
          }
        }
      }
      else if (part == inDrag && target)
      {
        Rect bounds = qd.screenBits.bounds;
        DragWindow(target, event.where, &bounds);
        this->cursorOwner_.reconcile();
        ToolboxWindow *dragged = FindToolboxWindow(group_, target);
        if (dragged)
        {
          dragged->storeCurrentNativeContentFrame();
        }
      }
      else if (part == inGrow && target)
      {
        ToolboxWindow *grown = FindToolboxWindow(group_, target);
        if (grown)
        {
          grown->handleGrow(event.where);
        }
      }
    }
    else if (event.what == activateEvt)
    {
      WindowPtr target = reinterpret_cast<WindowPtr>(event.message);
      if (group_)
      {
        const AppComponentGroup::Components comps = group_->getComponents();
        for (std::size_t i = 0; i < comps.size(); ++i)
        {
          Window *w = comps[i]->asWindow();
          ToolboxWindow *toolboxWindow = w ? w->asToolboxWindow() : 0;
          if (toolboxWindow && toolboxWindow->window() == target)
          {
            // A front-window change arrives as two records: the old window
            // without activeFlag, then the new one with it. Only the second
            // names the active window.
            if ((event.modifiers & activeFlag) != 0)
              setActiveWindow(toolboxWindow);
            // The grow icon follows the window hilite state; redraw only
            // its corner through the ordinary update event.
            toolboxWindow->invalidateGrowIcon();
            break;
          }
        }
      }
    }
    else if (event.what == osEvt && IsSuspendResumeEvent(event))
    {
      ToolboxWindow *active = activeWindow() ? activeWindow()->asToolboxWindow() : 0;
      if (active && active->window())
      {
        HiliteWindow(active->window(), activationPhase_ == ACTIVATION_FOREGROUND);
        active->invalidateGrowIcon();
      }
      if (activationPhase_ == ACTIVATION_FOREGROUND && menuBarDrawDeferred_)
      {
        // Background projection or bindings updated menu data but deferred
        // the shared menu bar write; show it once now the bar is ours again.
        DrawMenuBar();
        menuBarDrawDeferred_ = false;
      }
    }
    else if (event.what == keyDown || event.what == autoKey)
    {
      ToolboxWindow *active = activeWindow() ? activeWindow()->asToolboxWindow() : 0;
      char key = static_cast<char>(event.message & charCodeMask);
      // A Command key is a menu command, never text: it goes to MenuKey only,
      // so a focused field cannot swallow it, and a plain key never reaches
      // MenuKey (#1086). Option-only keys stay text. A held Command key's
      // autoKey repeats are swallowed so a command runs once per press.
      const bool command = (event.modifiers & cmdKey) != 0;
      bool handled = false;
#if LOKA_RETRO68_DIAGNOSTICS
      if (active && command && (key == 'd' || key == 'D'))
      {
        active->requestDeferredDebugDump();
        handled = true;
      }
#endif
      if (!command)
      {
        const unsigned char byte = static_cast<unsigned char>(key);
        if (byte >= 0x80 && ToolboxIsLeadByte(byte, static_cast<short>(GetScriptManagerVariable(smKeyScript))))
        {
          // Until T2 decodes this unit, consume its available trail and refuse
          // both bytes together. With no next key, refuse the lead alone (#1439).
          EventRecord trail;
          (void)GetNextEvent(keyDownMask | autoKeyMask, &trail);
          SysBeep(1);
          handled = true;
        }
        const unsigned char keyCode = static_cast<unsigned char>((event.message & keyCodeMask) >> 8);
        ToolboxScrollKey scrollKey;
        if (!handled && QueryToolboxScrollKey(keyCode, scrollKey))
          handled = active && active->handleScrollKey(scrollKey);
        if (!handled)
          handled = active && active->handleKeyDown(key);
        if (!handled)
        {
          handled = this->handleKeyPress(key);
        }
      }
      else if (!handled && event.what == keyDown)
      {
        long choice = MenuKey(key);
        if (choice != 0)
        {
          short menuId = static_cast<short>(choice >> 16);
          short item = static_cast<short>(choice & 0xFFFF);
          this->handleMenuSelection(menuId, item);
          HiliteMenu(0);
        }
      }
    }
    unsigned long now = TickCount();
    double elapsedSeconds = 0.0;
    if (now >= lastTick)
    {
      elapsedSeconds = static_cast<double>(now - lastTick) / 60.0;
    }
    lastTick = now;
    double dispatchElapsedSeconds = 0.0;
    if (this->consumeIdle(elapsedSeconds, dispatchElapsedSeconds))
    {
      this->handleIdle(dispatchElapsedSeconds);
    }
    this->present(activationPhase_, turn);
    this->cursorOwner_.assertIdle();
    if (event.what == nullEvent && group_)
    {
      const AppComponentGroup::Components comps = group_->getComponents();
      for (std::size_t i = 0; i < comps.size(); ++i)
      {
        Window *w = comps[i]->asWindow();
        ToolboxWindow *toolboxWindow = w ? w->asToolboxWindow() : 0;
        if (toolboxWindow)
        {
          toolboxWindow->flushDeferredDebugDump();
        }
      }
    }
    loka::toolbox::QuitIfOutOfMemoryReserveSpent();
  }
}

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
