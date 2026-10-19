#ifndef LOKA_TEST_IMAGE_GESTALT_H
#define LOKA_TEST_IMAGE_GESTALT_H

#include "Files.h"

enum
{
  gestaltQuickdrawVersion = 0x71642020,
  gestalt8BitQD = 0x100
};
inline short Gestalt(unsigned long, long *version)
{
  *version = gestalt8BitQD;
  return noErr;
}

#endif
