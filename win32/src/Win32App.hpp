#ifndef LOKA_WIN32APP_HPP
#define LOKA_WIN32APP_HPP

#include "app/core/App.hpp"
#include "core/Operation.hpp"
#include <windows.h>
#include <vector>

class Win32Window;

class Win32App : public App
{
protected:
  Win32App(AppConfigurable *config, HINSTANCE hInstance, int nCmdShow);
  virtual ~Win32App();
  friend class Win32PlatformContext;

public:
  virtual void run();
  virtual void quit();
  /** Acknowledges a command already dispatched by the Window attachment. */
  bool handleMenuCommand(int commandId, Window *window);

protected:
  /** Routes one non-quit message: menu shortcut, dialog navigation, dispatch. */
  static void TranslateOrDispatch(MSG &msg);
  virtual bool windowAdopted(Window *window);
  /** Offers the process command line's elements after element zero, once per
      bootstrap; refused elements are dropped (B3 page 2b R4). */
  virtual void collectLaunchRequests();
  virtual void projectMenu(Window *window, const loka::app::MenuSources &sources);

private:
  /** Flushes Scene work produced inside the focus completion's operation in
      the same iteration with the second window flush, and serves URLs before
      that flush so it delivers their results before the turn closes. The
      Null model is WindowAdmissionTestApp::operationLoop(). */
  void flushIterationTail(loka::core::Operation &turn, App::CompletionEntry entry);
  HINSTANCE hInstance_;
  int nCmdShow_;
};

#endif // LOKA_WIN32APP_HPP
