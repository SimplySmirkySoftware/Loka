#ifndef LOKA_HOST_DEVICES_H
#define LOKA_HOST_DEVICES_H
#include "Controls.h"
namespace toolbox_host
{
  extern std::vector<std::string> deskAccessories;
}
inline void OpenDeskAcc(const unsigned char *name) { toolbox_host::deskAccessories.push_back(HostPascalBytes(name)); }
#endif
