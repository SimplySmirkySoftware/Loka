#ifndef LOKA_TOOLBOX_WINDOW_HPP
#define LOKA_TOOLBOX_WINDOW_HPP
#include "ToolboxFullDrawReason.hpp"

#include "ToolboxScrollKey.hpp"
#include "ToolboxTabKey.hpp"
#include "app/core/Window.hpp"
#include "app/core/DialogResultTransport.hpp"
#include "ToolboxActivationPhase.hpp"
#include "ToolboxWindowGeometry.hpp"
#include <Windows.h>
#include <Events.h>
#include "ToolboxPresentBatch.hpp"

class App;
class ToolboxApp;
class ToolboxScenePlatformController;
class ToolboxWindowContext;

/** Native definition-procedure insets, measured once for a created window. */
class ToolboxWindowChrome
{
public:
  explicit ToolboxWindowChrome(WindowPtr window = 0);
  short left() const { return this->insets_.left; }
  short top() const { return this->insets_.top; }
  short right() const { return this->insets_.right; }
  short bottom() const { return this->insets_.bottom; }

private:
  Rect insets_;
};

class ToolboxWindow : public Window
{
public:
  virtual bool handleEditCommand(loka::app::EditVerb);
  ToolboxWindow(PlatformContext *context, const WindowProps &props);
  virtual ~ToolboxWindow();
  virtual ToolboxWindow *asToolboxWindow()
  {
    return this;
  }

  loka::app::DialogResultTransport &dialogResults() { return this->dialogResults_; }
  void setApp(App *app);
  void ensureSceneMounted();
  void open();
  void requestInvalidate();
  void requestInvalidateWithReason(ToolboxFullDrawReason reason);
  void requestInvalidateRect(const Rect &rect);
  void flushInvalidate();
  bool hasPendingInvalidate() const;
  bool handleMouseDown(const Point &globalPoint, const EventRecord &event);
  bool offerKeyEvent(const EventRecord &event);
  /** Tracks a grow-box drag from the given global mouse-down point, resizes
      the native window, and publishes the new content frame (GH #524). */
  void handleGrow(const Point &globalPoint);
  /** Publishes the current native content frame. */
  void storeCurrentNativeContentFrame();
  /** Invalidates the grow-box corner so the next update redraws it in the
      window's current hilite state. */
  void invalidateGrowIcon();
  bool handleKeyDown(char key);
  bool handleScrollKey(ToolboxScrollKey key);
  bool handleTabKey(ToolboxTabDirection direction);
  void drawDirty(const Rect &rect);
  void idleControls(ActivationPhase phase);
  void updateCursor(bool force = false);
  /** Borrow the app that owns this native window. */
  ToolboxApp *toolboxApp() const;
  void invalidateWindow();
  /** Not re-entrant; does not pump events. Owns the complete OS update bracket. */
  void drawForUpdate();
  void drawForFlush();

private:
  void drawWhole(const Rect &evidence);

public:
  /** Preserves the native content position after a menu-bar rebuild without
      reapplying the application's declared frame intent. */
  void preserveNativeContentPositionAfterMenuBarChange();
  virtual bool hasPendingScenePlatformSync() const;
  virtual void synchronizeScenePlatform();
  virtual void drainNativeRetirements();
  virtual bool dumpDebugStatsToTimestampedFile();
  virtual void resetDebugStats();
  virtual void requestDeferredDebugDump();
  virtual void flushDeferredDebugDump();
  virtual bool queryDisplayScalePercent(int &out) const;
  virtual bool queryDisplayDepth(int &out) const;
  // queryDisplayAppearance is deliberately not overridden: Classic has no way
  // to report light versus dark. The Appearance Manager and extensions such as
  // Kaleidoscope patch the standard definition procedures, so themes apply to
  // this backend already -- it draws through the standard CDEF and bakes no
  // colours -- but that is inheritance, not a fact anything can query.
  WindowPtr window() const
  {
    return window_;
  }
  /** True while the native window is drawn active (WindowRecord::hilited).
      Read through WindowPeek, as the chrome reads the region fields:
      IsWindowHilited is a Carbon accessor the Multiversal Interfaces lack. */
  bool isHilited() const;
  /** Answers port coordinates only while the native window exists. */
  bool queryPortRect(Rect &out) const
  {
    if (!this->window_)
      return false;
    out = this->window_->portRect;
    return true;
  }
  /** Answers the size box in port coordinates when the native window exists. */
  bool querySizeBoxRect(Rect &out) const
  {
    if (!this->window_)
      return false;
    out = ToolboxWindowSizeBoxRect(this->window_->portRect);
    return true;
  }
  ToolboxWindowContext *context() const
  {
    return context_;
  }
  ToolboxScenePlatformController *scenePlatformController() const
  {
    return scenePlatformController_;
  }

private:
  virtual void closeDialogResults() { this->dialogResults_.close(); }
  virtual loka::app::DialogResultDelivery *dialogResultDelivery() { return &this->dialogResults_; }
  loka::app::DialogResultTransport dialogResults_;
  static void TitleChangedThunk(void *userData);
  static void FrameChangedThunk(void *userData);
  App *app_;
  WindowPtr window_;
  ToolboxScenePlatformController *scenePlatformController_;
  ToolboxWindowContext *context_;
  ToolboxPresentBatch next_;
  bool pendingDebugDump_;
  ToolboxWindowChrome chrome_;

  virtual bool mountReplacementScene(loka::app::scene::Scene *next);
  // Deliberate rail counterpart of mountReplacementScene's resource checks.
  virtual bool hasLiveScenePlatform() const
  {
    return this->scenePlatformController_ && this->window_;
  }
  void mountScene();
  void teardownScene();
  loka::core::Frame nativeContentFrame() const;
  void drawGrowBox();
};

#endif // LOKA_TOOLBOX_WINDOW_HPP
