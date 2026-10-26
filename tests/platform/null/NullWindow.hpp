#ifndef LOKA_TESTS_PLATFORM_NULL_WINDOW_HPP
#define LOKA_TESTS_PLATFORM_NULL_WINDOW_HPP

#include "app/core/Window.hpp"
#include "app/core/App.hpp"
#include "app/core/DialogResultTransport.hpp"
#include "app/scene/Scene.hpp"
#include "platform/null/NullScenePlatformController.hpp"

namespace loka
{
  namespace app
  {
    namespace testing
    {
      class NullWindowTestAccess;
    }
  } // namespace app
} // namespace loka

class NullWindow : public Window
{
public:
  /** Concrete rail owns enrollment; App sees only its admission interface. */
  loka::app::DialogResultTransport &dialogResults() { return this->dialogResults_; }
  NullWindow(PlatformContext *context, const WindowProps &props, NullScenePlatformController *borrowedController = 0)
      : Window(context, props),
        app_(0),
        controller_(borrowedController),
        ownsController_(borrowedController == 0),
        mountedScene_(false),
        projectedEdited_(false),
        editedProjectionCount_(0)
  {
    if (!this->controller_)
    {
      this->controller_ = new NullScenePlatformController();
    }
    this->observeNativeState(this->editedState(), &NullWindow::EditedChangedThunk, this);
    this->dialogResults().open(*this);
    this->mountScene();
  }

  virtual bool handleEditCommand(loka::app::EditVerb verb)
  {
    if (!this->controller_)
      return false;
    return loka::app::scene::detail::InputInvocation(*this->controller_)(
        *this->controller_, &NullScenePlatformController::handleEditCommand, verb);
  }

  void setApp(App *app) { this->app_ = app; }
  App *app() const { return this->app_; }
  /** Deliberate Win32/Null counterpart: WM_DESTROY requests close via the App back-pointer. */
  void simulateCloseAsked()
  {
    if (this->app_ && !this->app_->closeAsked(this))
      this->app_->requestWindowClose(this);
  }
  void simulateNativeClose()
  {
    if (this->app_)
      this->app_->requestWindowClose(this);
  }

  virtual ~NullWindow()
  {
    this->detachNativeStateObservers();
    this->destroyScenePlatform();
  }

  /** Native teardown body used by App admission and terminal destruction. */
  void destroyScenePlatform()
  {
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(!this->controller_ || !this->controller_->borrowPhase().open());
#endif
    this->dialogResults().close();
    this->teardownScene();
    if (this->controller_)
    {
      this->controller_->destroy();
      this->controller_->recordWindowDisposed();
    }
    if (this->ownsController_)
    {
      delete this->controller_;
    }
    this->controller_ = 0;
  }

  void mountScene()
  {
    loka::app::scene::Scene *currentScene = this->scene();
    if (!currentScene || this->mountedScene_ || !this->controller_)
    {
      return;
    }
    this->mountReplacementScene(currentScene);
    this->mountedScene_ = true;
  }

  /** Mirrors the concrete windows' teardown ordering: the scene unmounts
      (and severs its controller pointer) before the controller can be
      deleted, so the scene manager's later scene destruction never reaches
      a freed controller. */
  void teardownScene()
  {
    if (!this->mountedScene_)
    {
      return;
    }
    loka::app::scene::Scene *currentScene = this->scene();
    if (currentScene)
    {
      this->unmountSceneForTeardown(*currentScene);
    }
    this->mountedScene_ = false;
  }

  NullScenePlatformController *scenePlatformController() const
  {
    return this->controller_;
  }

  virtual bool hasPendingScenePlatformSync() const
  {
    return this->controller_ ? this->controller_->hasPendingSync() : false;
  }

  virtual void synchronizeScenePlatform()
  {
    if (this->controller_)
    {
      this->controller_->synchronize();
    }
  }

  virtual void drainNativeRetirements()
  {
    if (this->controller_)
    {
      this->controller_->drainNativeRetirements();
    }
  }

protected:
  virtual bool hasLiveScenePlatform() const
  {
    return this->controller_ && this->mountedScene_;
  }

  virtual bool mountReplacementScene(loka::app::scene::Scene *next)
  {
    if (this->controller_)
    {
      if (!next->mount(this->controller_))
        return false;
      this->mountedScene_ = true;
    }
    return true;
  }

private:
  friend class loka::app::testing::NullWindowTestAccess;
  static void EditedChangedThunk(void *data)
  {
    NullWindow *self = static_cast<NullWindow *>(data);
    self->projectedEdited_ = self->editedState().get();
    ++self->editedProjectionCount_;
  }
  virtual void closeDialogResults() { this->dialogResults_.close(); }
  // Deliberate Win32/Null counterparts: stable service across native recreation.
  virtual loka::app::DialogResultDelivery *dialogResultDelivery()
  {
    return &this->dialogResults_;
  }
  loka::app::DialogResultTransport dialogResults_;

  // Deliberate counterpart of Win32's HWND comparison. This rail uses its
  // controller identity as fake native presence, independently of scene mount.
  virtual bool hasPendingNativeVisibility() const
  {
    return this->visibility_->get() != (this->controller_ != 0);
  }

  virtual void applyNativeVisibility()
  {
    if (!this->hasPendingNativeVisibility())
      return;
    if (this->visibility_->get())
    {
      this->controller_ = new NullScenePlatformController();
      this->ownsController_ = true;
      this->dialogResults().open(*this);
      this->mountScene();
    }
    else
      this->destroyScenePlatform();
  }

  App *app_;
  NullScenePlatformController *controller_;
  bool ownsController_;
  bool mountedScene_;
  bool projectedEdited_;
  unsigned editedProjectionCount_;

  NullWindow(const NullWindow &);
  NullWindow &operator=(const NullWindow &);
};

#endif // LOKA_TESTS_PLATFORM_NULL_WINDOW_HPP
