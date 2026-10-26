#include "Types.h"
enum
{
  gestaltSystemVersion = 0x73797376
};
OSErr Gestalt(OSType selector, long *response);
