#include "Win32RetirableContext.hpp"

Win32RetirableContext::Win32RetirableContext(Win32ScenePlatformController *controller)
    : loka::app::scene::NativeNodeContext(),
      window_(controller)
{
}
Win32RetirableContext::~Win32RetirableContext() {}

void Win32RetirableContext::positionNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry)
{
  this->window_.positionNativeWindow(hwnd, geometry);
}
void Win32RetirableContext::resizeNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry)
{
  this->window_.resizeNativeWindow(hwnd, geometry);
}
HWND Win32RetirableContext::createNativeChildWindow(DWORD exStyle,
                                                    LPCWSTR className,
                                                    LPCWSTR windowName,
                                                    DWORD style,
                                                    const loka::win32::NativeRect &geometry,
                                                    HWND parent,
                                                    HMENU menu,
                                                    HINSTANCE instance,
                                                    void *createParameter)
{
  return this->window_.createNativeChildWindow(
      exStyle, className, windowName, style, geometry, parent, menu, instance, createParameter);
}

HWND Win32RetirableContext::createSubordinateChildWindow(DWORD exStyle,
                                                         LPCWSTR className,
                                                         LPCWSTR windowName,
                                                         DWORD style,
                                                         const loka::win32::NativeRect &geometry,
                                                         HWND parent,
                                                         HMENU menu,
                                                         HINSTANCE instance,
                                                         void *createParameter)
{
  return this->window_.createSubordinateChildWindow(
      exStyle, className, windowName, style, geometry, parent, menu, instance, createParameter);
}
void Win32RetirableContext::retireWindow()
{
  this->window_.retireWindow();
}

void Win32RetirableContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                          loka::app::scene::NodeLifecycleFact next)
{
  this->onNativeFact(previous, next);
  if (next == loka::app::scene::NODE_FACT_RETIRED)
    this->retireWindow();
}
void Win32RetirableContext::onNativeFact(loka::app::scene::NodeLifecycleFact, loka::app::scene::NodeLifecycleFact) {}
