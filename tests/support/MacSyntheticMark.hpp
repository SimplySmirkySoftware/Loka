#ifndef LOKA_TESTS_MAC_SYNTHETIC_MARK_HPP
#define LOKA_TESTS_MAC_SYNTHETIC_MARK_HPP
#import <objc/runtime.h>
#include "MacObjCCompat.hpp"
#include "support/TestVerify.hpp"

namespace loka
{
  namespace testing
  {
    /** Send the native marking selector through ordinary view dispatch. */
    inline void MacSendMark(NSTextView *view, bool replacement = true)
    {
      if (replacement)
        [view setMarkedText:@"k" selectedRange:NSMakeRange(1, 0) replacementRange:NSMakeRange(NSNotFound, 0)];
      else
      {
        // The 10.0 selector is the one under test; it is deprecated, not removed.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [view setMarkedText:@"k" selectedRange:NSMakeRange(1, 0)];
#pragma clang diagnostic pop
      }
    }
  } // namespace testing
} // namespace loka
#endif
