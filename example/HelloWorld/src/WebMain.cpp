#include "WebAppEntry.hpp"
#include "ProductionAppConfig.hpp"
AppConfigurable *loka::web::CreateAppConfig(PlatformContext *platform)
{
  return new HelloWorldProductionAppConfig(platform);
}
