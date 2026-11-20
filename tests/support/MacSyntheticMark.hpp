#ifndef LOKA_TESTS_MAC_SYNTHETIC_MARK_HPP
#define LOKA_TESTS_MAC_SYNTHETIC_MARK_HPP
#import <objc/runtime.h>
#include "MacObjCCompat.hpp"
#include "support/TestVerify.hpp"

namespace loka
{
  namespace testing
  {
    /** Exercises settlement independently of the document view's marking refusal. */
    inline void MacSyntheticMark(NSTextView *view)
    {
      SEL selector = @selector(setMarkedText:selectedRange:replacementRange:);
      Method method = class_getInstanceMethod([NSTextView class], selector);
      LOKA_VERIFY(method);
      typedef void (*Mark)(id, SEL, id, NSRange, NSRange);
      Mark mark = reinterpret_cast<Mark>(method_getImplementation(method));
      mark(view, selector, @"k", NSMakeRange(1, 0), NSMakeRange(NSNotFound, 0));
      LOKA_VERIFY([view hasMarkedText]);
    }
    inline void MacVerifyMarkRefused(NSTextView *view, bool replacement)
    {
      NSString *before = [[view string] copy];
      NSRange selection = [view selectedRange];
      SEL selector = replacement ? @selector(setMarkedText:selectedRange:replacementRange:)
                                 : @selector(setMarkedText:selectedRange:);
      // Check each override independently: inherited AppKit forwarding could reach its sibling.
      LOKA_VERIFY([view methodForSelector:selector] != [NSTextView instanceMethodForSelector:selector]);
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
      LOKA_VERIFY(![view hasMarkedText] && [[view string] isEqual:before]
                  && NSEqualRanges([view selectedRange], selection));
      [before release];
    }
  } // namespace testing
} // namespace loka
#endif
