#ifndef LOKA_TOOLBOX_DEVICE_DEPTH_HPP
#define LOKA_TOOLBOX_DEVICE_DEPTH_HPP

#include <Quickdraw.h>

namespace loka
{
  namespace toolbox
  {
    /** Reads a selected Color QuickDraw device's depth; leaves out untouched
        when its pixel map is absent. Device selection belongs to the caller. */
    inline bool queryDeviceDepth(GDHandle device, int &out)
    {
      if (!device || !(*device)->gdPMap || !*(*device)->gdPMap)
      {
        return false;
      }
      out = (*(*device)->gdPMap)->pixelSize;
      return true;
    }
  } // namespace toolbox
} // namespace loka

#endif // LOKA_TOOLBOX_DEVICE_DEPTH_HPP
