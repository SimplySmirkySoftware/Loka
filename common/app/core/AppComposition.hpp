#ifndef LOKA_APP_COMPOSITION_HPP
#define LOKA_APP_COMPOSITION_HPP

#include <vector>
#include "app/url/OpenURLs.hpp"
#include <cassert>
#include "app/core/AppComponent.hpp"
#include "app/core/WindowDefinition.hpp"
#include "app/core/WindowSeat.hpp"
#include "dsl/composition/CompositionList.hpp"

class AppComposition
{
public:
  explicit AppComposition(PlatformContext *context);
  ~AppComposition();

  AppComposition &declare(const WindowDefinitionBase &def);
  AppComposition &declare(const WindowDefinitionBase *def);

  AppComposition &operator<<(const WindowDefinitionBase &def);
  AppComposition &operator<<(const WindowDefinitionBase *def);

  AppComposition &operator<<(const loka::app::DocumentWindowSeatDefinitionBase &def);
  /** Transfers the declared seat to App; null means absent or allocation refused. */
  loka::app::WindowSeat *takeWindowSeat();

  /** Borrows once per composition; duplicate registration asserts and refuses. */
  AppComposition &operator<<(const loka::app::OpenURLs &urls);
  /** Hands the borrow to App once; never deletes the config-owned opener. */
  loka::app::URLOpenerBase *takeURLOpener();

  std::vector<AppComponent *> build();

  PlatformContext *context() const
  {
    return context_;
  }
  void setContext(PlatformContext *ctx);

private:
  AppComposition &add(AppComponent *comp);
  std::vector<AppComponent *> components_;
  loka::dsl::CompositionList<WindowDefinitionBase> windowList_;
  PlatformContext *context_;
  loka::app::WindowSeat *windowSeat_;
  loka::app::URLOpenerBase *urlOpener_;
  AppComposition(const AppComposition &);
  AppComposition &operator=(const AppComposition &);
};

typedef AppComposition AppBuilder;

#endif // LOKA_APP_COMPOSITION_HPP
