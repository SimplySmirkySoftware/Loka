#include "Win32OwnedChildWindow.hpp"

#include <cassert>
#include "../Win32ScenePlatformController.hpp"

Win32OwnedChildWindow::Win32OwnedChildWindow(Win32ScenePlatformController *controller)
    : controller_(controller),
      hwnd_(0)
{
}

Win32OwnedChildWindow::~Win32OwnedChildWindow()
{
  assert(!this->hwnd_ && "native child must be queued before owner reclaim");
}

void Win32OwnedChildWindow::positionNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry)
{
  assert(this->controller_ && "a live HWND must have a native layout owner");
  if (this->controller_)
  {
    this->controller_->positionNativeWindow(hwnd, geometry);
  }
}

void Win32OwnedChildWindow::resizeNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry)
{
  assert(this->controller_ && "a live HWND must have a native layout owner");
  if (this->controller_)
    this->controller_->resizeNativeWindow(hwnd, geometry);
}

HWND Win32OwnedChildWindow::createNativeChildWindow(DWORD exStyle,
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

void Win32OwnedChildWindow::retireWindow()
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

void Win32OwnedChildWindow::raise()
{
  if (this->hwnd_)
    this->controller_->raiseNativeWindow(this->hwnd_);
}
