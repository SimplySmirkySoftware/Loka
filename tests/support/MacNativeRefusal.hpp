#ifndef LOKA_TESTS_SUPPORT_MAC_NATIVE_REFUSAL_HPP
#define LOKA_TESTS_SUPPORT_MAC_NATIVE_REFUSAL_HPP

#include <AppKit/AppKit.h>
#include <objc/runtime.h>

#include "support/TestVerify.hpp"

/** Test-only scoped native refusal, installed on the measured cell class. The
    inherited implementation is first added to that class, so the replacement
    reaches only it and its subclasses, and destruction restores it. */
class MacNativeRefusal
{
public:
  MacNativeRefusal(SEL selector, IMP replacement, Class cell = [NSTextFieldCell class])
  {
    Method inherited = class_getInstanceMethod(cell, selector);
    LOKA_VERIFY(inherited != 0);
    class_addMethod(cell, selector, method_getImplementation(inherited), method_getTypeEncoding(inherited));
    this->method_ = class_getInstanceMethod(cell, selector);
    this->original_ = method_setImplementation(this->method_, replacement);
  }
  ~MacNativeRefusal()
  {
    method_setImplementation(this->method_, this->original_);
  }

private:
  Method method_;
  IMP original_;
  MacNativeRefusal(const MacNativeRefusal &);
  MacNativeRefusal &operator=(const MacNativeRefusal &);
};

#endif
