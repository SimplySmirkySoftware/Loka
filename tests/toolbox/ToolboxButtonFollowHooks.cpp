#include "ToolboxButtonFollowAccess.hpp"
#include <cassert>
namespace
{
  enum Capability
  {
    UNKNOWN,
    CLOSED,
    OPEN
  };
  Capability capability = UNKNOWN;
  loka::testing::SilentButtonMoveObserver *observer = 0;
} // namespace
namespace loka
{
  namespace testing
  {
    bool CachedSilentButtonMove(bool (*query)())
    {
      if (capability == UNKNOWN)
        capability = query() ? OPEN : CLOSED;
      return capability == OPEN;
    }
    void ResetSilentButtonMoveCapability()
    {
      capability = UNKNOWN;
    }
    SilentButtonMoveScope::SilentButtonMoveScope(SilentButtonMoveObserver &value)
    {
      assert(!observer);
      observer = &value;
    }
    SilentButtonMoveScope::~SilentButtonMoveScope()
    {
      observer = 0;
    }
    void BeforeSilentButtonMove(ControlRef control)
    {
      if (observer)
        observer->before(control);
    }
    void AfterSilentButtonMove(ControlRef control)
    {
      if (observer)
        observer->after(control);
    }
  } // namespace testing
} // namespace loka
