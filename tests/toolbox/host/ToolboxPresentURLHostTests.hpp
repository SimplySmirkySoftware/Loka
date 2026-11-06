#include "platform/null/NullPlatformContext.hpp"
#include "app/core/AppComposition.hpp"
#include "app/url/URLOpener.hpp"
#include "support/LokaAllocFailure.hpp"
namespace
{
  struct PresentURLConfig : AppConfigurable
  {
    loka::app::URLOpener<2> opener;
    explicit PresentURLConfig(PlatformContext &platform)
        : AppConfigurable(&platform)
    {
    }
    virtual void compose(AppComposition &composition)
    {
      composition << loka::app::OpenURLs(this->opener);
    }
  };
  struct URLWatch
  {
    unsigned count;
    bool active;
    loka::core::State<loka::app::URLOpenOutcome> &fact;
    explicit URLWatch(PresentURLConfig &config)
        : count(0),
          active(false),
          fact(const_cast<loka::core::State<loka::app::URLOpenOutcome> &>(config.opener.outcome()))
    {
      this->fact.bind(&changed, this, false);
    }
    ~URLWatch()
    {
      this->fact.unbind(&changed, this);
    }
    static void changed(void *data)
    {
      URLWatch &self = *static_cast<URLWatch *>(data);
      ++self.count;
      self.active = loka::core::Operation::hasActive();
    }
  };
} // namespace
static void PresentURLPin(const char *name)
{
  NullPlatformContext platform;
  PresentURLConfig config(platform);
  ToolboxWindow window;
  ToolboxApp app(window, 0, &config);
  app.flush();
  URLWatch watch(config);
  loka::app::WebURL url;
  LOKA_VERIFY(loka::app::WebURL::parse(String::Literal("https://example.com/"), url));
  LOKA_VERIFY(config.opener.post(url) == loka::app::scene::POST_ACCEPTED);
  DialogPin dialog(window.controller);
  const bool modal = !std::strcmp(name, "dialog") || !std::strcmp(name, "quit-delivery");
  if (modal)
    dialog.draw();
  if (!std::strcmp(name, "quit-delivery"))
  {
    modalApp = &app;
    dialog.storage.bind(&QuitOnDelivery, 0, false);
  }
  if (!std::strcmp(name, "quit"))
    app.running_ = false;
  const bool background = !std::strcmp(name, "background");
  app.tick(background ? ACTIVATION_BACKGROUND : ACTIVATION_FOREGROUND);
  const bool deferred = modal || background || !std::strcmp(name, "quit");
  LOKA_VERIFY(platform.openedURLs().size() == (deferred ? 0u : 1u));
  LOKA_VERIFY(config.opener.pending() == (deferred ? 1u : 0u));
  LOKA_VERIFY(watch.count == (deferred ? 0u : 1u));
  if (!deferred)
    LOKA_VERIFY(watch.active);
  if (!std::strcmp(name, "dialog") || background)
  {
    app.tick();
    LOKA_VERIFY(platform.openedURLs().size() == 1 && config.opener.pending() == 0);
    LOKA_VERIFY(watch.count == 1 && watch.active);
  }
  if (!std::strcmp(name, "quit-delivery"))
    dialog.storage.unbind(&QuitOnDelivery, 0);
}
