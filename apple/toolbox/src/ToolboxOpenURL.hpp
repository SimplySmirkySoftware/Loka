#ifndef LOKA_TOOLBOX_OPEN_URL_HPP
#define LOKA_TOOLBOX_OPEN_URL_HPP
#include "app/url/WebURL.hpp"
#include "app/url/URLOpenKind.hpp"
#include <Components.h>

/** Synchronous borrowed native calls; the session retains no table beyond the call. */
struct ToolboxInternetConfigCalls
{
  bool (*hasComponentManager)();
  long (*countLaunchers)();
  ComponentInstance (*openLauncher)();
  OSType (*currentCreator)();
  OSStatus (*start)(ComponentInstance, OSType);
  OSStatus (*findConfigFile)(ComponentInstance);
  OSStatus (*launch)(ComponentInstance, ConstStr255Param, Ptr, long, long *, long *);
  OSStatus (*stop)(ComponentInstance);
  OSErr (*close)(ComponentInstance);
};
/** Owns one call-local Internet Config session. OPENED means IC accepted the launch. */
loka::app::URLOpenKind ToolboxOpenWebURL(const loka::app::WebURL &url, const ToolboxInternetConfigCalls &calls);
/** Production Component Manager and IC dispatch table. */
const ToolboxInternetConfigCalls &ToolboxInternetConfigNativeCalls();
#endif
