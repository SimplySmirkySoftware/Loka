#ifndef LOKA_MINESWEEPER_PRODUCTION_APP_CONFIG_HPP
#define LOKA_MINESWEEPER_PRODUCTION_APP_CONFIG_HPP
#include "MyAppConfig.hpp"
#include <ctime>

/** Production seed adapter. Cost: factory once per app construction; one clock read, no rows. */
class MineSweeperProductionAppConfig : public MineSweeperAppConfig
{
public:
  explicit MineSweeperProductionAppConfig(PlatformContext *context)
      : MineSweeperAppConfig(context, minesweeper::MainProps(static_cast<unsigned long>(std::time(0))))
  {
  }
};
#endif
