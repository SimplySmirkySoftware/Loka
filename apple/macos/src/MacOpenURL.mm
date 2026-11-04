#include "MacOpenURL.hpp"
#include <Foundation/Foundation.h>

loka::app::URLOpenKind MacOpenWebURL(const loka::app::WebURL &url, BOOL (*open)(NSURL *url))
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  const loka::core::StringBuffer bytes = url.text().bufferWithEncoding(loka::core::StringEncodingUtf8);
  NSString *text = [[[NSString alloc] initWithBytes:bytes.data() length:bytes.length()
                                           encoding:NSUTF8StringEncoding] autorelease];
  loka::app::URLOpenKind result = loka::app::URL_NOT_OPENED;
  if (text)
  {
    NSURL *nativeURL = [NSURL URLWithString:text];
    if (nativeURL)
      result = open(nativeURL) == YES ? loka::app::URL_OPENED : loka::app::URL_NOT_OPENED;
  }
  [pool drain];
  return result;
}
