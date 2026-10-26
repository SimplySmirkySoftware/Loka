#ifndef LOKA_TEST_TOOLBOX_FILE_CHOICE_HOST_HPP
#define LOKA_TEST_TOOLBOX_FILE_CHOICE_HOST_HPP
// Native neighbors only: production dialog, input door and present are tested.
#define LOKA_TOOLBOX_SCENE_PLATFORM_CONTROLLER_HPP
#define LOKA_TOOLBOX_APP_HPP
#include "ToolboxFullDrawPresenter.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "Quickdraw.h"
#include "ToolboxScrollKey.hpp"
#include "ToolboxTabKey.hpp"
#include "ToolboxPendingDialogs.hpp"
#include "app/core/DialogResultTransport.hpp"
struct Point { short v, h; };
class CursorOwner
{
public:
  unsigned reconciles;
  CursorOwner() : reconciles(0) {}
  void reconcile() { ++this->reconciles; }
};
class ToolboxScenePlatformController : public loka::app::scene::IPlatformController
{
public:
  loka::app::DialogResultTransport *transport;
  loka::app::DialogResultTransport *dialogResults() { return this->transport; }
  ToolboxPendingDialogs pending_;
  ToolboxPendingDialogs &pendingDialogs() { return this->pending_; }
  void (*onRender)(void *);
  void *renderData;
  ToolboxScenePlatformController() : transport(0), onRender(0), renderData(0) {}
  CursorOwner *cursorOwner() { return 0; }
  void render(ToolboxFullDrawPresenter *presenter)
  {
    if (presenter)
      presenter->eraseBeforePaint();
    if (this->onRender)
      this->onRender(this->renderData);
  }
  void renderDirty(const Rect &)
  {
    this->render(0);
  }
  bool handleMouseDown(const Point &) { return false; }
  bool handleKeyDown(char) { return false; }
  bool handleTabKey(ToolboxTabDirection)
  {
    return false;
  }
  bool handleScrollKey(ToolboxScrollKey)
  {
    return false;
  }
  void idleTextEdits() {}
  void onChange(loka::app::scene::Node *, loka::app::scene::NodeDirtyFlags, bool) {}
  void synchronize() {}
  bool hasPendingSync() const { return false; }
  void destroy() {}
};
#include "support/WindowAdmissionTestApp.hpp"
#include "ToolboxActivationPhase.hpp"
class ToolboxWindow : public Window
{
public:
  loka::app::DialogResultTransport results;
  virtual loka::app::DialogResultDelivery *dialogResultDelivery() { return &this->results; }
  virtual void closeDialogResults() { this->results.close(); }
  ToolboxScenePlatformController controller;
  bool hasController;
  explicit ToolboxWindow(const WindowProps &props = WindowProps(), PlatformContext *platform = 0)
      : Window(platform, props), hasController(true)
  { this->results.open(*this); this->controller.transport = &this->results; }
  virtual ~ToolboxWindow() { this->closeDialogResults(); }
  virtual ToolboxWindow *asToolboxWindow() { return this; }
  ToolboxScenePlatformController *scenePlatformController() { return hasController ? &controller : 0; }
  void flushInvalidate();
};
class ToolboxApp : public WindowAdmissionTestApp
{
public:
  CursorOwner cursorOwner_;
  CursorOwner &cursorOwner() { return this->cursorOwner_; }
  bool running_;
  ToolboxApp(Window &first, Window *second = 0) : WindowAdmissionTestApp(first, second), running_(true) {}
  void present(ActivationPhase, loka::core::Operation &);
  void addComponent(AppComponent *component) { this->group_->adopt(component); }
  void noGroupTick()
  {
    AppComponentGroup *saved = this->group_;
    this->group_ = 0;
    this->tick();
    this->group_ = saved;
  }
  void tick(ActivationPhase phase = ACTIVATION_FOREGROUND)
  { loka::core::Operation turn; this->present(phase, turn); }
};

#endif
