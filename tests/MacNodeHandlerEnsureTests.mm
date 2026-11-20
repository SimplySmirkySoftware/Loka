#include "app/nodes/controls/Card.hpp"
#include "MacObjCCompat.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "MacNodeHandlerEnsureTests.hpp"
#include "MacPlatformContext.hpp"
#include "MacApp.hpp"
#include "app/core/AppConfigurable.hpp"
#include "testing/app/NativeResourceRetirementTestAccess.hpp"
#include "support/ScrollViewSeat.hpp"
#include "support/MacRetirementProbe.hpp"
#include "support/TestVerify.hpp"
#include "support/RailTextLayoutFixture.hpp"
#include "support/MacNativeRefusal.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <AppKit/AppKit.h>
#include <cstdio>

#include "MacScenePlatformController.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "MacSceneLayout.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/ScrollBar.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"

namespace
{
  void verifyFixedColumnFitsAtBothScales()
  {
    using namespace loka::app;
    const int count = 3;
    const RailMetrics metrics[] = {RailMetrics(), loka::macos::DefaultRailMetrics()};
    for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
    {
      const loka::macos::MacProjection allocation(0, metrics[scaleIndex]);
      NSView *root = [[NSView alloc] initWithFrame:allocation.projectClientSize(257, 160).r];
      LOKA_VERIFY(root != nil);
      {
        MacScenePlatformController controller((void *)root, metrics[scaleIndex]);
        const int buttonHeight = controller.rowLayoutMetrics().buttonHeight;
        const int height = 2 * MacSceneLayout::kRootInset + count * buttonHeight + (count - 1) * MacSceneLayout::kGap;
        [root setFrameSize:allocation.projectClientSize(257, height).r.size];
        LOKA_VERIFY(controller.projection().clientCapacityToLu([root bounds].size.width) == 257);
        LOKA_VERIFY(controller.projection().clientCapacityToLu([root bounds].size.height) == height);
        if (scaleIndex == 1)
        {
          LOKA_VERIFY(controller.projection().projectEdge(8).pt == 10);
          LOKA_VERIFY(controller.projection().capacityToLu(301) == 240);
        }
        StackNode column((StackProps(STACK_AXIS_COLUMN)));
        for (int i = 0; i < count; ++i)
          column.addChild(new ButtonNode(ButtonProps()));
        controller.onChange(&column, loka::app::scene::NODE_DIRTY_NONE, false);
        controller.relayout(0, 0);
        NSArray *children = [root subviews];
        LOKA_VERIFY([children count] == static_cast<NSUInteger>(count));
        CGFloat bottom = 0;
        for (NSUInteger i = 0; i < [children count]; ++i)
        {
          const NSRect frame = [[children objectAtIndex:i] frame];
          if (NSMaxY(frame) > bottom)
            bottom = NSMaxY(frame);
        }
        LOKA_VERIFY(bottom == controller.projection().projectEdge(height - MacSceneLayout::kRootInset).pt);
        LOKA_VERIFY(bottom <= [root bounds].size.height);
        controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
      }
      [root release];
    }
  }

  void verifyTextColumnExtents()
  {
    using namespace loka::app;
    const RailMetrics defaults = loka::macos::DefaultRailMetrics();
    // The middle leg isolates fontScale from spaceScale.
    const RailMetrics metrics[] = {RailMetrics(), RailMetrics(defaults.fontScale, Ratio()), defaults};
    const char *strings[] = {"First", "First\nSecond"};
    for (int scaleIndex = 0; scaleIndex < 3; ++scaleIndex)
      for (int boxed = 0; boxed < 2; ++boxed)
        for (int sample = 0; sample < 2; ++sample)
        {
          const loka::macos::MacProjection allocation(0, metrics[scaleIndex]);
          NSView *root = [[NSView alloc] initWithFrame:allocation.projectClientSize(340, 250).r];
          LOKA_VERIFY(root != nil);
          {
            MacScenePlatformController controller(root, metrics[scaleIndex]);
            RailTextLayoutFixture fixture(boxed != 0, strings[sample]);
            controller.onChange(&fixture.column, loka::app::scene::NODE_DIRTY_NONE, false);
            controller.relayout(0, 0);
            const loka::macos::MacProjection &projection = controller.projection();
            LOKA_VERIFY(projection.clientCapacityToLu([root bounds].size.height) == 250);
            NSArray *views = [root subviews];
            LOKA_VERIFY([views count] == static_cast<NSUInteger>(boxed ? 5 : 3));
            NSView *page = [views objectAtIndex:0];
            NSView *caption = [views objectAtIndex:1];
            NSButton *button = (NSButton *)[views objectAtIndex:boxed ? 3 : 2];
            LOKA_VERIFY([button isKindOfClass:[NSButton class]]);

            // Reference native measurement: pin both the inverse and the lu
            // padding, not an equality that independent font/space ratios lack.
            NSFont *font = (NSFont *)controller.textFont(FontSize<18>());
            NSTextFieldCell *cell = [[NSTextFieldCell alloc] initTextCell:@"First"];
            [cell setFont:font];
            [cell setWraps:YES];
            [cell setScrollable:NO];
            [cell setLineBreakMode:NSLineBreakByWordWrapping];
            const int pageWidth = boxed ? 300 : 340 - 2 * MacSceneLayout::kRootInset;
            const NSRect bounds = NSMakeRect(0, 0, projection.projectLength(0, pageWidth).pt, 10000);
            const CGFloat oneLine = [cell cellSizeForBounds:bounds].height;
            [cell setStringValue:[NSString stringWithUTF8String:strings[sample]]];
            const CGFloat nativeHeight = [cell cellSizeForBounds:bounds].height;
            [cell release];
            // Short words cannot soft-wrap at 300 lu; the newline is the only
            // line break. NSCell's fixed padding can keep the ratio below two.
            const int lines = static_cast<int>(std::floor(nativeHeight / oneLine + 0.5));
            LOKA_VERIFY(lines == sample + 1);
            const int fontHeight = projection.measurementToLu(
                [font ascender] + std::fabs([font descender]) + [font leading]);
            const int minimum = fontHeight > 20 ? fontHeight : 20;
            const int measured = projection.measurementToLu(nativeHeight) + 2;
            const int textHeight = measured > minimum ? measured : minimum;
            const int captionY = MacSceneLayout::kRootInset + (boxed ? 170 : textHeight) + MacSceneLayout::kGap;
            // The boxed Row centres its plain caption against the Bold leaf.
            const Ratio &spaceScale = metrics[scaleIndex].spaceScale;
            const NSRect captionFrame = [[views objectAtIndex:boxed ? 2 : 1] frame];
            const double luPerPoint = static_cast<double>(spaceScale.den) / spaceScale.num;
            const int captionHeight = boxed ? static_cast<int>(std::floor(NSMaxY(captionFrame) * luPerPoint + 0.5)
                                                               - std::floor(NSMinY(captionFrame) * luPerPoint + 0.5))
                                            : 20;
            const int plainCaptionY = captionY + (boxed ? (captionHeight - 20) / 2 : 0);
            const int buttonY = captionY + captionHeight + MacSceneLayout::kGap;
            const int buttonHeight = projection.measurementToLu([[button cell] cellSize].height);
            LOKA_VERIFY(
                NSEqualRects([page frame],
                             projection
                                 .projectFrame(loka::core::Frame(
                                     MacSceneLayout::kRootInset, MacSceneLayout::kRootInset, pageWidth, textHeight))
                                 .r));
            LOKA_VERIFY([caption frame].origin.y == projection.projectEdge(plainCaptionY).pt);
            LOKA_VERIFY(NSMinY([button frame]) == projection.projectEdge(buttonY).pt);
            LOKA_VERIFY(NSMaxY([button frame]) == projection.projectEdge(buttonY + buttonHeight).pt);
            // The actual Scrapbook fixed Box prevents measured text changes
            // reaching the caption/buttons, for either one or two native lines.
            if (boxed)
              LOKA_VERIFY(buttonY == MacSceneLayout::kRootInset + 170 + captionHeight + 2 * MacSceneLayout::kGap);
            std::printf("  Mac text extent: font=%d/%d space=%d/%d boxed=%d lines=%d "
                        "nativeText=%.2f textLu=%d buttonLu=%d..%d framePt=%.2f..%.2f clientPt=%.2f\n",
                        metrics[scaleIndex].fontScale.num,
                        metrics[scaleIndex].fontScale.den,
                        metrics[scaleIndex].spaceScale.num,
                        metrics[scaleIndex].spaceScale.den,
                        boxed,
                        lines,
                        static_cast<double>(nativeHeight),
                        textHeight,
                        buttonY,
                        buttonY + buttonHeight,
                        static_cast<double>(NSMinY([button frame])),
                        static_cast<double>(NSMaxY([button frame])),
                        static_cast<double>([root bounds].size.height));
            controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
          }
          [root release];
        }
  }

  int gReentrantCreates = 0;
  int gReentrantAttachReads = 0;
  int gReentrantAfterAttaches = 0;
  int gReentrantDestroys = 0;

  class MacReentrantEnsureContext : public loka::app::scene::NodeContext
  {
  public:
    MacReentrantEnsureContext()
        : loka::app::scene::NodeContext()
    {
      ++gReentrantCreates;
    }

    virtual ~MacReentrantEnsureContext()
    {
      ++gReentrantDestroys;
    }

    void readLifecycleFactOnAttach()
    {
      ++gReentrantAttachReads;
    }
  };

  class MacReentrantEnsureHandler
      : public loka::app::scene::RetainedNodeHandler<MacReentrantEnsureHandler,
                                                     loka::app::ButtonNode,
                                                     MacReentrantEnsureContext>
  {
  public:
    static loka::app::ButtonNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asButtonNode() : 0;
    }

    static MacReentrantEnsureContext *create(loka::app::ButtonNode *,
                                             loka::app::scene::IPlatformController *,
                                             const loka::app::scene::LayoutState &)
    {
      return new MacReentrantEnsureContext();
    }

    static void afterAttach(MacReentrantEnsureContext *ctx)
    {
      ++gReentrantAfterAttaches;
      loka::app::scene::Node *owner = ctx->owner();
      LOKA_VERIFY(owner != 0);
      // Model the modal macOS dialog hazard from the ensure side: synchronous
      // app code REPLACES the node's live context (here with none) before
      // ensureContext returns, so the platform controller's local pointer is
      // stale on return. setContext(0) on a still-ATTACHED node is a live
      // replacement, not the retire door -- releaseContext's terminal
      // delivery assumes RETIRED was already written, so nothing is delivered
      // here. That is deliberate: this test owns the ensure re-read contract.
      // Retirement terminal delivery and queued native destruction are a
      // different contract, covered by the lifecycle-fact tests, and a
      // regression confined to those would not turn this test red.
      // Do not touch ctx after this call; it has been reclaimed.
      owner->setContext(0);
    }
  };

  NSUInteger countChildViews(NSView *root)
  {
    return [[root subviews] count];
  }

  bool frameEquals(NSView *view, CGFloat x, CGFloat y, CGFloat width, CGFloat height)
  {
    const NSRect frame = [view frame];
    return frame.origin.x == x && frame.origin.y == y && frame.size.width == width && frame.size.height == height;
  }
} // namespace

// macOS twin of testWin32NodeHandlerEnsureContract. Besides pinning context
// publication, reuse, placement, and typed refusal through the native controller
// seam. Unlike Win32, preparation preserves retained geometry. The final leg
// discriminates PR #254's modal re-entrancy hazard: the
// handler must return the Node's context after afterAttach, not its stale local.
void testMacNodeHandlerEnsureContract()
{
  std::printf("\n==== [testMacNodeHandlerEnsureContract] start ====\n");
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  verifyFixedColumnFitsAtBothScales();
  verifyTextColumnExtents();
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller((void *)root, loka::app::RailMetrics());

    // -- Button: full contract through the root view's child census --
    loka::app::ButtonProps buttonProps;
    loka::app::ButtonNode button(buttonProps);

    loka::app::scene::LayoutState state;
    state.x = 10;
    state.y = 20;
    state.width = 100;
    state.height = 30;
    const NSUInteger childrenBeforeEnsure = countChildViews(root);
    LOKA_VERIFY(controller.prepareProjectedLayout(&button, state));

    loka::app::scene::NodeContext *ctx = button.getContext();
    LOKA_VERIFY(ctx != 0 && "ensure must publish the created context through setContext");
    LOKA_VERIFY(countChildViews(root) == childrenBeforeEnsure + 1);
    NSView *buttonView = [[root subviews] objectAtIndex:childrenBeforeEnsure];
    LOKA_VERIFY(frameEquals(buttonView, 10, 20, 100, 30));

    // Preparation must not place a retained view at provisional geometry.
    // Placement follows only after the node resolves its dimensions.
    state.x = 40;
    state.y = 50;
    state.width = 120;
    state.height = 40;
    LOKA_VERIFY(controller.prepareProjectedLayout(&button, state));
    LOKA_VERIFY(button.getContext() == ctx && "re-ensure must reuse the existing context, not recreate it");
    LOKA_VERIFY(countChildViews(root) == childrenBeforeEnsure + 1 &&
                "re-ensure must not materialize another native view");
    LOKA_VERIFY(frameEquals(buttonView, 10, 20, 100, 30) && "preparation must preserve the retained frame");
    button.layoutProjected(&controller, state);
    LOKA_VERIFY(frameEquals(buttonView, 40, 50, 120, controller.rowLayoutMetrics().buttonHeight));

    // -- Text: same contract on a second native node kind --
    loka::app::TextProps textProps;
    loka::app::TextNode text(textProps);
    state.x = 5;
    state.y = 100;
    state.width = 200;
    state.height = 16;
    LOKA_VERIFY(controller.prepareProjectedLayout(&text, state));
    loka::app::scene::NodeContext *textCtx = text.getContext();
    LOKA_VERIFY(textCtx != 0 && "ensure must publish the created context through setContext");
    const NSUInteger childrenWithText = countChildViews(root);
    LOKA_VERIFY(childrenWithText == childrenBeforeEnsure + 2);
    LOKA_VERIFY(controller.prepareProjectedLayout(&text, state));
    LOKA_VERIFY(text.getContext() == textCtx);
    LOKA_VERIFY(countChildViews(root) == childrenWithText);

    // Alignment applies on the retained plain label without replacing it.
    NSTextField *textField = (NSTextField *)[[root subviews] objectAtIndex:childrenWithText - 1];
    const loka::app::TextAlign alignments[] = {
        loka::app::TEXT_ALIGN_CENTER, loka::app::TEXT_ALIGN_RIGHT, loka::app::TEXT_ALIGN_LEFT};
    const NSTextAlignment expected[] = {LOKA_MAC_TEXT_ALIGNMENT_CENTER, LOKA_MAC_TEXT_ALIGNMENT_RIGHT, LOKA_MAC_TEXT_ALIGNMENT_LEFT};
    for (int a = 0; a < 3; ++a)
    {
      text.props.blockStyle_.align(alignments[a]);
      textCtx->onPropsApplied();
      LOKA_VERIFY([textField alignment] == expected[a]);
      LOKA_VERIFY(text.getContext() == textCtx);
    }
    // Retained aligned -> undeclared restores AppKit's default alignment.
    text.props.blockStyle_ = loka::app::BlockStyle();
    textCtx->onPropsApplied();
    LOKA_VERIFY([textField alignment] == LOKA_MAC_TEXT_ALIGNMENT_NATURAL);
    LOKA_VERIFY(text.getContext() == textCtx);

    // -- TextEditor: installs its multiline context even with unavailable props --
    loka::app::TextEditorNode editor((loka::app::TextEditorProps()));
    LOKA_VERIFY(controller.prepareProjectedLayout(&editor, state));
    LOKA_VERIFY(editor.getContext());
    // Card bands install a real transparent view, retained across ensures.
    const NSUInteger beforeBands = countChildViews(root);
    for (int role = 0; role < loka::app::CARD_BAND_COUNT; ++role)
    {
      loka::app::CardBandNode band(loka::app::CardBandProps(static_cast<loka::app::CardBandRole>(role)));
      const loka::core::Frame frames[] = {loka::core::Frame(11, 17, 74, 3),
                                          loka::core::Frame(11, 20, 3, 57),
                                          loka::core::Frame(85, 17, 6, 54),
                                          loka::core::Frame(14, 71, 77, 6)};
      state.x = static_cast<short>(frames[role].x);
      state.y = static_cast<short>(frames[role].y);
      state.width = static_cast<short>(frames[role].width);
      state.height = static_cast<short>(frames[role].height);
      LOKA_VERIFY(controller.prepareProjectedLayout(&band, state));
      loka::app::scene::NodeContext *context = band.getContext();
      LOKA_VERIFY(context != 0);
      LOKA_VERIFY(controller.prepareProjectedLayout(&band, state));
      LOKA_VERIFY(band.getContext() == context);
      LOKA_VERIFY(countChildViews(root) == beforeBands + 1);
      LOKA_VERIFY(![[[root subviews] lastObject] isOpaque]);
    }
    LOKA_VERIFY(countChildViews(root) == beforeBands);
    // -- ScrollBar: known unsupported kinds take the typed-refusal path --
    loka::app::ScrollBarProps scrollProps;
    loka::app::ScrollBarNode scrollBar(scrollProps);
    state.x = 5;
    state.y = 130;
    state.width = 120;
    state.height = 16;
    LOKA_VERIFY(!controller.prepareProjectedLayout(&scrollBar, state) &&
                "an unsupported kind must refuse, not project");
    LOKA_VERIFY(!scrollBar.getContext());
    LOKA_VERIFY(countChildViews(root) == childrenWithText + 1 &&
                "a refusal must not materialize a native view");

    // -- Re-entrancy: afterAttach replaces the just-published context --
    gReentrantCreates = 0;
    gReentrantAttachReads = 0;
    gReentrantAfterAttaches = 0;
    gReentrantDestroys = 0;
    MacReentrantEnsureHandler reentrantHandler;
    LOKA_VERIFY(controller.registerNodeHandler(&reentrantHandler));
    loka::app::ButtonNode reentrantButton(buttonProps);
    LOKA_VERIFY(!controller.prepareProjectedLayout(&reentrantButton, state) &&
                "ensure must re-read the node after re-entrant attach work replaces its context");
    LOKA_VERIFY(!reentrantButton.getContext());
    LOKA_VERIFY(gReentrantCreates == 1);
    LOKA_VERIFY(gReentrantAttachReads == 1);
    LOKA_VERIFY(gReentrantAfterAttaches == 1);
    LOKA_VERIFY(gReentrantDestroys == 1);

    std::printf("  button and text contexts reused; scrollbar refused; re-entrant ensure returned null\n");
    // Nodes leave scope before the controller. Their terminal facts remove
    // native views synchronously; the controller then drains retained objects.
  }
  LOKA_VERIFY(countChildViews(root) == 0);
  [root release];
  [pool drain];
  std::printf("==== [testMacNodeHandlerEnsureContract] PASSED ====\n");
}

#include "app/nodes/AttributedText.hpp"
#include "platform/MacNativeGeometry.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/LokaAllocFailure.hpp"
#include "platform/String.hpp"
#include "MacGround.hpp"

#include "support/MacAttributedTextTestAccess.hpp"
namespace
{
  typedef loka::dsl::testing::MacAttributedTextTestAccess AttributedAccess;

  using loka::dsl::testing::CellAdvance;
  using loka::dsl::testing::DarwinMajor;
  using loka::dsl::testing::ReferenceTextGeometry;
  using loka::dsl::testing::TextAttribute;

  void VerifyAttributedHeight(loka::app::AttributedTextNode &node, MacScenePlatformController &controller,
                              NSView *root, short width)
  {
    loka::app::scene::LayoutState state;
    state.x = 3;
    state.y = 5;
    state.width = width;
    node.layoutProjected(&controller, state);
    NSView *field = AttributedAccess::leaf(root);
    const loka::macos::MacProjection &projection = controller.projection();
    NSAttributedString *text = AttributedAccess::projectionSnapshot(node);
    CGFloat points = projection.projectLength(state.x, state.x + width).pt;
    NSParagraphStyle *style = TextAttribute(AttributedAccess::storage(field), NSParagraphStyleAttributeName, 0);
    const CGFloat cell = CellAdvance(text, [style lineBreakMode], points);
    const CGFloat rendered = ReferenceTextGeometry(text, points).size.height;
    std::printf("attributed fixture width=%g cell=%g TextKit=%g\n", (double)points, (double)cell, (double)rendered);
    LOKA_VERIFY(state.height == projection.measurementToLu(rendered));
    static const bool cellParity = loka::dsl::testing::HasTahoeCellBaseline("attributed height");
    if (cellParity)
      LOKA_VERIFY(state.height == projection.measurementToLu(cell));
    LOKA_VERIFY(NSEqualRects([field frame], projection.projectFrame(
        loka::core::Frame(3, 5, width, state.height)).r));
  }

  class MacRefusedAttributedString : public loka::platform::String
  {
  public:
    virtual bool appendUtf8(std::string &) const { return false; }
  };
}

// Raw bodies deliberately bypass native String decoding, including split scalars.
namespace
{
  class MacRawAttributedString : public loka::platform::String
  {
  public:
    MacRawAttributedString(const char *bytes, std::size_t size)
        : bytes_(bytes, size)
    {
    }
    virtual bool appendUtf8(std::string &out) const
    {
      out.append(this->bytes_);
      return true;
    }

  private:
    std::string bytes_;
  };

  loka::core::String RawAttributed(const char *bytes, std::size_t size)
  {
    return loka::core::String(
        loka::core::Managed<loka::platform::String>::Wrap(new MacRawAttributedString(bytes, size)));
  }

  struct ExpectedAttributedRun
  {
    NSString *text;
    loka::app::TextStyle style;
  };

  // Expected strings/ranges are supplied by the fixture, never decoded from
  // its bodies. Measure the independently constructed expected native value.
  void
  CharacterizeAttributed(const loka::app::AttributedString &value, const ExpectedAttributedRun *runs, std::size_t count)
  {
    using namespace loka::app;
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
    {
      MacScenePlatformController controller(root, RailMetrics());
      AttributedTextProps props(value);
      props.blockStyle_ = BlockStyle().wrap(TEXT_WRAP_WORD).align(TEXT_ALIGN_CENTER);
      AttributedTextNode node(props);
      loka::app::scene::LayoutState state;
      state.width = 90;
      node.layoutProjected(&controller, state);
      NSAttributedString *actual = AttributedAccess::projectionSnapshot(node);
      NSMutableAttributedString *expected = [[[NSMutableAttributedString alloc] initWithString:@""] autorelease];
      for (std::size_t i = 0; i < count; ++i)
      {
        NSFont *font = (NSFont *)controller.textFont(runs[i].style);
        NSColor *color = nil;
        if (!loka::macos::QueryMacTextColor(runs[i].style.color_, color))
          color = loka::macos::MacLabelTextColor();
        NSMutableDictionary *attributes = [NSMutableDictionary
            dictionaryWithObjectsAndKeys:font, NSFontAttributeName, color, NSForegroundColorAttributeName, nil];
        bool underline = false;
        if (runs[i].style.queryUnderline(underline) && underline)
          [attributes setObject:[NSNumber numberWithInt:NSUnderlineStyleSingle] forKey:NSUnderlineStyleAttributeName];
        NSAttributedString *part = [[[NSAttributedString alloc] initWithString:runs[i].text
                                                                    attributes:attributes] autorelease];
        [expected appendAttributedString:part];
      }
      NSMutableParagraphStyle *paragraph = [[[NSMutableParagraphStyle alloc] init] autorelease];
      [paragraph setLineBreakMode:NSLineBreakByWordWrapping];
      [paragraph setAlignment:LOKA_MAC_TEXT_ALIGNMENT_CENTER];
      [expected addAttribute:NSParagraphStyleAttributeName value:paragraph range:NSMakeRange(0, [expected length])];
      LOKA_VERIFY([[actual string] isEqualToString:[expected string]]);
      for (NSUInteger u = 0; u < [expected length];)
      {
        NSRange actualRange, expectedRange;
        NSDictionary *actualAttributes = [actual attributesAtIndex:u effectiveRange:&actualRange];
        NSDictionary *expectedAttributes = [expected attributesAtIndex:u effectiveRange:&expectedRange];
        LOKA_VERIFY(NSEqualRanges(actualRange, expectedRange));
        LOKA_VERIFY([actualAttributes isEqualToDictionary:expectedAttributes]);
        u = NSMaxRange(expectedRange);
      }
      NSTextStorage *storage = AttributedAccess::storage(AttributedAccess::leaf(root));
      LOKA_VERIFY([[storage string] isEqualToString:[expected string]]);
      NSString *keys[] = {NSForegroundColorAttributeName, NSUnderlineStyleAttributeName, NSParagraphStyleAttributeName};
      for (unsigned k = 0; k < 3; ++k)
        for (NSUInteger u = 0; u < [expected length];)
        {
          NSRange actualRange, expectedRange;
          id installed = TextAttribute(storage, keys[k], u, &actualRange);
          id wanted = TextAttribute(expected, keys[k], u, &expectedRange);
          LOKA_VERIFY(installed == wanted || [installed isEqual:wanted]);
          LOKA_VERIFY(NSEqualRanges(actualRange, expectedRange));
          u = NSMaxRange(expectedRange);
        }
      const CGFloat cellHeight =
          CellAdvance(expected, NSLineBreakByWordWrapping, controller.projection().projectLength(0, 90).pt);
      const CGFloat expectedHeight =
          [expected length]
              ? ReferenceTextGeometry(expected, controller.projection().projectLength(0, 90).pt).size.height
              : cellHeight;
      std::printf("attributed fixture units=%lu cell=%g TextKit=%g\n",
                  (unsigned long)[expected length],
                  (double)cellHeight,
                  (double)expectedHeight);
      LOKA_VERIFY(state.height == controller.projection().measurementToLu(expectedHeight));
      static const bool cellParity = loka::dsl::testing::HasTahoeCellBaseline("attributed run characterization");
      if (cellParity)
        LOKA_VERIFY(state.height == controller.projection().measurementToLu(cellHeight));
    }
    [root release];
  }

  NSFont *MacAttributedMissingSystemFont(id, SEL, CGFloat)
  {
    return nil;
  }
} // namespace

void testMacAttributedTextCharacterizeStyles()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  const TextStyle small = FontSize<12>();
  const ExpectedAttributedRun linked[] = {{@"ab", small}};
  CharacterizeAttributed(Linked("a", small, LinkTarget(loka::core::String("x")))
                             + Linked("b", small, LinkTarget(loka::core::String("y"))),
                         linked,
                         1);
  const ExpectedAttributedRun empty[] = {{@"", small}};
  CharacterizeAttributed(Styled("", small) + Styled("", Italic), empty, 1);
  const TextStyle on = small + Underline + TextStyle().color(TEXT_COLOR_RED);
  const TextStyle off = small + TextStyle().underline(false);
  const TextStyle large = FontSize<24>();
  const ExpectedAttributedRun transitions[] = {{@"on", on}, {@"offunset", small}, {@"\nlarge", large}};
  CharacterizeAttributed(
      Styled("on", on) + Styled("off", off) + Styled("unset", small) + Styled("\nlarge", large), transitions, 3);
  [pool drain];
}

void testMacAttributedTextCharacterizeBytes()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  const TextStyle style = FontSize<12>();
  const char *bytes[] = {"\xEF\xBB\xBF"
                         "ab",
                         "\xEF\xBB\xBF",
                         "\xEF\xBB\xBF\xEF\xBB\xBF"
                         "a",
                         "a\xEF\xBB\xBF"
                         "b",
                         "a\0b"};
  const std::size_t lengths[] = {5, 3, 7, 5, 3};
  const unichar nul[] = {'a', 0, 'b'};
  NSString *strings[] = {@"ab", @"", @"\uFEFFa", @"a\uFEFFb", [NSString stringWithCharacters:nul length:3]};
  for (int i = 0; i < 5; ++i)
  {
    const ExpectedAttributedRun expected[] = {{strings[i], style}};
    CharacterizeAttributed(Styled(RawAttributed(bytes[i], lengths[i]), style), expected, 1);
  }
  const unichar pair[] = {0xD83D, 0xDE00};
  const ExpectedAttributedRun split[] = {{[NSString stringWithCharacters:pair length:2], style}};
  CharacterizeAttributed(
      Styled(RawAttributed("\xF0\x9F", 2), style) + Styled(RawAttributed("\x98\x80", 2), style), split, 1);
  [pool drain];
}

void testMacAttributedTextCharacterizeRefusals()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  {
    MacScenePlatformController controller(root, RailMetrics());
    const loka::core::String refused(
        loka::core::Managed<loka::platform::String>::Wrap(new MacRefusedAttributedString()));
    const AttributedString values[] = {Styled(RawAttributed("\xF0\x9F", 2), FontSize<12>())
                                           + Styled(RawAttributed("\x98\x80", 2), Italic),
                                       Styled(refused, Italic)};
    for (int i = 0; i < 2; ++i)
    {
      AttributedTextNode node((AttributedTextProps(values[i])));
      loka::app::scene::LayoutState state;
      state.width = 90;
      node.layoutProjected(&controller, state);
      LOKA_VERIFY(state.height == 0);
      LOKA_VERIFY([[AttributedAccess::storage(AttributedAccess::leaf(root)) string] length] == 0);
    }
  }
  MacScenePlatformController *controller = 0;
  {
    MacNativeRefusal refusal(@selector(systemFontOfSize:),
                             reinterpret_cast<IMP>(&MacAttributedMissingSystemFont),
                             object_getClass([NSFont class]));
    controller = new MacScenePlatformController(root, RailMetrics());
  }
  {
    LOKA_VERIFY(controller->textFont(SmallLabel) == 0);
    AttributedTextNode node((AttributedTextProps(Styled("visible", Monospace) + Styled("", SmallLabel))));
    loka::app::scene::LayoutState state;
    state.width = 90;
    node.layoutProjected(controller, state);
    LOKA_VERIFY(state.height == 0);
    LOKA_VERIFY([[AttributedAccess::storage(AttributedAccess::leaf(root)) string] length] == 0);
  }
  delete controller;
  [root release];
  [pool drain];
}

void testMacAttributedTextWholeLineProjection()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  const RailMetrics metrics[] = {RailMetrics(), RailMetrics(Ratio(13, 12), Ratio(5, 4))};
  for (int scale = 0; scale < 2; ++scale)
  {
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
    {
      MacScenePlatformController controller((void *)root, metrics[scale]);
      LOKA_VERIFY(controller.textShaping() == WHOLE_LINE);
      const TextStyle small = FontSize<12>();
      const TextStyle large = FontSize<24>() + Italic;
      // A supplementary character makes UTF-8 byte offsets and UTF-16 ranges
      // disagree. Splitting the small run must not create a new styled range.
      const AttributedString value = Styled("A", small) + Styled("\xF0\x9F\x98\x80 ", small)
          + Styled("large italic words wrap here", large);
      AttributedTextProps props(value);
      props.blockStyle_ = BlockStyle().wrap(TEXT_WRAP_WORD);
      AttributedTextNode node(props);
      loka::app::scene::LayoutState state;
      state.width = 90;
      // Pre-fix red: gRefusedMacAttributedText rejects this ensure.
      LOKA_VERIFY(controller.prepareProjectedLayout(&node, state));
      LOKA_VERIFY(node.getContext() != 0);
      VerifyAttributedHeight(node, controller, root, 90);
      NSView *field = AttributedAccess::leaf(root);
      NSAttributedString *native = AttributedAccess::projectionSnapshot(node);
      NSRange range;
      NSFont *first = [native attribute:NSFontAttributeName atIndex:0 effectiveRange:&range];
      LOKA_VERIFY([first isEqual:(NSFont *)controller.textFont(small)]);
      LOKA_VERIFY(range.location == 0 && range.length == 4);
      NSFont *second = [native attribute:NSFontAttributeName atIndex:4 effectiveRange:&range];
      LOKA_VERIFY([second isEqual:(NSFont *)controller.textFont(large)]);
      LOKA_VERIFY(range.location == 4 && range.length == [native length] - 4);
      const CGFloat narrowHeight = [field frame].size.height;
      VerifyAttributedHeight(node, controller, root, 260);
      LOKA_VERIFY([field frame].size.height < narrowHeight);
      loka::app::scene::NodeContext *context = node.getContext();
      node.props.blockStyle_ = BlockStyle().wrap(TEXT_WRAP_CHAR);
      context->onPropsApplied();
      VerifyAttributedHeight(node, controller, root, 90);
      NSParagraphStyle *paragraph = TextAttribute(AttributedAccess::storage(field), NSParagraphStyleAttributeName, 0);
      LOKA_VERIFY([paragraph lineBreakMode] == NSLineBreakByCharWrapping);
      node.props.blockStyle_ = BlockStyle().wrap(TEXT_WRAP_NONE).truncation(TEXT_TRUNCATION_ELLIPSIS);
      context->onPropsApplied();
      VerifyAttributedHeight(node, controller, root, 90);
      LOKA_VERIFY([(NSParagraphStyle *)TextAttribute(AttributedAccess::storage(field), NSParagraphStyleAttributeName, 0)
                      lineBreakMode]
                  == NSLineBreakByTruncatingTail);
      const TextAlign alignments[] = {TEXT_ALIGN_LEFT, TEXT_ALIGN_CENTER, TEXT_ALIGN_RIGHT};
      const NSTextAlignment expected[] = {LOKA_MAC_TEXT_ALIGNMENT_LEFT, LOKA_MAC_TEXT_ALIGNMENT_CENTER, LOKA_MAC_TEXT_ALIGNMENT_RIGHT};
      for (int a = 0; a < 3; ++a)
      {
        node.props.blockStyle_ = BlockStyle().wrap(TEXT_WRAP_NONE).align(alignments[a]);
        node.props.text(Styled("short", small));
        context->onPropsApplied();
        VerifyAttributedHeight(node, controller, root, 90);
        const CGFloat oneLine = [field frame].size.height;
        node.props.text(Styled("short\nline", small));
        context->onPropsApplied();
        VerifyAttributedHeight(node, controller, root, 90);
        LOKA_VERIFY([field frame].size.height > oneLine);
        NSParagraphStyle *aligned = TextAttribute(AttributedAccess::storage(field), NSParagraphStyleAttributeName, 0);
        LOKA_VERIFY([aligned alignment] == expected[a]);
      }
      LOKA_VERIFY(node.getContext() == context);
    }
    LOKA_VERIFY([[root subviews] count] == 0);
    [root release];
  }
  [pool drain];
}

void testMacAttributedTextRetainedLifecycle()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  {
    MacScenePlatformController controller((void *)root, RailMetrics());
    AttributedTextProps props(Styled("before", FontSize<12>()));
    props.blockStyle_ = BlockStyle().wrap(TEXT_WRAP_WORD);
    AttributedTextNode node(props);
    // A node placed directly (not through the definition factory) carries no
    // props type id; applyPropsToNode's compatibility check needs it.
    node.setPropsTypeId(AttributedTextProps::staticTypeId());
    VerifyAttributedHeight(node, controller, root, 100);
    MacAttributedTextContext *context = static_cast<MacAttributedTextContext *>(node.getContext());
    const loka::macos::MacLength measuredWidth = controller.projection().projectLength(3, 103);
    NSView *field = AttributedAccess::leaf(root);
    NSAttributedString *before = [AttributedAccess::projectionSnapshot(node) retain];
    loka::app::scene::NotifySubtreeNodeDetached(&node);
    loka::app::scene::LifecycleFactTestAccess::DeliverFacts(&node);
    LOKA_VERIFY([field isHidden]);
    LOKA_VERIFY([AttributedAccess::projectionSnapshot(node) isEqualToAttributedString:before]);
    loka::app::scene::NotifySubtreeNodeAttached(&node);
    loka::app::scene::LifecycleFactTestAccess::DeliverFacts(&node);
    LOKA_VERIFY(![field isHidden]);
    LOKA_VERIFY(AttributedAccess::reusable(*context, measuredWidth));
    LOKA_VERIFY(node.getContext() == context);
    LOKA_VERIFY([AttributedAccess::projectionSnapshot(node) isEqualToAttributedString:before]);
    [before release];
    loka::app::scene::NotifySubtreeNodeDetached(&node);
    loka::app::scene::LifecycleFactTestAccess::DeliverFacts(&node);
    AttributedTextProps replacement(Styled("updated while hidden", FontSize<24>() + Italic));
    replacement.blockStyle_ = BlockStyle().wrap(TEXT_WRAP_WORD);
    AttributedTextDefinition definition(replacement);
    LOKA_VERIFY(definition.applyPropsToNode(&node));
    LOKA_VERIFY(!AttributedAccess::measured(*context) && AttributedAccess::height(*context) == 0);
    VerifyAttributedHeight(node, controller, root, 100);
    LOKA_VERIFY(AttributedAccess::reusable(*context, measuredWidth));
    LOKA_VERIFY([field isHidden]);
    loka::app::scene::NotifySubtreeNodeAttached(&node);
    loka::app::scene::LifecycleFactTestAccess::DeliverFacts(&node);
    LOKA_VERIFY(![field isHidden]);
    LOKA_VERIFY(AttributedAccess::reusable(*context, measuredWidth));
    LOKA_VERIFY([[AttributedAccess::storage(field) string] isEqualToString:@"updated while hidden"]);
    LOKA_VERIFY(node.getContext() == context);
    NSTextStorage *storage = AttributedAccess::storage(field);
    LOKA_VERIFY([[storage string] isEqualToString:[AttributedAccess::projectionSnapshot(node) string]]);
    loka::app::scene::LifecycleFactTestAccess::MarkSubtreeRetired(&node);
    loka::app::scene::LifecycleFactTestAccess::DeliverFacts(&node);
    LOKA_VERIFY([[root subviews] count] == 0);
    LOKA_VERIFY([[storage string] length] == 0);
  }
  [root release];
  [pool drain];
}

void testMacAttributedTextRefusalClearsProjection()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  {
    MacScenePlatformController controller((void *)root, RailMetrics());
    AttributedTextProps props(Styled("visible", FontSize<12>()));
    AttributedTextNode node(props);
    node.setPropsTypeId(AttributedTextProps::staticTypeId());
    VerifyAttributedHeight(node, controller, root, 100);
    const loka::core::String text("allocation refusal");
    loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 1);
    const AttributedString refused = Styled(text, Italic);
    loka::core::testing::allowLokaAllocRaw();
    LOKA_VERIFY(!refused.valid());
    LOKA_VERIFY(AttributedText(refused).applyPropsToNode(&node));
    loka::app::scene::LayoutState state;
    state.width = 100;
    node.layoutProjected(&controller, state);
    LOKA_VERIFY(state.height == 0 && [[AttributedAccess::storage(AttributedAccess::leaf(root)) string] length] == 0);
    LOKA_VERIFY(AttributedText(Styled("recovered", Italic)).applyPropsToNode(&node));
    VerifyAttributedHeight(node, controller, root, 100);
    const loka::core::String unreadable(loka::core::Managed<loka::platform::String>::Wrap(new MacRefusedAttributedString()));
    LOKA_VERIFY(AttributedText(Styled(unreadable, Italic)).applyPropsToNode(&node));
    node.layoutProjected(&controller, state);
    LOKA_VERIFY(state.height == 0 && [[AttributedAccess::storage(AttributedAccess::leaf(root)) string] length] == 0);
    LOKA_VERIFY(AttributedText(Styled("recovered again", Italic)).applyPropsToNode(&node));
    VerifyAttributedHeight(node, controller, root, 100);
  }
  [root release];
  [pool drain];
}

namespace
{

  void VerifySource(
      const MacAttributedTextContext &context, NSUInteger start, NSUInteger end, std::size_t first, std::size_t last)
  {
    loka::app::TextSegmentRange out(99);
    LOKA_VERIFY(AttributedAccess::query(context, start, end, out));
    LOKA_VERIFY(out.first() == first && out.last() == last);
  }

  void VerifyNoSource(const MacAttributedTextContext &context, NSUInteger start, NSUInteger end)
  {
    loka::app::TextSegmentRange out = loka::app::TextSegmentRange(97).through(99);
    LOKA_VERIFY(!AttributedAccess::query(context, start, end, out));
    LOKA_VERIFY(out.first() == 97 && out.last() == 99);
  }

  MacAttributedTextContext &AttributedContext(loka::app::AttributedTextNode &node)
  {
    LOKA_VERIFY(node.getContext() != 0);
    return *static_cast<MacAttributedTextContext *>(node.getContext());
  }

  loka::app::AttributedString ManyAttributed(const loka::core::String &last, const loka::app::TextStyle &lastStyle)
  {
    using namespace loka::app;
    AttributedString::Builder builder(34);
    for (int i = 0; i < 33; ++i)
      LOKA_VERIFY(builder.append(loka::core::String("a"), FontSize<12>()));
    LOKA_VERIFY(builder.append(last, lastStyle));
    return builder.build();
  }
} // namespace

void testMacAttributedTextSourceIntervals()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  {
    MacScenePlatformController controller(root, RailMetrics());
    AttributedTextNode node((AttributedTextProps(Styled("a", FontSize<12>()))));
    VerifyAttributedHeight(node, controller, root, 100);
    MacAttributedTextContext &context = AttributedContext(node);
    VerifySource(context, 0, 1, 0, 0);
    const TextStyle style = FontSize<12>();
    LOKA_VERIFY(AttributedAccess::build(context, Styled("a", style) + Styled("b", style), controller));
    VerifySource(context, 0, 1, 0, 0);
    VerifySource(context, 1, 2, 1, 1);
    VerifySource(context, 0, 2, 0, 1);
    VerifyNoSource(context, 0, 0);
    VerifyNoSource(context, 1, 0);
    VerifyNoSource(context, 0, 3);
    VerifyNoSource(context, 2, 3);
    VerifyNoSource(context, 2, 2);
    LOKA_VERIFY(AttributedAccess::build(context, Styled("\xF0\x9F\x98\x80", style), controller));
    VerifySource(context, 0, 1, 0, 0);
    VerifySource(context, 1, 2, 0, 0);
    VerifySource(context, 0, 2, 0, 0);
    LOKA_VERIFY(AttributedAccess::build(context,
                                        Styled(RawAttributed("a\xF0\x9F", 3), style)
                                            + Styled(RawAttributed("\x98\x80"
                                                                   "b",
                                                                   3),
                                                     style),
                                        controller));
    VerifySource(context, 0, 1, 0, 0);
    VerifyNoSource(context, 1, 2);
    VerifyNoSource(context, 2, 3);
    VerifyNoSource(context, 1, 3);
    VerifyNoSource(context, 0, 4);
    VerifySource(context, 3, 4, 1, 1);
    LOKA_VERIFY(AttributedAccess::build(context,
                                        Styled("", style) + Styled("a", style) + Styled("", Italic) + Styled("b", style)
                                            + Styled("", style),
                                        controller));
    VerifySource(context, 0, 1, 1, 1);
    VerifySource(context, 1, 2, 3, 3);
    VerifySource(context, 0, 2, 1, 3);
    LOKA_VERIFY(
        AttributedAccess::build(context, Styled("a", style) + Styled("", Italic) + Styled("b", style), controller));
    VerifySource(context, 0, 2, 0, 2);
    VerifySource(context, 1, 2, 2, 2);
    LOKA_VERIFY(AttributedAccess::build(context, Styled("a", style) + Styled("b", Italic), controller));
    VerifySource(context, 0, 2, 0, 1);
  }
  [root release];
  [pool drain];
}

void testMacAttributedTextSourceAgreement()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  {
    MacScenePlatformController controller(root, RailMetrics());
    AttributedTextNode node((AttributedTextProps(Styled("a", FontSize<12>()))));
    VerifyAttributedHeight(node, controller, root, 100);
    MacAttributedTextContext &context = AttributedContext(node);
    const TextStyle style = FontSize<12>();
    const char *bytes[] = {"\xEF\xBB\xBF"
                           "a",
                           "\xEF\xBB\xBF\xEF\xBB\xBF"
                           "a",
                           "\xEF\xBB\xBF"};
    const std::size_t sizes[] = {4, 7, 3};
    const NSUInteger lengths[] = {1, 2, 0};
    for (int i = 0; i < 3; ++i)
    {
      LOKA_VERIFY(AttributedAccess::build(context,
                                          Styled("p", style) + Styled(RawAttributed(bytes[i], sizes[i]), Italic)
                                              + Styled("n", style),
                                          controller));
      VerifySource(context, 0, 1, 0, 0);
      for (NSUInteger u = 1; u <= lengths[i]; ++u)
        VerifyNoSource(context, u, u + 1);
      if (lengths[i])
        VerifyNoSource(context, 0, lengths[i] + 2);
      VerifySource(context, lengths[i] + 1, lengths[i] + 2, 2, 2);
    }
    // Interior and doubled interior BOMs agree; rejecting all BOMs is too broad.
    LOKA_VERIFY(AttributedAccess::build(context,
                                        Styled("p", Italic)
                                            + Styled(RawAttributed("a\xEF\xBB\xBF\xEF\xBB\xBF"
                                                                   "b",
                                                                   8),
                                                     style),
                                        controller));
    VerifySource(context, 0, 1, 0, 0);
    VerifySource(context, 1, 5, 1, 1);
    LOKA_VERIFY(AttributedAccess::build(context, Styled(RawAttributed("a\0b", 3), style), controller));
    VerifySource(context, 0, 3, 0, 0);
  }
  [root release];
  [pool drain];
}

void testMacAttributedTextSourceSearchEmpties()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  {
    MacScenePlatformController controller(root, RailMetrics());
    AttributedTextNode node((AttributedTextProps(Styled("a", FontSize<12>()))));
    VerifyAttributedHeight(node, controller, root, 100);
    MacAttributedTextContext &context = AttributedContext(node);
    const std::size_t n = 40;
    AttributedString::Builder builder(3 * n + 2);
    for (std::size_t i = 0; i < 3 * n + 2; ++i)
      LOKA_VERIFY(builder.append(loka::core::String(i == n ? "a" : i == 2 * n + 1 ? "b" : ""), FontSize<12>()));
    const AttributedString value = builder.build();
    LOKA_VERIFY(AttributedAccess::build(context, value, controller));
    VerifySource(context, 0, 1, n, n);
    VerifySource(context, 1, 2, 2 * n + 1, 2 * n + 1);
    VerifySource(context, 0, 2, n, 2 * n + 1);
    AttributedString::Builder empties(40);
    for (int i = 0; i < 40; ++i)
      LOKA_VERIFY(empties.append(loka::core::String(""), FontSize<12>()));
    const AttributedString empty = empties.build();
    node.props.text(empty);
    context.onPropsApplied();
    loka::core::testing::failLokaAllocRaw("TextLineBreaker", "Table", 0);
    const int baseline = loka::core::testing::lokaAllocRawLive();
    loka::app::scene::LayoutState state;
    state.width = 100;
    node.layoutProjected(&controller, state);
    LOKA_VERIFY([[AttributedAccess::storage(AttributedAccess::leaf(root)) string] length] == 0);
    LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == baseline + 1);
    LOKA_VERIFY(AttributedAccess::complete(context));
    LOKA_VERIFY(AttributedAccess::measured(context));
    VerifyNoSource(context, 0, 0);
    VerifyNoSource(context, 0, 1);
    context.onPropsApplied();
    LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == baseline);
    loka::core::testing::allowLokaAllocRaw();
  }
  [root release];
  [pool drain];
}

void testMacAttributedTextSourceAllocationLifecycle()
{
  using namespace loka::app;
  using namespace loka::core::testing;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  const AttributedString value = ManyAttributed(loka::core::String("b"), Italic);
  for (int allocation = 1; allocation <= 2; ++allocation)
  {
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
    {
      MacScenePlatformController controller(root, RailMetrics());
      AttributedTextNode node((AttributedTextProps(value)));
      VerifyAttributedHeight(node, controller, root, 100);
      MacAttributedTextContext &context = AttributedContext(node);
      context.onPropsApplied();
      failLokaAllocRaw("TextLineBreaker", "Table", allocation);
      const int baseline = lokaAllocRawLive();
      loka::app::scene::LayoutState state;
      state.width = 100;
      node.layoutProjected(&controller, state);
      LOKA_VERIFY(state.height == 0 && [[AttributedAccess::storage(AttributedAccess::leaf(root)) string] length] == 0);
      LOKA_VERIFY(!AttributedAccess::complete(context) && !AttributedAccess::measured(context));
      LOKA_VERIFY(lokaAllocRawLive() == baseline);
      VerifyNoSource(context, 0, 1);
      node.layoutProjected(&controller, state);
      LOKA_VERIFY(AttributedAccess::complete(context));
      LOKA_VERIFY([[AttributedAccess::storage(AttributedAccess::leaf(root)) string] length] == 34);
      LOKA_VERIFY(lokaAllocRawLive() == baseline + 1); // Only retained slots survive build.
      VerifySource(context, 0, 34, 0, 33);
      context.onPropsApplied();
      LOKA_VERIFY(lokaAllocRawLive() == baseline);
      VerifyNoSource(context, 0, 1);
      node.layoutProjected(&controller, state);
      LOKA_VERIFY(lokaAllocRawLive() == baseline + 1);
      loka::app::scene::LifecycleFactTestAccess::MarkSubtreeRetired(&node);
      loka::app::scene::LifecycleFactTestAccess::DeliverFacts(&node);
      LOKA_VERIFY(lokaAllocRawLive() == baseline);
      VerifyNoSource(context, 0, 1);
      allowLokaAllocRaw();
    }
    [root release];
  }
  [pool drain];
}

void testMacAttributedTextSourceLateFailure()
{
  using namespace loka::app;
  using namespace loka::core::testing;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  const loka::core::String refused(loka::core::Managed<loka::platform::String>::Wrap(new MacRefusedAttributedString()));
  const AttributedString failures[] = {ManyAttributed(refused, Italic),
                                       ManyAttributed(RawAttributed("\xC0\xAF", 2), Italic)};
  const AttributedString good = ManyAttributed(loka::core::String("b"), Italic);
  const AttributedString disagreements[] = {ManyAttributed(RawAttributed("\xEF\xBB\xBF"
                                                                         "a",
                                                                         4),
                                                           Italic),
                                            ManyAttributed(RawAttributed("\xEF\xBB\xBF\xEF\xBB\xBF"
                                                                         "a",
                                                                         7),
                                                           Italic)};
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  {
    MacScenePlatformController controller(root, RailMetrics());
    AttributedTextNode node((AttributedTextProps(Styled("a", FontSize<12>()))));
    VerifyAttributedHeight(node, controller, root, 100);
    MacAttributedTextContext &context = AttributedContext(node);
    context.onPropsApplied();
    failLokaAllocRaw("TextLineBreaker", "Table", 0);
    const int baseline = lokaAllocRawLive();
    for (int i = 0; i < 2; ++i)
    {
      // No layout clear can mask build's cleanup: inspect immediately.
      LOKA_VERIFY(!AttributedAccess::build(context, failures[i], controller));
      LOKA_VERIFY(!AttributedAccess::complete(context));
      LOKA_VERIFY(!AttributedAccess::measured(context));
      LOKA_VERIFY(lokaAllocRawLive() == baseline);
      VerifyNoSource(context, 0, 1);
      LOKA_VERIFY(AttributedAccess::build(context, good, controller));
      LOKA_VERIFY(lokaAllocRawLive() == baseline + 1);
      VerifySource(context, 0, 34, 0, 33);
      LOKA_VERIFY(AttributedAccess::build(context, disagreements[i], controller));
      VerifySource(context, 0, 33, 0, 32);
      VerifyNoSource(context, 33, 34);
      if (i == 1)
        VerifyNoSource(context, 34, 35);
      LOKA_VERIFY(lokaAllocRawLive() == baseline + 1);
    }
    context.onPropsApplied();
    LOKA_VERIFY(lokaAllocRawLive() == baseline);
    allowLokaAllocRaw();
  }
  [root release];
  [pool drain];
}

#include <objc/runtime.h>
#include <objc/message.h>
#include "app/nodes/boundary/StdComposition.hpp"
#include "core/util/StateTrackerGuard.hpp"

// Plain Text measures its cell. The attributed instrument below counts only the install interval.
// Install on that class only (even when the SDK inherits it from NSCell).
static unsigned gMac970Measurements = 0;
static NSLineBreakMode gMacMeasuredLineBreakMode = NSLineBreakByClipping;
@interface NSTextFieldCell (Loka970MeasurementCounter)
- (NSSize)loka970_cellSizeForBounds:(NSRect)bounds;
@end
@implementation NSTextFieldCell (Loka970MeasurementCounter)
- (NSSize)loka970_cellSizeForBounds:(NSRect)bounds
{
  ++gMac970Measurements;
  gMacMeasuredLineBreakMode = [self lineBreakMode];
  return [self loka970_cellSizeForBounds:bounds];
}
@end

namespace
{
  unsigned richForces, richClears, richInstalls, sizeWrites, displayRequests;
  NSView *countedLeaf;
  NSTextContainer *countedContainer;
  NSLayoutManager *installingManager;
  IMP installImpl, forceImpl, sizeImpl, displayImpl;
  BOOL CountInstall(
      id self, SEL cmd, NSAttributedString *value, const loka::app::AttributedString *snapshot, CGFloat *extent)
  {
    countedLeaf = self;
    countedContainer = AttributedAccess::container(self);
    if (value)
      ++richInstalls;
    else
      ++richClears;
    installingManager = AttributedAccess::manager(self);
    BOOL result = ((BOOL(*)(id, SEL, NSAttributedString *, const loka::app::AttributedString *, CGFloat *))installImpl)(
        self, cmd, value, snapshot, extent);
    installingManager = nil;
    return result;
  }
  NSRange CountForce(id self, SEL cmd, NSTextContainer *container)
  {
    if (self == installingManager)
      ++richForces;
    return ((NSRange(*)(id, SEL, NSTextContainer *))forceImpl)(self, cmd, container);
  }
  void CountSize(id self, SEL cmd, NSSize size)
  {
    if (self == countedContainer)
      ++sizeWrites;
    ((void (*)(id, SEL, NSSize))sizeImpl)(self, cmd, size);
  }
  void CountDisplay(id self, SEL cmd, BOOL flag)
  {
    if (self == countedLeaf && flag)
      ++displayRequests;
    ((void (*)(id, SEL, BOOL))displayImpl)(self, cmd, flag);
  }

  class MacMeasurementCounter
  {
  public:
    MacMeasurementCounter()
        : install_(prepare(NSClassFromString(@"LokaAttributedTextView"),
                           sel_registerName("installValue:snapshot:extent:"),
                           installImpl),
                   (IMP)CountInstall,
                   NSClassFromString(@"LokaAttributedTextView")),
          force_(prepare([NSLayoutManager class], @selector(glyphRangeForTextContainer:), forceImpl),
                 (IMP)CountForce,
                 [NSLayoutManager class]),
          size_(prepare([NSTextContainer class], @selector(setContainerSize:), sizeImpl),
                (IMP)CountSize,
                [NSTextContainer class]),
          display_(prepare(NSClassFromString(@"LokaAttributedTextView"), @selector(setNeedsDisplay:), displayImpl),
                   (IMP)CountDisplay,
                   NSClassFromString(@"LokaAttributedTextView"))
    {
      richForces = richClears = richInstalls = sizeWrites = displayRequests = 0;
      countedLeaf = nil;
      countedContainer = nil;
      Class cell = [NSTextFieldCell class];
      Method inherited = class_getInstanceMethod(cell, @selector(cellSizeForBounds:));
      LOKA_VERIFY(inherited != 0);
      class_addMethod(cell, @selector(cellSizeForBounds:), method_getImplementation(inherited),
                      method_getTypeEncoding(inherited));
      this->original_ = class_getInstanceMethod(cell, @selector(cellSizeForBounds:));
      this->counting_ = class_getInstanceMethod(cell, @selector(loka970_cellSizeForBounds:));
      LOKA_VERIFY(this->original_ && this->counting_);
      method_exchangeImplementations(this->original_, this->counting_);
      gMac970Measurements = 0;
    }
    ~MacMeasurementCounter()
    {
      method_exchangeImplementations(this->original_, this->counting_);
    }
    unsigned calls(bool rich = false) const
    {
      return rich ? richForces : gMac970Measurements;
    }

  private:
    static SEL prepare(Class cls, SEL selector, IMP &original)
    {
      original = class_getMethodImplementation(cls, selector);
      return selector;
    }
    MacNativeRefusal install_, force_, size_, display_;
    Method original_;
    Method counting_;
    MacMeasurementCounter(const MacMeasurementCounter &);
    MacMeasurementCounter &operator=(const MacMeasurementCounter &);
  };

  NSAttributedString *LeafSnapshot(const loka::app::TextNode &, NSView *view)
  {
    return [(NSTextField *)view attributedStringValue];
  }
  NSAttributedString *LeafSnapshot(const loka::app::AttributedTextNode &node, NSView *)
  {
    return AttributedAccess::projectionSnapshot(node);
  }
  NSString *PresentedText(NSView *view, bool attributed)
  {
    return attributed ? [AttributedAccess::storage(view) string] : [(NSTextField *)view stringValue];
  }
  template <class Leaf>
  void VerifyMacMeasurementReuse(Leaf &node, bool attributed)
  {
    using namespace loka::app;
    using namespace loka::app::scene;
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 400)];
    {
      MacScenePlatformController controller((void *)root, loka::macos::DefaultRailMetrics());
      MacMeasurementCounter counter;
      LayoutState state;
      state.width = 101;
      NotifySubtreeNodeAttached(&node);
      node.layoutProjected(&controller, state);
      LOKA_VERIFY([[root subviews] count] == 1);
      NSView *field = [[root subviews] objectAtIndex:0];
      LOKA_VERIFY(counter.calls(attributed) == 1); // Positive control: swizzle must observe the real call.
      const short height = state.height;
      LOKA_VERIFY(height > 0);
      NSAttributedString *before = [LeafSnapshot(node, field) retain];
      state.x = 4; // Same endpoint phase at spaceScale 5/4.
      state.y = 23;
      state.height = 999;
      // Bypass ensure's plain refresh: only layout's placement can satisfy this pin.
      const short advance = node.layout(&controller, state);
      LOKA_VERIFY(counter.calls(attributed) == 1);
      LOKA_VERIFY(state.height == height);
      LOKA_VERIFY(advance == state.y + height);
      LOKA_VERIFY(NSEqualRects([field frame], controller.projection().projectFrame(
          loka::core::Frame(4, 23, 101, height)).r));
      if (attributed)
        LOKA_VERIFY(LeafSnapshot(node, field) == before);
      [before release];
      state.x = 1;
      LOKA_VERIFY(controller.projection().projectLength(0, 101).pt == 126);
      LOKA_VERIFY(controller.projection().projectLength(1, 102).pt == 127);
      unsigned calls = counter.calls(attributed);
      node.layout(&controller, state);
      LOKA_VERIFY(counter.calls(attributed) == calls + (attributed ? 1 : 0));
      state.width = 80;
      calls = counter.calls(attributed);
      node.layout(&controller, state);
      LOKA_VERIFY(counter.calls(attributed) == calls + 1);
      node.layout(&controller, state);
      LOKA_VERIFY(counter.calls(attributed) == calls + 1);
      NotifySubtreeNodeDetached(&node);
      LifecycleFactTestAccess::DeliverFacts(&node);
      LOKA_VERIFY([field isHidden]);
      before = [LeafSnapshot(node, field) retain];
      NotifySubtreeNodeAttached(&node);
      LifecycleFactTestAccess::DeliverFacts(&node);
      LOKA_VERIFY(![field isHidden]);
      LOKA_VERIFY([LeafSnapshot(node, field) isEqualToAttributedString:before]);
      [before release];
      calls = counter.calls(attributed);
      node.layout(&controller, state);
      LOKA_VERIFY(counter.calls(attributed) == calls + 1);
      node.layout(&controller, state);
      LOKA_VERIFY(counter.calls(attributed) == calls + 1);
      if (!attributed)
      {
        NSTextField *plain = (NSTextField *)field;
        // Native presentation drift must be reconciled on a measurement hit.
        [plain setAlignment:LOKA_MAC_TEXT_ALIGNMENT_RIGHT];
        [[plain cell] setWraps:NO];
        [[plain cell] setScrollable:YES];
        [[plain cell] setLineBreakMode:NSLineBreakByClipping];
        [[plain cell] setFont:[NSFont systemFontOfSize:30]];
        node.layout(&controller, state);
        LOKA_VERIFY(counter.calls(attributed) == calls + 1);
        LOKA_VERIFY([plain alignment] == LOKA_MAC_TEXT_ALIGNMENT_LEFT);
        LOKA_VERIFY([[plain cell] wraps] && ![[plain cell] isScrollable]);
        LOKA_VERIFY([[plain cell] lineBreakMode] == NSLineBreakByWordWrapping);
        LOKA_VERIFY([[[plain cell] font] isEqual:(NSFont *)controller.textFont(FontSize<12>())]);
      }
      state.width = 0;
      node.layout(&controller, state);
      calls = counter.calls(attributed);
      node.layout(&controller, state);
      LOKA_VERIFY(counter.calls(attributed) == calls);
      // Node is caller-owned and must retire before this controller drains.
      LifecycleFactTestAccess::MarkSubtreeRetired(&node);
      LifecycleFactTestAccess::DeliverFacts(&node);
      LOKA_VERIFY([[root subviews] count] == 0);
      LOKA_VERIFY([PresentedText(field, attributed) length] == 0);
      node.setContext(0);
    }
    [root release];
  }

  class MacMeasurementOwner : public loka::app::scene::BoundaryNodeFor<MacMeasurementOwner>
  {
  public:
    explicit MacMeasurementOwner(const loka::app::scene::BoundaryPropsFor<MacMeasurementOwner> &props =
                                     loka::app::scene::BoundaryPropsFor<MacMeasurementOwner>())
        : loka::app::scene::BoundaryNodeFor<MacMeasurementOwner>(props) {}
    virtual void composeNode(loka::app::scene::NodeComposition &) {}
  };

  // A real CollectUtf8 refusal, with no production hook. Changing availability
  // models recovery of the same input; no props/State mark rescues the retry.
  class MacRetryString : public loka::platform::String
  {
  public:
    MacRetryString() : available_(true), attempts_(0) {}
    void available(bool value) { this->available_ = value; }
    unsigned attempts() const { return this->attempts_; }
    virtual bool appendUtf8(std::string &out) const
    {
      ++this->attempts_;
      if (!this->available_)
      {
        // Refusal may leave partial output; callers must honor the result.
        out.append("partial");
        return false;
      }
      out.append("retry the same wrapped text");
      return true;
    }
  private:
    bool available_;
    mutable unsigned attempts_;
  };

  template <class Leaf> void VerifyMacMeasurementRefusal(Leaf &node, MacRetryString &source, bool attributed)
  {
    using namespace loka::app;
    using namespace loka::app::scene;
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
    {
      MacScenePlatformController controller((void *)root, RailMetrics());
      MacMeasurementCounter counter;
      LayoutState state;
      state.width = 100;
      node.layoutProjected(&controller, state);
      LOKA_VERIFY([[root subviews] count] == 1);
      NSView *field = [[root subviews] objectAtIndex:0];
      LOKA_VERIFY(counter.calls(attributed) == 1 && state.height > 0);
      source.available(false);
      state.width = 90; // Force a miss without mutating the logical input.
      const short refusedAdvance = node.layout(&controller, state);
      LOKA_VERIFY(refusedAdvance == state.y);
      LOKA_VERIFY(state.height == 0 && [PresentedText(field, attributed) length] == 0);
      LOKA_VERIFY(NSIsEmptyRect([field frame]));
      const unsigned refusedAttempts = source.attempts();
      node.layout(&controller, state);
      LOKA_VERIFY(source.attempts() > refusedAttempts);
      LOKA_VERIFY(counter.calls(attributed) == 1);
      source.available(true);
      node.layout(&controller, state);
      LOKA_VERIFY(counter.calls(attributed) == 2 && state.height > 0);
      LOKA_VERIFY([PresentedText(field, attributed) isEqualToString:@"retry the same wrapped text"]);
      LOKA_VERIFY(!NSIsEmptyRect([field frame]));
      node.layout(&controller, state);
      LOKA_VERIFY(counter.calls(attributed) == 2);
      // Establish the controller's root, then retry only through its native queue.
      controller.onChange(&node, NODE_DIRTY_LAYOUT, false);
      const unsigned beforeRetry = counter.calls(attributed);
      node.requeueLayoutInputs(NODE_DIRTY_PROPS);
      source.available(false);
      controller.relayout(0, 0);
      LOKA_VERIFY(controller.hasPendingRelayout());
      const unsigned beforeFlush = source.attempts();
      MacScenePlatformController::flushPendingRelayouts();
      LOKA_VERIFY(source.attempts() > beforeFlush);
      LOKA_VERIFY(controller.hasPendingRelayout());
      LOKA_VERIFY(counter.calls(attributed) == beforeRetry);
      source.available(true);
      MacScenePlatformController::flushPendingRelayouts();
      LOKA_VERIFY(!controller.hasPendingRelayout());
      LOKA_VERIFY(counter.calls(attributed) == beforeRetry + 1);
      LOKA_VERIFY([PresentedText(field, attributed) isEqualToString:@"retry the same wrapped text"]);
      LOKA_VERIFY(!NSIsEmptyRect([field frame]));
      controller.relayout(0, 0);
      LOKA_VERIFY(counter.calls(attributed) == beforeRetry + 1);
      LifecycleFactTestAccess::MarkSubtreeRetired(&node);
      LifecycleFactTestAccess::DeliverFacts(&node);
      node.setContext(0);
    }
    [root release];
  }
}

namespace
{
  void VerifyMacPlainTextWrapModes()
  {
    using namespace loka::app;
    using namespace loka::app::scene;
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 400)];
    {
      MacScenePlatformController controller((void *)root, RailMetrics());
      MacMeasurementCounter counter;
      TextNode node((Text("abcdefgh") + FontSize<12>()
                     + BlockStyle().wrap(TEXT_WRAP_CHAR)).props);
      node.setPropsTypeId(TextProps::staticTypeId());
      NSFont *font = (NSFont *)controller.textFont(FontSize<12>());
      // Include the cell's horizontal inset when choosing a four-glyph width.
      NSTextFieldCell *reference = [[[NSTextFieldCell alloc] initTextCell:@"abcd"] autorelease];
      [reference setFont:font];
      LayoutState state;
      state.width = controller.projection().measurementToLu([reference cellSize].width);
      NotifySubtreeNodeAttached(&node);
      const unsigned before = counter.calls();
      node.layoutProjected(&controller, state);
      LOKA_VERIFY([[root subviews] count] == 1);
      NSTextField *field = (NSTextField *)[[root subviews] objectAtIndex:0];
      const unsigned calls = counter.calls();
      std::printf("#45 CHAR: width=%d height=%d measureMode=%ld paintMode=%ld calls=%u\n",
                  state.width, state.height, (long)gMacMeasuredLineBreakMode,
                  (long)[[field cell] lineBreakMode], calls);
      LOKA_VERIFY(calls == before + 1);
      LOKA_VERIFY(gMacMeasuredLineBreakMode == NSLineBreakByCharWrapping);
      LOKA_VERIFY([[field cell] lineBreakMode] == NSLineBreakByCharWrapping);
      [reference setStringValue:@"abcd\nefgh"];
      [reference setWraps:YES];
      [reference setScrollable:NO];
      [reference setLineBreakMode:NSLineBreakByCharWrapping];
      const int twoLineHeight = controller.projection().measurementToLu(
          [reference cellSizeForBounds:NSMakeRect(0, 0, 400, CGFLOAT_MAX)].height) + 2;
      LOKA_VERIFY(state.height == twoLineHeight);
      // Native oracle also accommodates AppKit's overlong-word rule. Compare
      // each mode with the corresponding actual cell at the allocated width.
      const TextWrap wraps[] = {TEXT_WRAP_CHAR, TEXT_WRAP_WORD, TEXT_WRAP_CHAR};
      for (int i = 0; i < 3; ++i)
      {
        LOKA_VERIFY((Text("abcdefgh") + FontSize<12>() + BlockStyle().wrap(wraps[i]))
                        .applyPropsToNode(&node));
        node.layout(&controller, state);
        const NSLineBreakMode expected = wraps[i] == TEXT_WRAP_CHAR
            ? NSLineBreakByCharWrapping : NSLineBreakByWordWrapping;
        LOKA_VERIFY(gMacMeasuredLineBreakMode == expected);
        LOKA_VERIFY([[field cell] lineBreakMode] == expected);
        [reference setStringValue:@"abcdefgh"];
        [reference setWraps:YES];
        [reference setScrollable:NO];
        [reference setLineBreakMode:expected];
        const NSRect bounds = NSMakeRect(0, 0, controller.projection().projectLength(0, state.width).pt,
                                         CGFLOAT_MAX);
        const int measured = controller.projection().measurementToLu(
            [reference cellSizeForBounds:bounds].height) + 2;
        LOKA_VERIFY(state.height == (measured > 20 ? measured : 20));
        std::printf("#45 mode=%ld nativeHeight=%d reservedHeight=%d\n",
                    (long)expected, measured, state.height);
      }
      LifecycleFactTestAccess::MarkSubtreeRetired(&node);
      LifecycleFactTestAccess::DeliverFacts(&node);
      node.setContext(0);
    }
    [root release];
  }
}

void testMacPlainTextMeasurementReuse()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  TextNode node((Text("words that wrap over several lines") + FontSize<12>()
                 + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
  VerifyMacMeasurementReuse(node, false);
  VerifyMacPlainTextWrapModes();
  [pool drain];
}

void testMacAttributedTextMeasurementReuse()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  AttributedTextNode node((AttributedText(Styled("words that wrap over several lines", FontSize<12>()))
                           + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
  VerifyMacMeasurementReuse(node, true);
  [pool drain];
}

void testMacTextMeasurementRefusalRecovery()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  MacRetryString *source = new MacRetryString();
  const loka::core::String value(loka::core::Managed<loka::platform::String>::Wrap(source));
  TextNode plain((Text(value) + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
  VerifyMacMeasurementRefusal(plain, *source, false);
  AttributedTextNode rich((AttributedText(Styled(value, FontSize<12>()))
                           + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
  VerifyMacMeasurementRefusal(rich, *source, true);
  [pool drain];
}

void testMacTextMeasurementInputPublications()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  {
    MacScenePlatformController controller((void *)root, RailMetrics());
    MutableState<String> content(String::Literal("before wrapped words"));
    MutableState<TextStyle> style((FontSize<12>()));
    MutableState<AttributedString> rich(Styled("before wrapped words", FontSize<12>()));
    PushStateTracker tracker;
    tracker.addState(&content);
    tracker.addState(&style);
    tracker.addState(&rich);
    MacMeasurementOwner owner;
    TextNode plain(((Text(&content) + &style) + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
    AttributedTextNode attributed((AttributedText(&rich) + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
    plain.setPropsTypeId(TextProps::staticTypeId());
    attributed.setPropsTypeId(AttributedTextProps::staticTypeId());
    BoundaryNode::declareBoundaryDirtySources(&plain, &owner);
    BoundaryNode::declareBoundaryDirtySources(&attributed, &owner);
    MacMeasurementCounter counter;
    LayoutState state;
    state.width = 100;
    plain.layoutProjected(&controller, state);
    attributed.layoutProjected(&controller, state);
    LOKA_VERIFY(counter.calls() == 1 && counter.calls(true) == 1);
    NSTextField *plainField = (NSTextField *)[[root subviews] objectAtIndex:0];
    NSView *richField = AttributedAccess::view(AttributedContext(attributed));
    LOKA_VERIFY([[root subviews] objectAtIndex:1] == richField);
    {
      StateTrackerGuard guard(&tracker);
      content.set(String::Literal("after wrapped words"));
      rich.set(Styled("after wrapped words", FontSize<12>()));
    }
    plain.layout(&controller, state);
    attributed.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 2 && counter.calls(true) == 2);
    LOKA_VERIFY([[plainField stringValue] isEqualToString:@"after wrapped words"]);
    LOKA_VERIFY([[AttributedAccess::storage(richField) string] isEqualToString:@"after wrapped words"]);
    {
      StateTrackerGuard guard(&tracker);
      style.set(FontSize<24>());
      rich.set(Styled("after wrapped words", FontSize<24>()));
    }
    plain.layout(&controller, state);
    attributed.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 3 && counter.calls(true) == 3);
    LOKA_VERIFY(((Text(&content) + &style) + BlockStyle().wrap(TEXT_WRAP_WORD).align(TEXT_ALIGN_RIGHT))
                    .applyPropsToNode(&plain));
    LOKA_VERIFY((AttributedText(&rich) + BlockStyle().wrap(TEXT_WRAP_WORD).align(TEXT_ALIGN_RIGHT))
                    .applyPropsToNode(&attributed));
    plain.layout(&controller, state);
    attributed.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 4 && counter.calls(true) == 4);
    LOKA_VERIFY([plainField alignment] == LOKA_MAC_TEXT_ALIGNMENT_RIGHT);
    NSParagraphStyle *paragraph = TextAttribute(AttributedAccess::storage(richField), NSParagraphStyleAttributeName, 0);
    LOKA_VERIFY([paragraph alignment] == LOKA_MAC_TEXT_ALIGNMENT_RIGHT);
    plain.layout(&controller, state);
    attributed.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 4 && counter.calls(true) == 4);
    // Retained content/source/style replacement and style removal all mark.
    LOKA_VERIFY((Text("replacement words") + FontSize<12>() + BlockStyle().wrap(TEXT_WRAP_WORD))
                    .applyPropsToNode(&plain));
    LOKA_VERIFY((AttributedText(Styled("replacement words", FontSize<12>()))
                + BlockStyle().wrap(TEXT_WRAP_WORD)).applyPropsToNode(&attributed));
    plain.layout(&controller, state);
    attributed.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 5 && counter.calls(true) == 5);
    LOKA_VERIFY([[plainField stringValue] isEqualToString:@"replacement words"]);
    LOKA_VERIFY([[AttributedAccess::storage(richField) string] isEqualToString:@"replacement words"]);
    LOKA_VERIFY(Text("unstyled").applyPropsToNode(&plain));
    plain.layout(&controller, state);
    LOKA_VERIFY([plainField alignment] == LOKA_MAC_TEXT_ALIGNMENT_NATURAL);
    LOKA_VERIFY(state.height == layout::FallbackControlMetrics::kTextHeight);
    LOKA_VERIFY((Text("activated words") + FontSize<24>() + BlockStyle().wrap(TEXT_WRAP_WORD))
                    .applyPropsToNode(&plain));
    plain.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 6 && counter.calls(true) == 5);
    // Parked publication plus reattach: table remains, but return consumes the mark.
    NotifySubtreeNodeDetached(&attributed);
    LifecycleFactTestAccess::DeliverFacts(&attributed);
    LOKA_VERIFY([richField isHidden]);
    LOKA_VERIFY((AttributedText(&rich) + BlockStyle().wrap(TEXT_WRAP_WORD)).applyPropsToNode(&attributed));
    {
      StateTrackerGuard guard(&tracker);
      rich.set(Styled("changed while parked", FontSize<12>()));
    }
    NotifySubtreeNodeAttached(&attributed);
    LifecycleFactTestAccess::DeliverFacts(&attributed);
    attributed.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 6 && counter.calls(true) == 6);
    LOKA_VERIFY([[AttributedAccess::storage(richField) string] isEqualToString:@"changed while parked"]);
    attributed.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 6 && counter.calls(true) == 6);
    LOKA_VERIFY(((Text(&content) + &style) + BlockStyle().wrap(TEXT_WRAP_WORD)).applyPropsToNode(&plain));
    plain.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 7 && counter.calls(true) == 6);
    NotifySubtreeNodeDetached(&plain);
    LifecycleFactTestAccess::DeliverFacts(&plain);
    LOKA_VERIFY([plainField isHidden]);
    {
      StateTrackerGuard guard(&tracker);
      content.set(String::Literal("plain changed while parked"));
    }
    NotifySubtreeNodeAttached(&plain);
    LifecycleFactTestAccess::DeliverFacts(&plain);
    plain.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 8 && counter.calls(true) == 6);
    LOKA_VERIFY([[plainField stringValue] isEqualToString:@"plain changed while parked"]);
    plain.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 8 && counter.calls(true) == 6);
    owner.clearObservedStateEntries();
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
}

#include "app/scene/ability/CapturableBitmap.hpp"
#include "core/resource/Image.hpp"

void testMacCaptureRefusalReleasesBitmapOnce()
{
  NSAutoreleasePool *outer = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  LOKA_VERIFY(root != nil);
  {
    MacPlatformContext owner;
    struct CaptureConfig : AppConfigurable
    {
      explicit CaptureConfig(PlatformContext &context) : AppConfigurable(&context) {}
      virtual void compose(AppComposition &) {}
    } config(owner);
    MacApp app(&config);
    typedef loka::app::testing::NativeResourceRetirementTestAccess Access;
    MacScenePlatformController controller((void *)root, loka::app::RailMetrics(), &owner);
    loka::app::ButtonProps buttonProps;
    loka::app::ButtonNode button(buttonProps);
    loka::app::TextProps textProps;
    loka::app::TextNode text(textProps);
    loka::app::scene::LayoutState state;
    state.x = 10;
    state.y = 20;
    state.width = 100;
    state.height = 30;
    LOKA_VERIFY(controller.prepareProjectedLayout(&button, state));
    state.y = 60;
    LOKA_VERIFY(controller.prepareProjectedLayout(&text, state));
    loka::app::scene::NodeContext *const contexts[] = {button.getContext(), text.getContext()};
    for (int i = 0; i < 2; ++i)
    {
      LOKA_VERIFY(contexts[i]);
      const loka::app::scene::ICapturableBitmap *capturable = contexts[i]->asCapturableBitmap();
      LOKA_VERIFY(capturable);
      // Mutations: either control keeps inline release; consuming publication
      // double-releases on Record/ControlBlock refusal; inner pool consumes the
      // ticket's retain. The outer pool remains alive throughout retirement.
      const char *owners[] = {"Image", "Managed"};
      const char *types[] = {"Record", "ControlBlock"};
      for (int failure = 0; failure != 2; ++failure)
      {
        int released = 0;
        NSAutoreleasePool *inner = [[NSAutoreleasePool alloc] init];
        loka::core::testing::failLokaAllocRaw(owners[failure], types[failure], 1);
        loka::core::resource::Image image;
        LOKA_VERIFY(!capturable->captureBitmap(image));
        LOKA_VERIFY(!image.isValid());
        LOKA_VERIFY(Access::held(owner) == 0 && Access::queued(owner) == 1);
        observeMacNativeDeallocation(Access::queuedHandle(owner), released);
        [inner drain];
        LOKA_VERIFY(released == 0);
        app.flushInvalidationsTick();
        LOKA_VERIFY(released == 1);
        LOKA_VERIFY(Access::queued(owner) == 0 && Access::inFlight(owner) == 0);
        app.flushInvalidationsTick();
        LOKA_VERIFY(released == 1);
        LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
        loka::core::testing::allowLokaAllocRaw();
      }
      {
        int released = 0;
        NSAutoreleasePool *inner = [[NSAutoreleasePool alloc] init];
        loka::core::resource::Image image;
        LOKA_VERIFY(capturable->captureBitmap(image));
        observeMacNativeDeallocation(image.nativeHandle(), released);
        [inner drain];
        image = loka::core::resource::Image();
        LOKA_VERIFY(released == 0 && Access::queued(owner) == 1);
        app.flushInvalidationsTick();
        LOKA_VERIFY(released == 1);
      }
    }
  }
  [root release];
  [outer drain];
  std::printf("==== [testMacCaptureRefusalReleasesBitmapOnce] PASSED ====\n");
}

void testMacSmallLabelUsesTheSmallSystemFont()
{
  std::printf("\n==== [testMacSmallLabelUsesTheSmallSystemFont] start ====\n");
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  LOKA_VERIFY(root != nil);
  {
    // The rail's default metrics have a non-unit fontScale (13/12); the role,
    // like the unset size, is a system size that fontScale does not touch.
    MacScenePlatformController controller((void *)root, loka::macos::DefaultRailMetrics());
    NSFont *body = (NSFont *)controller.textFont(TextStyle());
    NSFont *small = (NSFont *)controller.textFont(SmallLabel);
    LOKA_VERIFY(small != nil && small != body);
    LOKA_VERIFY([small pointSize] == [NSFont smallSystemFontSize]);
    LOKA_VERIFY([[small familyName] isEqualToString:[body familyName]]);
    // Weight and the fixed-pitch column compose with the role on its own row.
    NSFont *smallBold = (NSFont *)controller.textFont(SmallLabel + Bold);
    LOKA_VERIFY(smallBold != nil && smallBold != small);
    LOKA_VERIFY([smallBold pointSize] == [NSFont smallSystemFontSize]);
    LOKA_VERIFY(([[NSFontManager sharedFontManager] traitsOfFont:smallBold] & NSBoldFontMask) != 0);
    // A declared normal weight is a declaration, not bold: it reads the plain slot.
    LOKA_VERIFY(controller.textFont(SmallLabel + TextStyle().weight(TEXT_WEIGHT_NORMAL)) == small);
    NSFont *smallFixed = (NSFont *)controller.textFont(SmallLabel + Monospace);
    LOKA_VERIFY(smallFixed != nil && [smallFixed isFixedPitch]);
    LOKA_VERIFY([smallFixed pointSize] == [NSFont smallSystemFontSize]);
  }
  [root release];
  [pool drain];
  std::printf("==== [testMacSmallLabelUsesTheSmallSystemFont] PASSED ====\n");
}

#include "app/nodes/controls/Ribbon.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include <climits>
#include <limits>

namespace
{
  short VerifyMacTextNaturalWidth(loka::app::TextNode &node, MacScenePlatformController &controller, NSView *root)
  {
    short width = 0;
    LOKA_VERIFY(controller.queryNaturalWidth(&node, width));
    loka::app::scene::LayoutState state;
    state.width = width;
    node.layoutProjected(&controller, state);
    LOKA_VERIFY([[root subviews] count] == 1);
    NSTextField *field = (NSTextField *)[[root subviews] objectAtIndex:0];
    LOKA_VERIFY([field isKindOfClass:[NSTextField class]]);
    // The presented context is the independent reference, including its font
    // and cell insets. No controller answer or measurement hook is overridden.
    const NSSize measured = [[field cell] cellSizeForBounds:NSMakeRect(0, 0, CGFLOAT_MAX, CGFLOAT_MAX)];
    LOKA_VERIFY(width == controller.projection().measurementToLu(measured.width));
    return width;
  }
} // namespace

void testMacTextNaturalWidthMatchesLayout()
{
  using namespace loka::app;
  using namespace loka::core;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  const RailMetrics metrics[] = {RailMetrics(), loka::macos::DefaultRailMetrics()};
  for (int scale = 0; scale < 2; ++scale)
  {
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 640, 160)];
    {
      MacScenePlatformController controller(root, metrics[scale]);
      short body = 0, small = 0;
      {
        TextNode node(Text("Alignment settings").props);
        body = VerifyMacTextNaturalWidth(node, controller, root);
      }
      {
        TextNode node((Text("Alignment settings") + SmallLabel).props);
        small = VerifyMacTextNaturalWidth(node, controller, root);
      }
      {
        TextNode node((Text("Alignment settings") + SmallLabel + Bold).props);
        LOKA_VERIFY(VerifyMacTextNaturalWidth(node, controller, root) > small);
      }
      LOKA_VERIFY(small < body);
      MutableState<String> text(String::Literal("Alignment settings"));
      MutableState<TextStyle> style(SmallLabel);
      PushStateTracker tracker;
      tracker.addState(&text);
      tracker.addState(&style);
      TextNode live((Text(&text) + &style).props);
      short before = 0, after = 0;
      LOKA_VERIFY(controller.queryNaturalWidth(&live, before));
      LOKA_VERIFY(before == small);
      {
        StateTrackerGuard guard(&tracker);
        style.set(SmallLabel + Bold);
      }
      LOKA_VERIFY(controller.queryNaturalWidth(&live, after));
      LOKA_VERIFY(after > before);
      before = after;
      {
        StateTrackerGuard guard(&tracker);
        text.set(String::Literal("Alignment settings and additional controls"));
      }
      LOKA_VERIFY(controller.queryNaturalWidth(&live, after));
      LOKA_VERIFY(after > before);
      LOKA_VERIFY(VerifyMacTextNaturalWidth(live, controller, root) == after);
    }
    LOKA_VERIFY([[root subviews] count] == 0);
    [root release];
  }
  [pool drain];
}

namespace
{
  NSSize MacZeroTextMeasurement(id, SEL, NSRect)
  {
    return NSMakeSize(0, 20);
  }

  NSSize MacNegativeTextMeasurement(id, SEL, NSRect)
  {
    return NSMakeSize(-1, 20);
  }

  NSSize MacInfiniteTextMeasurement(id, SEL, NSRect)
  {
    return NSMakeSize(HUGE_VAL, 20);
  }

  NSSize MacNanTextMeasurement(id, SEL, NSRect)
  {
    return NSMakeSize(std::numeric_limits<CGFloat>::quiet_NaN(), 20);
  }

  class MacInvalidUtf8String : public loka::platform::String
  {
  public:
    virtual bool appendUtf8(std::string &out) const
    {
      out.append(1, static_cast<char>(0xff));
      return true;
    }
  };

  NSFont *MacMissingTextFont(id, SEL)
  {
    return nil;
  }

  NSFont *MacMissingSystemFont(id, SEL, CGFloat)
  {
    return nil;
  }
} // namespace

void testMacTextNaturalWidthDeclines()
{
  using namespace loka::app;
  using namespace loka::core;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 640, 160)];
  {
    MacScenePlatformController controller(root, RailMetrics());
    short width = 17;
    TextNode absent((TextProps()));
    LOKA_VERIFY(!controller.queryNaturalWidth(&absent, width));
    LOKA_VERIFY(width == 17);
    const char *values[] = {"", "first\nsecond", "first\rsecond"};
    for (int i = 0; i < 3; ++i)
    {
      TextNode node(Text(values[i]).props);
      LOKA_VERIFY(!controller.queryNaturalWidth(&node, width));
      LOKA_VERIFY(width == 17);
    }
    const String invalidUtf8(Managed<loka::platform::String>::Wrap(new MacInvalidUtf8String()));
    TextNode invalid(Text(invalidUtf8).props);
    LOKA_VERIFY(!controller.queryNaturalWidth(&invalid, width));
    LOKA_VERIFY(width == 17);
    TextNode oversized(Text(String(std::string(SHRT_MAX + 1, 'W'))).props);
    LOKA_VERIFY(!controller.queryNaturalWidth(&oversized, width));
    LOKA_VERIFY(width == 17);
    MacRetryString *source = new MacRetryString();
    const String value(Managed<loka::platform::String>::Wrap(source));
    TextNode retry(Text(value).props);
    LOKA_VERIFY(controller.queryNaturalWidth(&retry, width));
    const short good = width;
    source->available(false);
    width = 17;
    LOKA_VERIFY(!controller.queryNaturalWidth(&retry, width));
    LOKA_VERIFY(width == 17);
    source->available(true);
    LOKA_VERIFY(controller.queryNaturalWidth(&retry, width));
    LOKA_VERIFY(width == good);
    const IMP measurements[] = {reinterpret_cast<IMP>(&MacZeroTextMeasurement),
                                reinterpret_cast<IMP>(&MacNegativeTextMeasurement),
                                reinterpret_cast<IMP>(&MacInfiniteTextMeasurement),
                                reinterpret_cast<IMP>(&MacNanTextMeasurement)};
    for (int i = 0; i < 4; ++i)
    {
      MacNativeRefusal refusal(@selector(cellSizeForBounds:), measurements[i]);
      width = 17;
      LOKA_VERIFY(!controller.queryNaturalWidth(&retry, width));
      LOKA_VERIFY(width == 17);
    }
    {
      MacNativeRefusal refusal(@selector(font), reinterpret_cast<IMP>(&MacMissingTextFont));
      width = 17;
      LOKA_VERIFY(!controller.queryNaturalWidth(&retry, width));
      LOKA_VERIFY(width == 17);
    }
    LOKA_VERIFY(controller.queryNaturalWidth(&retry, width));
    LOKA_VERIFY(width == good);
  }
  MacScenePlatformController *missingFont = 0;
  {
    MacNativeRefusal refusal(
        @selector(systemFontOfSize:), reinterpret_cast<IMP>(&MacMissingSystemFont), object_getClass([NSFont class]));
    missingFont = new MacScenePlatformController(root, RailMetrics());
  }
  // Native defaults work again, but this controller captured a refused styled
  // font. The query must not substitute the fresh label's default for it.
  TextNode styled((Text("Alignment") + SmallLabel).props);
  TextNode unstyled(Text("Alignment").props);
  short width = 17;
  LOKA_VERIFY(missingFont->textFont(SmallLabel) == 0);
  LOKA_VERIFY(!missingFont->queryNaturalWidth(&styled, width));
  LOKA_VERIFY(width == 17);
  LOKA_VERIFY(missingFont->queryNaturalWidth(&unstyled, width));
  delete missingFont;
  [root release];
  [pool drain];
}

void testMacRibbonGroupNaturalWidthFrames()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::dsl;
  using namespace loka::dsl::testing;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  const RailMetrics metrics[] = {RailMetrics(), loka::macos::DefaultRailMetrics()};
  // The short caption is the band SimpleText will carry; whichever part is
  // wider sets the seat. The long caption must win, so the caption counts.
  const char *captions[] = {"Alignment", "Paragraph alignment and indentation controls"};
  for (int run = 0; run < 4; ++run)
  {
    const int scale = run / 2;
    const int sample = run % 2;
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 640, 160)];
    {
      MacScenePlatformController controller(root, metrics[scale]);
      NodeDefinitionBase *definition =
          (RibbonControl().testId("NaturalGroup")
           << RibbonItem("Open")
           << (RibbonGroup(captions[sample]) << RibbonItem("L") << RibbonItem("C") << RibbonItem("R"))
           << RibbonItem("Close"))
              .clone();
      LOKA_VERIFY(definition != 0);
      Scene scene(definition);
      LOKA_VERIFY(scene.mount(&controller));
      SceneTestAccess::updateAttached(scene, true);
      controller.relayout(640, 160);
      Node *anchor = 0;
      FlowError error;
      LOKA_VERIFY(LookupNodeById<Node>(&scene, "NaturalGroup", anchor, error) == FLOW_STEP_SUCCEEDED);
      LOKA_VERIFY(anchor && anchor->asStackNode());
      StackNode *ribbon = anchor->asStackNode();
      CompositionCursor<Node> top(ribbon->childrenHead(), ribbon->childrenCount());
      Node *open = top.next();
      RibbonGroupNode *frame = top.next()->asRibbonGroupNode();
      LOKA_VERIFY(frame && frame->childrenCount() == 4);
      StackNode *group = frame->childrenHead()->asStackNode();
      LOKA_VERIFY(group != 0);
      CompositionCursor<Node> parts(group->childrenHead(), group->childrenCount());
      StackNode *items = parts.next()->asStackNode();
      Node *caption = parts.next();
      LOKA_VERIFY(items && caption && caption->asTextNode());
      const int gap = MacSceneLayout::kGap;
      int itemsWidth = 2 * gap;
      CompositionCursor<Node> buttons(items->childrenHead(), items->childrenCount());
      for (Node *button = buttons.next(); button; button = buttons.next())
      {
        short width = 0;
        LOKA_VERIFY(controller.queryNaturalWidth(button, width));
        itemsWidth += width;
      }
      short captionWidth = 0, openWidth = 0;
      LOKA_VERIFY(controller.queryNaturalWidth(caption, captionWidth));
      LOKA_VERIFY(controller.queryNaturalWidth(open, openWidth));
      const int seat = captionWidth > itemsWidth ? captionWidth : itemsWidth;
      LOKA_VERIFY([[root subviews] count] == 9);
      NSTextField *captionField = (NSTextField *)[[root subviews] objectAtIndex:4];
      NSButton *closeButton = (NSButton *)[[root subviews] objectAtIndex:8];
      LOKA_VERIFY([captionField isKindOfClass:[NSTextField class]]);
      LOKA_VERIFY([[captionField stringValue] isEqualToString:[NSString stringWithUTF8String:captions[sample]]]);
      LOKA_VERIFY([[closeButton title] isEqualToString:@"Close"]);
      const int groupX = MacSceneLayout::kRootInset + openWidth + gap;
      const loka::macos::MacProjection &projection = controller.projection();
      LOKA_VERIFY([captionField frame].origin.x == projection.projectEdge(groupX + RibbonGroupNode::kFrameInset).pt);
      LOKA_VERIFY(
          [captionField frame].size.width
          == projection
                 .projectLength(groupX + RibbonGroupNode::kFrameInset, groupX + RibbonGroupNode::kFrameInset + seat)
                 .pt);
      LOKA_VERIFY([closeButton frame].origin.x
                  == projection.projectEdge(groupX + seat + 2 * RibbonGroupNode::kFrameInset + gap).pt);
      std::printf(
          "macOS ribbon widths: caption=%d items=%d scale=%d sample=%d\n", captionWidth, itemsWidth, scale, sample);
      if (sample == 1)
        LOKA_VERIFY(captionWidth > itemsWidth);
      // Content precedes the three closed edge leaves in the projection.
      NSArray *edgeViews = [[NSArray alloc] initWithObjects:[[root subviews] objectAtIndex:5],
                                                            [[root subviews] objectAtIndex:6],
                                                            [[root subviews] objectAtIndex:7],
                                                            nil];
      CompositionCursor<Node> edges(frame->childrenHead(), frame->childrenCount());
      edges.next();
      NodeContext *contexts[3];
      Node *edgeNodes[3];
      for (int i = 0; i < 3; ++i)
      {
        edgeNodes[i] = edges.next();
        LOKA_VERIFY(edgeNodes[i] && edgeNodes[i]->asRibbonGroupEdgeNode());
        contexts[i] = edgeNodes[i]->getContext();
        LOKA_VERIFY(contexts[i] != 0);
        LOKA_VERIFY([[edgeViews objectAtIndex:i] isFlipped]);
        LOKA_VERIFY(![[edgeViews objectAtIndex:i] isHidden]);
      }
      const NSRect left = [[edgeViews objectAtIndex:0] frame];
      const NSRect right = [[edgeViews objectAtIndex:1] frame];
      const NSRect bottom = [[edgeViews objectAtIndex:2] frame];
      const int groupWidth = seat + 2 * RibbonGroupNode::kFrameInset;
      LOKA_VERIFY(NSMinX(left) == projection.projectEdge(groupX).pt);
      LOKA_VERIFY(NSMinY(left) == projection.projectEdge(MacSceneLayout::kRootInset).pt);
      LOKA_VERIFY(NSMaxX(left) == projection.projectEdge(groupX + 1).pt);
      LOKA_VERIFY(NSMinX(right) == projection.projectEdge(groupX + groupWidth - 1).pt);
      LOKA_VERIFY(NSMaxX(right) == projection.projectEdge(groupX + groupWidth).pt);
      LOKA_VERIFY(NSMinY(right) == NSMinY(left));
      LOKA_VERIFY(NSMaxY(left) == NSMinY(bottom));
      LOKA_VERIFY(NSMaxY(right) == NSMinY(bottom));
      LOKA_VERIFY(NSMinX(bottom) == NSMinX(left));
      LOKA_VERIFY(NSMaxX(bottom) == NSMaxX(right));
      LOKA_VERIFY(NSMinY(bottom) > NSMaxY([captionField frame]));
      // At unit scale the bottom is exactly one point; projection may round
      // the non-unit metric's one-lu strip to one or two points.
      if (scale == 0)
        LOKA_VERIFY(bottom.size.height == 1);
      controller.relayout(640, 160);
      LOKA_VERIFY([[root subviews] count] == 9);
      for (int i = 0; i < 3; ++i)
      {
        LOKA_VERIFY(edgeNodes[i]->getContext() == contexts[i]);
        LOKA_VERIFY([[root subviews] objectAtIndex:5 + i] == [edgeViews objectAtIndex:i]);
      }
      NotifySubtreeNodeDetached(frame);
      LifecycleFactTestAccess::DeliverFacts(frame);
      for (int i = 0; i < 3; ++i)
        LOKA_VERIFY([[edgeViews objectAtIndex:i] isHidden]);
      NotifySubtreeNodeAttached(frame);
      LifecycleFactTestAccess::DeliverFacts(frame);
      for (int i = 0; i < 3; ++i)
        LOKA_VERIFY(![[edgeViews objectAtIndex:i] isHidden]);
      SceneTestAccess::unmount(scene);
      for (int i = 0; i < 3; ++i)
        LOKA_VERIFY([[edgeViews objectAtIndex:i] superview] == nil);
      [edgeViews release];
    }
    LOKA_VERIFY([[root subviews] count] == 0);
    [root release];
  }
  [pool drain];
}

void testMacRibbonGroupEdgeEnsureAndRetirement()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  {
    MacScenePlatformController controller(root, RailMetrics());
    int released = 0;
    NSView *view = nil;
    NSAutoreleasePool *nativePool = [[NSAutoreleasePool alloc] init];
    {
      RibbonGroupEdgeNode edge((RibbonGroupEdgeProps(RIBBON_GROUP_EDGE_LEFT)));
      LayoutState state;
      state.x = 7;
      state.y = 9;
      state.width = 0;
      state.height = 0;
      // The controller's ensure may offer a fallback height for creation; the
      // edge's own layout then places it at exactly the offered strip.
      LOKA_VERIFY(edge.layoutProjected(&controller, state) == 9);
      LOKA_VERIFY([[root subviews] count] == 1);
      view = [[root subviews] objectAtIndex:0];
      LOKA_VERIFY(NSEqualRects([view frame], NSMakeRect(7, 9, 0, 0)));
      NodeContext *context = edge.getContext();
      state.width = 1;
      state.height = 19;
      LOKA_VERIFY(controller.prepareProjectedLayout(&edge, state));
      LOKA_VERIFY(edge.getContext() == context);
      LOKA_VERIFY([[root subviews] count] == 1);
      LOKA_VERIFY([[root subviews] objectAtIndex:0] == view);
      LOKA_VERIFY(NSEqualRects([view frame], NSMakeRect(7, 9, 0, 0)));
      LOKA_VERIFY(edge.layoutProjected(&controller, state) == 28);
      LOKA_VERIFY(NSEqualRects([view frame], NSMakeRect(7, 9, 1, 19)));
      state.x = 11;
      state.y = 13;
      state.height = 0;
      LOKA_VERIFY(edge.layoutProjected(&controller, state) == 13);
      LOKA_VERIFY(state.height == 0);
      LOKA_VERIFY(NSEqualRects([view frame], NSMakeRect(11, 13, 1, 0)));
      state.x = 17;
      state.y = 23;
      state.height = 11;
      LOKA_VERIFY(context->layout(&controller, state) == 34);
      LOKA_VERIFY(NSEqualRects([view frame], NSMakeRect(17, 23, 1, 11)));
      observeMacNativeDeallocation(view, released);
    }
    [nativePool drain];
    LOKA_VERIFY(released == 0);
    LOKA_VERIFY([view superview] == nil);
    LOKA_VERIFY([view isHidden]);
    controller.drainNativeRetirements();
    LOKA_VERIFY(released == 1);
    controller.drainNativeRetirements();
    LOKA_VERIFY(released == 1);
  }
  [root release];
  [pool drain];
}

#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/RectSurface.hpp"
#include "app/ConfirmDialog.hpp"
#include <climits>
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "context/MacScrollViewContext.hpp"
// Keep Show after RequestSettlement and all other includes (#1359).
#include "app/nodes/nestable/Show.hpp"

namespace
{
  class DirectGapStack : public loka::app::StackNode
  {
  public:
    explicit DirectGapStack(const loka::app::StackProps &props)
        : StackNode(props)
    {
    }
    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<DirectGapStack>();
    }
  };
} // namespace

void testMacContainerGapLeafExits()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  const RailMetrics metrics[] = {RailMetrics(), loka::macos::DefaultRailMetrics()};
  for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
    for (int leaf = 0; leaf < 8; ++leaf)
    {
      const loka::macos::MacProjection allocation(0, metrics[scaleIndex]);
      NSView *root = [[NSView alloc] initWithFrame:allocation.projectClientSize(640, 400).r];
      {
        MacScenePlatformController controller(root, allocation.railMetrics());
        const loka::macos::MacProjection &scale = controller.projection();
        // Fragment gives Cell an unspecified height, exercising its natural exit.
        FragmentNode content((FragmentProps()));
        scene::Node *first = 0;
        switch (leaf)
        {
        case 0:
          first = new TextNode(TextProps("Text"));
          break;
        case 1:
          first = new ButtonNode(ButtonProps().text("Button"));
          break;
        case 2:
          first = new EditTextNode((EditTextProps()));
          break;
        case 3:
          first = new PopupMenuNode((PopupMenuProps()));
          break;
        case 4:
          first = new ImageViewNode((ImageViewProps()));
          break;
        case 5:
          first = new CellNode(CellProps().text("Cell"));
          break;
        case 6:
          first = new AttributedTextNode(AttributedTextProps(Styled("Styled", Bold)));
          break;
        case 7:
          first = new RectSurfaceNode(RectSurfaceProps().size(80, 30));
          break;
        }
        LOKA_VERIFY(first != 0);
        TextNode *next = new TextNode(TextProps("Following"));
        content.addChild(first);
        content.addChild(next);
        controller.onChange(&content, scene::NODE_DIRTY_NONE, false);
        // Root padding consumes the height, leaving an unspecified seat.
        controller.relayout(640, 2 * MacSceneLayout::kRootInset);
        LOKA_VERIFY(first->getContext() && next->getContext());
        const NSRect frame = [[[root subviews] objectAtIndex:0] frame];
        const NSRect following = [[[root subviews] objectAtIndex:1] frame];
        LOKA_VERIFY(NSMinX(frame) == scale.projectEdge(MacSceneLayout::kRootInset).pt);
        LOKA_VERIFY(NSMinY(frame) == scale.projectEdge(MacSceneLayout::kRootInset).pt);
        // At these scales projection is injective; recover the actual painted
        // bottom independently of the leaf's returned layout advance.
        int bottom = MacSceneLayout::kRootInset;
        while (scale.projectEdge(bottom).pt < NSMaxY(frame))
          ++bottom;
        LOKA_VERIFY(NSMaxY(frame) == scale.projectEdge(bottom).pt);
        LOKA_VERIFY(NSMinY(following) == scale.projectEdge(bottom + MacSceneLayout::kGap).pt);
        controller.onChange(0, scene::NODE_DIRTY_NONE, false);
      }
      [root release];
    }
  [pool drain];
}

void testMacContainerGapColumnAndPassThrough()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  const RailMetrics metrics[] = {RailMetrics(), loka::macos::DefaultRailMetrics()};
  for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
    for (int direct = 0; direct < 2; ++direct)
    {
      const loka::macos::MacProjection allocation(0, metrics[scaleIndex]);
      NSView *root = [[NSView alloc] initWithFrame:allocation.projectClientSize(640, 400).r];
      {
        MacScenePlatformController controller(root, allocation.railMetrics());
        const loka::macos::MacProjection &scale = controller.projection();
        const StackProps props(STACK_AXIS_COLUMN);
        StackNode *column = direct ? new DirectGapStack(props) : new StackNode(props);
        TextNode *text = new TextNode(TextProps("First"));
        ButtonNode *button = new ButtonNode(ButtonProps().text("Second"));
        EditTextNode *edit = new EditTextNode((EditTextProps()));
        FragmentNode *fragment = new FragmentNode((FragmentProps()));
        TextNode *last = new TextNode(TextProps("Last"));
        TextNode *tail = new TextNode(TextProps("Tail"));
        column->addChild(text);
        column->addChild(button);
        column->addChild(edit);
        fragment->addChild(last);
        fragment->addChild(tail);
        column->addChild(fragment);
        controller.onChange(column, scene::NODE_DIRTY_NONE, false);
        controller.relayout(640, 400);
        scene::Node *nodes[] = {text, button, edit, last, tail};
        int top = MacSceneLayout::kRootInset;
        for (int i = 0; i < 5; ++i)
        {
          LOKA_VERIFY(nodes[i]->getContext() != 0);
          const NSRect frame = [[[root subviews] objectAtIndex:i] frame];
          LOKA_VERIFY(NSMinX(frame) == scale.projectEdge(MacSceneLayout::kRootInset).pt);
          LOKA_VERIFY(NSMinY(frame) == scale.projectEdge(top).pt);
          while (scale.projectEdge(top).pt < NSMaxY(frame))
            ++top;
          LOKA_VERIFY(NSMaxY(frame) == scale.projectEdge(top).pt);
          top += MacSceneLayout::kGap;
        }
        controller.onChange(0, scene::NODE_DIRTY_NONE, false);
        delete column;
      }
      [root release];
    }
  [pool drain];
}

void testMacContainerGapNoSeats()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  using namespace loka::app::scene;
  using namespace loka::dsl;
  using namespace loka::dsl::testing;
  const RailMetrics metrics[] = {RailMetrics(), loka::macos::DefaultRailMetrics()};
  for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
  {
    const loka::macos::MacProjection allocation(0, metrics[scaleIndex]);
    NSView *root = [[NSView alloc] initWithFrame:allocation.projectClientSize(640, 400).r];
    {
      MacScenePlatformController controller(root, allocation.railMetrics());
      const loka::macos::MacProjection &scale = controller.projection();
      loka::core::MutableState<bool> hidden(false);
      // A plain NSView has no MacWindow/dialog transport, so this cannot present an alert.
      loka::core::MutableState<ConfirmResult> resultStorage;
      NodeState<ConfirmResult> result(&resultStorage, 0);
      NodeDefinitionBase *definition =
          (Fragment().testId("gapRoot") << Text("First") << Fragment() << (Show(hidden) << Text("Hidden"))
                                        << ConfirmDialog().result(result) << Text("Second"))
              .clone();
      LOKA_VERIFY(definition != 0);
      Scene scene(definition);
      LOKA_VERIFY(scene.mount(&controller));
      SceneTestAccess::updateAttached(scene, true);
      controller.relayout(640, 160);
      TextNode *first = 0;
      TextNode *second = 0;
      loka::dsl::FlowError error;
      LOKA_VERIFY(ResolveSelector(&scene, WithinAnchor("gapRoot").descendant<TextNode>(1), first, error)
                  == loka::dsl::FLOW_STEP_SUCCEEDED);
      LOKA_VERIFY(ResolveSelector(&scene, WithinAnchor("gapRoot").descendant<TextNode>(2), second, error)
                  == loka::dsl::FLOW_STEP_SUCCEEDED);
      LOKA_VERIFY(first && second);
      const NSRect a = [[[root subviews] objectAtIndex:0] frame];
      const NSRect b = [[[root subviews] objectAtIndex:1] frame];
      int bottom = MacSceneLayout::kRootInset;
      while (scale.projectEdge(bottom).pt < NSMaxY(a))
        ++bottom;
      LOKA_VERIFY(NSMinY(b) == scale.projectEdge(bottom + MacSceneLayout::kGap).pt);
      LOKA_VERIFY([[root subviews] count] == 2);
      SceneTestAccess::unmount(scene);
      controller.drainNativeRetirements();
    }
    [root release];
  }
  [pool drain];
}

namespace
{
  class DirectGapBox : public loka::app::BoxNode
  {
  public:
    explicit DirectGapBox(const loka::app::BoxProps &props)
        : BoxNode(props)
    {
    }
    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<DirectGapBox>();
    }
  };
} // namespace

void testMacContainerGapBox()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  for (int direct = 0; direct < 2; ++direct)
  {
    const loka::app::RailMetrics metrics;
    const loka::macos::MacProjection allocation(0, metrics);
    NSView *root = [[NSView alloc] initWithFrame:allocation.projectClientSize(640, 400).r];
    {
      MacScenePlatformController controller(root, allocation.railMetrics());
      const loka::macos::MacProjection &scale = controller.projection();
      FragmentNode content((FragmentProps()));
      BoxNode *box =
          direct ? new DirectGapBox(BoxProps().setSize(200, 100)) : new BoxNode(BoxProps().setSize(200, 100));
      TextNode *first = new TextNode(TextProps("First"));
      TextNode *second = new TextNode(TextProps("Second"));
      TextNode *following = new TextNode(TextProps("Following"));
      box->addChild(first);
      box->addChild(second);
      content.addChild(box);
      content.addChild(following);
      controller.onChange(&content, loka::app::scene::NODE_DIRTY_NONE, false);
      controller.relayout(640, 160);
      LOKA_VERIFY(first->getContext() && second->getContext() && following->getContext());
      const NSRect a = [[[root subviews] objectAtIndex:0] frame];
      const NSRect b = [[[root subviews] objectAtIndex:1] frame];
      const NSRect c = [[[root subviews] objectAtIndex:2] frame];
      LOKA_VERIFY(NSMinY(a) == scale.projectEdge(MacSceneLayout::kRootInset).pt);
      LOKA_VERIFY(NSMinY(b) == NSMaxY(a) + MacSceneLayout::kGap);
      LOKA_VERIFY(NSMinY(c) == scale.projectEdge(MacSceneLayout::kRootInset + 100 + MacSceneLayout::kGap).pt);
      controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
    }
    [root release];
  }
  [pool drain];
}

void testMacContainerGapRow()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  const RailMetrics metrics[] = {RailMetrics(), loka::macos::DefaultRailMetrics()};
  for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
    for (int aligned = 0; aligned < 2; ++aligned)
    {
      const loka::macos::MacProjection allocation(0, metrics[scaleIndex]);
      NSView *root = [[NSView alloc] initWithFrame:allocation.projectClientSize(640, 400).r];
      {
        MacScenePlatformController controller(root, allocation.railMetrics());
        const loka::macos::MacProjection &scale = controller.projection();
        StackNode column((StackProps(STACK_AXIS_COLUMN)));
        StackProps rowProps(STACK_AXIS_ROW);
        if (aligned)
          rowProps.alignVertical(VERTICAL_ALIGNMENT_CENTER);
        StackNode *row = new StackNode(rowProps);
        ButtonNode *first = new ButtonNode(ButtonProps().text("First"));
        ButtonNode *second = new ButtonNode(ButtonProps().text("Second"));
        TextNode *following = new TextNode(TextProps("Following"));
        row->addChild(first);
        row->addChild(second);
        column.addChild(row);
        column.addChild(following);
        controller.onChange(&column, scene::NODE_DIRTY_NONE, false);
        controller.relayout(640, 160);
        const NSRect a = [[[root subviews] objectAtIndex:0] frame];
        const NSRect b = [[[root subviews] objectAtIndex:1] frame];
        const NSRect c = [[[root subviews] objectAtIndex:2] frame];
        const int width = (640 - 2 * MacSceneLayout::kRootInset - MacSceneLayout::kGap) / 2;
        LOKA_VERIFY(NSMaxX(a) == scale.projectEdge(MacSceneLayout::kRootInset + width).pt);
        LOKA_VERIFY(NSMinX(b) == scale.projectEdge(MacSceneLayout::kRootInset + width + MacSceneLayout::kGap).pt);
        LOKA_VERIFY(NSMinY(c)
                    == scale
                           .projectEdge(MacSceneLayout::kRootInset + controller.rowLayoutMetrics().buttonHeight
                                        + MacSceneLayout::kGap)
                           .pt);
        controller.onChange(0, scene::NODE_DIRTY_NONE, false);
      }
      [root release];
    }
  [pool drain];
}

namespace
{
  // Deliberate Win32 twin: invert only exact projected edges from native frames.
  int gapDoorEdgeLu(const loka::macos::MacProjection &scale, CGFloat edge)
  {
    int lu = 0;
    while (lu < SHRT_MAX && scale.projectEdge(lu).pt < edge)
      ++lu;
    LOKA_VERIFY(scale.projectEdge(lu).pt == edge);
    return lu;
  }

  NSRect gapDoorFrame(NSView *parent, NSUInteger index)
  {
    LOKA_VERIFY([[parent subviews] count] > index);
    return [[[parent subviews] objectAtIndex:index] frame];
  }

  void verifyGapDoorInterval(MacScenePlatformController &controller, NSView *parent,
                             NSUInteger first, NSUInteger second, int gap)
  {
    // Match the existing Mac container pins: frames use the rail's top-down
    // coordinates; ScrollView children are in the flipped document view.
    const NSRect before = gapDoorFrame(parent, first);
    const NSRect after = gapDoorFrame(parent, second);
    const loka::macos::MacProjection &scale = controller.projection();
    const int bottom = gapDoorEdgeLu(scale, NSMaxY(before));
    LOKA_VERIFY(NSMinY(after) == scale.projectEdge(bottom + gap).pt);
  }

  void verifyGapDoorColumn(bool registered, bool fullSequence)
  {
    using namespace loka::app;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
    const loka::macos::MacProjection scale(0, loka::macos::DefaultRailMetrics());
    NSView *root = [[NSView alloc] initWithFrame:scale.projectClientSize(640, 400).r];
    LOKA_VERIFY(root != nil);
    {
      MacScenePlatformController controller(root, scale.railMetrics());
      StackProps props(STACK_AXIS_COLUMN);
      props.gap(fullSequence ? 16 : 20);
      StackNode *column = registered ? new StackNode(props) : new DirectGapStack(props);
      TextNode *head = fullSequence ? new TextNode(TextProps("Head")) : 0;
      FragmentNode *fragment = new FragmentNode((FragmentProps()));
      TextNode *first = new TextNode(TextProps("First"));
      TextNode *second = new TextNode(TextProps("Second"));
      ButtonNode *tail = fullSequence ? new ButtonNode(ButtonProps().text("Tail")) : 0;
      if (fullSequence)
        column->addChild(head);

      fragment->addChild(first);
      fragment->addChild(second);
      column->addChild(fragment);
      if (fullSequence)
        column->addChild(tail);

      controller.onChange(column, scene::NODE_DIRTY_NONE, false);
      controller.relayout(640, 400);
      verifyGapDoorInterval(controller, root, fullSequence ? 1 : 0, fullSequence ? 2 : 1,
                            fullSequence ? 16 : 20);
      if (fullSequence)
      {
        LOKA_VERIFY(NSMinY(gapDoorFrame(root, 0)) == scale.projectEdge(MacSceneLayout::kRootInset).pt);
        verifyGapDoorInterval(controller, root, 0, 1, 16);
        verifyGapDoorInterval(controller, root, 2, 3, 16);
      }
      controller.onChange(0, scene::NODE_DIRTY_NONE, false);
      delete column;
    }
    [root release];
    [pool drain];
  }

  enum GapDoorReset { GAP_DOOR_BOX, GAP_DOOR_ZSTACK, GAP_DOOR_GRID, GAP_DOOR_SCROLL, GAP_DOOR_COLUMN };

  void verifyGapDoorReset(GapDoorReset reset)
  {
    using namespace loka::app;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
    const loka::macos::MacProjection scale(0, loka::macos::DefaultRailMetrics());
    NSView *root = [[NSView alloc] initWithFrame:scale.projectClientSize(640, 400).r];
    LOKA_VERIFY(root != nil);
    {
      MacScenePlatformController controller(root, scale.railMetrics());
      StackProps props(STACK_AXIS_COLUMN);
      props.gap(20);
      loka::testing::ScrollViewSeat viewport;
      DirectGapStack column(props);
      scene::NestableNode *container = 0;
      switch (reset)
      {
      case GAP_DOOR_BOX: container = new BoxNode((BoxProps())); break;
      case GAP_DOOR_ZSTACK: container = new ZStackNode((ZStackProps())); break;
      case GAP_DOOR_GRID: container = new GridNode(GridProps(1, 1)); break;
      case GAP_DOOR_SCROLL:
        container = new ScrollViewNode(ScrollViewProps(viewport.state()));
        break;
      case GAP_DOOR_COLUMN: container = new StackNode(StackProps(STACK_AXIS_COLUMN)); break;
      }
      LOKA_VERIFY(container != 0);
      FragmentNode *inside = new FragmentNode((FragmentProps()));
      TextNode *first = new TextNode(TextProps("Inside first"));
      TextNode *second = new TextNode(TextProps("Inside second"));
      inside->addChild(first);
      inside->addChild(second);
      container->addChild(inside);
      column.addChild(container);
      FragmentNode *sibling = new FragmentNode((FragmentProps()));
      TextNode *third = new TextNode(TextProps("Sibling first"));
      TextNode *fourth = new TextNode(TextProps("Sibling second"));
      sibling->addChild(third);
      sibling->addChild(fourth);
      column.addChild(sibling);
      controller.onChange(&column, scene::NODE_DIRTY_NONE, false);
      controller.relayout(640, 400);
      // Scroll children use document-local coordinates; all other children use root coordinates.
      if (reset == GAP_DOOR_SCROLL)
        LOKA_VERIFY(container->getContext() != 0);
      NSView *parent = reset == GAP_DOOR_SCROLL
          ? (NSView *)static_cast<MacScrollViewContext *>(container->getContext())->documentView() : root;
      verifyGapDoorInterval(controller, parent, 0, 1, 6);
      verifyGapDoorInterval(controller, root, reset == GAP_DOOR_SCROLL ? 1 : 2,
                            reset == GAP_DOOR_SCROLL ? 2 : 3, 20);
      controller.onChange(0, scene::NODE_DIRTY_NONE, false);
    }
    [root release];
    [pool drain];
  }
}

void testMacGapDoorColumnFragment()
{
  verifyGapDoorColumn(false, true);
}

void testMacGapDoorBoxSiblingIsolation()
{
  verifyGapDoorReset(GAP_DOOR_BOX);
}

void testMacGapDoorZStackReset()
{
  verifyGapDoorReset(GAP_DOOR_ZSTACK);
}

void testMacGapDoorGridReset()
{
  verifyGapDoorReset(GAP_DOOR_GRID);
}

void testMacGapDoorScrollViewResetAndSiblingIsolation()
{
  verifyGapDoorReset(GAP_DOOR_SCROLL);
}

void testMacGapDoorUnspecifiedColumnReset()
{
  verifyGapDoorReset(GAP_DOOR_COLUMN);
}

void testMacGapDoorRegisteredColumnBridge()
{
  verifyGapDoorColumn(true, false);
}

void testMacGapDoorRowFragment()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  const loka::macos::MacProjection scale(0, loka::macos::DefaultRailMetrics());
  NSView *root = [[NSView alloc] initWithFrame:scale.projectClientSize(640, 400).r];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller(root, scale.railMetrics());
    StackProps props(STACK_AXIS_ROW);
    props.gap(20);
    DirectGapStack row(props);
    FragmentNode *fragment = new FragmentNode((FragmentProps()));
    TextNode *first = new TextNode(TextProps("First"));
    TextNode *second = new TextNode(TextProps("Second"));
    fragment->addChild(first);
    fragment->addChild(second);
    row.addChild(fragment);
    controller.onChange(&row, scene::NODE_DIRTY_NONE, false);
    controller.relayout(640, 400);
    verifyGapDoorInterval(controller, root, 0, 1, 20);
    controller.onChange(0, scene::NODE_DIRTY_NONE, false);
  }
  [root release];
  [pool drain];
}

void testMacGapDoorRootFragment()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  const loka::macos::MacProjection scale(0, loka::macos::DefaultRailMetrics());
  NSView *root = [[NSView alloc] initWithFrame:scale.projectClientSize(640, 400).r];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller(root, scale.railMetrics());
    FragmentNode fragment((FragmentProps()));
    TextNode *first = new TextNode(TextProps("First"));
    TextNode *second = new TextNode(TextProps("Second"));
    fragment.addChild(first);
    fragment.addChild(second);
    controller.onChange(&fragment, scene::NODE_DIRTY_NONE, false);
    controller.relayout(640, 400);
    LOKA_VERIFY(NSMinY(gapDoorFrame(root, 0)) == scale.projectEdge(MacSceneLayout::kRootInset).pt);
    verifyGapDoorInterval(controller, root, 0, 1, 6);
    controller.onChange(0, scene::NODE_DIRTY_NONE, false);
  }
  [root release];
  [pool drain];
}

void testMacGapDoorScrollViewContent()
{
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  const loka::macos::MacProjection scale(0, loka::macos::DefaultRailMetrics());
  NSView *root = [[NSView alloc] initWithFrame:scale.projectClientSize(640, 400).r];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller(root, scale.railMetrics());
    StackProps props(STACK_AXIS_COLUMN);
    props.gap(20);
    loka::testing::ScrollViewSeat viewport;
    DirectGapStack column(props);
    ScrollViewNode *scroll = new ScrollViewNode(ScrollViewProps(viewport.state()));
    BoxNode *box = new BoxNode((BoxProps()));
    FragmentNode *fragment = new FragmentNode((FragmentProps()));
    TextNode *first = new TextNode(TextProps("Box first"));
    TextNode *second = new TextNode(TextProps("Box second"));
    TextNode *third = new TextNode(TextProps("Fragment first"));
    TextNode *fourth = new TextNode(TextProps("Fragment second"));
    box->addChild(first);
    box->addChild(second);
    fragment->addChild(third);
    fragment->addChild(fourth);
    scroll->addChild(box);
    scroll->addChild(fragment);
    column.addChild(scroll);
    controller.onChange(&column, scene::NODE_DIRTY_NONE, false);
    controller.relayout(640, 400);
    LOKA_VERIFY(scroll->getContext() != 0);
    NSView *parent = (NSView *)static_cast<MacScrollViewContext *>(scroll->getContext())->documentView();
    verifyGapDoorInterval(controller, parent, 0, 1, 6);
    verifyGapDoorInterval(controller, parent, 1, 2, 6);
    verifyGapDoorInterval(controller, parent, 2, 3, 6);
    controller.onChange(0, scene::NODE_DIRTY_NONE, false);
  }
  [root release];
  [pool drain];
}

void testMacTextItalicTraits()
{
  std::printf("\n==== [testMacTextItalicTraits] start ====\n");
  using namespace loka::app;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller((void *)root, loka::macos::DefaultRailMetrics());
    const TextStyle styles[] = {TextStyle(), TextStyle().italic(false), TextStyle().italic(true)};
    NSFontManager *manager = [NSFontManager sharedFontManager];
    NSFont *plain = nil;
    for (int i = 0; i < 3; ++i)
    {
      const TextDefinitionWithAttr text = Text("italic") + styles[i];
      NSFont *font = (NSFont *)controller.textFont(text.props.resolvedTextStyle());
      LOKA_VERIFY(font != nil);
      plain = i == 0 ? font : plain;
      // The italic slot holds what the OS offers for the plain font: 10.9's
      // Lucida Grande has no italic face, so the trait branch is skipped there.
      NSFont *offered = i == 2 ? [manager convertFont:plain toHaveTrait:NSItalicFontMask] : nil;
      const bool expectItalic = offered && ([manager traitsOfFont:offered] & NSItalicFontMask) != 0;
      if (i == 2 && !expectItalic)
        std::printf("[skip] italic trait: %s has no italic face here\n", [[plain fontName] UTF8String]);
      LOKA_VERIFY((([manager traitsOfFont:font] & NSItalicFontMask) != 0) == expectItalic);
      LOKA_VERIFY(i < 2 || [font isEqual:(offered ? offered : plain)]);
    }
  }
  [root release];
  [pool drain];
  std::printf("==== [testMacTextItalicTraits] PASSED ====\n");
}

namespace loka
{
  namespace testing
  {
    void failMacTextPresentationStep(unsigned ordinal);
    void failMacCardBitmaps(unsigned count);
    void failMacTextKitAllocation(unsigned ordinal);
  }
} // namespace loka

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;

  void VerifyTextKitCounts(unsigned clears, unsigned installs, unsigned writes, unsigned forces)
  {
    LOKA_VERIFY(richClears == clears && richInstalls == installs);
    LOKA_VERIFY(sizeWrites == writes && richForces == forces);
  }

  void VerifyTextKitWidth(NSView *view, CGFloat width)
  {
    LOKA_VERIFY(AttributedAccess::width(view) == width);
    LOKA_VERIFY(NSEqualSizes([AttributedAccess::container(view) containerSize], NSMakeSize(width, 1.0e7)));
  }
} // namespace

void testMacAttributedTextInstallSequence()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 240, 160)] autorelease];
  {
    MacScenePlatformController controller(root, RailMetrics());
    AttributedTextNode node((AttributedText(Styled("abc def", TextStyle())) + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
    LayoutState state;
    LOKA_VERIFY(controller.prepareProjectedLayout(&node, state));
    MacAttributedTextContext &context = AttributedContext(node);
    NSView *view = AttributedAccess::view(context);
    NSTextStorage *storage = AttributedAccess::storage(view);
    NSLayoutManager *manager = AttributedAccess::manager(view);
    NSTextContainer *container = AttributedAccess::container(view);
    LOKA_VERIFY([[storage layoutManagers] objectAtIndex:0] == manager);
    LOKA_VERIFY([[manager textContainers] objectAtIndex:0] == container);
    LOKA_VERIFY(![manager backgroundLayoutEnabled] && [container lineFragmentPadding] == 2);
    VerifyTextKitWidth(view, 0);
    LOKA_VERIFY(!AttributedAccess::measured(context));
    MacMeasurementCounter counter;
    const int host = DarwinMajor();
    if (host != 13 && host != 25)
      std::printf("[skip] attributed degenerate widths : no recorded baseline for this host\n");
    for (int i = 0; i < 2; ++i)
    {
      state.width = i ? 3 : 0;
      context.layout(&controller, state);
      VerifyTextKitCounts(i + 1, i + 1, i, i + 1);
      VerifyTextKitWidth(view, state.width);
      NSAttributedString *value = AttributedAccess::projectionSnapshot(node);
      CGFloat oracle = ReferenceTextGeometry(value, state.width).size.height;
      std::printf("width=%d cell=%g TextKit=%g\n",
                  state.width,
                  (double)CellAdvance(value, NSLineBreakByWordWrapping, state.width),
                  (double)oracle);
      LOKA_VERIFY(state.height == controller.projection().measurementToLu(oracle));
      // Probe G, system-font "abc def", default typesetter. Zero is a completed advance on Mavericks.
      if (host == 13 || host == 25)
        LOKA_VERIFY(state.height == (i ? 96 : host == 13 ? 0 : 16));
      LOKA_VERIFY(AttributedAccess::reusable(context, controller.projection().projectLength(0, state.width)));
      [view setFrameSize:NSMakeSize(111, 150)];
      VerifyTextKitWidth(view, state.width);
      unsigned displayed = displayRequests;
      context.layout(&controller, state);
      LOKA_VERIFY(displayRequests > displayed);
      displayed = displayRequests;
      context.layout(&controller, state);
      LOKA_VERIFY(displayRequests > displayed);
      VerifyTextKitCounts(i + 1, i + 1, i, i + 1);
    }
    state.inputs = NODE_DIRTY_PROPS;
    context.layout(&controller, state);
    VerifyTextKitCounts(3, 3, 1, 3);
    state.width = 120;
    context.layout(&controller, state);
    VerifyTextKitCounts(4, 4, 2, 4);
    VerifyTextKitWidth(view, 120);
    state.inputs = NODE_DIRTY_NONE;
    LOKA_VERIFY(AttributedAccess::height(context) > 0);
    const AttributedString replacement = Styled("large\nsecond\nthird", FontSize<24>());
    node.props.text(replacement);
    LOKA_VERIFY(AttributedAccess::build(context, replacement, controller));
    LOKA_VERIFY(AttributedAccess::complete(context) && !AttributedAccess::measured(context));
    LOKA_VERIFY(AttributedAccess::height(context) == 0);
    LOKA_VERIFY(!AttributedAccess::reusable(context, controller.projection().projectLength(0, 120)));
    context.layout(&controller, state);
    LOKA_VERIFY(state.height
                == controller.projection().measurementToLu(
                    ReferenceTextGeometry(AttributedAccess::projectionSnapshot(node), 120).size.height));
    context.layout(&controller, state);
    VerifyTextKitCounts(5, 5, 2, 5);
    NSAttributedString *installed = [AttributedAccess::projectionSnapshot(node) retain];
    node.props.text(AttributedString());
    state.width = 90;
    {
      MacNativeRefusal missing(
          @selector(systemFontOfSize:), (IMP)MacAttributedMissingSystemFont, object_getClass([NSFont class]));
      context.layout(&controller, state);
    }
    VerifyTextKitCounts(7, 6, 3, 6);
    LOKA_VERIFY(!AttributedAccess::measured(context) && !AttributedAccess::complete(context) && state.height == 0);
    VerifyTextKitWidth(view, 90);
    context.layout(&controller, state);
    VerifyTextKitCounts(8, 7, 3, 7);
    LOKA_VERIFY(state.height > 0 && AttributedAccess::measured(context));
    LOKA_VERIFY([installed length] && ![AttributedAccess::projectionSnapshot(node) length]);
    [installed release];
    node.props.text(Styled(RawAttributed("\xC0\xAF", 2), Italic));
    state.width = 60;
    context.layout(&controller, state);
    VerifyTextKitCounts(10, 7, 3, 7);
    VerifyTextKitWidth(view, 90);
    LOKA_VERIFY(!AttributedAccess::measured(context) && state.height == 0);
    node.props.text(replacement);
    context.layout(&controller, state);
    VerifyTextKitCounts(11, 8, 4, 8);
    VerifyTextKitWidth(view, 60);
    LOKA_VERIFY(AttributedAccess::measured(context) && state.height > 0);
    LOKA_VERIFY(storage == AttributedAccess::storage(view) && manager == AttributedAccess::manager(view)
                && container == AttributedAccess::container(view));
  }
  [pool drain];
}

void testMacAttributedTextEmptyAdvances()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)] autorelease];
  const NSLineBreakMode modes[] = {
      NSLineBreakByClipping, NSLineBreakByWordWrapping, NSLineBreakByCharWrapping, NSLineBreakByTruncatingTail};
  const TextWrap wraps[] = {TEXT_WRAP_NONE, TEXT_WRAP_WORD, TEXT_WRAP_CHAR, TEXT_WRAP_NONE};
  const AttributedString empty[] = {AttributedString(),
                                    Styled("", Italic) + Styled("", FontSize<24>()),
                                    Styled(RawAttributed("\xEF\xBB\xBF", 3), FontSize<24>())};
  for (int mode = 0; mode < 4; ++mode)
  {
    MacScenePlatformController controller(root, loka::macos::DefaultRailMetrics());
    AttributedTextProps props(Styled("large", FontSize<24>()));
    props.blockStyle_ = BlockStyle().wrap(wraps[mode]);
    if (mode)
      props.blockStyle_.truncation(TEXT_TRUNCATION_ELLIPSIS);
    AttributedTextNode node(props);
    AttributedTextNode sibling((AttributedTextProps(Styled("next", Italic))));
    LayoutState state;
    state.width = 101;
    state.x = 1;
    state.y = 7;
    node.layoutProjected(&controller, state);
    for (int sample = 0; sample < 3; ++sample)
    {
      node.props.text(empty[sample]);
      node.getContext()->onPropsApplied();
      const short bottom = node.layout(&controller, state);
      NSAttributedString *installed = AttributedAccess::projectionSnapshot(node);
      LOKA_VERIFY(![installed length]);
      const CGFloat width = controller.projection().projectLength(1, 102).pt;
      const CGFloat oracle = CellAdvance(installed, modes[mode], width);
      LOKA_VERIFY(state.height == controller.projection().measurementToLu(oracle));
      LayoutState next = state;
      next.y = bottom;
      sibling.layoutProjected(&controller, next);
      LOKA_VERIFY([AttributedAccess::view(AttributedContext(sibling)) frame].origin.y
                  == controller.projection().projectEdge(7 + controller.projection().measurementToLu(oracle)).pt);
      node.props.text(Styled("large", FontSize<24>()));
      node.getContext()->onPropsApplied();
      node.layout(&controller, state);
      LOKA_VERIFY(state.height > controller.projection().measurementToLu(oracle));
    }
  }
  [pool drain];
}

namespace
{
  class RetirementController : public MacScenePlatformController
  {
  public:
    explicit RetirementController(NSView *root)
        : MacScenePlatformController(root, RailMetrics()),
          callbacks(0)
    {
    }
    virtual void onChange(Node *node, NodeDirtyFlags flags, bool full)
    {
      ++callbacks;
      MacScenePlatformController::onChange(node, flags, full);
    }
    virtual void requestRelayout()
    {
      ++callbacks;
      MacScenePlatformController::requestRelayout();
    }
    unsigned callbacks;
  };
  int partialDeaths[4];
  IMP releasePartial;
  void ObservePartialDealloc(id self, SEL cmd)
  {
    observeMacNativeDeallocation(self, partialDeaths[0]);
    id objects[] = {
        AttributedAccess::storage(self), AttributedAccess::manager(self), AttributedAccess::container(self)};
    for (int i = 0; i < 3; ++i)
    {
      if (objects[i])
        observeMacNativeDeallocation(objects[i], partialDeaths[i + 1]);
    }
    ((void (*)(id, SEL))releasePartial)(self, cmd);
  }
} // namespace

void testMacAttributedTextKitLifetime()
{
  NSAutoreleasePool *outer = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 160, 160)] autorelease];
  Class leaf = NSClassFromString(@"LokaAttributedTextView");
  releasePartial = class_getMethodImplementation(leaf, @selector(dealloc));
  for (int ordinal = 1; ordinal <= 4; ++ordinal)
  {
    RetirementController controller(root);
    int deaths[4] = {};
    NSView *view = nil;
    NSAutoreleasePool *local = [[NSAutoreleasePool alloc] init];
    {
      AttributedTextNode node((AttributedTextProps(Styled("alive", Italic))));
      LayoutState state;
      state.width = 160;
      for (int i = 0; i < 4; ++i)
        partialDeaths[i] = 0;
      {
        NSAutoreleasePool *failed = [[NSAutoreleasePool alloc] init];
        MacNativeRefusal track(@selector(dealloc), (IMP)ObservePartialDealloc, leaf);
        loka::testing::failMacTextKitAllocation(ordinal);
        LOKA_VERIFY(!controller.prepareProjectedLayout(&node, state));
        LOKA_VERIFY(!node.getContext() && [[root subviews] count] == 0);
        [failed drain];
      }
      for (int i = 0; i < 4; ++i)
        LOKA_VERIFY(partialDeaths[i] == (i < ordinal ? 1 : 0));
      node.layoutProjected(&controller, state);
      LOKA_VERIFY(state.height > 0);
      view = AttributedAccess::view(AttributedContext(node));
      observeMacNativeDeallocation(view, deaths[0]);
      observeMacNativeDeallocation(AttributedAccess::storage(view), deaths[1]);
      observeMacNativeDeallocation(AttributedAccess::manager(view), deaths[2]);
      observeMacNativeDeallocation(AttributedAccess::container(view), deaths[3]);
      if (ordinal == 2)
        [view retain];
    }
    LOKA_VERIFY([view isHidden] && ![view superview] && ![AttributedAccess::storage(view) length]);
    [local drain];
    for (int i = 0; i < 4; ++i)
      LOKA_VERIFY(!deaths[i]);
    const unsigned callbacks = controller.callbacks;
    LOKA_VERIFY(!controller.hasPendingRelayout());
    controller.drainNativeRetirements();
    if (ordinal == 2)
    {
      for (int i = 0; i < 4; ++i)
        LOKA_VERIFY(!deaths[i]);
      [view release];
    }
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    for (int i = 0; i < 4; ++i)
      LOKA_VERIFY(deaths[i] == 1);
    LOKA_VERIFY(!controller.hasPendingRelayout() && controller.callbacks == callbacks);
  }
  [outer drain];
}

namespace
{
  struct SubmittedGlyphs
  {
    NSRange background, foreground;
    unsigned backgrounds, foregrounds;
  };
  SubmittedGlyphs submitted;
  IMP drawInk, drawBackground;
  void RecordInk(id self, SEL cmd, NSRange range, NSPoint origin)
  {
    ++submitted.foregrounds;
    submitted.foreground = range;
    ((void (*)(id, SEL, NSRange, NSPoint))drawInk)(self, cmd, range, origin);
  }
  void RecordBackground(id self, SEL cmd, NSRange range, NSPoint origin)
  {
    ++submitted.backgrounds;
    submitted.background = range;
    ((void (*)(id, SEL, NSRange, NSPoint))drawBackground)(self, cmd, range, origin);
  }

  // The bitmap is larger than the leaf. Direct invocation bypasses AppKit's view clipping.
  enum BandPainter
  {
    PAINT_VIEW,
    PAINT_FULL_RANGE,
    PAINT_UNCLIPPED_RANGE
  };
  NSBitmapImageRep *PaintBand(NSView *view, NSRect dirty, BandPainter painter, bool clipDirty)
  {
    const int width = 240, height = 240;
    NSBitmapImageRep *bitmap = [[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:0
                                                                        pixelsWide:width
                                                                        pixelsHigh:height
                                                                     bitsPerSample:8
                                                                   samplesPerPixel:4
                                                                          hasAlpha:YES
                                                                          isPlanar:NO
                                                                    colorSpaceName:NSDeviceRGBColorSpace
                                                                       bytesPerRow:width * 4
                                                                      bitsPerPixel:32] autorelease];
    LOKA_VERIFY(bitmap);
    std::memset([bitmap bitmapData], 0, width * height * 4);
    NSGraphicsContext *destination = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CGContextRef cg = (CGContextRef)[destination graphicsPort];
    NSGraphicsContext *flipped = [NSGraphicsContext graphicsContextWithGraphicsPort:cg flipped:YES];
#pragma clang diagnostic pop
    LOKA_VERIFY(flipped);
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:flipped];
    [flipped saveGraphicsState];
    CGContextTranslateCTM(cg, 0, height);
    CGContextScaleCTM(cg, 1, -1);
    if (clipDirty)
      NSRectClip(dirty);
    if (painter != PAINT_VIEW)
    {
      NSLayoutManager *manager = AttributedAccess::manager(view);
      NSTextContainer *container = AttributedAccess::container(view);
      NSRange all = painter == PAINT_FULL_RANGE
                        ? [manager glyphRangeForTextContainer:container]
                        : [manager glyphRangeForBoundingRect:NSIntersectionRect(dirty, [view bounds])
                                             inTextContainer:container];
      [manager drawBackgroundForGlyphRange:all atPoint:NSZeroPoint];
      [manager drawGlyphsForGlyphRange:all atPoint:NSZeroPoint];
    }
    else
      [view drawRect:dirty];
    CGContextFlush(cg);
    [flipped restoreGraphicsState];
    [NSGraphicsContext restoreGraphicsState];
    return bitmap;
  }

  unsigned InkColumns(NSBitmapImageRep *bitmap, NSInteger start, NSInteger end)
  {
    unsigned count = 0;
    for (NSInteger y = 0; y < [bitmap pixelsHigh]; ++y)
      for (NSInteger x = start; x < end; ++x)
      {
        NSUInteger pixel[4];
        [bitmap getPixel:pixel atX:x y:y];
        if (pixel[3])
          ++count;
      }
    return count;
  }
  void VerifyBand(NSView *view, NSRect band)
  {
    NSLayoutManager *manager = AttributedAccess::manager(view);
    NSTextContainer *container = AttributedAccess::container(view);
    NSRange all = [manager glyphRangeForTextContainer:container];
    NSRange expected = [manager glyphRangeForBoundingRect:band inTextContainer:container];
    LOKA_VERIFY(expected.length && expected.length < all.length);
    NSBitmapImageRep *full = PaintBand(view, band, PAINT_FULL_RANGE, true);
    LOKA_VERIFY(InkColumns(full, 0, [full pixelsWide]) > 0);
    submitted.foregrounds = submitted.backgrounds = 0;
    NSBitmapImageRep *actual = PaintBand(view, band, PAINT_VIEW, true);
    LOKA_VERIFY(submitted.foregrounds == 1 && submitted.backgrounds == 1);
    LOKA_VERIFY(NSEqualRanges(expected, submitted.foreground) && NSEqualRanges(expected, submitted.background));
    LOKA_VERIFY(std::memcmp([full bitmapData], [actual bitmapData], [full bytesPerRow] * [full pixelsHigh]) == 0);
  }
} // namespace

void testMacAttributedTextDrawBands()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 240, 240)] autorelease];
  {
    MacScenePlatformController controller(root, RailMetrics());
    const TextStyle style = FontSize<24>() + Italic + Underline + TextStyle().color(TEXT_COLOR_BLACK);
    AttributedTextNode node((AttributedTextProps(
        Styled("first\nffff italic overhang\nunderlined\n\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d latin\nlast", style))));
    LayoutState state;
    state.width = 180;
    node.layoutProjected(&controller, state);
    NSView *view = AttributedAccess::view(AttributedContext(node));
    NSLayoutManager *manager = AttributedAccess::manager(view);
    Class cls = [manager class];
    drawInk = class_getMethodImplementation(cls, @selector(drawGlyphsForGlyphRange:atPoint:));
    drawBackground = class_getMethodImplementation(cls, @selector(drawBackgroundForGlyphRange:atPoint:));
    MacNativeRefusal ink(@selector(drawGlyphsForGlyphRange:atPoint:), (IMP)RecordInk, cls);
    MacNativeRefusal bg(@selector(drawBackgroundForGlyphRange:atPoint:), (IMP)RecordBackground, cls);
    NSString *text = [AttributedAccess::storage(view) string];
    NSString *samples[] = {@"ffff", @"underlined", @"\u05e9", @"last"};
    for (unsigned i = 0; i < 4; ++i)
    {
      NSUInteger index = [text rangeOfString:samples[i]].location;
      LOKA_VERIFY(index != NSNotFound);
      NSUInteger glyph = [manager glyphRangeForCharacterRange:NSMakeRange(index, 1) actualCharacterRange:0].location;
      NSRect line = [manager lineFragmentUsedRectForGlyphAtIndex:glyph effectiveRange:0];
      NSRect box = [manager boundingRectForGlyphRange:NSMakeRange(glyph, 1)
                                      inTextContainer:AttributedAccess::container(view)];
      LOKA_VERIFY(box.size.width > 0);
      VerifyBand(view,
                 NSMakeRect(NSMidX(box), line.origin.y, std::max((CGFloat)2, box.size.width / 2), line.size.height));
    }
    submitted.foregrounds = submitted.backgrounds = 0;
    PaintBand(view, NSMakeRect(300, 300, 20, 20), PAINT_VIEW, false);
    LOKA_VERIFY(!submitted.foregrounds && !submitted.backgrounds);

    // Find a column that actually cuts ink; the unbounded reference must bleed.
    NSBitmapImageRep *control = PaintBand(view, [view bounds], PAINT_FULL_RANGE, false);
    int edge = 4;
    while (edge < 50 && (!InkColumns(control, edge - 1, edge) || !InkColumns(control, edge, edge + 1)))
      ++edge;
    LOKA_VERIFY(edge < 50 && InkColumns(control, edge, 180));
    [view setFrameSize:NSMakeSize(edge, state.height)];
    if ([view respondsToSelector:@selector(setClipsToBounds:)])
      [view setClipsToBounds:NO];
    NSBitmapImageRep *bleed = PaintBand(view, [view bounds], PAINT_UNCLIPPED_RANGE, false);
    LOKA_VERIFY(InkColumns(bleed, edge, 180) > 0);
    NSBitmapImageRep *clipped = PaintBand(view, [view bounds], PAINT_VIEW, false);
    LOKA_VERIFY(InkColumns(clipped, 0, edge) && !InkColumns(clipped, edge, 180));

    // Compare the two actual paint modes over identical text and an overflowing middle line.
    node.props.text(Styled("first\nWWWWWWWWWWWWWWWWWWWWWWWWWWWW\nlast", style));
    node.props.blockStyle_ = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
    node.getContext()->onPropsApplied();
    node.layout(&controller, state);
    NSRect tail = [manager lineFragmentUsedRectForGlyphAtIndex:6 effectiveRange:0];
    NSRect tokenBand = NSMakeRect(NSMaxX(tail) - 24, tail.origin.y, 24, tail.size.height);
    VerifyBand(view, tokenBand);
    NSBitmapImageRep *ellipsis = PaintBand(view, tokenBand, PAINT_VIEW, true);
    node.props.blockStyle_ = BlockStyle();
    node.getContext()->onPropsApplied();
    node.layout(&controller, state);
    NSBitmapImageRep *clip = PaintBand(view, tokenBand, PAINT_VIEW, true);
    LOKA_VERIFY(std::memcmp([ellipsis bitmapData], [clip bitmapData], [clip bytesPerRow] * [clip pixelsHigh]) != 0);
  }
  [pool drain];
}

namespace
{
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101200
  const NSEventType kTextDown = NSEventTypeLeftMouseDown, kTextUp = NSEventTypeLeftMouseUp;
  const NSUInteger kTextUpMask = NSEventMaskLeftMouseUp;
#else
  const NSEventType kTextDown = NSLeftMouseDown, kTextUp = NSLeftMouseUp;
  const NSUInteger kTextUpMask = NSLeftMouseUpMask;
#endif
  unsigned clickCount;
  NSEventType forwarded[2];
  NSView *clickParent;
} // namespace
@interface LokaTextClickParent : NSView
@end
@implementation LokaTextClickParent
- (void)mouseDown:(NSEvent *)event
{
  LOKA_VERIFY(self == clickParent && clickCount < 2);
  forwarded[clickCount++] = [event type];
}
- (void)mouseUp:(NSEvent *)event
{
  LOKA_VERIFY(self == clickParent && clickCount < 2);
  forwarded[clickCount++] = [event type];
}
@end

namespace
{
  void VerifyClick(NSWindow *window, NSView *target, bool key)
  {
    if (key)
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
      [window makeKeyAndOrderFront:nil];
      [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    }
    else
      [window resignKeyWindow];
    if (([window isKeyWindow] != NO) != key)
    {
      std::printf("[skip] %s click on %s: window cannot establish intended key state\n",
                  key ? "key" : "non-key",
                  [NSStringFromClass([target class]) UTF8String]);
      return;
    }
    NSPoint point = [target convertPoint:NSMakePoint(4, 4) toView:nil];
    LOKA_VERIFY([clickParent hitTest:[clickParent convertPoint:point fromView:nil]] == target);
    NSEvent *down = [NSEvent mouseEventWithType:kTextDown
                                       location:point
                                  modifierFlags:0
                                      timestamp:1
                                   windowNumber:[window windowNumber]
                                        context:nil
                                    eventNumber:31
                                     clickCount:1
                                       pressure:1];
    NSEvent *up = [NSEvent mouseEventWithType:kTextUp
                                     location:point
                                modifierFlags:0
                                    timestamp:1.1
                                 windowNumber:[window windowNumber]
                                      context:nil
                                  eventNumber:32
                                   clickCount:1
                                     pressure:0];
    LOKA_VERIFY([target acceptsFirstMouse:down]);
    clickCount = 0;
    [NSApp postEvent:up atStart:NO];
    [window sendEvent:down];
    NSEvent *remaining = [NSApp nextEventMatchingMask:kTextUpMask
                                            untilDate:[NSDate dateWithTimeIntervalSinceNow:0.2]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
    if (remaining)
      [window sendEvent:remaining];
    LOKA_VERIFY(clickCount == 2 && forwarded[0] == kTextDown && forwarded[1] == kTextUp);
  }

  void PaintUnclipped(id self, SEL, NSRect)
  {
    NSLayoutManager *manager = AttributedAccess::manager(self);
    NSRange range = [manager glyphRangeForTextContainer:AttributedAccess::container(self)];
    [manager drawGlyphsForGlyphRange:range atPoint:NSZeroPoint];
  }
  NSBitmapImageRep *CaptureParent(NSView *parent)
  {
    NSBitmapImageRep *image = [parent bitmapImageRepForCachingDisplayInRect:[parent bounds]];
    LOKA_VERIFY(image);
    [parent cacheDisplayInRect:[parent bounds] toBitmapImageRep:image];
    return image;
  }
  unsigned ChangedColumns(NSBitmapImageRep *before, NSBitmapImageRep *after, NSInteger left, NSInteger right)
  {
    unsigned changed = 0;
    for (NSInteger y = 0; y < [before pixelsHigh]; ++y)
      for (NSInteger x = left; x < right; ++x)
      {
        NSUInteger a[5] = {}, b[5] = {};
        [before getPixel:a atX:x y:y];
        [after getPixel:b atX:x y:y];
        if (std::memcmp(a, b, sizeof(a)) != 0)
          ++changed;
      }
    return changed;
  }
  BOOL UseRectDrawing(id, SEL);
  void VerifyViewClipping(NSView *parent, NSView *view)
  {
    if (![view respondsToSelector:@selector(setClipsToBounds:)])
    {
      std::printf("[skip] native bounds clipping selector unavailable\n");
      return;
    }
    LOKA_VERIFY([view clipsToBounds]);
    NSRect original = [view frame];
    [view setFrame:NSMakeRect(0, 40, 8, 40)];
    MacNativeRefusal fallback(@selector(wantsUpdateLayer), (IMP)UseRectDrawing, [view class]);
    MacNativeRefusal paint(@selector(drawRect:), (IMP)PaintUnclipped, [view class]);
    [view setHidden:YES];
    NSBitmapImageRep *empty = CaptureParent(parent);
    [view setHidden:NO];
    [view setClipsToBounds:NO];
    [view setNeedsDisplay:YES];
    NSBitmapImageRep *unclipped = CaptureParent(parent);
    const NSInteger edge = 8 * [empty pixelsWide] / (NSInteger)[parent bounds].size.width;
    LOKA_VERIFY(ChangedColumns(empty, unclipped, 0, edge));
    [view setClipsToBounds:YES];
    [view setNeedsDisplay:YES];
    NSBitmapImageRep *clipped = CaptureParent(parent);
    if (ChangedColumns(empty, unclipped, edge, edge * 5))
      LOKA_VERIFY(!ChangedColumns(empty, clipped, edge, edge * 5));
    else
      std::printf("[skip] native clipsToBounds pixel isolation: host clips with property disabled\n");
    [view setFrame:original];
  }
  IMP originalSubviews;
  unsigned emptyForwards;
  void CountEmptySubviews(id self, SEL cmd, NSArray *views)
  {
    LOKA_VERIFY(![views count]);
    ++emptyForwards;
    ((void (*)(id, SEL, NSArray *))originalSubviews)(self, cmd, views);
  }
  void AttemptChild(NSView *view, NSView *child, NSArray *children, int door)
  {
    if (door == 0)
      [view addSubview:child];
    else if (door == 1)
      [view addSubview:child positioned:NSWindowAbove relativeTo:nil];
    else
      [view setSubviews:children];
  }
  unsigned layerPaints;
  IMP layerDraw;
  void CountLayerPaint(id self, SEL cmd, NSRect rect)
  {
    ++layerPaints;
    ((void (*)(id, SEL, NSRect))layerDraw)(self, cmd, rect);
  }
} // namespace

void testMacAttributedTextWindow()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSWindow *window = [[[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, 240, 160)
                                                  styleMask:LOKA_MAC_WINDOW_STYLE_TITLED
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO] autorelease];
  if (!window || ![NSScreen mainScreen])
  {
    std::printf("[skip] attributed text window: no WindowServer screen\n");
    [pool drain];
    return;
  }
  LokaTextClickParent *root = [[[LokaTextClickParent alloc] initWithFrame:NSMakeRect(0, 0, 240, 160)] autorelease];
  [window setContentView:root];
  [window orderFrontRegardless];
  clickParent = root;
  {
    MacScenePlatformController controller(root, RailMetrics());
    AttributedTextNode node((AttributedTextProps(Styled("leaf", Italic))));
    NotifySubtreeNodeAttached(&node);
    LayoutState state;
    state.width = 80;
    node.layoutProjected(&controller, state);
    NSView *view = AttributedAccess::view(AttributedContext(node));
    LOKA_VERIFY([view window] == window && [view isFlipped] && ![[view subviews] count]);
    NSTextField *label = [[[NSTextField alloc] initWithFrame:NSMakeRect(100, 0, 90, 30)] autorelease];
    [label setEditable:NO];
    [label setSelectable:NO];
    [label setBezeled:NO];
    [label setDrawsBackground:NO];
    [label setStringValue:@"old label"];
    [root addSubview:label];
    for (int key = 0; key < 2; ++key)
    {
      VerifyClick(window, label, key != 0);
      VerifyClick(window, view, key != 0);
    }
    VerifyViewClipping(root, view);
    NSTextView *sibling = [[[NSTextView alloc] initWithFrame:NSMakeRect(0, 80, 160, 30)] autorelease];
    [root addSubview:sibling];
    LOKA_VERIFY([window makeFirstResponder:sibling] && [window firstResponder] == sibling);
    if ([NSView instancesRespondToSelector:@selector(setSubviews:)])
    {
      emptyForwards = 0;
      originalSubviews = class_getMethodImplementation([NSView class], @selector(setSubviews:));
      MacNativeRefusal forwardedEmpty(@selector(setSubviews:), (IMP)CountEmptySubviews, [NSView class]);
      [view setSubviews:[NSArray array]];
      LOKA_VERIFY(emptyForwards == 1);
    }
    else
      [view setSubviews:[NSArray array]];
    LOKA_VERIFY(![[view subviews] count]);
#ifdef NDEBUG
    for (int i = 0; i < 3; ++i)
    {
      AttemptChild(view, sibling, [NSArray arrayWithObject:sibling], i);
      LOKA_VERIFY(![[view subviews] count] && [sibling superview] == root);
    }
#endif
    if ([view respondsToSelector:@selector(setWantsLayer:)])
    {
      Class leaf = [view class];
      layerDraw = class_getMethodImplementation(leaf, @selector(drawTextInRect:));
      MacNativeRefusal record(@selector(drawTextInRect:), (IMP)CountLayerPaint, leaf);
      [view setWantsLayer:YES];
      [view display];
      if (![view layer])
        std::printf("[skip] attributed resize: layer unavailable\n");
      else
      {
        layerPaints = 0;
        [view setFrameSize:NSMakeSize(70, 60)];
        [window displayIfNeeded];
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
        LOKA_VERIFY(layerPaints);
        if ([view respondsToSelector:@selector(layerContentsRedrawPolicy)])
          LOKA_VERIFY([view layerContentsRedrawPolicy] == LOKA_MAC_LAYER_REDRAW_DURING_VIEW_RESIZE);
        else
          std::printf("[skip] attributed resize: redraw policy getter unavailable\n");
        VerifyTextKitWidth(view, 80);
      }
    }
    else
      std::printf("[skip] attributed resize: layer selector unavailable\n");
    NSScrollView *scroll = [[[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 40, 20)] autorelease];
    [root addSubview:scroll];
    [view setFrameSize:NSMakeSize(120, 140)];
    [scroll setDocumentView:view];
    [[scroll contentView] scrollToPoint:NSMakePoint(1, 2)];
    LOKA_VERIFY([[scroll contentView] bounds].origin.y == 2);
    LOKA_VERIFY(![[view subviews] count]);
    [scroll setDocumentView:nil];
  }
  clickParent = nil;
  [window orderOut:nil];
  [pool drain];
}

#ifndef NDEBUG
#include <cerrno>
#include <crt_externs.h>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
  const char *const childActionKey = "LOKA_MAC_ATTRIBUTED_CHILD";
  const char *const childActions[] = {"addSubview", "addSubviewPositioned", "setSubviewsNonEmpty"};

  void ForwardedNonEmptySubviews(id, SEL, NSArray *)
  {
    // A forwarded setSubviews: can reenter the leaf's addSubview: assertion.
    // Exit at the super send so that assertion cannot satisfy the death test.
    _exit(42);
  }

  void RunAttributedChildAction(const char *action)
  {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
    NSView *view = [[[NSClassFromString(@"LokaAttributedTextView") alloc] initWithFrame:NSZeroRect] autorelease];
    NSView *child = [[[NSView alloc] initWithFrame:NSZeroRect] autorelease];
    NSArray *children = [NSArray arrayWithObject:child];
    LOKA_VERIFY(view);
    for (int door = 0; door < 3; ++door)
    {
      if (std::strcmp(action, childActions[door]) != 0)
        continue;
      if (door == 2 && [NSView instancesRespondToSelector:@selector(setSubviews:)])
      {
        MacNativeRefusal intercept(@selector(setSubviews:), (IMP)ForwardedNonEmptySubviews, [NSView class]);
        AttemptChild(view, child, children, door);
      }
      else
        AttemptChild(view, child, children, door);
      break;
    }
    [pool drain];
    _exit(1);
  }

  void VerifyAttributedChildAssertion(const char *action,
                                      const char *testName = "testMacAttributedTextChildAssertions",
                                      const char *diagnostic = "AttributedText cannot host children")
  {
    // The runner's single-name entry executes only the selected test in the new image.
    char *binary = (*_NSGetArgv())[0];
    char *args[] = {binary, const_cast<char *>(testName), 0};
    std::string setting = std::string(childActionKey) + "=" + action;
    std::vector<char *> environment;
    for (char **entry = *_NSGetEnviron(); *entry; ++entry)
      environment.push_back(*entry);
    environment.push_back(const_cast<char *>(setting.c_str()));
    environment.push_back(0);

    FILE *errors = std::tmpfile();
    LOKA_VERIFY(errors);
    posix_spawn_file_actions_t redirects;
    LOKA_VERIFY(posix_spawn_file_actions_init(&redirects) == 0);
    LOKA_VERIFY(posix_spawn_file_actions_adddup2(&redirects, fileno(errors), STDERR_FILENO) == 0);
    pid_t pid;
    int launched = posix_spawnp(&pid, binary, &redirects, 0, args, &environment[0]);
    posix_spawn_file_actions_destroy(&redirects);
    LOKA_VERIFY(launched == 0);

    int status = 0;
    pid_t result = 0;
    for (int poll = 0; poll < 1000; ++poll)
    {
      result = waitpid(pid, &status, WNOHANG);
      if (result == pid)
        break;
      LOKA_VERIFY(result == 0 || errno == EINTR);
      usleep(10000);
    }
    if (result != pid)
    {
      kill(pid, SIGKILL);
      while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
      {
      }
      std::fprintf(stderr, "Timed out waiting for attributed child %s\n", action);
    }
    std::rewind(errors);
    char line[1024];
    bool diagnosed = false;
    while (std::fgets(line, sizeof(line), errors))
      if (std::strstr(line, diagnostic))
        diagnosed = true;
    std::fclose(errors);
    if (result != pid || !WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT || !diagnosed)
      std::fprintf(
          stderr, "Attributed child %s: wait status %d, expected assertion seen %d\n", action, status, diagnosed);
    LOKA_VERIFY(result == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT && diagnosed);
  }
} // namespace
#endif

void testMacAttributedTextChildAssertions()
{
#ifndef NDEBUG
  const char *action = std::getenv(childActionKey);
  if (action)
    RunAttributedChildAction(action);
  for (int i = 0; i < 3; ++i)
    VerifyAttributedChildAssertion(childActions[i]);
#else
  std::printf("[skip] child assertion requires debug assertions; release refusal exercised in window test\n");
#endif
}

void testMacAttributedTextLineAdvances()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 200, 240)] autorelease];
  {
    MacScenePlatformController controller(root, RailMetrics());
    AttributedTextNode node((AttributedTextProps(Styled("abc", TextStyle()))));
    LayoutState state;
    state.width = 200;
    node.layoutProjected(&controller, state);
    const short single = state.height;
    NSView *view = AttributedAccess::view(AttributedContext(node));
    const char *samples[] = {"abc\n", "\n"};
    for (int i = 0; i < 2; ++i)
    {
      node.props.text(Styled(samples[i], TextStyle()));
      node.getContext()->onPropsApplied();
      node.layout(&controller, state);
      NSAttributedString *text = AttributedAccess::projectionSnapshot(node);
      const CGFloat oracle = ReferenceTextGeometry(text, state.width).size.height;
      LOKA_VERIFY(state.height == controller.projection().measurementToLu(oracle) && state.height > single);
      std::printf("LF fixture %d cell=%g TextKit=%g\n",
                  i,
                  (double)CellAdvance(text, NSLineBreakByClipping, state.width),
                  (double)oracle);
    }
    node.props.text(Styled("abc defgh ijklmnop", TextStyle()));
    // Choose a width that fits a prefix of the second word, with the native font on this rig.
    state.width = (short)std::ceil(
        [@"abc de" sizeWithAttributes:[NSDictionary dictionaryWithObject:(NSFont *)controller.textFont(TextStyle())
                                                                  forKey:NSFontAttributeName]]
            .width
        + 4);
    NSRange lines[2];
    for (int i = 0; i < 2; ++i)
    {
      node.props.blockStyle_ = BlockStyle().wrap(i == 0 ? TEXT_WRAP_WORD : TEXT_WRAP_CHAR);
      node.getContext()->onPropsApplied();
      node.layout(&controller, state);
      NSAttributedString *text = AttributedAccess::projectionSnapshot(node);
      [AttributedAccess::manager(view) lineFragmentRectForGlyphAtIndex:0 effectiveRange:&lines[i]];
      LOKA_VERIFY(state.height
                  == controller.projection().measurementToLu(ReferenceTextGeometry(text, state.width).size.height));
    }
    LOKA_VERIFY(lines[0].length < lines[1].length);
    std::string tall;
    for (int i = 0; i < 3000; ++i)
      tall.append("a\n");
    node.props.text(Styled(RawAttributed(tall.data(), tall.size()), FontSize<24>()));
    node.getContext()->onPropsApplied();
    state.y = 20;
    short bottom = node.layout(&controller, state);
    LOKA_VERIFY([AttributedAccess::manager(view) usedRectForTextContainer:AttributedAccess::container(view)].size.height
                > SHRT_MAX);
    LOKA_VERIFY(state.height == SHRT_MAX && bottom == SHRT_MAX);
  }
  [pool drain];
}

namespace
{
  struct HitTestPool
  {
    NSAutoreleasePool *pool;
    HitTestPool()
        : pool([[NSAutoreleasePool alloc] init])
    {
      [NSApplication sharedApplication];
    }
    ~HitTestPool()
    {
      [pool drain];
    }
  };

  struct HitLayout
  {
    MacScenePlatformController controller;
    AttributedTextNode node;
    LayoutState state;
    NSView *view;
    NSLayoutManager *manager;
    NSTextContainer *container;
    HitLayout(const AttributedString &value, short width = 200, const BlockStyle &block = BlockStyle())
        : controller([[[NSView alloc] initWithFrame:NSZeroRect] autorelease], RailMetrics()),
          node((AttributedText(value) + block).props)
    {
      state.width = width;
      node.layoutProjected(&controller, state);
      view = AttributedAccess::view(AttributedContext(node));
      manager = AttributedAccess::manager(view);
      container = AttributedAccess::container(view);
      LOKA_VERIFY(AttributedAccess::complete(AttributedContext(node)));
    }

    NSUInteger lookup(NSPoint p) const
    {
      return [manager glyphIndexForPoint:p inTextContainer:container fractionOfDistanceThroughGlyph:NULL];
    }

    NSUInteger glyph(NSUInteger character) const
    {
      return [manager glyphRangeForCharacterRange:NSMakeRange(character, 1) actualCharacterRange:NULL].location;
    }

    void expect(NSPoint point, MacTextHitAnswer answer, size_t first = 0, size_t last = 0)
    {
      MacTextHit out = {TextSegmentRange(97).through(99), -1};
      const MacTextHitAnswer actual = AttributedAccess::prepareSourceHit(AttributedContext(node), point, out);
      if (actual != answer)
        std::fprintf(stderr, "hit at (%g, %g): expected %d, got %d\n", point.x, point.y, answer, actual);
      LOKA_VERIFY(actual == answer);
      if (answer == MAC_HIT_SOURCE)
      {
        LOKA_VERIFY(out.source.first() == first && out.source.last() == last);
        LOKA_VERIFY(out.containerWidth == AttributedAccess::width(view));
      }
      else
        LOKA_VERIFY(out.source.first() == 97 && out.source.last() == 99 && out.containerWidth == -1);
    }
  };

  // Independent oracle: copy the borrowed rectangle list before any further TextKit reads.
  struct HitCluster
  {
    NSRange chars, glyphs;
    NSRect line, fragment;
    std::vector<NSRect> rects;
    NSUInteger shown;
    CGFloat cutoff;
    HitCluster(const HitLayout &layout, NSUInteger glyph)
        : shown(0)
    {
      NSLayoutManager *m = layout.manager;
      LOKA_VERIFY(glyph < [m numberOfGlyphs]);
      chars = [m characterRangeForGlyphRange:NSMakeRange(glyph, 1) actualGlyphRange:&glyphs];
      NSRange actual;
      NSRange reverse = [m glyphRangeForCharacterRange:chars actualCharacterRange:&actual];
      LOKA_VERIFY(NSEqualRanges(actual, chars) && NSEqualRanges(reverse, glyphs));
      LOKA_VERIFY(NSEqualRanges([m characterRangeForGlyphRange:glyphs actualGlyphRange:NULL], chars));
      for (NSUInteger i = glyphs.location; i < NSMaxRange(glyphs); ++i)
        if (![m notShownAttributeForGlyphAtIndex:i])
          ++shown;
      line = [m lineFragmentUsedRectForGlyphAtIndex:glyph effectiveRange:NULL];
      fragment = [m lineFragmentRectForGlyphAtIndex:glyph effectiveRange:NULL];
      cutoff = NSMaxX(fragment) - [layout.container lineFragmentPadding];
      NSUInteger k = 0;
      NSRectArray r = [m rectArrayForCharacterRange:chars
                       withinSelectedCharacterRange:NSMakeRange(NSNotFound, 0)
                                    inTextContainer:layout.container
                                          rectCount:&k];
      if (k)
        rects.assign(r, r + k);
    }

    bool inRects(NSPoint p) const
    {
      for (size_t i = 0; i < rects.size(); ++i)
        if (NSMouseInRect(p, rects[i], YES))
          return true;
      return false;
    }

    NSRect visibleRect() const
    {
      LOKA_VERIFY(rects.size() == 1);
      return NSIntersectionRect(rects.front(), line);
    }
  };

  NSPoint HitCentre(NSRect r)
  {
    LOKA_VERIFY(!NSIsEmptyRect(r));
    return NSMakePoint(NSMidX(r), NSMidY(r));
  }

  void VerifyCharacterHit(HitLayout &layout,
                          NSUInteger character,
                          NSRange chars,
                          size_t first,
                          size_t last,
                          MacTextHitAnswer answer = MAC_HIT_SOURCE,
                          bool ambiguous = false)
  {
    HitCluster cluster(layout, layout.glyph(character));
    LOKA_VERIFY(NSEqualRanges(cluster.chars, chars) && cluster.shown);
    NSPoint p = HitCentre(cluster.visibleRect());
    LOKA_VERIFY(NSLocationInRange(layout.lookup(p), cluster.glyphs) && p.x < cluster.cutoff);
    LOKA_VERIFY(NSMouseInRect(p, [layout.view bounds], YES));
    if (ambiguous)
      VerifyNoSource(AttributedContext(layout.node), chars.location, NSMaxRange(chars));
    else
      VerifySource(AttributedContext(layout.node), chars.location, NSMaxRange(chars), first, last);
    layout.expect(p, answer, first, last);
  }

  void VerifyMixedHit(HitLayout &layout, NSUInteger start, size_t first, size_t last, MacTextHitAnswer answer)
  {
    HitCluster cluster(layout, layout.glyph(start));
    // The mixed shown/hidden shape is the host's glyph choice, not the hit contract; only that witness skips.
    LOKA_VERIFY(cluster.shown);
    if (cluster.shown == cluster.glyphs.length)
      std::printf("[skip] mixed shown/hidden cluster at character %lu: every glyph is shown on this host\n",
                  static_cast<unsigned long>(start));
    for (NSUInteger g = cluster.glyphs.location; g < NSMaxRange(cluster.glyphs); ++g)
    {
      NSRange actual;
      NSRange chars = [layout.manager characterRangeForGlyphRange:NSMakeRange(g, 1) actualGlyphRange:&actual];
      LOKA_VERIFY(NSEqualRanges(chars, cluster.chars) && NSEqualRanges(actual, cluster.glyphs));
    }
    VerifyCharacterHit(layout, start, NSMakeRange(start, 2), first, last, answer, answer == MAC_HIT_REFUSED);
  }
} // namespace

void testMacTextHitSources()
{
  HitTestPool pool;
  const TextStyle plain;
  HitLayout layout(Styled("a", plain) + Styled("", plain) + Styled("b c", plain) + Styled("d", Italic));
  const size_t segments[] = {0, 2, 2, 2, 3};
  for (NSUInteger i = 0; i < 5; ++i)
    VerifyCharacterHit(layout, i, NSMakeRange(i, 1), segments[i], segments[i]);
}

void testMacTextHitClusters()
{
  HitTestPool pool;
  const TextStyle plain;
  HitLayout split(Styled("e", plain) + Styled("\xCC\x81x", plain));
  VerifyMixedHit(split, 0, 0, 1, MAC_HIT_SOURCE);
  HitLayout whole(Styled("e\xCC\x81x", plain) + Styled("\xF0\x9F\x98\x80", plain));
  VerifyMixedHit(whole, 0, 0, 0, MAC_HIT_SOURCE);
  VerifyMixedHit(whole, 3, 1, 1, MAC_HIT_SOURCE);
  HitLayout ambiguous(Styled(RawAttributed("a\xF0\x9F", 3), plain)
                      + Styled(RawAttributed("\x98\x80"
                                             "b",
                                             3),
                               plain));
  VerifyCharacterHit(ambiguous, 0, NSMakeRange(0, 1), 0, 0);
  VerifyMixedHit(ambiguous, 1, 0, 0, MAC_HIT_REFUSED);
  VerifyCharacterHit(ambiguous, 3, NSMakeRange(3, 1), 1, 1);

  HitLayout ligature(Linked("f", plain, LinkTarget(loka::core::String("one")))
                     + Linked("i", plain, LinkTarget(loka::core::String("two"))));
  HitCluster f(ligature, ligature.glyph(0));
  NSFont *font = TextAttribute(AttributedAccess::storage(ligature.view), NSFontAttributeName, 0);
  if (NSEqualRanges(f.chars, NSMakeRange(0, 1)))
    std::printf("[skip] ligature not formed by %s on this host\n", [[font fontName] UTF8String]);
  else
  {
    std::printf("fi ligature formed by %s\n", [[font fontName] UTF8String]);
    VerifyCharacterHit(ligature, 0, NSMakeRange(0, 2), 0, 1);
  }
}

namespace
{
  AttributedString GutterText(bool rtl)
  {
    // One segment per character, including the Latin space that straddled the probe cutoff.
    const char *text = rtl ? "אבגדהוזחטי" : "Open the documentation";
    const size_t stride = rtl ? 2 : 1;
    AttributedString::Builder builder(std::strlen(text) / stride);
    for (size_t i = 0; text[i]; i += stride)
      LOKA_VERIFY(builder.append(RawAttributed(text + i, stride), TextStyle()));
    return builder.build();
  }

  void VerifyGutter(HitLayout &layout)
  {
    HitCluster first(layout, 0);
    NSPoint p = NSMakePoint(first.cutoff, NSMidY(first.line));
    const CGFloat padding = [layout.container lineFragmentPadding];
    // Identify the visual advance crossing the cutoff independently of point lookup.
    CGFloat left = -CGFLOAT_MAX, right = CGFLOAT_MAX;
    NSUInteger crossing = NSNotFound;
    for (NSUInteger g = 0; g < [layout.manager numberOfGlyphs]; ++g)
    {
      const CGFloat x = NSMinX(first.fragment) + [layout.manager locationForGlyphAtIndex:g].x;
      if (x < first.cutoff && x > left)
      {
        left = x;
        crossing = g;
      }
      if (x >= first.cutoff && x < right)
        right = x;
    }
    if (crossing != NSNotFound && right != CGFLOAT_MAX && right > first.cutoff)
    {
      HitCluster c(layout, crossing);
      p.x = (left + first.cutoff) / 2;
      if (c.shown && layout.lookup(p) == crossing && c.inRects(p) && NSMouseInRect(p, c.line, YES))
      {
        VerifySource(AttributedContext(layout.node), c.chars.location, NSMaxRange(c.chars), crossing, crossing);
        layout.expect(p, MAC_HIT_SOURCE, crossing, crossing);
      }
      else
        std::printf("[skip] cutoff-crossing advance lacks eligible lookup\n");
    }
    else
      std::printf("[skip] no advance straddles the gutter edge\n");

    for (unsigned wider = 0; wider < 2; ++wider)
    {
      if (wider)
        [layout.view setFrameSize:NSMakeSize(NSMaxX(first.fragment) + padding, NSHeight([layout.view bounds]))];
      for (unsigned interior = 0; interior < 2; ++interior)
      {
        p.x = first.cutoff + interior * padding / 2;
        HitCluster c(layout, layout.lookup(p));
        LOKA_VERIFY(NSMouseInRect(p, [layout.view bounds], YES));
        if (c.shown && c.inRects(p) && NSMouseInRect(p, c.line, YES))
        {
          VerifySource(AttributedContext(layout.node),
                       c.chars.location,
                       NSMaxRange(c.chars),
                       c.chars.location,
                       NSMaxRange(c.chars) - 1);
        }
        else
          std::printf("[skip] gutter x=%g lacks eligible rect/line lookup\n", p.x);
        layout.expect(p, MAC_HIT_NONE);
      }
    }
    p.x = NSMaxX(first.fragment);
    LOKA_VERIFY(NSMouseInRect(p, [layout.view bounds], YES));
    layout.expect(p, MAC_HIT_NONE);
  }
} // namespace

void testMacTextHitBidi()
{
  HitTestPool pool;
  TextStyle plain;
  HitLayout mixed(Styled("A", plain) + Styled("א", plain) + Styled("ב", plain) + Styled("Z", plain));
  HitCluster aleph(mixed, mixed.glyph(1)), bet(mixed, mixed.glyph(2));
  NSRect right = aleph.visibleRect(), left = bet.visibleRect();
  LOKA_VERIFY(NSMaxX(left) == NSMinX(right));
  CGFloat inset = std::min(NSWidth(left), NSWidth(right)) / 8;
  for (unsigned i = 0; i < 2; ++i)
  {
    const NSUInteger index = i ? 1 : 2;
    NSPoint p = NSMakePoint(NSMaxX(left) + (i ? inset : -inset), NSMidY(left));
    HitCluster c(mixed, mixed.lookup(p));
    LOKA_VERIFY(c.chars.location == index && c.chars.length == 1 && c.shown && c.inRects(p));
    LOKA_VERIFY(NSMouseInRect(p, c.line, YES) && p.x < c.cutoff);
    VerifySource(AttributedContext(mixed.node), index, index + 1, index, index);
    mixed.expect(p, MAC_HIT_SOURCE, index, index);
  }
  const TextAlign alignments[] = {TEXT_ALIGN_LEFT, TEXT_ALIGN_CENTER, TEXT_ALIGN_RIGHT};
  for (unsigned rtl = 0; rtl < 2; ++rtl)
    for (unsigned a = 0; a < 3; ++a)
    {
      HitLayout clipped(GutterText(rtl != 0), 40, BlockStyle().align(alignments[a]));
      std::printf("clipped gutter rtl=%u alignment=%u\n", rtl, a);
      VerifyGutter(clipped);
    }
}

namespace
{
  void VerifyHiddenCharacter(HitLayout &layout, NSUInteger character, NSLineBreakMode mode, const char *name)
  {
    NSParagraphStyle *style =
        TextAttribute(AttributedAccess::storage(layout.view), NSParagraphStyleAttributeName, character);
    LOKA_VERIFY([style lineBreakMode] == mode);
    HitCluster cluster(layout, layout.glyph(character));
    LOKA_VERIFY(NSEqualRanges(cluster.chars, NSMakeRange(character, 1)));
    VerifySource(AttributedContext(layout.node), character, character + 1, 0, 0);
    for (size_t r = 0; !cluster.shown && r < cluster.rects.size(); ++r)
    {
      const NSRect area = NSIntersectionRect(cluster.rects[r], cluster.line);
      if (NSIsEmptyRect(area))
        continue;
      for (unsigned i = 0; i < 16; ++i)
      {
        NSPoint p = NSMakePoint(NSMinX(area) + NSWidth(area) * (i + 0.5) / 16, NSMidY(area));
        if (p.x < cluster.cutoff && NSMouseInRect(p, [layout.view bounds], YES)
            && NSLocationInRange(layout.lookup(p), cluster.glyphs))
        {
          layout.expect(p, MAC_HIT_NONE);
          return;
        }
      }
    }
    std::printf("[skip] %s lacks an all-hidden lookup in its visible advance\n", name);
  }

  void VerifyLeadingGap(HitLayout &layout, MacTextHitAnswer answer)
  {
    HitCluster first(layout, 0);
    NSRect rect = first.visibleRect();
    NSPoint p = NSMakePoint((NSMinX(first.line) + NSMinX(rect)) / 2, NSMidY(rect));
    if (layout.lookup(p) != 0 || !first.shown || first.inRects(p) || !NSMouseInRect(p, first.line, YES)
        || p.x >= first.cutoff)
    {
      std::printf("[skip] no shown first-letter lookup in the leading padding gap\n");
      return;
    }
    VerifySource(AttributedContext(layout.node), first.chars.location, NSMaxRange(first.chars), 0, 0);
    layout.expect(p, answer);
  }

  void VerifyNearestMiss(HitLayout &layout, NSPoint p)
  {
    HitCluster nearest(layout, layout.lookup(p));
    LOKA_VERIFY(NSMouseInRect(p, [layout.view bounds], YES) && nearest.shown && p.x < nearest.cutoff);
    LOKA_VERIFY(!NSMouseInRect(p, nearest.line, YES));
    VerifySource(AttributedContext(layout.node), nearest.chars.location, NSMaxRange(nearest.chars), 0, 0);
    layout.expect(p, MAC_HIT_NONE);
  }

  bool VerifyOutsideUsedLine(HitLayout &layout)
  {
    const NSRect bounds = [layout.view bounds];
    for (NSUInteger g = 0; g < [layout.manager numberOfGlyphs]; ++g)
    {
      HitCluster c(layout, g);
      if (!c.shown)
        continue;
      for (size_t r = 0; r < c.rects.size(); ++r)
      {
        NSRect rect = NSIntersectionRect(c.rects[r], bounds);
        const CGFloat x = NSMinX(rect), y = NSMinY(rect);
        const CGFloat end = std::min(NSMaxX(rect), c.cutoff);
        if (end <= x || NSIsEmptyRect(rect))
          continue;
        rect.size.width = end - x;
        // Probe only the portions left after subtracting the used line.
        const NSRect bands[] = {NSMakeRect(x, y, NSMinX(c.line) - x, NSHeight(rect)),
                                NSMakeRect(NSMaxX(c.line), y, end - NSMaxX(c.line), NSHeight(rect)),
                                NSMakeRect(x, y, NSWidth(rect), NSMinY(c.line) - y),
                                NSMakeRect(x, NSMaxY(c.line), NSWidth(rect), NSMaxY(rect) - NSMaxY(c.line))};
        for (unsigned b = 0; b < 4; ++b)
        {
          if (NSIsEmptyRect(bands[b]))
            continue;
          NSRect area = NSIntersectionRect(rect, bands[b]);
          if (NSIsEmptyRect(area))
            continue;
          NSPoint p = HitCentre(area);
          if (!NSLocationInRange(layout.lookup(p), c.glyphs))
            continue;
          LOKA_VERIFY(c.inRects(p) && !NSMouseInRect(p, c.line, YES) && p.x < c.cutoff);
          VerifySource(AttributedContext(layout.node), c.chars.location, NSMaxRange(c.chars), 0, 0);
          layout.expect(p, MAC_HIT_NONE);
          return true;
        }
      }
    }
    return false;
  }
} // namespace

void testMacTextHitWhitespace()
{
  HitTestPool pool;
  HitLayout layout(Styled("a\tb\nc d", TextStyle()), 300, BlockStyle().wrap(TEXT_WRAP_WORD));
  VerifyHiddenCharacter(layout, 1, NSLineBreakByWordWrapping, "tab");
  VerifyHiddenCharacter(layout, 3, NSLineBreakByWordWrapping, "newline");
  VerifyCharacterHit(layout, 5, NSMakeRange(5, 1), 0, 0);
}

void testMacTextHitEdges()
{
  HitTestPool pool;
  HitLayout layout(Styled("Visible", TextStyle()));
  HitCluster first(layout, 0);
  NSRect rect = first.visibleRect(), bounds = [layout.view bounds];
  NSPoint centre = HitCentre(rect);
  LOKA_VERIFY(layout.lookup(centre) == 0 && first.shown && centre.x < first.cutoff);
  VerifySource(AttributedContext(layout.node), first.chars.location, NSMaxRange(first.chars), 0, 0);
  [layout.view setFrameSize:NSMakeSize(centre.x, NSHeight(bounds))];
  layout.expect(centre, MAC_HIT_NONE);
  NSPoint p = NSMakePoint((NSMinX(rect) + centre.x) / 2, centre.y);
  LOKA_VERIFY(layout.lookup(p) == 0);
  layout.expect(p, MAC_HIT_SOURCE);
  layout.expect(NSMakePoint((centre.x + NSMaxX(rect)) / 2, centre.y), MAC_HIT_NONE);
  [layout.view setFrameSize:NSMakeSize(NSWidth(bounds), centre.y)];
  layout.expect(centre, MAC_HIT_NONE);
  p = NSMakePoint(centre.x, (NSMinY(rect) + centre.y) / 2);
  LOKA_VERIFY(layout.lookup(p) == 0);
  layout.expect(p, MAC_HIT_SOURCE);
  [layout.view setFrameSize:NSMakeSize(NSWidth(bounds), NSHeight(bounds) * 2)];
  p.y = NSMinY(bounds);
  LOKA_VERIFY(layout.lookup(p) == 0 && first.inRects(p) && NSMouseInRect(p, first.line, YES));
  layout.expect(p, MAC_HIT_SOURCE);
  p = NSMakePoint(NSMinX(bounds), centre.y);
  LOKA_VERIFY(!first.inRects(p) && layout.lookup(p) == 0);
  layout.expect(p, MAC_HIT_NONE);
  VerifyLeadingGap(layout, MAC_HIT_NONE);
  p = NSMakePoint((NSMaxX(first.line) + first.cutoff) / 2, centre.y);
  VerifyNearestMiss(layout, p);
  p = NSMakePoint(centre.x, (NSMaxY(first.line) + NSMaxY([layout.view bounds])) / 2);
  VerifyNearestMiss(layout, p);
  // Inclusive minimum-x edge: move the bounds origin onto a glyph-interior x so the edge point is a source hit.
  [layout.view setBoundsOrigin:NSMakePoint(centre.x, NSMinY(bounds))];
  p = NSMakePoint(NSMinX([layout.view bounds]), centre.y);
  LOKA_VERIFY(p.x == centre.x && layout.lookup(p) == 0);
  layout.expect(p, MAC_HIT_SOURCE);
  layout.expect(NSMakePoint(p.x - 0.5, p.y), MAC_HIT_NONE);
  [layout.view setBoundsOrigin:NSMakePoint(NSMinX(bounds), NSMinY(bounds))];
  HitLayout wrapped(
      Styled("wrapped words\nwith trailing spaces   ", TextStyle()), 48, BlockStyle().wrap(TEXT_WRAP_WORD));
  HitLayout clipped(Styled("Open the documentation", TextStyle()), 40);
  if (!VerifyOutsideUsedLine(layout) && !VerifyOutsideUsedLine(wrapped) && !VerifyOutsideUsedLine(clipped))
    std::printf("[skip] no shown candidate rectangle outside its used line left of the gutter\n");
}

void testMacTextHitTruncation()
{
  HitTestPool pool;
  const BlockStyle tail = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
  HitLayout overflow(Styled("Open the documentation now", TextStyle()), 60, tail);
  HitLayout fits(Styled("Fits", TextStyle()), 300, tail);
  const bool supported =
      [overflow.manager respondsToSelector:@selector(truncatedGlyphRangeInLineFragmentForGlyphAtIndex:)];
  std::printf("[skip] %s branch unavailable (fragment selector %s)\n",
              supported ? "selector-less" : "selector-present",
              supported ? "present" : "absent");
  const MacTextHitAnswer ordinary = supported ? MAC_HIT_SOURCE : MAC_HIT_REFUSED;
  VerifyCharacterHit(overflow, 0, NSMakeRange(0, 1), 0, 0, ordinary);
  if ([fits.manager respondsToSelector:@selector(truncatedGlyphRangeInLineFragmentForGlyphAtIndex:)])
    LOKA_VERIFY([fits.manager truncatedGlyphRangeInLineFragmentForGlyphAtIndex:0].location == NSNotFound);
  VerifyCharacterHit(fits, 0, NSMakeRange(0, 1), 0, 0, ordinary);
  VerifyLeadingGap(fits, supported ? MAC_HIT_NONE : MAC_HIT_REFUSED);
  HitLayout tab(Styled("a\tb", TextStyle()), 300, tail);
  VerifyHiddenCharacter(tab, 1, NSLineBreakByTruncatingTail, "tail-mode tab");
  for (NSUInteger g = 1; g < [overflow.manager numberOfGlyphs]; ++g)
  {
    HitCluster carrier(overflow, g);
    if (!carrier.shown || carrier.chars.length <= 1 || NSMaxRange(carrier.glyphs) != [overflow.manager numberOfGlyphs])
      continue;
    if (supported)
    {
      const NSRange truncated = [overflow.manager truncatedGlyphRangeInLineFragmentForGlyphAtIndex:g];
      LOKA_VERIFY(truncated.location == g && NSIntersectionRange(truncated, carrier.glyphs).length);
    }
    NSPoint p = HitCentre(carrier.visibleRect());
    LOKA_VERIFY(p.x < carrier.cutoff && NSLocationInRange(overflow.lookup(p), carrier.glyphs));
    VerifySource(AttributedContext(overflow.node), carrier.chars.location, NSMaxRange(carrier.chars), 0, 0);
    overflow.expect(p, supported ? MAC_HIT_NONE : MAC_HIT_REFUSED);
    return;
  }
  std::printf("[skip] no nonzero ellipsis carrier covering the tail\n");
}

void testMacTextHitRefusals()
{
  HitTestPool pool;
  HitLayout layout(Styled("ok", TextStyle()));
  const CGFloat applied = AttributedAccess::width(layout.view);
  NSSize size = [layout.container containerSize];
  size.width *= 2;
  [layout.container setContainerSize:size];
  [layout.manager glyphRangeForTextContainer:layout.container];
  LOKA_VERIFY([layout.container containerSize].width != applied && AttributedAccess::width(layout.view) == applied);
  VerifyCharacterHit(layout, 0, NSMakeRange(0, 1), 0, 0);
  const NSPoint prior = HitCentre(HitCluster(layout, 0).visibleRect());
  layout.node.props.text(Styled(RawAttributed("\xC0\xAF", 2), Italic));
  layout.node.getContext()->onPropsApplied();
  layout.node.layout(&layout.controller, layout.state);
  LOKA_VERIFY(!AttributedAccess::complete(AttributedContext(layout.node)));
  layout.expect(prior, MAC_HIT_REFUSED);

  HitLayout empty(AttributedString(), 100);
  LOKA_VERIFY(![empty.manager numberOfGlyphs] && !NSIsEmptyRect([empty.view bounds]));
  empty.expect(HitCentre([empty.view bounds]), MAC_HIT_NONE);

  HitLayout partial(Styled("one line\nsecond line\nthird line", TextStyle()), 80, BlockStyle().wrap(TEXT_WRAP_WORD));
  size = [partial.container containerSize];
  size.height = NSMaxY(HitCluster(partial, 0).fragment);
  [partial.container setContainerSize:size];
  NSRange prefix = [partial.manager glyphRangeForTextContainer:partial.container];
  if (!(prefix.location == 0 && prefix.length && prefix.length < [partial.manager numberOfGlyphs]))
  {
    std::printf("[skip] shortened container did not produce a nonempty proper prefix\n");
    return;
  }
  HitCluster first(partial, 0);
  NSPoint p = HitCentre(first.visibleRect());
  LOKA_VERIFY(first.shown && partial.lookup(p) == 0 && p.x < first.cutoff);
  VerifySource(AttributedContext(partial.node), first.chars.location, NSMaxRange(first.chars), 0, 0);
  partial.expect(p, MAC_HIT_REFUSED);
}

namespace
{
  NSWindow *borrowedWindow;
  NSWindow *ReturnBorrowedWindow(id, SEL)
  {
    return borrowedWindow;
  }
  CALayer *testLayer;
  CALayer *ReturnTestLayer(id, SEL)
  {
    return testLayer;
  }
  CALayer *UnexpectedLayer(id, SEL)
  {
    LOKA_VERIFY(false && "unexpected layer read");
    return nil;
  }
  NSRect UnexpectedBounds(id, SEL)
  {
    LOKA_VERIFY(false && "unexpected geometry read");
    return NSZeroRect;
  }
  CGFloat DifferentScale(id, SEL)
  {
    return 37;
  }
  void RefuseLayerRequest(id, SEL, BOOL wants)
  {
    LOKA_VERIFY(!wants);
  }
  BOOL invertedFlip;
  BOOL OppositeFlip(id, SEL)
  {
    return invertedFlip;
  }
  IMP instancesRespond;
  BOOL HideUpdateLayer(id cls, SEL cmd, SEL query)
  {
    return query == @selector(wantsUpdateLayer) ? NO : ((BOOL(*)(id, SEL, SEL))instancesRespond)(cls, cmd, query);
  }

  NSWindow *PresentationWindow()
  {
    if (!MacTextLayerCapable())
    {
      std::puts("[skip] this artifact/runtime has no L");
      return nil;
    }
    NSWindow *window = [NSScreen mainScreen] ? [[[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 320, 160)
                                                                            styleMask:LOKA_MAC_WINDOW_STYLE_TITLED
                                                                              backing:NSBackingStoreBuffered
                                                                                defer:NO] autorelease]
                                             : nil;
    if (!window)
      std::puts("[skip] no window for the checked preparation");
    return window;
  }

  /** Complete one ordinary host display, including its implicit transaction. */
  void DisplayWindow(NSWindow *window)
  {
    [window displayIfNeeded];
    // A layer-backed window runs updateLayer when the implicit transaction commits, which an app's run loop
    // does at the end of the same turn (Tahoe 26.7.1: no image after displayIfNeeded alone, presented after the
    // flush in ZStack, Cell and ScrollView). The flush is that commit, not a second display of the leaf.
    [CATransaction flush];
  }

  /** One host display followed by a parent capture limited to the visible part of this leaf's text. */
  void VerifyRedFallbackInk(HitLayout &layout)
  {
    NSView *view = layout.view;
    NSView *parent = [view superview];
    NSRect text = [layout.manager usedRectForTextContainer:layout.container];
    NSRect region =
        NSIntersectionRect([parent bounds], [view convertRect:NSIntersectionRect(text, [view bounds]) toView:parent]);
    LOKA_VERIFY(!NSIsEmptyRect(region));
    [view setNeedsDisplay:YES];
    // Caching can itself draw. Witness foreground submission in the ordinary display before capturing.
    {
      SEL draw = @selector(drawGlyphsForGlyphRange:atPoint:);
      Class cls = [layout.manager class];
      drawInk = class_getMethodImplementation(cls, draw);
      MacNativeRefusal ink(draw, (IMP)RecordInk, cls);
      submitted.foregrounds = 0;
      DisplayWindow([view window]);
      LOKA_VERIFY(submitted.foregrounds && submitted.foreground.length);
    }
    MacPresentedText fact;
    LOKA_VERIFY(![view queryPresentedFact:&fact]);
    NSBitmapImageRep *image = [parent bitmapImageRepForCachingDisplayInRect:region];
    LOKA_VERIFY(image);
    [parent cacheDisplayInRect:region toBitmapImageRep:image];
    bool redInk = false;
    for (NSInteger y = 0; y < [image pixelsHigh] && !redInk; ++y)
      for (NSInteger x = 0; x < [image pixelsWide] && !redInk; ++x)
      {
        NSColor *color = [[image colorAtX:x y:y] colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
        redInk = [color alphaComponent] > 0.25 && [color redComponent] > [color greenComponent] + 0.25 &&
                 [color redComponent] > [color blueComponent] + 0.25;
      }
    LOKA_VERIFY(redInk);
    LOKA_VERIFY(![view queryPresentedFact:&fact]);
  }

  void DisplayText(NSView *view)
  {
    [view setNeedsDisplay:YES];
    [view displayIfNeeded];
    MacPresentedText fact;
    LOKA_VERIFY([view queryPresentedFact:&fact]);
  }

  struct CheckedLayout : HitLayout
  {
    CheckedLayout(NSWindow *window, const AttributedString &value)
        : HitLayout(value, 200, BlockStyle().wrap(TEXT_WRAP_WORD))
    {
      NSView *parent = [view superview];
      [parent setFrame:[[window contentView] bounds]];
      [window setContentView:parent];
      LOKA_VERIFY([view window] == window && ![window isVisible]);
      DisplayText(view);
    }

    NSPoint point()
    {
      return HitCentre(HitCluster(*this, glyph(0)).visibleRect());
    }

    void install(const AttributedString &value)
    {
      node.props.text(value);
      node.getContext()->onPropsApplied();
      node.layout(&controller, state);
    }

    void checked(NSPoint p, MacTextHitAnswer expected)
    {
      const AttributedString seed = Styled("seed", Bold);
      MacPresentedText fact = {seed, -2};
      MacTextHit hit = {TextSegmentRange(90).through(91), -3};
      LOKA_VERIFY(AttributedAccess::prepareTextHit(AttributedContext(node), p, hit, fact) == expected);
      if (expected == MAC_HIT_SOURCE)
      {
        LOKA_VERIFY(hit.containerWidth == fact.width && fact.width == AttributedAccess::width(view));
        LOKA_VERIFY(fact.value.hasSameSegmentValues(node.props.text_->get()));
      }
      else
      {
        LOKA_VERIFY(fact.width == -2 && fact.value.hasSameSegmentValues(seed));
        LOKA_VERIFY(hit.containerWidth == -3 && hit.source.first() == 90 && hit.source.last() == 91);
      }
    }
  };

  AttributedString LinkText(const char *body, const char *target)
  {
    return Linked(body, TextStyle(), LinkTarget(loka::core::String(target)));
  }

  void Candidate(
      const MacAttributedTextContext &context, NSPoint point, const char *target, size_t first = 0, size_t last = 0)
  {
    const LinkTarget seed(loka::core::String("untouched"));
    AttributedString::LinkRun run = {97, 99, seed};
    LOKA_VERIFY(AttributedAccess::queryPressCandidate(context, point, run) == (target != 0));
    if (target)
      LOKA_VERIFY(run.first == first && run.last == last && run.target == LinkTarget(loka::core::String(target)));
    else
      LOKA_VERIFY(run.first == 97 && run.last == 99 && run.target == seed);
  }
} // namespace

void testMacTextLayerCapability()
{
  LOKA_VERIFY(MacTextLayerCompiledIn());
  HitTestPool pool;
  Class meta = object_getClass((id)[NSView class]);
  instancesRespond = class_getMethodImplementation(meta, @selector(instancesRespondToSelector:));
  {
    MacNativeRefusal noCapability(@selector(instancesRespondToSelector:), (IMP)HideUpdateLayer, meta);
    Class leafClass = NSClassFromString(@"LokaAttributedTextView");
    MacNativeRefusal record(@selector(setWantsLayer:), (IMP)RefuseLayerRequest, leafClass);
    NSView *leaf = [[[leafClass alloc] initWithFrame:NSZeroRect] autorelease];
    LOKA_VERIFY(leaf && !MacTextLayerCapable());
  }
  LOKA_VERIFY(MacTextLayerCapable()
              == (MacTextLayerCompiledIn() && [NSView instancesRespondToSelector:@selector(wantsUpdateLayer)]));
  std::printf("leaf compiled=1 runtime L=%d\n", int(MacTextLayerCapable()));
}

void testMacTextPresentationFailedReplacement()
{
  HitTestPool pool;
  NSWindow *window = PresentationWindow();
  if (!window)
    return;
  const AttributedString a = LinkText("old", "A"), b = LinkText("new", "B");
  CheckedLayout layout(window, a);
  MacPresentedText fact;
  LOKA_VERIFY([layout.view queryPresentedFact:&fact] && fact.value.hasSameSegmentValues(a));
  NSAttributedString *nativeB = [[[NSAttributedString alloc] initWithString:@"new"] autorelease];
  CGFloat extent;
  loka::testing::failMacTextPresentationStep(1);
  LOKA_VERIFY(![layout.view installValue:nativeB snapshot:&b extent:&extent]);
  LOKA_VERIFY(![layout.view lokaHasInstalledText]);
  LOKA_VERIFY(![layout.view lokaHasPresentationImage]);
  LOKA_VERIFY(![layout.view queryPresentedFact:&fact]);
  LOKA_VERIFY([[AttributedAccess::storage(layout.view) string] isEqualToString:@"new"]);
}

namespace
{
  struct LayerActionProbe
  {
    CALayer *target;
    IMP gravity, contents, scale;
    enum Write
    {
      GRAVITY = 1,
      CONTENTS = 2,
      SCALE = 4
    };
    unsigned observed, actionsEnabled;

    /** Sample the active transaction once per setter entry on the observed layer. */
    void record(id receiver, Write bit)
    {
      if (receiver == this->target)
      {
        this->observed |= bit;
        if (![[CATransaction valueForKey:kCATransactionDisableActions] boolValue])
          this->actionsEnabled |= bit;
      }
    }
  };
  LayerActionProbe *actionProbe;

  void ObserveGravity(id receiver, SEL cmd, NSString *gravity)
  {
    actionProbe->record(receiver, LayerActionProbe::GRAVITY);
    ((void (*)(id, SEL, NSString *))actionProbe->gravity)(receiver, cmd, gravity);
  }
  void ObserveContents(id receiver, SEL cmd, id contents)
  {
    actionProbe->record(receiver, LayerActionProbe::CONTENTS);
    ((void (*)(id, SEL, id))actionProbe->contents)(receiver, cmd, contents);
  }
  void ObserveScale(id receiver, SEL cmd, CGFloat scale)
  {
    actionProbe->record(receiver, LayerActionProbe::SCALE);
    ((void (*)(id, SEL, CGFloat))actionProbe->scale)(receiver, cmd, scale);
  }
} // namespace

void testMacTextPresentationLayerActions()
{
  HitTestPool pool;
  NSWindow *window = PresentationWindow();
  if (!window)
    return;
  CheckedLayout layout(window, LinkText("writes", "A"));
  CALayer *layer = [layout.view layer];
  Class cls = object_getClass(layer);
  LayerActionProbe probe = {
    layer,
    class_getMethodImplementation(cls, @selector(setContentsGravity:)),
    class_getMethodImplementation(cls, @selector(setContents:)),
    class_getMethodImplementation(cls, @selector(setContentsScale:)),
    0,
    0
  };
  [CATransaction begin];
  [CATransaction setValue:(id)kCFBooleanFalse forKey:kCATransactionDisableActions];
  LOKA_VERIFY(![[CATransaction valueForKey:kCATransactionDisableActions] boolValue]);
  actionProbe = &probe;
  {
    MacNativeRefusal gravity(@selector(setContentsGravity:), (IMP)ObserveGravity, cls);
    MacNativeRefusal contents(@selector(setContents:), (IMP)ObserveContents, cls);
    MacNativeRefusal scale(@selector(setContentsScale:), (IMP)ObserveScale, cls);
    [layout.view updateLayer];
  }
  actionProbe = 0;
  LOKA_VERIFY(probe.observed == (LayerActionProbe::GRAVITY | LayerActionProbe::CONTENTS | LayerActionProbe::SCALE));
  LOKA_VERIFY(probe.actionsEnabled == 0);
  LOKA_VERIFY(![[CATransaction valueForKey:kCATransactionDisableActions] boolValue]);
  [CATransaction commit];
}

void testMacTextPresentationOwner()
{
  HitTestPool pool;
  NSWindow *window = PresentationWindow();
  if (!window)
    return;
  const AttributedString value = LinkText("one two three four five six seven", "A");
  CheckedLayout layout(window, value);
  NSView *view = layout.view;
  const CGFloat width = [view appliedWidth];
  MacPresentedText fact;
  [view applyContainerWidth:width];
  LOKA_VERIFY([view queryPresentedFact:&fact]);
  const CGFloat narrow = NSMaxX(HitCluster(layout, layout.glyph(4)).visibleRect());
  [view applyContainerWidth:narrow];
  LOKA_VERIFY(![view lokaHasPresentationImage] && [view lokaHasInstalledText]);
  VerifyTextKitWidth(view, narrow);
  [layout.manager glyphRangeForTextContainer:layout.container];
  [view setFrameSize:NSMakeSize(width, [layout.manager usedRectForTextContainer:layout.container].size.height * 4)];
  DisplayText(view);
  HitCluster wrapped(layout, layout.glyph(8));
  LOKA_VERIFY(NSMinY(wrapped.line) > NSMinY(HitCluster(layout, 0).line));
  layout.checked(HitCentre(wrapped.visibleRect()), MAC_HIT_SOURCE);
  NSBitmapImageRep *wrappedImage =
      [[[NSBitmapImageRep alloc] initWithCGImage:(CGImageRef)[[view layer] contents]] autorelease];
  const CGFloat scale = [window backingScaleFactor];
  bool wrappedInk = false;
  for (NSInteger y = static_cast<NSInteger>(std::ceil(NSMinY(wrapped.line) * scale));
       y < std::floor(NSMaxY(wrapped.line) * scale) && y < [wrappedImage pixelsHigh];
       ++y)
    for (NSInteger x = 0; x < [wrappedImage pixelsWide]; ++x)
    {
      NSUInteger pixel[5] = {};
      [wrappedImage getPixel:pixel atX:x y:y];
      if (pixel[3])
        wrappedInk = true;
    }
  LOKA_VERIFY(wrappedInk);
  LOKA_VERIFY([view installValue:nil snapshot:0 extent:0]);
  LOKA_VERIFY(![view lokaHasInstalledText] && ![view lokaHasPresentationImage]);
  {
    MacNativeRefusal noInstalled(@selector(layer), (IMP)UnexpectedLayer, [view class]);
    [view updateLayer];
  }
  LOKA_VERIFY(![view lokaHasPresentationImage]);
  const AttributedString empty;
  CGFloat extent;
  NSAttributedString *native = [[[NSAttributedString alloc] initWithString:@""] autorelease];
  loka::testing::failMacTextPresentationStep(1);
  LOKA_VERIFY(![view installValue:native snapshot:&empty extent:&extent]);
  LOKA_VERIFY(![view lokaHasInstalledText]);
  LOKA_VERIFY([view installValue:native snapshot:&empty extent:&extent]);
  LOKA_VERIFY([view lokaHasInstalledText] && ![view lokaHasPresentationImage]);
  DisplayText(view);
  LOKA_VERIFY([view queryPresentedFact:&fact] && fact.value.empty());
  for (unsigned refusal = 0; refusal <= 3; ++refusal)
  {
    id old = [[[view layer] contents] retain];
    if (refusal == 0)
      loka::testing::failMacCardBitmaps(1);
    else
      loka::testing::failMacTextPresentationStep(refusal);
    [view updateLayer];
    LOKA_VERIFY(![view lokaHasPresentationImage] && ![view queryPresentedFact:&fact]);
    LOKA_VERIFY([[view layer] contents] == old);
    [old release];
    DisplayText(view);
  }

  testLayer = [[view layer] retain];
  [view removeFromSuperview];
  LOKA_VERIFY(![view window] && ![view lokaHasPresentationImage]);
  {
    MacNativeRefusal presentLayer(@selector(layer), (IMP)ReturnTestLayer, [view class]);
    MacNativeRefusal noGeometry(@selector(bounds), (IMP)UnexpectedBounds, [view class]);
    [view updateLayer];
  }
  {
    MacNativeRefusal noRead(@selector(layer), (IMP)UnexpectedLayer, [view class]);
    LOKA_VERIFY(![view lokaHasPresentationImage] && ![view queryPresentedFact:&fact]);
  }
  [testLayer release];
  testLayer = nil;
  NSWindow *other = PresentationWindow();
  LOKA_VERIFY(other);
  [[other contentView] addSubview:view];
  LOKA_VERIFY(![view lokaHasPresentationImage]);
  DisplayText(view);
  [[window contentView] addSubview:view];
  LOKA_VERIFY(![view lokaHasPresentationImage]);
  DisplayText(view);
  [view retain];
  loka::app::scene::LifecycleFactTestAccess::MarkSubtreeRetired(&layout.node);
  loka::app::scene::LifecycleFactTestAccess::DeliverFacts(&layout.node);
  LOKA_VERIFY(![view window] && ![view lokaHasInstalledText] && ![view lokaHasPresentationImage]);
  [view release];
}

void testMacTextPresentationDeallocation()
{
  HitTestPool pool;
  NSWindow *window = PresentationWindow();
  if (!window)
    return;
  // A window move normally consumes the retain before dealloc. Supply just the two native reads
  // to an unparented leaf so the destructor is the first invalidating operation. AppKit autoreleases
  // references to the leaf during setup, so setup and release each drain their own pool; otherwise
  // dealloc waits for the outer drain (measured on Tahoe: retainCount 8 at release).
  const AttributedString value;
  NSView *leaf = nil;
  CGImageRef image = 0;
  {
    HitTestPool setup;
    leaf = [[NSClassFromString(@"LokaAttributedTextView") alloc] initWithFrame:[[window contentView] bounds]];
    CGFloat extent;
    [leaf applyContainerWidth:NSWidth([leaf bounds])];
    NSAttributedString *native = [[[NSAttributedString alloc] initWithString:@""] autorelease];
    LOKA_VERIFY([leaf installValue:native snapshot:&value extent:&extent]);
    testLayer = [[CALayer alloc] init];
    borrowedWindow = window;
    {
      MacNativeRefusal layer(@selector(layer), (IMP)ReturnTestLayer, [leaf class]);
      MacNativeRefusal attached(@selector(window), (IMP)ReturnBorrowedWindow, [leaf class]);
      [leaf updateLayer];
      LOKA_VERIFY([leaf lokaHasPresentationImage]);
    }
    image = CGImageRetain((CGImageRef)[testLayer contents]);
  }
  LOKA_VERIFY(image);
  CFIndex before = CFGetRetainCount(image);
  {
    HitTestPool teardown;
    [leaf release];
  }
  LOKA_VERIFY(CFGetRetainCount(image) == before - 1);
  CGImageRelease(image);
  [testLayer release];
  testLayer = nil;
  borrowedWindow = nil;
}

void testMacTextPresentationCheck()
{
  HitTestPool pool;
  NSWindow *window = PresentationWindow();
  if (!window)
    return;
  const AttributedString value = LinkText("visible", "A");
  CheckedLayout layout(window, value);
  NSView *view = layout.view;
  [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
  const NSPoint in = layout.point();
  const NSPoint outside = NSMakePoint(NSMaxX([view bounds]), in.y);
  layout.install(value);
  layout.checked(in, MAC_HIT_REFUSED);
  layout.checked(outside, MAC_HIT_REFUSED);
  DisplayText(view);
  layout.checked(in, MAC_HIT_SOURCE);
  layout.checked(outside, MAC_HIT_NONE);
  LOKA_VERIFY(![view lokaFactWithAbsentLayer]);
  {
    MacNativeRefusal nilLayer(@selector(layer), (IMP)ReturnTestLayer, [view class]);
    MacNativeRefusal noGeometry(@selector(bounds), (IMP)UnexpectedBounds, [view class]);
    [view updateLayer];
    LOKA_VERIFY(![view lokaHasPresentationImage]);
  }
  DisplayText(view);
  CALayer *layer = [view layer];
  {
    MacNativeRefusal scaleRead(@selector(backingScaleFactor), (IMP)DifferentScale, [window class]);
    layout.checked(in, MAC_HIT_REFUSED);
  }
  invertedFlip = ![layer contentsAreFlipped];
  {
    MacNativeRefusal flipRead(@selector(contentsAreFlipped), (IMP)OppositeFlip, [layer class]);
    layout.checked(in, MAC_HIT_SOURCE);
  }
  id image = [[layer contents] retain];
  [layer setContents:nil];
  layout.checked(in, MAC_HIT_REFUSED);
  [layer setContents:image];
  [image release];
  CGFloat scale = [layer contentsScale];
  [layer setContentsScale:scale + 1];
  layout.checked(in, MAC_HIT_REFUSED);
  [layer setContentsScale:scale];
  [layer setContentsRect:CGRectMake(0, 0, 0.5, 1)];
  layout.checked(in, MAC_HIT_REFUSED);
  [layer setContentsRect:CGRectMake(0, 0, 1, 1)];
  NSString *gravity = [layer contentsGravity];
  [layer setContentsGravity:kCAGravityCenter];
  layout.checked(in, MAC_HIT_REFUSED);
  [layer setContentsGravity:gravity];
  [layer setTransform:CATransform3DMakeScale(2, 1, 1)];
  layout.checked(in, MAC_HIT_REFUSED);
  [layer setTransform:CATransform3DIdentity];
  NSRect bounds = [view bounds];
  // Resize the frame, not the bounds alone: a bounds size unequal to the frame makes AppKit
  // scale the layer transform as well (measured on Tahoe 26.7.1), which would hide the size field.
  const NSSize frame = [view frame].size;
  [view setFrameSize:NSMakeSize(frame.width + 1, frame.height)];
  layout.checked(in, MAC_HIT_REFUSED);
  [view setFrameSize:frame];
  [view setBounds:bounds];
  layout.checked(in, MAC_HIT_SOURCE);
  [view setBoundsOrigin:in];
  layout.checked(in, MAC_HIT_REFUSED);
  [view updateLayer];
  LOKA_VERIFY(![view lokaHasPresentationImage]);
  [view setBounds:bounds];
  DisplayText(view);
  layout.checked(in, MAC_HIT_SOURCE);
  layout.install(value);
  CaptureParent(view);
  layout.checked(in, MAC_HIT_SOURCE);
  layout.install(value);
  NSBitmapImageRep *destination = [view bitmapImageRepForCachingDisplayInRect:[view bounds]];
  NSGraphicsContext *graphics = [NSGraphicsContext graphicsContextWithBitmapImageRep:destination];
  LOKA_VERIFY(graphics);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  [layer renderInContext:(CGContextRef)[graphics graphicsPort]];
#pragma clang diagnostic pop
  layout.checked(in, MAC_HIT_SOURCE);
  layout.install(Styled(RawAttributed("a\xF0\x9F", 3), TextStyle())
                 + Styled(RawAttributed("\x98\x80", 2), TextStyle()));
  DisplayText(view);
  layout.checked(HitCentre(HitCluster(layout, layout.glyph(1)).visibleRect()), MAC_HIT_REFUSED);
}

void testMacTextPresentationCandidate()
{
  HitTestPool pool;
  NSWindow *window = PresentationWindow();
  if (!window)
    return;
  const AttributedString a = LinkText("body", "A"), b = LinkText("body", "B");
  CheckedLayout layout(window, a);
  MacAttributedTextContext &context = AttributedContext(layout.node);
  const NSPoint point = layout.point();
  Candidate(context, point, "A");
  loka::core::MutableState<AttributedString> live(a);
  layout.node.props.text(&live);
  live.set(b);
  Candidate(context, point, 0);
  layout.install(b);
  Candidate(context, point, 0);
  DisplayText(layout.view);
  Candidate(context, point, "B");
  layout.install(Styled("body", TextStyle()));
  DisplayText(layout.view);
  Candidate(context, point, 0);
  layout.install(LinkText("e", "A") + LinkText("\xCC\x81x", "B"));
  DisplayText(layout.view);
  Candidate(context, layout.point(), 0);
  layout.install(LinkText("e", "A") + LinkText("\xCC\x81x", "A"));
  DisplayText(layout.view);
  Candidate(context, layout.point(), "A", 0, 1);
  layout.install(LinkText("f", "A") + LinkText("i", "B"));
  DisplayText(layout.view);
  if (HitCluster(layout, 0).chars.length == 2)
  {
    std::puts("formed fi cluster crosses destinations");
    Candidate(context, layout.point(), 0);
  }
  else
    std::puts("[skip] fi ligature not formed");
  layout.install(a);
  DisplayText(layout.view);
  Candidate(context, point, "A");
  NotifySubtreeNodeDetached(&layout.node);
  Candidate(context, point, 0);
  NotifySubtreeNodeAttached(&layout.node);
  Candidate(context, point, "A");
  // Take the test's installed context out of the public slot before replacing it. setContext would delete it.
  layout.node.context = 0;
  layout.node.layoutProjected(&layout.controller, layout.state);
  LOKA_VERIFY(layout.node.getContext() && layout.node.getContext() != &context);
  Candidate(context, layout.point(), 0);
  context.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
  delete &context;
  MacAttributedTextContext &replacement = AttributedContext(layout.node);
  replacement.onPropsApplied();
  Candidate(replacement, point, 0);
}

void testMacTextPresentationSnapshotLifetime()
{
  HitTestPool pool;
  HitLayout layout(LinkText("owned", "value"));
  MacAttributedTextContext &context = AttributedContext(layout.node);
  LOKA_VERIFY(AttributedAccess::snapshot(context).hasSameSegmentValues(layout.node.props.text_->get()));
  context.onPropsApplied();
  LOKA_VERIFY(!AttributedAccess::complete(context) && AttributedAccess::snapshot(context).segmentCount() == 0);
}

void testMacTextPresentationTranslatedOrigin()
{
  HitTestPool pool;
  NSWindow *window = PresentationWindow();
  if (!window)
    return;
  const AttributedString a = Linked("HHHH", Blue, LinkTarget(loka::core::String("A")));
  const AttributedString b = Linked("HHHH", Red, LinkTarget(loka::core::String("B")));
  CheckedLayout layout(window, a);
  NSView *view = layout.view;
  const NSRect frame = [view frame];
  const NSPoint glyph = layout.point();
  Candidate(AttributedContext(layout.node), glyph, "A");
  [view setBoundsOrigin:glyph];
  layout.install(b);
  LOKA_VERIFY(NSEqualRects(frame, [view frame]) && NSEqualPoints([view bounds].origin, glyph));
  VerifyRedFallbackInk(layout);
  LOKA_VERIFY(![view wantsUpdateLayer]);
  layout.expect(glyph, MAC_HIT_SOURCE);
  layout.checked(glyph, MAC_HIT_REFUSED);
  Candidate(AttributedContext(layout.node), glyph, 0);
  [view setBoundsOrigin:NSZeroPoint];
  [view setNeedsDisplay:YES];
  DisplayWindow(window);
  layout.checked(glyph, MAC_HIT_SOURCE);
  Candidate(AttributedContext(layout.node), glyph, "B");
}

#include <limits>
void testMacTextPresentationBudget()
{
  HitTestPool pool;
  if (!MacTextLayerCompiledIn())
  {
    std::puts("[skip] this artifact/runtime has no L");
    return;
  }
  LOKA_VERIFY(MacTestTextBudget(NSMakeSize(4096, 1), 1));
  LOKA_VERIFY(!MacTestTextBudget(NSMakeSize(4097, 1), 1));
  LOKA_VERIFY(MacTestTextBudget(NSMakeSize(2048, 2048), 1));
  LOKA_VERIFY(!MacTestTextBudget(NSMakeSize(2048, 2048.5), 1));
  LOKA_VERIFY(!MacTestTextBudget(NSMakeSize(2048, 2049), 1));
  const CGFloat invalid[] = {-1,
                             0,
                             std::numeric_limits<CGFloat>::infinity(),
                             -std::numeric_limits<CGFloat>::infinity(),
                             std::numeric_limits<CGFloat>::quiet_NaN()};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
  {
    LOKA_VERIFY(!MacTestTextBudget(NSMakeSize(invalid[i], 1), 1));
    LOKA_VERIFY(!MacTestTextBudget(NSMakeSize(1, invalid[i]), 1));
    LOKA_VERIFY(!MacTestTextBudget(NSMakeSize(1, 1), invalid[i]));
  }
  NSWindow *window = PresentationWindow();
  if (!window)
    return;
  HitLayout layout(Styled("HHHH", Red));
  NSView *view = layout.view;
  const NSSize good = [view frame].size;
  // One pixel beyond the width limit at native scale keeps the line visible and the ink capture small.
  const NSSize oversized = NSMakeSize(4097 / [window backingScaleFactor], good.height);
  NSView *parent = [view superview];
  [parent setFrame:[[window contentView] bounds]];
  [view setFrameSize:oversized];
  [window setContentView:parent];
  LOKA_VERIFY(![view wantsUpdateLayer]);
  VerifyRedFallbackInk(layout);
  [view setFrameSize:good];
  DisplayText(view);
  [view setFrameSize:oversized];
  LOKA_VERIFY(![view wantsUpdateLayer]);
  VerifyRedFallbackInk(layout);
  [view setFrameSize:NSMakeSize(good.width, 4097 / [window backingScaleFactor])];
  MacPresentedText fact;
  LOKA_VERIFY(![view wantsUpdateLayer] && ![view queryPresentedFact:&fact]);
  [view setFrameSize:NSZeroSize];
  LOKA_VERIFY(![view wantsUpdateLayer] && ![view queryPresentedFact:&fact]);
  [view setFrameSize:good];
  DisplayText(view);
}

namespace
{
  // A test process installs each interposition only while the corresponding real interval is on stack.
#if defined(LOKA_LIFECYCLE_AUDIT) && !defined(NDEBUG)
  void NestedDisplay(id view, SEL, NSRect)
  {
    [view setNeedsDisplay:YES];
    [view displayIfNeeded];
  }
  MacTextHitAnswer ChangeWidthDuringHit(id view, SEL, NSPoint, NSRange *)
  {
    [view applyContainerWidth:[view appliedWidth] + 1];
    return MAC_HIT_REFUSED;
  }
#endif
} // namespace

void testMacTextPresentationIntervals()
{
#if defined(LOKA_LIFECYCLE_AUDIT) && !defined(NDEBUG)
  HitTestPool pool;
  NSWindow *window = PresentationWindow();
  if (!window)
    return;
  const char *action = std::getenv(childActionKey);
  if (action)
  {
    CheckedLayout layout(window, LinkText("interval", "A"));
    if (std::strcmp(action, "nestedDisplay") == 0)
    {
      MacNativeRefusal nest(@selector(drawTextInRect:), (IMP)NestedDisplay, [layout.view class]);
      [layout.view updateLayer];
    }
    else
    {
      MacNativeRefusal width(
          sel_registerName("queryHitCharacters:range:"), (IMP)ChangeWidthDuringHit, [layout.view class]);
      layout.checked(layout.point(), MAC_HIT_SOURCE);
    }
    _exit(1);
  }
  VerifyAttributedChildAssertion("nestedDisplay", "testMacTextPresentationIntervals", "occupied_");
  VerifyAttributedChildAssertion("widthDuringHit", "testMacTextPresentationIntervals", "occupied_");
#else
  std::puts("[skip] interval death pins require LOKA_LIFECYCLE_AUDIT and enabled assertions");
#endif
}

/** Synchronous drawRect control. The caller keeps the source alive through the capture. */
@interface LokaTextRasterReference : NSView
{
@private
  NSView *source_;
}
- (id)initWithSource:(NSView *)view;
@end
@implementation LokaTextRasterReference
- (id)initWithSource:(NSView *)view
{
  self = [super initWithFrame:[view bounds]];
  if (self)
    source_ = view;
  return self;
}
- (BOOL)isFlipped
{
  return YES;
}
- (BOOL)wantsUpdateLayer
{
  return NO;
}
- (void)drawRect:(NSRect)dirty
{
  [source_ drawRect:dirty];
}
@end

namespace
{
  IMP textPainter;
  CGAffineTransform paintTransform;
  void RecordTextTransform(id view, SEL cmd, NSRect rect)
  {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    paintTransform = CGContextGetCTM((CGContextRef)[[NSGraphicsContext currentContext] graphicsPort]);
#pragma clang diagnostic pop
    ((void (*)(id, SEL, NSRect))textPainter)(view, cmd, rect);
  }

  BOOL UseRectDrawing(id, SEL)
  {
    return NO;
  }

  // Borrow the immutable installed image after the production bitmap owner's destruction.
  NSBitmapImageRep *RawTextImage(NSView *view)
  {
    MacPresentedText fact;
    LOKA_VERIFY([view queryPresentedFact:&fact]);
    CGImageRef image = (CGImageRef)[[view layer] contents];
    LOKA_VERIFY(image && CFGetTypeID(image) == CGImageGetTypeID());
    return [[[NSBitmapImageRep alloc] initWithCGImage:image] autorelease];
  }

  NSBitmapImageRep *DrawRectControl(NSView *view)
  {
    NSView *control = [[LokaTextRasterReference alloc] initWithSource:view];
    [[view superview] addSubview:control];
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1090
    if ([control respondsToSelector:@selector(setAppearance:)] &&
        [view respondsToSelector:@selector(effectiveAppearance)])
      [control setAppearance:[view effectiveAppearance]];
#endif
    NSBitmapImageRep *image = CaptureParent(control);
    [control removeFromSuperview];
    [control release];
    return image;
  }

  void ReportPixelDifferences(const char *label, NSBitmapImageRep *left, NSBitmapImageRep *right)
  {
    unsigned long count = 0;
    const long leftWidth = [left pixelsWide], leftHeight = [left pixelsHigh];
    const long rightWidth = [right pixelsWide], rightHeight = [right pixelsHigh];
    const long width = std::min(leftWidth, rightWidth), height = std::min(leftHeight, rightHeight);
    for (NSInteger y = 0; y < height; ++y)
      for (NSInteger x = 0; x < width; ++x)
      {
        NSUInteger a[5] = {}, b[5] = {};
        [left getPixel:a atX:x y:y];
        [right getPixel:b atX:x y:y];
        if (std::memcmp(a, b, sizeof(a)))
          ++count;
      }
    std::printf("%s: images %ldx%ld vs %ldx%ld px, overlap %ldx%ld px (%lu differing), "
                "unmatched pixels %ld left / %ld right\n",
                label,
                leftWidth,
                leftHeight,
                rightWidth,
                rightHeight,
                width,
                height,
                count,
                leftWidth * leftHeight - width * height,
                rightWidth * rightHeight - width * height);
  }

  // A fully covered stroke gives the resolved ink without weighting edge antialiasing.
  CGFloat InkLightness(NSBitmapImageRep *image)
  {
    NSUInteger peak = 0;
    CGFloat lightness = 0;
    for (NSInteger y = 0; y < [image pixelsHigh]; ++y)
      for (NSInteger x = 0; x < [image pixelsWide]; ++x)
      {
        NSUInteger rgba[5] = {};
        [image getPixel:rgba atX:x y:y];
        if (rgba[3] > peak)
        {
          peak = rgba[3];
          lightness = (rgba[0] + rgba[1] + rgba[2]) / (3.0 * peak);
        }
      }
    LOKA_VERIFY(peak);
    return lightness;
  }
} // namespace

void testMacTextPresentationPixels()
{
  HitTestPool pool;
  NSWindow *window = PresentationWindow();
  if (!window)
    return;
  CheckedLayout layout(window, Styled("|||   |||", FontSize<18>()));
  NSView *view = layout.view;
  const CGFloat scale = [window backingScaleFactor];
  NSSize size = [view frame].size;
  size.height += 5 + 0.5 / scale;
  [view setFrameSize:size];
  textPainter = class_getMethodImplementation([view class], @selector(drawTextInRect:));
  {
    MacNativeRefusal read(@selector(drawTextInRect:), (IMP)RecordTextTransform, [view class]);
    DisplayText(view);
  }
  NSBitmapImageRep *raw = RawTextImage(view);
  LOKA_VERIFY([raw pixelsHigh] == std::ceil(size.height * scale) && [raw pixelsWide] == std::ceil(size.width * scale));
  LOKA_VERIFY(paintTransform.ty == [raw pixelsHigh] && paintTransform.a == scale && paintTransform.d == -scale);
  CALayer *layer = [view layer];
  const BOOL flipped = [layer contentsAreFlipped];
  std::printf("artifact Darwin %d scale=%g flipped=%d; [skip] opposite native flip branch\n",
              loka::dsl::testing::DarwinMajor(),
              scale,
              int(flipped));
  std::puts(scale == 1 ? "[skip] scale 2 on this window" : "[skip] scale 1 on this window");
  std::puts("[skip] physical display scale transition on this single-window fixture");
  LOKA_VERIFY([[layer contentsGravity] isEqualToString:flipped ? kCAGravityBottomLeft : kCAGravityTopLeft]);
  LOKA_VERIFY([view respondsToSelector:@selector(layerContentsPlacement)]);
  LOKA_VERIFY(((NSInteger(*)(id, SEL))objc_msgSend)(view, @selector(layerContentsPlacement)) == 11);
  std::vector<NSRect> boxes;
  for (NSUInteger g = 0; g < [layout.manager numberOfGlyphs]; ++g)
  {
    NSRect box = [layout.manager boundingRectForGlyphRange:NSMakeRange(g, 1) inTextContainer:layout.container];
    boxes.push_back(NSInsetRect(box, -1 / scale, -1 / scale));
  }
  unsigned ink = 0, outside = 0;
  for (NSInteger y = 0; y < [raw pixelsHigh]; ++y)
    for (NSInteger x = 0; x < [raw pixelsWide]; ++x)
    {
      bool covered = false;
      const NSPoint p = NSMakePoint((x + 0.5) / scale, (y + 0.5) / scale);
      for (size_t g = 0; g < boxes.size() && !covered; ++g)
        covered = NSPointInRect(p, boxes[g]);
      NSUInteger rgba[5] = {};
      [raw getPixel:rgba atX:x y:y];
      if (rgba[3])
      {
        LOKA_VERIFY(covered);
        ++ink;
      }
      if (!covered)
        ++outside;
    }
  LOKA_VERIFY(ink && outside);
  NSBitmapImageRep *control = DrawRectControl(view);
  [view setNeedsDisplay:YES];
  NSView *parent = [view superview];
  const NSRect region = [view convertRect:[view bounds] toView:parent];
  NSBitmapImageRep *composed = [parent bitmapImageRepForCachingDisplayInRect:region];
  LOKA_VERIFY(composed);
  [parent cacheDisplayInRect:region toBitmapImageRep:composed];
  MacPresentedText fact;
  LOKA_VERIFY([view queryPresentedFact:&fact]);
  std::printf("fractional height remainder=%g px\n", std::ceil(size.height * scale) - size.height * scale);
  ReportPixelDifferences("drawRect vs raw", control, raw);
  ReportPixelDifferences("drawRect vs composed", control, composed);
}

void testMacTextPresentationColour()
{
  HitTestPool pool;
  NSWindow *window = PresentationWindow();
  if (!window)
    return;
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1090
  if (NSAppKitVersionNumber >= 1671)
  {
    CheckedLayout layout(window, Styled("Appearance |||", Bold));
    NSView *view = layout.view;
    NSAppearance *light = [NSAppearance appearanceNamed:@"NSAppearanceNameAqua"];
    NSAppearance *dark = [NSAppearance appearanceNamed:@"NSAppearanceNameDarkAqua"];
    LOKA_VERIFY(light && dark);
    id appAppearance = [NSApp respondsToSelector:@selector(effectiveAppearance)]
                           ? [NSApp performSelector:@selector(effectiveAppearance)]
                           : nil;
    NSString *match = [appAppearance
        performSelector:NSSelectorFromString(@"bestMatchFromAppearancesWithNames:")
             withObject:[NSArray arrayWithObjects:@"NSAppearanceNameDarkAqua", @"NSAppearanceNameAqua", nil]];
    const bool appDark = [match isEqualToString:@"NSAppearanceNameDarkAqua"];
    for (int generation = 0; generation < 4; ++generation)
    {
      const bool darkInkBackground = (generation % 2 == 0) != appDark;
      id previous = [[[view layer] contents] retain];
      [view setAppearance:darkInkBackground ? dark : light];
      if (generation == 3)
      {
        [view setNeedsDisplay:YES];
        CaptureParent(view);
      }
      else
      {
        [view displayIfNeeded];
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
      }
      NSBitmapImageRep *raw = RawTextImage(view);
      LOKA_VERIFY([[view layer] contents] != previous);
      [previous release];
      CGFloat owned = InkLightness(raw), control = InkLightness(DrawRectControl(view));
      std::printf("appearance %s route %s, own/control ink %g/%g\n",
                  darkInkBackground ? "dark" : "light",
                  generation == 3 ? "cache" : "ordinary",
                  owned,
                  control);
      LOKA_VERIFY((owned > 0.5) == darkInkBackground && std::fabs(owned - control) < 0.12);
    }
    return;
  }
#endif
  std::puts("[skip] no dark appearance");
}

namespace
{
  NSPoint FirstLinkPoint(AttributedTextNode &node)
  {
    NSView *view = AttributedAccess::view(AttributedContext(node));
    NSLayoutManager *manager = AttributedAccess::manager(view);
    NSUInteger count = 0;
    NSRectArray rects = [manager rectArrayForCharacterRange:NSMakeRange(0, 1)
                               withinSelectedCharacterRange:NSMakeRange(NSNotFound, 0)
                                            inTextContainer:AttributedAccess::container(view)
                                                  rectCount:&count];
    LOKA_VERIFY(count);
    NSRect rect = rects[0];
    return HitCentre(NSIntersectionRect(rect, [manager lineFragmentUsedRectForGlyphAtIndex:0 effectiveRange:NULL]));
  }
  void FirstOrdinaryCandidate(NSWindow *window, AttributedTextNode &node, const char *host)
  {
    const NSPoint point = FirstLinkPoint(node);
    std::fprintf(stderr, "Darwin %d %s first ordinary display\n", loka::dsl::testing::DarwinMajor(), host);
    DisplayWindow(window);
    Candidate(AttributedContext(node), point, "destination");
  }
} // namespace

void testMacTextPresentationIntegration()
{
  HitTestPool pool;
  NSWindow *window = PresentationWindow();
  if (!window)
    return;
  NSView *root = [window contentView];
  const AttributedTextProps link(LinkText("go", "destination"));
  for (int host = 0; host < 3; ++host)
  {
    MacScenePlatformController controller(root, RailMetrics());
    loka::testing::ScrollViewSeat viewport;
    if (host == 1)
    {
      // Cell is a leaf. Exercise its production layered native view as the child controller's parent.
      CellNode cell((CellProps()));
      LayoutState state;
      state.width = 200;
      cell.layoutProjected(&controller, state);
      NSView *native = [[root subviews] lastObject];
      LOKA_VERIFY([native isKindOfClass:NSClassFromString(@"LokaCellView")]);
      MacScenePlatformController inside(native, RailMetrics());
      AttributedTextNode node(link);
      node.layoutProjected(&inside, state);
      FirstOrdinaryCandidate(window, node, "Cell native host");
    }
    else
    {
      NestableNode *parent = host == 0
                                 ? static_cast<NestableNode *>(new ZStackNode((ZStackProps())))
                                 : static_cast<NestableNode *>(new ScrollViewNode(ScrollViewProps(viewport.state())));
      if (host == 0)
        parent->addChild(new TextNode(TextProps("ground")));
      AttributedTextNode *node = new AttributedTextNode(link);
      parent->addChild(node);
      controller.onChange(parent, NODE_DIRTY_NONE, false);
      controller.relayout(320, 160);
      FirstOrdinaryCandidate(window, *node, host == 0 ? "ZStack" : "ScrollView");
      controller.onChange(0, NODE_DIRTY_NONE, false);
      delete parent;
    }
  }
}

void testMacTextPresentationPredicate()
{
  HitTestPool pool;
  const AttributedString value = LinkText("body", "A"), other = LinkText("body", "B");
  const MacTextHit hit = {TextSegmentRange(0), 42};
  MacPresentedText fact = {value, hit.containerWidth};
  LOKA_VERIFY(MacTestEligibleRun(hit, fact, value, value));
  fact.width += 1;
  LOKA_VERIFY(!MacTestEligibleRun(hit, fact, value, value));
  fact.width = hit.containerWidth;
  fact.value = other;
  LOKA_VERIFY(!MacTestEligibleRun(hit, fact, value, value));
}
