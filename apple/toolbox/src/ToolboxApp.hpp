#ifndef LOKA_TOOLBOX_APP_HPP
#define LOKA_TOOLBOX_APP_HPP

#include "app/core/App.hpp"
#include "core/Operation.hpp"
#include <vector>
#include <Menus.h>
#include <Quickdraw.h>
#include "ToolboxActivationPhase.hpp"
#include "ToolboxBusy.hpp"
#include "ToolboxMenuAttachment.hpp"
#include "ToolboxTwoByteKey.hpp"
class ToolboxApp;
class ToolboxSceneDebugStats;

/** The app-wide cursor writer. Scopes borrow it only for non-yielding work. */
class CursorOwner : public ToolboxBusyOwner
{
public:
  enum HoverCursor { HOVER_ARROW, HOVER_IBEAM };
  enum AppliedCursor { UNKNOWN, ARROW, IBEAM, WATCH };

  explicit CursorOwner(ToolboxApp &app);
  /** Load optional resources once, after Toolbox initialization. */
  void initialize();
  void setHover(HoverCursor cursor, bool force = false);
  void apply(bool force);
  /** Re-sample the front window after a native writer may have changed cursors. */
  void reconcile();
  void assertIdle() const;
#ifdef TEST_BUILD
  void copyDiagnostics(ToolboxSceneDebugStats &snapshot) const;
#endif

private:
  /** Startup copy survives resource purging without update-cycle allocation. */
  class CachedCursor
  {
  public:
    CachedCursor() : value_(), available_(false) {}
    void load(short resourceId);
    const Cursor *get() const { return this->available_ ? &this->value_ : 0; }
  private:
    Cursor value_;
    bool available_;
  };
  CursorOwner(const CursorOwner &);
  CursorOwner &operator=(const CursorOwner &);
  virtual void enterBusy();
  virtual void exitBusy();
  ToolboxApp &app_;
  HoverCursor hoverCursor_;
  short busyDepth_;
  AppliedCursor lastApplied_;
  unsigned long nativeApplies_;
  unsigned long outerEntries_;
  unsigned long outerExits_;
  CachedCursor iBeam_;
  CachedCursor watch_;
};

class ToolboxApp : public App
{
protected:
  explicit ToolboxApp(AppConfigurable *config);
  virtual ~ToolboxApp();
  friend class ToolboxPlatformContext;

public:
  CursorOwner &cursorOwner() { return this->cursorOwner_; }
  virtual void run();
  virtual void quit();
  void handleMenuSelection(short menuId, short item);
  virtual void projectMenu(Window *window, const loka::app::MenuSources &sources);
  ToolboxMenuAttachment &menuAttachment() { return this->menuAttachment_; }

  /** Called by projection or live bindings after updating app-owned menu
      data. Foreground: redraws the menu bar immediately. Background: the
      shared menu bar is the foreground application's surface, so the single
      redraw is deferred until resume. */
  void requestMenuBarDraw();

protected:
  virtual bool windowAdopted(Window *window);
  virtual void bootstrapWindowRefused(Window *window);
  virtual void bootstrapWindowless();

private:
  friend class CursorOwner;
  void sampleHover(bool force);
  /** Applies recorded scene changes and, while foreground, paints each window
      once at the run-loop tick's presentation boundary. */
  void present(ActivationPhase phase, loka::core::Operation &turn);
  /** The run loop owns this; every step branches on it rather than taking
      per-step booleans (see ToolboxActivationPhase.hpp). */
  ActivationPhase activationPhase_;
  CursorOwner cursorOwner_;
  /** Background projection or bindings changed menu data; one DrawMenuBar is owed at
      resume. */
  bool menuBarDrawDeferred_;
  ToolboxMenuAttachment menuAttachment_;
  ToolboxTwoByteKey twoByteKey_;
  bool running_;
};

#ifdef TEST_BUILD
#include "debug/ToolboxSceneDebugStats.hpp"
inline void CursorOwner::copyDiagnostics(ToolboxSceneDebugStats &snapshot) const
{
  snapshot.cursorNativeApplies = this->nativeApplies_;
  snapshot.cursorOuterEntries = this->outerEntries_;
  snapshot.cursorOuterExits = this->outerExits_;
  snapshot.cursorDepth = this->busyDepth_;
  snapshot.cursorLastApplied = this->lastApplied_;
}
#endif

#endif // LOKA_TOOLBOX_APP_HPP
