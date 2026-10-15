#include "Win32RetirableContext.hpp"

#include <cassert>
#include "../Win32ScenePlatformController.hpp"

Win32RetirableContext::Win32RetirableContext(Win32ScenePlatformController *controller)
    : loka::app::scene::NativeNodeContext(),
      controller_(controller),
      hwnd_(0)
{
}

Win32RetirableContext::~Win32RetirableContext() {}

void Win32RetirableContext::positionNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry)
{
  assert(this->controller_ && "a live HWND must have a native layout owner");
  if (this->controller_)
  {
    this->controller_->positionNativeWindow(hwnd, geometry);
  }
}

void Win32RetirableContext::resizeNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry)
{
  assert(this->controller_ && "a live HWND must have a native layout owner");
  if (this->controller_)
    this->controller_->resizeNativeWindow(hwnd, geometry);
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
  assert(this->controller_ && "a native child must have a projection owner");
  this->hwnd_ = this->controller_
                    ? this->controller_->createNativeChildWindow(
                          exStyle, className, windowName, style, geometry, parent, menu, instance, createParameter)
                    : 0;
  return this->hwnd_;
}

void Win32RetirableContext::retireWindow()
{
  if (!this->hwnd_)
  {
    return;
  }
  assert(this->controller_ && "a live HWND must have a controller retirement owner");
  if (!this->controller_)
  {
    return;
  }
  SetWindowLongPtr(this->hwnd_, GWLP_USERDATA, 0);
  this->controller_->queueNativeRetirement(this->hwnd_);
  this->hwnd_ = NULL;
}

void Win32RetirableContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                          loka::app::scene::NodeLifecycleFact next)
{
  this->onNativeFact(previous, next);
  if (next == loka::app::scene::NODE_FACT_RETIRED)
    this->retireWindow();
}

void Win32RetirableContext::onNativeFact(loka::app::scene::NodeLifecycleFact,
                                        loka::app::scene::NodeLifecycleFact)
{
}
