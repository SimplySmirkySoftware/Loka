#ifndef LOKA_WIN32WINDOW_HPP
#define LOKA_WIN32WINDOW_HPP

#include "app/core/Window.hpp"
#include "app/core/DialogResultTransport.hpp"
#include "Win32MenuAttachment.hpp"
#include <windows.h>
#include <string>

class PlatformContext;
class Win32App;

namespace loka
{
  namespace core
  {
    namespace scene
    {
      class Scene;
    }
  } // namespace core
} // namespace loka

class Win32ScenePlatformController;

/** Win32-specific Window implementation. */
class Win32Window : public Window
{
public:
  /** Concrete rail owns enrollment; App sees only its admission interface. */
  loka::app::DialogResultTransport &dialogResults() { return this->dialogResults_; }
  Win32Window(PlatformContext *context, const WindowProps &props);
  virtual ~Win32Window();
  virtual Win32Window *asWin32Window()
  {
    return this;
  }

  virtual bool handleEditCommand(loka::app::EditVerb verb);
  void setApp(Win32App *app);
  /** Borrows the rail owner; absent before enrollment. */
  Win32App *win32App() const { return this->app_; }
  Win32MenuAttachment &menuAttachment() { return this->menuAttachment_; }
  HWND hwnd() const
  {
    return hwnd_;
  }

  /** Reads the native window position and client size as one content frame. */
  bool queryNativeContentFrame(loka::core::Frame &out) const;
  /** Publishes the current native content frame as a rail-owned fact. */
  bool storeCurrentNativeContentFrame();
  /** Projects a content frame into the native window. */
  bool applyNativeContentFrame(const loka::core::Frame &frame);
  /** Detaches an installed menu during teardown while preserving the native
      client frame and publishing no logical State. */
  bool detachMenuForTeardown(HMENU expectedMenu);

  /** Borrows a Loka root; foreign window classes have no Window owner. */
  static Win32Window *FromHwnd(HWND hwnd);

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  virtual void onShow();
  virtual void onHide();
  virtual bool hasPendingScenePlatformSync() const;
  virtual void synchronizeScenePlatform();
  virtual void drainNativeRetirements();
  virtual bool queryDisplayScalePercent(int &out) const;
  virtual bool queryDisplayDepth(int &out) const;
  virtual bool queryDisplayAppearance(DisplayAppearance &out) const;

protected:
  HWND hwnd_;
  Win32App *app_;

  virtual void onCreate();

private:
  friend class Win32InputDoor;
  bool handleEditCommandInput(loka::app::EditVerb verb);
  void handleNativeSize(WPARAM wParam, LPARAM lParam);
  void handleNativeDpi(WPARAM wParam, LPARAM lParam);
  virtual void closeDialogResults() { this->dialogResults_.close(); }
  // Deliberate Win32/Null counterparts: stable service across native recreation.
  virtual loka::app::DialogResultDelivery *dialogResultDelivery()
  {
    return &this->dialogResults_;
  }
  loka::app::DialogResultTransport dialogResults_;

  void createNativeWindow();
  void destroyNativeWindow();
  virtual bool hasPendingNativeVisibility() const;
  virtual void applyNativeVisibility();
  static void TitleChangedThunk(void *userData);
  static void FrameChangedThunk(void *userData);
  virtual bool mountReplacementScene(loka::app::scene::Scene *next);
  // Deliberate rail counterpart of mountReplacementScene's resource checks.
  virtual bool hasLiveScenePlatform() const
  {
    return this->scenePlatformController_ && this->hwnd_;
  }
  void mountScene();
  void teardownScene();
  bool handleCommand(WPARAM wParam, LPARAM lParam);

  Win32MenuAttachment menuAttachment_;
  Win32ScenePlatformController *scenePlatformController_;
};

#endif // LOKA_WIN32WINDOW_HPP
