#ifndef LOKA_FLOPPY_BIRD_APP_CONFIG_HPP
#define LOKA_FLOPPY_BIRD_APP_CONFIG_HPP

#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/Menu.hpp"
#include "app/RectSurface.hpp"
#include "app/core/WindowDefinition.hpp"
#include "GameModel.hpp"
#include "MainNode.hpp"

class FloppyBirdAppConfig : public AppConfigurable
{
public:
  explicit FloppyBirdAppConfig(PlatformContext *ctx, unsigned long gameSeed = 1UL)
      : AppConfigurable(ctx),
        game_(gameSeed)
  {
  }

  virtual void compose(AppComposition &c)
  {
    c << WindowDef(this->productionWindowProps(
                       loka::app::scene::Boundary<floppybird::MainNode>(
                           floppybird::MainProps(&this->game_)))
                       .idlePolicy(loka::app::IdlePolicy::interval(loka_floppy_bird::kFixedStepSeconds))
                       .onIdle(&FloppyBirdAppConfig::WindowIdleThunk, this)
                       .onKeyPress(&FloppyBirdAppConfig::WindowKeyPressThunk, this));
  }

  virtual void composeDefaultMenu(loka::app::MenuComposition &c)
  {
    using namespace loka::app;
    c.declare(StandardMenuPresets());
  }

  void handleWindowIdle(Window *window, double elapsedSeconds)
  {
    (void)window;
    this->game_.advanceFrame(elapsedSeconds);
  }

  bool handleWindowKeyPress(Window *window, char key)
  {
    (void)window;
    if (key != ' ')
    {
      return false;
    }
    this->game_.flap();
    return true;
  }

protected:
  /** Declares FloppyBird's production window presentation around a supplied
      scene so non-production vehicles cannot drift its title or frame. */
  WindowProps productionWindowProps(const loka::app::scene::NodeDefinitionBase &scene) const
  {
    return WindowProps()
        .frame(50, 50, 380, 340)
        .scene(scene)
        .title("LokaFloppyBird")
        .visible(true);
  }

  floppybird::GameModel &gameModel()
  {
    return this->game_;
  }

private:
  static void WindowIdleThunk(Window *window, double elapsedSeconds, void *userData)
  {
    FloppyBirdAppConfig *self = static_cast<FloppyBirdAppConfig *>(userData);
    if (self)
    {
      self->handleWindowIdle(window, elapsedSeconds);
    }
  }

  static bool WindowKeyPressThunk(Window *window, char key, void *userData)
  {
    FloppyBirdAppConfig *self = static_cast<FloppyBirdAppConfig *>(userData);
    return self ? self->handleWindowKeyPress(window, key) : false;
  }

  floppybird::GameModel game_;
};

#endif
