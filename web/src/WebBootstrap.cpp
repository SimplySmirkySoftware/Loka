#include "app/bootstrap/PlatformBootstrap.hpp"
namespace loka
{
  namespace platform
  {
    /** Web has no process runtime initialization. Cost: once per start, O(1), no rows.
        WebSession supplies the nonblocking owner chain instead of RunApp. */
    void InitPlatformRuntime() {}
  } // namespace platform
} // namespace loka
