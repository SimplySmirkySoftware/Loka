#ifndef LOKA_TEST_IMAGE_QUICKDRAW_H
#define LOKA_TEST_IMAGE_QUICKDRAW_H

#include "../Quickdraw.h"

// Display queries are unrelated to image decoding; no device is installed.
struct PixMap
{
  short pixelSize;
};
typedef PixMap **PixMapHandle;
struct GDevice
{
  PixMapHandle gdPMap;
};
typedef GDevice **GDHandle;
inline GDHandle GetMainDevice()
{
  return 0;
}

#endif
