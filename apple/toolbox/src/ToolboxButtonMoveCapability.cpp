// Included by ToolboxPlatformContext.cpp and the host platform fixture.
#include "ToolboxButtonMoveCapability.hpp"
#include <Gestalt.h>
#ifdef TEST_BUILD
#include "../../../tests/toolbox/ToolboxButtonFollowAccess.hpp"
#endif
namespace
{
  // 'appr'. Spelled here because the Multiversal Interfaces do not declare gestaltAppearanceAttr.
  const OSType kAppearanceSelector = 0x61707072;
  bool QuerySilentButtonMove()
  {
    long version = 0;
    if (Gestalt(gestaltSystemVersion, &version) != noErr || version != 0x0710)
      return false;
    long attributes = 0;
    return Gestalt(kAppearanceSelector, &attributes) == gestaltUndefSelectorErr;
  }
} // namespace
bool ToolboxCanSilentlyMoveButton()
{
#ifdef TEST_BUILD
  return loka::testing::CachedSilentButtonMove(QuerySilentButtonMove);
#else
  static const bool allowed = QuerySilentButtonMove();
  return allowed;
#endif
}
