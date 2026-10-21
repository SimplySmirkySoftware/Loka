#include "ToolboxHost.hpp"
void updateEntry(ToolboxWindow &window)
{
  window.drawForUpdate();
  window.drawForFlush();
}
