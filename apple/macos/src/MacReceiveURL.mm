#include "MacReceiveURL.hpp"
#include <Foundation/Foundation.h>
#include <ApplicationServices/ApplicationServices.h>

bool MacURLTextFromDescriptor(NSAppleEventDescriptor *descriptor, loka::core::String &out)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSAppleEventDescriptor *coerced = [descriptor coerceToDescriptorType:typeUTF8Text];
  if (!coerced)
    coerced = [descriptor coerceToDescriptorType:typeChar];
  if (coerced)
  {
    NSData *data = [coerced data];
    out = loka::core::String::Utf8(static_cast<const char *>([data bytes]), [data length]);
  }
  const bool converted = coerced != nil;
  [pool drain];
  return converted;
}
