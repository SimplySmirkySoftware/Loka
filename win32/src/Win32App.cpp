#include "app/MenuComposition.hpp"
#include "Win32App.hpp"
#include "Win32Window.hpp"
#include <windows.h>
#include <commdlg.h>
#include "app/core/App.hpp"
#include "platform/Win32IdlePacer.hpp"

Win32App::Win32App(AppConfigurable *config, HINSTANCE hInstance, int nCmdShow)
    : App(config),
      hInstance_(hInstance),
      nCmdShow_(nCmdShow)
{
  // App already owns the shared configuration state.
}

Win32App::~Win32App()
{
  this->retireComponents();
}

void Win32App::quit()
{
  PostQuitMessage(0);
}

void Win32App::TranslateOrDispatch(MSG &msg)
{
  HWND root = msg.hwnd ? GetAncestor(msg.hwnd, GA_ROOT) : NULL;
  Win32Window *window = Win32Window::FromHwnd(root);
  if (window && window->menuAttachment().translateAccelerator(msg))
    return;
  // Both consumed paths still reach the run loop's admission/completion tail.
  if (root && IsDialogMessageW(root, &msg))
    return;
  TranslateMessage(&msg);
  DispatchMessageW(&msg);
}

bool Win32App::windowAdopted(Window *window)
{
  Win32Window *win32Window = window ? window->asWin32Window() : 0;
  if (win32Window)
    win32Window->setApp(this);
  return true;
}

void Win32App::run()
{
  App::run();

  LARGE_INTEGER frequency;
  LARGE_INTEGER lastTick;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&lastTick);
  loka::platform::Win32IdlePacer idlePacer;

  bool running = true;
  // The run loop is the clock regime: a joining write without an Operation
  // turn is a defect here and a legacy fallback everywhere else (#1057 C5).
  loka::core::Operation::Regime regime;
  while (running)
  {
    bool handledMessage = false;
    bool idleDispatched = false;
    LARGE_INTEGER now;
    loka::app::IdlePolicy waitPolicy;
    {
      loka::core::Operation turn;
      MSG msg;
      while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
      {
        handledMessage = true;
        if (msg.message == WM_QUIT)
        {
          running = false;
          break;
        }
        TranslateOrDispatch(msg);
      }
      if (!running)
      {
        break;
      }

      const loka::app::IdlePolicy policy = this->idlePolicy();

      QueryPerformanceCounter(&now);
      double elapsedSeconds = 0.0;
      if (frequency.QuadPart > 0)
      {
        elapsedSeconds = static_cast<double>(now.QuadPart - lastTick.QuadPart) / static_cast<double>(frequency.QuadPart);
      }
      lastTick = now;

      if (policy.mode == loka::app::IDLE_MODE_NONE)
      {
        idlePacer.reset();
        this->flushIterationTail(turn);
        if (this->hasPendingWindowAdmission())
          continue;
        waitPolicy = policy;
      }
      else
      {
        double candidateElapsedSeconds = 0.0;
        const bool idleCandidate = this->consumeIdle(elapsedSeconds, candidateElapsedSeconds);
        double dispatchElapsedSeconds = candidateElapsedSeconds;
        idleDispatched = idleCandidate;
        if (policy.mode == loka::app::IDLE_MODE_EVERY_TICK)
        {
          idleDispatched = idleCandidate
                           && idlePacer.gateEveryTick(
                               candidateElapsedSeconds, policy, now.QuadPart, frequency.QuadPart, dispatchElapsedSeconds);
        }
        if (idleDispatched)
        {
          this->handleIdle(dispatchElapsedSeconds);
        }
        this->flushIterationTail(turn);
        waitPolicy = this->idlePolicy();
        if (waitPolicy.mode == loka::app::IDLE_MODE_NONE)
        {
          idlePacer.reset();
          continue;
        }
        if (this->hasPendingWindowAdmission())
          continue;
      }
    } // Close and destroy the turn before either wait.
    if (waitPolicy.mode == loka::app::IDLE_MODE_NONE)
    {
      if (!handledMessage)
      {
#ifdef LOKA_LIFECYCLE_AUDIT
        assert(!loka::core::Operation::hasActive());
#endif
        WaitMessage();
      }
      continue;
    }
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(!loka::core::Operation::hasActive());
#endif
    idlePacer.wait(waitPolicy, idleDispatched, now.QuadPart, frequency.QuadPart);
  }
}

void Win32App::flushIterationTail(loka::core::Operation &turn)
{
  turn.settle();
  this->admitAndApplyWindows();
  this->reconcileFocus();
  this->admitAndApplyWindows();
  turn.close();
  this->reclaimWindows();
}

bool Win32App::handleMenuCommand(int commandId, Window *window)
{
  // Lookup and emission belong to the Window attachment; acknowledge dispatch.
  (void)commandId;
  (void)window;
  return true;
}

void Win32App::projectMenu(Window *activeWindow, const loka::app::MenuSources &sources)
{
  Win32Window *win = activeWindow ? activeWindow->asWin32Window() : 0;
  if (!win || !win->hwnd())
    return;
  // Temporary bridge, mirrored by the other rails until identity projection lands.
  loka::core::OwnedDef<loka::app::MenuBarDefinition> merged(
      loka::app::MergeMenuBars(sources.base, sources.overlay));
  if (!sources.absent() && !merged.isSet())
    return;
  win->menuAttachment().project(merged.get(), sources.source);
}
