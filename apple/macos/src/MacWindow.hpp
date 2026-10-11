#ifndef LOKA_MAC_WINDOW_HPP
#define LOKA_MAC_WINDOW_HPP

#include "app/core/Window.hpp"
#include "app/core/DialogResultTransport.hpp"

class MacApp;
class MacScenePlatformController;
namespace loka { namespace macos { class MacProjection; } }

namespace loka
{
  namespace dsl
  {
    namespace testing
    {
      class MacWindowTestAccess;
    }
  } // namespace dsl
  namespace core
  {
    namespace scene
    {
      class Scene;
    }
  } // namespace core
} // namespace loka

class MacWindow : public Window
{
public:
  /** Concrete rail owns enrollment; App sees only its admission interface. */
  loka::app::DialogResultTransport &dialogResults() { return this->dialogResults_; }
  MacWindow(PlatformContext *context, const WindowProps &props);
  virtual ~MacWindow();
  virtual MacWindow *asMacWindow()
  {
    return this;
  }

  /** Borrow the owner of this rail's root view while its delegate is attached. */
  static MacWindow *fromRootView(void *rootView);
  void setApp(MacApp *app);
  /** Borrows the rail owner; absent before the Window is enrolled. */
  MacApp *macApp() const { return this->app_; }

  virtual void onShow();
  virtual void onHide();
  virtual bool hasPendingScenePlatformSync() const;
  virtual void synchronizeScenePlatform();
  virtual void drainNativeRetirements();
  virtual bool queryDisplayScalePercent(int &out) const;
  virtual bool queryDisplayDepth(int &out) const;
  virtual bool queryDisplayAppearance(DisplayAppearance &out) const;

  void handleWindowWillClose();
  void handleWindowDidBecomeKey();
  bool handleKeyPress(char key);

protected:
  virtual void onCreate();

private:
  friend class MacInputDoor;
  void handleWindowDidResize();
  void handleWindowDidMove();
  friend class ::loka::dsl::testing::MacWindowTestAccess;

  // Deliberate Win32/Null counterpart: stable across native recreation.
  virtual void closeDialogResults() { this->dialogResults_.close(); }
  virtual loka::app::DialogResultDelivery *dialogResultDelivery()
  {
    return &this->dialogResults_;
  }
  loka::app::DialogResultTransport dialogResults_;

  loka::macos::MacProjection projection() const;
  void createNativeWindow();
  void destroyNativeWindow();
  virtual bool hasPendingNativeVisibility() const;
  virtual void applyNativeVisibility();
  static void TitleChangedThunk(void *userData);
  static void EditedChangedThunk(void *userData);
  static void FrameChangedThunk(void *userData);
  virtual bool mountReplacementScene(loka::app::scene::Scene *next);
  // Deliberate rail counterpart of mountReplacementScene's resource checks.
  virtual bool hasLiveScenePlatform() const
  {
    return this->scenePlatformController_ && this->window_ && this->contentView_;
  }
  void mountScene();
  void teardownScene();

  void *window_;
  void *contentView_;
  void *delegate_;
  MacApp *app_;
  bool closing_;

  MacScenePlatformController *scenePlatformController_;
};

#endif // LOKA_MAC_WINDOW_HPP
