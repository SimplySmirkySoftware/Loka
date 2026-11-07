#ifndef LOKA_TOOLBOX_INTERNET_CONFIG_DISPATCH_HPP
#define LOKA_TOOLBOX_INTERNET_CONFIG_DISPATCH_HPP
#include <Components.h>
#include <MixedMode.h>

// One descriptor home for both CPUs. Parameter bytes exclude the instance.
#define LOKA_IC_START_DESCRIPTOR 0x00040000UL
#define LOKA_IC_FIND_DESCRIPTOR 0x00060002UL
#define LOKA_IC_LAUNCH_DESCRIPTOR 0x00140011UL
#define LOKA_IC_STOP_DESCRIPTOR 0x00000001UL
#define LOKA_IC_WORDS(descriptor) 0x2F3C, ((descriptor) >> 16), ((descriptor) & 0xFFFF), 0x7000, 0xA82A

// CFM has no _ComponentDispatch inline. CallComponentUPP takes one pointer to
// the parameter block the 68K inline builds on the stack: descriptor first,
// then the parameters in reverse declaration order (Pascal pushes left to
// right), the instance last, with 68K (two-byte) packing. Measured on Mac OS
// 9.2.2: passing the parameters themselves returns badComponentInstance.
#define LOKA_IC_CALL_COMPONENT_PROC_INFO 0x000000F0UL // Pascal, long result, one pointer
#pragma pack(push, 2)
struct ToolboxICStartParams
{
  unsigned char flags;
  unsigned char paramSize;
  short what;
  OSType creator;
  ComponentInstance instance;
};
struct ToolboxICFindParams
{
  unsigned char flags;
  unsigned char paramSize;
  short what;
  void *folders;
  short count;
  ComponentInstance instance;
};
struct ToolboxICLaunchParams
{
  unsigned char flags;
  unsigned char paramSize;
  short what;
  long *selEnd;
  long *selStart;
  long length;
  Ptr data;
  ConstStr255Param hint;
  ComponentInstance instance;
};
struct ToolboxICStopParams
{
  unsigned char flags;
  unsigned char paramSize;
  short what;
  ComponentInstance instance;
};
#pragma pack(pop)
#define LOKA_IC_HEADER(descriptor)                                                                                     \
  0, static_cast<unsigned char>((descriptor) >> 16), static_cast<short>((descriptor) & 0xFFFF)

typedef long (*ToolboxCallUniversalProc)(UniversalProcPtr, ProcInfoType, ...);
inline OSStatus
ToolboxICDispatchStart(ToolboxCallUniversalProc call, UniversalProcPtr upp, ComponentInstance inst, OSType creator)
{
  ToolboxICStartParams params = {LOKA_IC_HEADER(LOKA_IC_START_DESCRIPTOR), creator, inst};
  return call(upp, LOKA_IC_CALL_COMPONENT_PROC_INFO, &params);
}
inline OSStatus ToolboxICDispatchFind(ToolboxCallUniversalProc call, UniversalProcPtr upp, ComponentInstance inst)
{
  ToolboxICFindParams params = {LOKA_IC_HEADER(LOKA_IC_FIND_DESCRIPTOR), 0, 0, inst};
  return call(upp, LOKA_IC_CALL_COMPONENT_PROC_INFO, &params);
}
inline OSStatus ToolboxICDispatchLaunch(ToolboxCallUniversalProc call,
                                        UniversalProcPtr upp,
                                        ComponentInstance inst,
                                        ConstStr255Param hint,
                                        Ptr data,
                                        long length,
                                        long *start,
                                        long *end)
{
  ToolboxICLaunchParams params = {LOKA_IC_HEADER(LOKA_IC_LAUNCH_DESCRIPTOR), end, start, length, data, hint, inst};
  return call(upp, LOKA_IC_CALL_COMPONENT_PROC_INFO, &params);
}
inline OSStatus ToolboxICDispatchStop(ToolboxCallUniversalProc call, UniversalProcPtr upp, ComponentInstance inst)
{
  ToolboxICStopParams params = {LOKA_IC_HEADER(LOKA_IC_STOP_DESCRIPTOR), inst};
  return call(upp, LOKA_IC_CALL_COMPONENT_PROC_INFO, &params);
}
#endif
