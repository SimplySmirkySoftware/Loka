#ifndef LOKA_TOOLBOX_UNICODE_HOST_HPP
#define LOKA_TOOLBOX_UNICODE_HOST_HPP
#define LOKA_TOOLBOX_WINDOW_HPP
#include <Windows.h>
/** Window neighbor only; the production ledger TU is compiled unchanged. */
class ToolboxWindow
{
public:
  ToolboxWindow()
      : port_()
  {
  }
  WindowPtr window()
  {
    return &this->port_;
  }

private:
  GrafPort port_;
};
#endif
