#include "ToolboxScenarioAppConfig.hpp"
#if !defined(LOKA_RETRO68) || !defined(TEST_BUILD)
#error SmirkyCard scenario vehicle requires a Classic test build
#endif
int main()
{
  return RunSmirkyCardScenarioApplication();
}
