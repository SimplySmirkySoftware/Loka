#include "Types.h"
enum
{
  gestaltSystemVersion = 0x73797376,
  gestaltAppearanceAttr = 0x61707072,
  gestaltUndefSelectorErr = -5551
};
OSErr Gestalt(OSType selector, long *response);
