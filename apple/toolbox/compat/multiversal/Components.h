#ifndef LOKA_TOOLBOX_MULTIVERSAL_COMPONENTS_H
#define LOKA_TOOLBOX_MULTIVERSAL_COMPONENTS_H
#include <Multiverse.h>

// Universal Component Manager declarations absent from Multiversal.
typedef struct ComponentDescription
{
  OSType componentType;
  OSType componentSubType;
  OSType componentManufacturer;
  unsigned long componentFlags;
  unsigned long componentFlagsMask;
} ComponentDescription;
typedef long ComponentResult;
// Multiversal also omits the Universal 32-bit status spelling.
typedef long OSStatus;
extern "C"
{
  pascal long CountComponents(ComponentDescription *) M68K_INLINE(0x7003, 0xA82A);
  pascal ComponentInstance OpenDefaultComponent(OSType, OSType) M68K_INLINE(0x7021, 0xA82A);
  pascal OSErr CloseComponent(ComponentInstance) M68K_INLINE(0x7008, 0xA82A);
#if TARGET_CPU_PPC
  extern UniversalProcPtr CallComponentUPP;
#endif
}
#endif
