#include "WebAppEntry.hpp"
#include "MyAppConfig.hpp"
/** Cost: Web bootstrap once; constructs one app-owned config. */
AppConfigurable *loka::web::CreateAppConfig(PlatformContext *platform)
{
  return new TutorialAppConfig(platform);
}
