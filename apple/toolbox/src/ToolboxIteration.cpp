#include "ToolboxIdleClock.hpp"

namespace
{
  const unsigned long kOsEventMessageMask = 0xFF000000UL;
#if defined(LOKA_TOOLBOX_MULTIVERSAL_INTERFACES)
  const unsigned long kSuspendResumeEventMessage = SUSPENDRESUMEBITS;
  const unsigned long kResumeEventFlag = RESUME;
#else
  const unsigned long kSuspendResumeEventMessage = static_cast<unsigned long>(suspendResumeMessage) << 24;
  const unsigned long kResumeEventFlag = resumeFlag;
#endif

  /** The keyboard script owns byte boundaries, including committed IM text. */
  bool ToolboxIsLeadByte(unsigned char byte, short script)
  {
    return script != smRoman && CharacterByteType(reinterpret_cast<Ptr>(&byte), 0, script) == smFirstByte;
  }

  bool ToolboxIsTrailByte(unsigned char lead, unsigned char byte, short script)
  {
    unsigned char pair[] = {lead, byte};
    return script != smRoman && CharacterByteType(reinterpret_cast<Ptr>(pair), 1, script) == smLastByte;
  }

  bool IsSuspendResumeEvent(const EventRecord &event)
  {
    return (static_cast<unsigned long>(event.message) & kOsEventMessageMask) == kSuspendResumeEventMessage;
  }

  bool IsResumeEvent(const EventRecord &event)
  {
    return (static_cast<unsigned long>(event.message) & kResumeEventFlag) != 0;
  }

} // namespace

void ToolboxApp::iterate(ToolboxIdleClock &clock, bool gotEvent, const EventRecord &fetched)
{
  (void)gotEvent;
  EventRecord record = fetched;
  loka::core::Operation turn;
  // Consume the OS foreground fact before idle work or hover can write the
  // shared cursor on the iteration that delivers a suspend event.
  if (record.what == osEvt && IsSuspendResumeEvent(record))
  {
    this->activationPhase_ = IsResumeEvent(record) ? ACTIVATION_FOREGROUND : ACTIVATION_BACKGROUND;
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
  if (record.what == updateEvt)
  {
    // An OS update event arrives in every phase -- another window exposing
    // part of a background Loka window still needs that region repainted,
    // and skipping BeginUpdate/EndUpdate would leave the update region
    // pending so WaitNextEvent returns the same event forever. Mutation-driven
    // window presentation also runs in every phase (#503); present() keeps
    // pending dialogs and focus reconciliation foreground-only.
    WindowPtr target = reinterpret_cast<WindowPtr>(record.message);
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
          // The entry snapshots update evidence before opening its update bracket.
          toolboxWindow->drawForUpdate();
          break;
        }
      }
    }
  }
  else if (record.what == mouseDown)
  {
    WindowPtr target = 0;
    short part = FindWindow(record.where, &target);
    if (part == inMenuBar)
    {
      long choice = MenuSelect(record.where);
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
      if (TrackGoAway(target, record.where))
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
          // Queued damage never drops a press; the controller refuses presses
          // on parked rows itself through the render-order press door (#1493).
          clicked->handleMouseDown(record.where, record);
          this->cursorOwner_.reconcile();
        }
      }
    }
    else if (part == inDrag && target)
    {
      Rect bounds = qd.screenBits.bounds;
      DragWindow(target, record.where, &bounds);
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
        grown->handleGrow(record.where);
      }
    }
  }
  else if (record.what == activateEvt)
  {
    WindowPtr target = reinterpret_cast<WindowPtr>(record.message);
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
          if ((record.modifiers & activeFlag) != 0)
            setActiveWindow(toolboxWindow);
          // The grow icon follows the window hilite state; redraw only
          // its corner through the ordinary update event.
          toolboxWindow->invalidateGrowIcon();
          break;
        }
      }
    }
  }
  else if (record.what == osEvt && IsSuspendResumeEvent(record))
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
  else if (record.what == keyDown || record.what == autoKey)
  {
    ToolboxWindow *active = activeWindow() ? activeWindow()->asToolboxWindow() : 0;
    char key = static_cast<char>(record.message & charCodeMask);
    // A Command key is a menu command, never text: it goes to MenuKey only,
    // so a focused field cannot swallow it, and a plain key never reaches
    // MenuKey (#1086). Option-only keys stay text. A held Command key's
    // autoKey repeats are swallowed so a command runs once per press.
    const bool command = (record.modifiers & cmdKey) != 0;
    bool handled = false;
    const unsigned char byte = static_cast<unsigned char>(key);
    const short keyScript = static_cast<short>(GetScriptManagerVariable(smKeyScript));
    // An input method delivers committed text one byte per key-down, and the
    // trail may arrive in a later iteration, so the lead is held across
    // non-key events instead of pulling another event (#1439).
    if (!command && active && active->offerKeyEvent(record))
    {
      this->twoByteKey_.release();
      handled = true;
    }
    if (!handled && this->twoByteKey_.holdsLead())
    {
      const unsigned char lead = this->twoByteKey_.lead();
      this->twoByteKey_.release();
      handled = !command && ToolboxIsTrailByte(lead, byte, keyScript);
      // Refuse a complete pair once. Otherwise drop only the held lead and
      // let this event follow its normal path, including holding a new lead.
      SysBeep(1);
    }
#if LOKA_RETRO68_DIAGNOSTICS
    if (active && command && (key == 'd' || key == 'D'))
    {
      active->requestDeferredDebugDump();
      handled = true;
    }
#endif
    if (!command)
    {
      if (!handled && byte >= 0x80 && ToolboxIsLeadByte(byte, keyScript))
      {
        this->twoByteKey_.hold(byte);
        handled = true;
      }
      const unsigned char keyCode = static_cast<unsigned char>((record.message & keyCodeMask) >> 8);
      ToolboxScrollKey scrollKey;
      if (!handled && QueryToolboxScrollKey(keyCode, scrollKey))
        handled = active && active->handleScrollKey(scrollKey);
      ToolboxTabDirection tabDirection;
      if (!handled && QueryToolboxTabKey(byte, (record.modifiers & shiftKey) != 0, tabDirection))
        handled = active && active->handleTabKey(tabDirection);
      if (!handled)
        handled = active && active->handleKeyDown(key);
      if (!handled)
      {
        handled = this->handleKeyPress(key);
      }
    }
    else if (!handled && record.what == keyDown)
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
  double elapsedSeconds = clock.elapsedSeconds(TickCount());
  double dispatchElapsedSeconds = 0.0;
  if (this->consumeIdle(elapsedSeconds, dispatchElapsedSeconds))
  {
    this->handleIdle(dispatchElapsedSeconds);
  }
  this->present(activationPhase_, turn);
  this->cursorOwner_.assertIdle();
  if (record.what == nullEvent && group_)
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
