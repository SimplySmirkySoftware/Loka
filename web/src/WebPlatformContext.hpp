#ifndef LOKA_WEB_PLATFORM_CONTEXT_HPP
#define LOKA_WEB_PLATFORM_CONTEXT_HPP
#include "app/PlatformContext.hpp"
#include "app/core/App.hpp"
namespace loka
{
  namespace web
  {
    namespace testing
    {
      struct Access;
    }
  } // namespace web
} // namespace loka
class WebSession;
/** Web-local bootstrap adapter. Cost: once per requested owner, no row scans. */
class WebPlatformContext : public PlatformContext
{
public:
  explicit WebPlatformContext(WebSession &session)
      : session_(session)
  {
  }
  virtual App *createApp(AppConfigurable *, NativeModuleHandle, int) const;
  virtual Window *createWindow(const WindowProps &);
  virtual loka::app::scene::NodeContext *createNodeContext(loka::app::scene::Node *) const;
  virtual bool openFile(const loka::file::File &, loka::platform::file::FileHandle &) const
  {
    return false;
  }
  virtual bool
  createImageFromBlob(const loka::core::resource::Blob &, std::size_t, std::size_t, loka::core::resource::Image &)
  {
    return false;
  }

private:
  friend struct loka::web::testing::Access;
  WebSession &session_;
};
/** App rails keep the protected admission doors behind their concrete App.
    Cost: each tail calls seven doors, each traversing only its own App rows. */
class WebApp : public App
{
public:
  WebApp(AppConfigurable *config, WebSession &session)
      : App(config),
        session_(session)
  {
  }
  virtual ~WebApp()
  {
    this->retireComponents();
  }
  virtual void quit();
  void tail(loka::core::Operation &turn, CompletionEntry entry);
  /** Cost: input validation up to twice per click, once per resize; App walks its pending-close rows. */
  bool closing(Window *window) const
  {
    return this->isWindowClosePending(window);
  }
  /** Cost: continuation/schedule once per tail; App walks its component rows. */
  bool pending() const
  {
    return this->hasPendingWindowAdmission();
  }
  /** Cost: bootstrap once; reads one App-owned result. */
  bool windowless() const
  {
    return this->windowlessAfterLaunch();
  }

private:
  WebSession &session_;
};
#endif
