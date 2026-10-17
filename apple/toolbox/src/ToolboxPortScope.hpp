#ifndef LOKA_TOOLBOX_PORT_SCOPE_HPP
#define LOKA_TOOLBOX_PORT_SCOPE_HPP
#include <Quickdraw.h>
/** Select an owning window for native work, restoring the caller's port on every exit. */
class ToolboxPortScope
{
public:
  explicit ToolboxPortScope(GrafPtr port)
  {
    GetPort(&this->previous_);
    SetPort(port);
  }
  ~ToolboxPortScope()
  {
    SetPort(this->previous_);
  }

private:
  ToolboxPortScope(const ToolboxPortScope &);
  ToolboxPortScope &operator=(const ToolboxPortScope &);
  GrafPtr previous_;
};
#endif
