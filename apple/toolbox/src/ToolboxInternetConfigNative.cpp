#include "ToolboxOpenURL.hpp"
#include "ToolboxInternetConfigDispatch.hpp"
#include <Gestalt.h>
#include <Processes.h>
#include <cstring>

#if TARGET_CPU_68K
static pascal OSStatus LokaICStart(ComponentInstance, OSType) = {LOKA_IC_WORDS(LOKA_IC_START_DESCRIPTOR)};
static pascal OSStatus LokaICFind(ComponentInstance, short, void *) = { LOKA_IC_WORDS(LOKA_IC_FIND_DESCRIPTOR) };
static pascal OSStatus LokaICLaunch(ComponentInstance, ConstStr255Param, Ptr, long, long *, long *) = {
  LOKA_IC_WORDS(LOKA_IC_LAUNCH_DESCRIPTOR)
};
static pascal OSStatus LokaICStop(ComponentInstance) = {LOKA_IC_WORDS(LOKA_IC_STOP_DESCRIPTOR)};
#else
// The target block layout is the 68K stack image: descriptor, parameters, instance.
typedef char LokaICStartParamsSize[sizeof(ToolboxICStartParams) == 12 ? 1 : -1];
typedef char LokaICFindParamsSize[sizeof(ToolboxICFindParams) == 14 ? 1 : -1];
typedef char LokaICLaunchParamsSize[sizeof(ToolboxICLaunchParams) == 28 ? 1 : -1];
typedef char LokaICStopParamsSize[sizeof(ToolboxICStopParams) == 8 ? 1 : -1];
#endif
namespace
{
  bool HasComponentManager()
  {
    long result;
    return Gestalt(gestaltComponentMgr, &result) == noErr;
  }
  long CountLaunchers()
  {
    ComponentDescription description = {'PREF', 'ICAp', 0, 0, 0};
    return CountComponents(&description);
  }
  ComponentInstance OpenLauncher()
  {
    return OpenDefaultComponent('PREF', 'ICAp');
  }
  OSType CurrentCreator()
  {
    ProcessSerialNumber psn;
    if (GetCurrentProcess(&psn) != noErr)
      return '????';
    ProcessInfoRec info;
    std::memset(&info, 0, sizeof(info));
    info.processInfoLength = sizeof(info);
    if (GetProcessInformation(&psn, &info) != noErr)
      return '????';
    return info.processSignature;
  }
  OSStatus Start(ComponentInstance inst, OSType creator)
  {
#if TARGET_CPU_68K
    return LokaICStart(inst, creator);
#else
    return ToolboxICDispatchStart(&CallUniversalProc, CallComponentUPP, inst, creator);
#endif
  }
  OSStatus Find(ComponentInstance inst)
  {
#if TARGET_CPU_68K
    return LokaICFind(inst, 0, 0);
#else
    return ToolboxICDispatchFind(&CallUniversalProc, CallComponentUPP, inst);
#endif
  }
  OSStatus Launch(ComponentInstance inst, ConstStr255Param hint, Ptr data, long length, long *start, long *end)
  {
#if TARGET_CPU_68K
    return LokaICLaunch(inst, hint, data, length, start, end);
#else
    return ToolboxICDispatchLaunch(&CallUniversalProc, CallComponentUPP, inst, hint, data, length, start, end);
#endif
  }
  OSStatus Stop(ComponentInstance inst)
  {
#if TARGET_CPU_68K
    return LokaICStop(inst);
#else
    return ToolboxICDispatchStop(&CallUniversalProc, CallComponentUPP, inst);
#endif
  }
  OSErr CloseLauncher(ComponentInstance inst)
  {
    return CloseComponent(inst);
  }
} // namespace
const ToolboxInternetConfigCalls &ToolboxInternetConfigNativeCalls()
{
  static const ToolboxInternetConfigCalls calls = {&HasComponentManager,
                                                   &CountLaunchers,
                                                   &OpenLauncher,
                                                   &CurrentCreator,
                                                   &Start,
                                                   &Find,
                                                   &Launch,
                                                   &Stop,
                                                   &CloseLauncher};
  return calls;
}
