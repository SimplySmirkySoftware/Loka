#include "app/nodes/controls/Card.hpp"
#include "MacObjCCompat.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "MacNodeHandlerEnsureTests.hpp"
#include "MacPlatformContext.hpp"
#include "MacApp.hpp"
#include "app/core/AppConfigurable.hpp"
#include "testing/app/NativeResourceRetirementTestAccess.hpp"
#include "support/MacRetirementProbe.hpp"
#include "support/TestVerify.hpp"
#include "support/RailTextLayoutFixture.hpp"
#include "support/MacNativeRefusal.hpp"
#include <cmath>

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
            const int buttonY = captionY + 20 + MacSceneLayout::kGap;
            const int buttonHeight = projection.measurementToLu([[button cell] cellSize].height);
            LOKA_VERIFY(
                NSEqualRects([page frame],
                             projection
                                 .projectFrame(loka::core::Frame(
                                     MacSceneLayout::kRootInset, MacSceneLayout::kRootInset, pageWidth, textHeight))
                                 .r));
            LOKA_VERIFY([caption frame].origin.y == projection.projectEdge(captionY).pt);
            LOKA_VERIFY(NSMinY([button frame]) == projection.projectEdge(buttonY).pt);
            LOKA_VERIFY(NSMaxY([button frame]) == projection.projectEdge(buttonY + buttonHeight).pt);
            // The actual Scrapbook fixed Box prevents measured text changes
            // reaching the caption/buttons, for either one or two native lines.
            if (boxed)
              LOKA_VERIFY(buttonY == MacSceneLayout::kRootInset + 170 + 20 + 2 * MacSceneLayout::kGap);
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
    LOKA_VERIFY(frameEquals(buttonView, 10, 20, 100, 30) &&
                "preparation must preserve the retained frame");
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

namespace
{
  NSTextField *AttributedField(NSView *root)
  {
    LOKA_VERIFY([[root subviews] count] == 1);
    return (NSTextField *)[[root subviews] objectAtIndex:0];
  }

  void VerifyAttributedHeight(loka::app::AttributedTextNode &node, MacScenePlatformController &controller,
                              NSView *root, short width)
  {
    loka::app::scene::LayoutState state;
    state.x = 3;
    state.y = 5;
    state.width = width;
    node.layoutProjected(&controller, state);
    NSTextField *field = AttributedField(root);
    const loka::macos::MacProjection &projection = controller.projection();
    const NSSize rendered = [[field cell] cellSizeForBounds:loka::macos::MacMeasurementBounds(
        projection.projectLength(state.x, state.x + width))];
    LOKA_VERIFY(state.height == projection.measurementToLu(rendered.height));
    LOKA_VERIFY(NSEqualRects([field frame], projection.projectFrame(
        loka::core::Frame(3, 5, width, state.height)).r));
  }

  class MacRefusedAttributedString : public loka::platform::String
  {
  public:
    virtual bool appendUtf8(std::string &) const { return false; }
  };
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
      NSTextField *field = AttributedField(root);
      NSAttributedString *native = [field attributedStringValue];
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
      LOKA_VERIFY([[field cell] lineBreakMode] == NSLineBreakByCharWrapping);
      NSParagraphStyle *paragraph = [[field attributedStringValue]
          attribute:NSParagraphStyleAttributeName atIndex:0 effectiveRange:0];
      LOKA_VERIFY([paragraph lineBreakMode] == NSLineBreakByCharWrapping);
      node.props.blockStyle_ = BlockStyle().wrap(TEXT_WRAP_NONE).truncation(TEXT_TRUNCATION_ELLIPSIS);
      context->onPropsApplied();
      VerifyAttributedHeight(node, controller, root, 90);
      LOKA_VERIFY([[field cell] lineBreakMode] == NSLineBreakByTruncatingTail);
      LOKA_VERIFY(![[field cell] wraps]);
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
        LOKA_VERIFY(![[field cell] wraps] && ![[field cell] isScrollable]);
        if ([[field cell] respondsToSelector:@selector(usesSingleLineMode)])
          LOKA_VERIFY(![[field cell] usesSingleLineMode]);
        NSParagraphStyle *aligned = [[field attributedStringValue]
            attribute:NSParagraphStyleAttributeName atIndex:0 effectiveRange:0];
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
    loka::app::scene::NodeContext *context = node.getContext();
    NSTextField *field = AttributedField(root);
    NSAttributedString *before = [[field attributedStringValue] retain];
    loka::app::scene::NotifySubtreeNodeDetached(&node);
    loka::app::scene::LifecycleFactTestAccess::DeliverFacts(&node);
    LOKA_VERIFY([field isHidden]);
    LOKA_VERIFY([[field attributedStringValue] isEqualToAttributedString:before]);
    loka::app::scene::NotifySubtreeNodeAttached(&node);
    loka::app::scene::LifecycleFactTestAccess::DeliverFacts(&node);
    LOKA_VERIFY(![field isHidden]);
    LOKA_VERIFY(node.getContext() == context);
    LOKA_VERIFY([[field attributedStringValue] isEqualToAttributedString:before]);
    [before release];
    loka::app::scene::NotifySubtreeNodeDetached(&node);
    loka::app::scene::LifecycleFactTestAccess::DeliverFacts(&node);
    AttributedTextProps replacement(Styled("updated while hidden", FontSize<24>() + Italic));
    replacement.blockStyle_ = BlockStyle().wrap(TEXT_WRAP_WORD);
    AttributedTextDefinition definition(replacement);
    LOKA_VERIFY(definition.applyPropsToNode(&node));
    VerifyAttributedHeight(node, controller, root, 100);
    LOKA_VERIFY([field isHidden]);
    loka::app::scene::NotifySubtreeNodeAttached(&node);
    loka::app::scene::LifecycleFactTestAccess::DeliverFacts(&node);
    LOKA_VERIFY(![field isHidden]);
    LOKA_VERIFY([[field stringValue] isEqualToString:@"updated while hidden"]);
    LOKA_VERIFY(node.getContext() == context);
    loka::app::scene::LifecycleFactTestAccess::MarkSubtreeRetired(&node);
    loka::app::scene::LifecycleFactTestAccess::DeliverFacts(&node);
    LOKA_VERIFY([[root subviews] count] == 0);
    LOKA_VERIFY([[field stringValue] length] == 0);
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
    NSTextField *field = AttributedField(root);
    const loka::core::String text("allocation refusal");
    loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 1);
    const AttributedString refused = Styled(text, Italic);
    loka::core::testing::allowLokaAllocRaw();
    LOKA_VERIFY(!refused.valid());
    LOKA_VERIFY(AttributedText(refused).applyPropsToNode(&node));
    loka::app::scene::LayoutState state;
    state.width = 100;
    node.layoutProjected(&controller, state);
    LOKA_VERIFY(state.height == 0 && [[field stringValue] length] == 0);
    LOKA_VERIFY(AttributedText(Styled("recovered", Italic)).applyPropsToNode(&node));
    VerifyAttributedHeight(node, controller, root, 100);
    const loka::core::String unreadable(loka::core::Managed<loka::platform::String>::Wrap(new MacRefusedAttributedString()));
    LOKA_VERIFY(AttributedText(Styled(unreadable, Italic)).applyPropsToNode(&node));
    node.layoutProjected(&controller, state);
    LOKA_VERIFY(state.height == 0 && [[field stringValue] length] == 0);
    LOKA_VERIFY(AttributedText(Styled("recovered again", Italic)).applyPropsToNode(&node));
    VerifyAttributedHeight(node, controller, root, 100);
  }
  [root release];
  [pool drain];
}

#include <objc/runtime.h>
#include "app/nodes/boundary/StdComposition.hpp"
#include "core/util/StateTrackerGuard.hpp"

// Both production measurement sites send this selector to NSTextFieldCell.
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
  class MacMeasurementCounter
  {
  public:
    MacMeasurementCounter()
    {
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
    unsigned calls() const { return gMac970Measurements; }
  private:
    Method original_;
    Method counting_;
    MacMeasurementCounter(const MacMeasurementCounter &);
    MacMeasurementCounter &operator=(const MacMeasurementCounter &);
  };

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
      NSTextField *field = AttributedField(root);
      LOKA_VERIFY(counter.calls() == 1); // Positive control: swizzle must observe the real call.
      const short height = state.height;
      LOKA_VERIFY(height > 0);
      NSAttributedString *before = [[field attributedStringValue] retain];
      state.x = 4; // Same endpoint phase at spaceScale 5/4.
      state.y = 23;
      state.height = 999;
      // Bypass ensure's plain refresh: only layout's placement can satisfy this pin.
      const short advance = node.layout(&controller, state);
      LOKA_VERIFY(counter.calls() == 1);
      LOKA_VERIFY(state.height == height);
      LOKA_VERIFY(advance == state.y + height);
      LOKA_VERIFY(NSEqualRects([field frame], controller.projection().projectFrame(
          loka::core::Frame(4, 23, 101, height)).r));
      if (attributed)
        LOKA_VERIFY([field attributedStringValue] == before);
      [before release];
      state.x = 1;
      LOKA_VERIFY(controller.projection().projectLength(0, 101).pt == 126);
      LOKA_VERIFY(controller.projection().projectLength(1, 102).pt == 127);
      unsigned calls = counter.calls();
      node.layout(&controller, state);
      LOKA_VERIFY(counter.calls() == calls + (attributed ? 1 : 0));
      state.width = 80;
      calls = counter.calls();
      node.layout(&controller, state);
      LOKA_VERIFY(counter.calls() == calls + 1);
      node.layout(&controller, state);
      LOKA_VERIFY(counter.calls() == calls + 1);
      NotifySubtreeNodeDetached(&node);
      LifecycleFactTestAccess::DeliverFacts(&node);
      LOKA_VERIFY([field isHidden]);
      before = [[field attributedStringValue] retain];
      NotifySubtreeNodeAttached(&node);
      LifecycleFactTestAccess::DeliverFacts(&node);
      LOKA_VERIFY(![field isHidden]);
      LOKA_VERIFY([[field attributedStringValue] isEqualToAttributedString:before]);
      [before release];
      calls = counter.calls();
      node.layout(&controller, state);
      LOKA_VERIFY(counter.calls() == calls + 1);
      node.layout(&controller, state);
      LOKA_VERIFY(counter.calls() == calls + 1);
      if (!attributed)
      {
        // Native presentation drift must be reconciled on a measurement hit.
        [field setAlignment:LOKA_MAC_TEXT_ALIGNMENT_RIGHT];
        [[field cell] setWraps:NO];
        [[field cell] setScrollable:YES];
        [[field cell] setLineBreakMode:NSLineBreakByClipping];
        [[field cell] setFont:[NSFont systemFontOfSize:30]];
        node.layout(&controller, state);
        LOKA_VERIFY(counter.calls() == calls + 1);
        LOKA_VERIFY([field alignment] == LOKA_MAC_TEXT_ALIGNMENT_LEFT);
        LOKA_VERIFY([[field cell] wraps] && ![[field cell] isScrollable]);
        LOKA_VERIFY([[field cell] lineBreakMode] == NSLineBreakByWordWrapping);
        LOKA_VERIFY([[[field cell] font] isEqual:(NSFont *)controller.textFont(FontSize<12>())]);
      }
      state.width = 0;
      node.layout(&controller, state);
      calls = counter.calls();
      node.layout(&controller, state);
      LOKA_VERIFY(counter.calls() == calls);
      // Node is caller-owned and must retire before this controller drains.
      LifecycleFactTestAccess::MarkSubtreeRetired(&node);
      LifecycleFactTestAccess::DeliverFacts(&node);
      LOKA_VERIFY([[root subviews] count] == 0);
      LOKA_VERIFY([[field stringValue] length] == 0);
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

  template <class Leaf>
  void VerifyMacMeasurementRefusal(Leaf &node, MacRetryString &source)
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
      NSTextField *field = AttributedField(root);
      LOKA_VERIFY(counter.calls() == 1 && state.height > 0);
      source.available(false);
      state.width = 90; // Force a miss without mutating the logical input.
      const short refusedAdvance = node.layout(&controller, state);
      LOKA_VERIFY(refusedAdvance == state.y);
      LOKA_VERIFY(state.height == 0 && [[field stringValue] length] == 0);
      LOKA_VERIFY(NSIsEmptyRect([field frame]));
      const unsigned refusedAttempts = source.attempts();
      node.layout(&controller, state);
      LOKA_VERIFY(source.attempts() > refusedAttempts);
      LOKA_VERIFY(counter.calls() == 1);
      source.available(true);
      node.layout(&controller, state);
      LOKA_VERIFY(counter.calls() == 2 && state.height > 0);
      LOKA_VERIFY([[field stringValue] isEqualToString:@"retry the same wrapped text"]);
      LOKA_VERIFY(!NSIsEmptyRect([field frame]));
      node.layout(&controller, state);
      LOKA_VERIFY(counter.calls() == 2);
      // Establish the controller's root, then retry only through its native queue.
      controller.onChange(&node, NODE_DIRTY_LAYOUT, false);
      const unsigned beforeRetry = counter.calls();
      node.requeueLayoutInputs(NODE_DIRTY_PROPS);
      source.available(false);
      controller.relayout(0, 0);
      LOKA_VERIFY(controller.hasPendingRelayout());
      const unsigned beforeFlush = source.attempts();
      MacScenePlatformController::flushPendingRelayouts();
      LOKA_VERIFY(source.attempts() > beforeFlush);
      LOKA_VERIFY(controller.hasPendingRelayout());
      LOKA_VERIFY(counter.calls() == beforeRetry);
      source.available(true);
      MacScenePlatformController::flushPendingRelayouts();
      LOKA_VERIFY(!controller.hasPendingRelayout());
      LOKA_VERIFY(counter.calls() == beforeRetry + 1);
      LOKA_VERIFY([[field stringValue] isEqualToString:@"retry the same wrapped text"]);
      LOKA_VERIFY(!NSIsEmptyRect([field frame]));
      controller.relayout(0, 0);
      LOKA_VERIFY(counter.calls() == beforeRetry + 1);
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
      NSTextField *field = AttributedField(root);
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
  VerifyMacMeasurementRefusal(plain, *source);
  AttributedTextNode rich((AttributedText(Styled(value, FontSize<12>()))
                           + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
  VerifyMacMeasurementRefusal(rich, *source);
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
    LOKA_VERIFY(counter.calls() == 2);
    NSTextField *plainField = (NSTextField *)[[root subviews] objectAtIndex:0];
    NSTextField *richField = (NSTextField *)[[root subviews] objectAtIndex:1];
    {
      StateTrackerGuard guard(&tracker);
      content.set(String::Literal("after wrapped words"));
      rich.set(Styled("after wrapped words", FontSize<12>()));
    }
    plain.layout(&controller, state);
    attributed.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 4);
    LOKA_VERIFY([[plainField stringValue] isEqualToString:@"after wrapped words"]);
    LOKA_VERIFY([[richField stringValue] isEqualToString:@"after wrapped words"]);
    {
      StateTrackerGuard guard(&tracker);
      style.set(FontSize<24>());
      rich.set(Styled("after wrapped words", FontSize<24>()));
    }
    plain.layout(&controller, state);
    attributed.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 6);
    LOKA_VERIFY(((Text(&content) + &style) + BlockStyle().wrap(TEXT_WRAP_WORD).align(TEXT_ALIGN_RIGHT))
                    .applyPropsToNode(&plain));
    LOKA_VERIFY((AttributedText(&rich) + BlockStyle().wrap(TEXT_WRAP_WORD).align(TEXT_ALIGN_RIGHT))
                    .applyPropsToNode(&attributed));
    plain.layout(&controller, state);
    attributed.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 8);
    LOKA_VERIFY([plainField alignment] == LOKA_MAC_TEXT_ALIGNMENT_RIGHT);
    NSParagraphStyle *paragraph = [[richField attributedStringValue]
        attribute:NSParagraphStyleAttributeName atIndex:0 effectiveRange:0];
    LOKA_VERIFY([paragraph alignment] == LOKA_MAC_TEXT_ALIGNMENT_RIGHT);
    plain.layout(&controller, state);
    attributed.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 8);
    // Retained content/source/style replacement and style removal all mark.
    LOKA_VERIFY((Text("replacement words") + FontSize<12>() + BlockStyle().wrap(TEXT_WRAP_WORD))
                    .applyPropsToNode(&plain));
    LOKA_VERIFY((AttributedText(Styled("replacement words", FontSize<12>()))
                + BlockStyle().wrap(TEXT_WRAP_WORD)).applyPropsToNode(&attributed));
    plain.layout(&controller, state);
    attributed.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 10);
    LOKA_VERIFY([[plainField stringValue] isEqualToString:@"replacement words"]);
    LOKA_VERIFY([[richField stringValue] isEqualToString:@"replacement words"]);
    LOKA_VERIFY(Text("unstyled").applyPropsToNode(&plain));
    plain.layout(&controller, state);
    LOKA_VERIFY([plainField alignment] == LOKA_MAC_TEXT_ALIGNMENT_NATURAL);
    LOKA_VERIFY(state.height == layout::FallbackControlMetrics::kTextHeight);
    LOKA_VERIFY((Text("activated words") + FontSize<24>() + BlockStyle().wrap(TEXT_WRAP_WORD))
                    .applyPropsToNode(&plain));
    plain.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 11);
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
    LOKA_VERIFY(counter.calls() == 12);
    LOKA_VERIFY([[richField stringValue] isEqualToString:@"changed while parked"]);
    attributed.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 12);
    LOKA_VERIFY(((Text(&content) + &style) + BlockStyle().wrap(TEXT_WRAP_WORD)).applyPropsToNode(&plain));
    plain.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 13);
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
    LOKA_VERIFY(counter.calls() == 14);
    LOKA_VERIFY([[plainField stringValue] isEqualToString:@"plain changed while parked"]);
    plain.layout(&controller, state);
    LOKA_VERIFY(counter.calls() == 14);
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
    NSFont *smallFixed = (NSFont *)controller.textFont(SmallLabel, true);
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
      DirectGapStack column(props);
      scene::NestableNode *container = 0;
      switch (reset)
      {
      case GAP_DOOR_BOX: container = new BoxNode((BoxProps())); break;
      case GAP_DOOR_ZSTACK: container = new ZStackNode((ZStackProps())); break;
      case GAP_DOOR_GRID: container = new GridNode(GridProps(1, 1)); break;
      case GAP_DOOR_SCROLL: container = new ScrollViewNode((ScrollViewProps())); break;
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
    DirectGapStack column(props);
    ScrollViewNode *scroll = new ScrollViewNode((ScrollViewProps()));
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
