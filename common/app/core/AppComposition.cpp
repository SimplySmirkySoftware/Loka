#include "app/core/AppComposition.hpp"

AppComposition::AppComposition(PlatformContext *context)
    : components_(),
      windowList_(),
      context_(context),
      windowSeat_(0),
      urlReceiveSeat_(0),
      urlOpener_(0)
{
  assert(context_ && "AppComposition: PlatformContext* must not be null");
}

AppComposition::~AppComposition()
{
  windowList_.clear();
  delete this->windowSeat_;
  // Once per composition: destroy only an untaken Match copy and its child chains.
  delete this->urlReceiveSeat_;
}

AppComposition &AppComposition::declare(const WindowDefinitionBase &def)
{
  windowList_.appendClone(def);
  return *this;
}

AppComposition &AppComposition::declare(const WindowDefinitionBase *def)
{
  if (!def)
  {
    return *this;
  }
  return declare(*def);
}

AppComposition &AppComposition::operator<<(const WindowDefinitionBase &def)
{
  return declare(def);
}

AppComposition &AppComposition::operator<<(const WindowDefinitionBase *def)
{
  return declare(def);
}

AppComposition &AppComposition::operator<<(const loka::app::DocumentWindowSeatDefinitionBase &def)
{
  assert(!this->windowSeat_ && "one document window seat per composition");
  if (!this->windowSeat_)
    this->windowSeat_ = def.createSeat();
  return *this;
}

loka::app::WindowSeat *AppComposition::takeWindowSeat()
{
  loka::app::WindowSeat *seat = this->windowSeat_;
  this->windowSeat_ = 0;
  return seat;
}

std::vector<AppComponent *> AppComposition::build()
{
  assert(context_ && "AppComposition::build requires PlatformContext");
  std::vector<AppComponent *> result = components_;
  if (windowList_.count() == 0)
  {
    return result;
  }
  loka::dsl::CompositionCursor<WindowDefinitionBase> it(windowList_.head(), windowList_.count());
  for (WindowDefinitionBase *def = it.next(); def; def = it.next())
  {
    class Window *window = def->create(context_);
    if (window)
    {
      result.push_back(window);
    }
  }
  windowList_.clear();
  return result;
}

void AppComposition::setContext(PlatformContext *ctx)
{
  context_ = ctx;
}

AppComposition &AppComposition::operator<<(const loka::app::OpenURLs &urls)
{
  assert(!this->urlOpener_ && "one URL opener per composition");
  if (!this->urlOpener_)
    this->urlOpener_ = &urls.opener();
  return *this;
}

loka::app::URLOpenerBase *AppComposition::takeURLOpener()
{
  loka::app::URLOpenerBase *opener = this->urlOpener_;
  this->urlOpener_ = 0;
  return opener;
}

AppComposition &AppComposition::operator<<(const loka::app::URLReceiveSeatDefinitionBase &def)
{
  assert(!this->urlReceiveSeat_ && "one URL receive seat per composition");
  if (!this->urlReceiveSeat_)
    this->urlReceiveSeat_ = def.createSeat();
  return *this;
}

loka::app::URLReceiveSeatBase *AppComposition::takeURLReceiveSeat()
{
  loka::app::URLReceiveSeatBase *seat = this->urlReceiveSeat_;
  this->urlReceiveSeat_ = 0;
  return seat;
}
