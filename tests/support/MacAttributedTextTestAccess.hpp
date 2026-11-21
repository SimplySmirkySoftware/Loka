#ifndef LOKA_MAC_ATTRIBUTED_TEXT_TEST_ACCESS_HPP
#define LOKA_MAC_ATTRIBUTED_TEXT_TEST_ACCESS_HPP
#include "MacObjCCompat.hpp"
#ifndef LOKA_MAC_HAS_CALAYER
#error "include MacObjCCompat.hpp first"
#endif
#include "support/TestVerify.hpp"
#include <sys/utsname.h>
#include <cstdlib>
#include "context/MacAttributedTextContext.hpp"

/** One production predicate comparison of the test-supplied values. */
bool MacTestEligibleRun(const MacTextHit &hit,
                        const MacPresentedText &fact,
                        const loka::app::AttributedString &snapshot,
                        const loka::app::AttributedString &current);
/** One out-of-line budget check per test size. */
bool MacTestTextBudget(NSSize size, CGFloat scale);

#include "testing/MacPresentedOwnerTestAccess.hpp"
namespace loka
{
  namespace testing
  {
    /** Arm or disarm one borrowed buffer around the synthetic same-image replacement. */
    void traceMacPresentedImage(char *buffer);
  } // namespace testing
} // namespace loka
/** Pass trace reads, once per receiver hook in the test binary. */
unsigned MacTestRepaintEntries();
/** One lexical interval read per forwarded native send. */
bool MacTestInsideRepaint();
/** One production coverage calculation on the supplied damage rects per test. */
bool MacTestRectsCover(const NSRect *rects, NSInteger count, NSRect target);

// The implementation and ownership of these getters stay on the rail-private leaf.
@interface NSView (LokaAttributedTextTestReads)
- (NSTextStorage *)lokaTextStorage;
- (NSLayoutManager *)lokaLayoutManager;
- (NSTextContainer *)lokaTextContainer;
- (CGFloat)appliedWidth;
/** One production width application per invalidation pin. */
- (void)applyContainerWidth:(CGFloat)width;
/** One storage replacement and measurement of this leaf per nonempty install request. */
- (BOOL)installValue:(NSAttributedString *)value
            snapshot:(const loka::app::AttributedString *)snapshot
              extent:(CGFloat *)extent;
/** One routed native tuple read per observation. */
- (BOOL)queryPresentedFact:(MacPresentedText *)out;
/** One draw of this view's glyphs in the requested band per display. */
- (void)drawTextInRect:(NSRect)rect;
/** One synthetic missing-layer tuple check per field-isolation pin. */
- (BOOL)lokaFactWithAbsentLayer;
/** One installed-tag read per owner observation. */
- (BOOL)lokaHasInstalledText;
/** One retained-pointer read per owner observation. */
- (BOOL)lokaHasPresentationImage;
/** One presentation path read per fixture observation. */
- (int)lokaPresentedPath;
/** One production placement invalidation per interval pin. */
- (void)dropWindowPresentation;
@end

namespace loka
{
  namespace dsl
  {
    namespace testing
    {
      inline int DarwinMajor()
      {
        struct utsname host;
        LOKA_VERIFY(uname(&host) == 0);
        return std::atoi(host.release);
      }
      inline bool HasTahoeCellBaseline(const char *fixture)
      {
        if (DarwinMajor() == 25)
          return true;
        std::printf("[skip] %s Tahoe cell parity : no recorded baseline for this host\n", fixture);
        return false;
      }

      inline id TextAttribute(NSAttributedString *text, NSString *key, NSUInteger index, NSRange *range = 0)
      {
        LOKA_VERIFY(index < [text length]);
        return [text attribute:key atIndex:index longestEffectiveRange:range inRange:NSMakeRange(0, [text length])];
      }
      inline NSRect ReferenceTextGeometry(NSAttributedString *text, CGFloat width)
      {
        NSTextStorage *storage = [[[NSTextStorage alloc] initWithAttributedString:text] autorelease];
        NSLayoutManager *manager = [[[NSLayoutManager alloc] init] autorelease];
        NSTextContainer *container =
            [[[NSTextContainer alloc] initWithContainerSize:NSMakeSize(width, 1.0e7)] autorelease];
        [container setLineFragmentPadding:2];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [manager setUsesScreenFonts:NO];
#pragma clang diagnostic pop
        [manager addTextContainer:container];
        [storage addLayoutManager:manager];
        [manager glyphRangeForTextContainer:container];
        return [manager usedRectForTextContainer:container];
      }

      inline CGFloat CellAdvance(NSAttributedString *value, NSLineBreakMode mode, CGFloat width)
      {
        NSTextFieldCell *cell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
        [cell setAttributedStringValue:value];
        if ([cell respondsToSelector:@selector(setUsesSingleLineMode:)])
          [cell setUsesSingleLineMode:NO];
        [cell setWraps:mode == NSLineBreakByWordWrapping || mode == NSLineBreakByCharWrapping];
        [cell setScrollable:NO];
        [cell setLineBreakMode:mode];
        return [cell cellSizeForBounds:NSMakeRect(0, 0, width, CGFLOAT_MAX)].height;
      }

      class MacAttributedTextTestAccess
      {
      public:
        enum PresentedPath
        {
          NONE,
          LAYER,
          WINDOW
        };
        /** One out-of-line tag read per observation, independent of SDK. */
        static int presentedPath(NSView *view)
        {
          return [view lokaPresentedPath];
        }
        /** One invalidation of the supplied leaf per fixture reset. */
        static void dropWindowPresentation(NSView *view)
        {
          [view dropWindowPresentation];
        }
        static NSView *view(const MacAttributedTextContext &context)
        {
          return (NSView *)context.view_;
        }
        static NSView *leaf(NSView *parent)
        {
          LOKA_VERIFY([[parent subviews] count] == 1);
          NSView *leaf = [[parent subviews] objectAtIndex:0];
          LOKA_VERIFY([leaf isKindOfClass:NSClassFromString(@"LokaAttributedTextView")]);
          return leaf;
        }
        static NSTextStorage *storage(NSView *leaf)
        {
          return [leaf lokaTextStorage];
        }
        static NSLayoutManager *manager(NSView *leaf)
        {
          return [leaf lokaLayoutManager];
        }
        static NSTextContainer *container(NSView *leaf)
        {
          return [leaf lokaTextContainer];
        }
        static CGFloat width(NSView *leaf)
        {
          return [leaf appliedWidth];
        }
        static NSAttributedString *projectionSnapshot(const app::AttributedTextNode &node)
        {
          LOKA_VERIFY(node.getContext());
          const MacAttributedTextContext *context = static_cast<MacAttributedTextContext *>(node.getContext());
          return (NSAttributedString *)context->projection_.value();
        }
        static int height(const MacAttributedTextContext &context)
        {
          return context.projection_.height();
        }
        static bool reusable(const MacAttributedTextContext &context, const macos::MacLength &width)
        {
          return context.projection_.reusable(width);
        }
        static bool build(MacAttributedTextContext &context,
                          const app::AttributedString &value,
                          const MacScenePlatformController &controller)
        {
          return context.projection_.build(value, app::BlockStyle(), controller);
        }
        static bool
        query(const MacAttributedTextContext &context, NSUInteger start, NSUInteger end, app::TextSegmentRange &out)
        {
          return context.projection_.queryProvenance(start, end, out);
        }
        /** One context preparation per test point; the forwarder walks no rows. */
        static MacTextHitAnswer
        prepareSourceHit(const MacAttributedTextContext &context, NSPoint point, MacTextHit &out)
        {
          return context.prepareSourceHit(point, out);
        }
        /** One checked preparation per fixture point, forwarding without walking rows. */
        static MacTextHitAnswer
        prepareTextHit(const MacAttributedTextContext &context, NSPoint p, MacTextHit &hit, MacPresentedText &fact)
        {
          return context.prepareTextHit(p, hit, fact);
        }
        /** One production candidate query per test point. */
        static bool
        queryPressCandidate(const MacAttributedTextContext &context, NSPoint p, app::AttributedString::LinkRun &out)
        {
          return context.queryPressCandidate(p, out);
        }
        /** One borrowed snapshot per projection lifetime pin. */
        static const app::AttributedString &snapshot(const MacAttributedTextContext &context)
        {
          return context.projection_.snapshot();
        }
        static bool complete(const MacAttributedTextContext &context)
        {
          return context.projection_.value() != 0;
        }
        static bool measured(const MacAttributedTextContext &context)
        {
          CGFloat constraint = 0;
          return context.projection_.measurement_.queryConstraint(constraint);
        }
      };
    } // namespace testing
  } // namespace dsl
} // namespace loka

#endif
