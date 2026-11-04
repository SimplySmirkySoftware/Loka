#include "Win32PlatformContext.hpp"
#include "app/url/WebURL.hpp"
#include <cstdio>

// Manual interactive-desktop probe; never part of the unit-test registry.
int main()
{
  Win32PlatformContext context;
  loka::app::WebURL url;
  if (!loka::app::WebURL::parse(loka::core::String("https://example.com/"), url))
    return 2;
  const loka::app::URLOpenKind kind = context.openURL(url);
  std::printf("openURL kind=%d (URL_OPENED=%d)\n", static_cast<int>(kind), static_cast<int>(loka::app::URL_OPENED));
  return kind == loka::app::URL_OPENED ? 0 : 1;
}
