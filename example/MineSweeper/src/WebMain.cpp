#include "WebAppEntry.hpp"
#include "ProductionAppConfig.hpp"
/** Cost: Web bootstrap once; constructs one app-owned production config. */
AppConfigurable *loka::web::CreateAppConfig(PlatformContext *platform)
{
  return new MineSweeperProductionAppConfig(platform);
}
