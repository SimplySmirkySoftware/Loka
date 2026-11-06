#include "ToolboxOpenURL.hpp"
namespace
{
  bool unavailable()
  {
    return false;
  }
} // namespace
// Unrelated host platform tests have no Component Manager. The real seam exits
// before any remaining entry; production always supplies the complete table.
const ToolboxInternetConfigCalls &ToolboxInternetConfigNativeCalls()
{
  static const ToolboxInternetConfigCalls calls = {unavailable, 0, 0, 0, 0, 0, 0, 0, 0};
  return calls;
}
