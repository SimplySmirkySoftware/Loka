#include "MacNodeHandlerEnsureTests.hpp"
#include "MacObjCCompat.hpp"
#include "MacScenePlatformController.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/Text.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "MacSceneLayout.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/MacNativeRefusal.hpp"
#include "support/TestVerify.hpp"
#include "support/MacWindowSession.hpp"
#include <cstdio>
#include <climits>
#include <cmath>
#include <string>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::dsl;
  using namespace loka::dsl::testing;

  // Every test lays out in one client box. MacScenePlatformController::performLayout
  // starts its root MacSceneLayout::kRootInset in from each edge.
  const int kClientWidth = 640;
  const int kClientHeight = 160;
  // A seat declared with width(n) instead of a natural width.
  const int kFixedWidth = 120;
  // Written before a declined query, which must leave it untouched.
  const short kUntouched = 17;

  NSView *newRootView()
  {
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, kClientWidth, kClientHeight)];
    LOKA_VERIFY(root != nil);
    return root;
  }

  NSButton *buttonAt(NSView *root, NSUInteger index)
  {
    NSArray *views = [root subviews];
    LOKA_VERIFY(index < [views count]);
    NSButton *button = (NSButton *)[views objectAtIndex:index];
    LOKA_VERIFY([button isKindOfClass:[NSButton class]]);
    return button;
  }

  // The reference is the control the rail projects for the node, never a
  // separately built cell: on macOS 10.14 through 13 a standalone NSButtonCell
  // reports a narrower size than the NSButton's own cell.
  CGFloat projectedCellWidth(const ButtonProps &props, NSString *title)
  {
    NSView *root = newRootView();
    CGFloat width = 0;
    {
      MacScenePlatformController controller(root, RailMetrics());
      ButtonNode node(props);
      controller.onChange(&node, NODE_DIRTY_NONE, false);
      controller.relayout(kClientWidth, kClientHeight);
      NSButton *button = buttonAt(root, 0);
      LOKA_VERIFY([[button title] isEqualToString:title]);
      width = [[button cell] cellSize].width;
      controller.onChange(0, NODE_DIRTY_NONE, false);
    }
    LOKA_VERIFY([[root subviews] count] == 0);
    [root release];
    return width;
  }

  // A different type key bypasses the registered Stack handler while retaining
  // the Stack shape; this pins the controller's direct Row fallback separately.
  class DirectRow : public StackNode
  {
  public:
    DirectRow() : StackNode(StackProps(STACK_AXIS_ROW))
    {
      this->props.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL;
    }
    virtual const void *nodeTypeKey() const { return NodeTypeToken<DirectRow>(); }
  };

  void verifyRibbonFrames(const RailMetrics &metrics)
  {
    NSView *root = newRootView();
    {
      MacScenePlatformController controller(root, metrics);
      NodeDefinitionBase *definition =
          (RibbonControl() << RibbonItem("New") << RibbonItem("Save As...")
                           << RibbonItem("Fixed").width(kFixedWidth)).clone();
      LOKA_VERIFY(definition != 0);
      Scene scene(definition);
      LOKA_VERIFY(scene.mount(&controller));
      SceneTestAccess::updateAttached(scene, true);
      controller.relayout(kClientWidth, kClientHeight);
      LOKA_VERIFY([[root subviews] count] == 3);
      const loka::macos::MacProjection &projection = controller.projection();
      int x = MacSceneLayout::kRootInset;
      for (NSUInteger i = 0; i < 3; ++i)
      {
        NSButton *button = buttonAt(root, i);
        NSString *title = i == 0 ? @"New" : i == 1 ? @"Save As..." : @"Fixed";
        LOKA_VERIFY([[button title] isEqualToString:title]);
        // The projected button's own cell, with AppKit's default font and
        // every implicit setting, is the width the measurement must answer.
        const CGFloat points = [[button cell] cellSize].width;
        const int width = i == 2 ? kFixedWidth : projection.measurementToLu(points);
        const NSRect frame = [button frame];
        LOKA_VERIFY(frame.origin.x == projection.projectEdge(x).pt);
        LOKA_VERIFY(frame.size.width == projection.projectLength(x, x + width).pt);
        if (metrics.spaceScale.isUnit())
          LOKA_VERIFY(frame.size.width == (i == 2 ? kFixedWidth : std::ceil(points)));
        if (i)
          LOKA_VERIFY(frame.origin.x > NSMaxX([buttonAt(root, i - 1) frame]));
        x += width + MacSceneLayout::kGap;
      }
      SceneTestAccess::unmount(scene);
    }
    LOKA_VERIFY([[root subviews] count] == 0);
    [root release];
  }
}

void testMacButtonNaturalWidthMatchesCell()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = newRootView();
  {
    MacScenePlatformController controller(root, RailMetrics());
    ButtonNode newButton(ButtonProps().text(loka::core::String::Literal("New")));
    ButtonNode saveButton(ButtonProps().text(loka::core::String::Literal("Save As...")));
    short newWidth = 0, saveWidth = 0, repeated = 0;
    LOKA_VERIFY(controller.queryNaturalWidth(&newButton, newWidth));
    LOKA_VERIFY(controller.queryNaturalWidth(&saveButton, saveWidth));
    LOKA_VERIFY(newWidth < saveWidth);
    LOKA_VERIFY(newWidth == std::ceil(projectedCellWidth(newButton.props, @"New")));
    LOKA_VERIFY(saveWidth == std::ceil(projectedCellWidth(saveButton.props, @"Save As...")));
    LOKA_VERIFY(controller.queryNaturalWidth(&newButton, repeated));
    LOKA_VERIFY(repeated == newWidth);
    StackNode other((StackProps()));
    repeated = kUntouched;
    LOKA_VERIFY(!controller.queryNaturalWidth(&other, repeated));
    LOKA_VERIFY(!controller.queryNaturalWidth(0, repeated));
    LOKA_VERIFY(repeated == kUntouched);
    // SHRT_MAX + 1 wide letters measure far past SHRT_MAX lu at any rail scale.
    ButtonNode oversized(ButtonProps().text(loka::core::String(std::string(SHRT_MAX + 1, 'W'))));
    LOKA_VERIFY(!controller.queryNaturalWidth(&oversized, repeated));
    LOKA_VERIFY(repeated == kUntouched);

    // With no text prop the projected button keeps NSButton's default title.
    ButtonNode untitled((ButtonProps()));
    LOKA_VERIFY(controller.queryNaturalWidth(&untitled, repeated));
    LOKA_VERIFY(repeated == std::ceil(projectedCellWidth(untitled.props, @"Button")));
  }
  [root release];
  [pool drain];
}

void testMacIntrinsicImageNaturalWidthMatchesProjection()
{
  using namespace loka::app;
  using namespace loka::core;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  const RailMetrics metrics[] = {RailMetrics(), loka::macos::DefaultRailMetrics()};
  for (int i = 0; i < 2; ++i)
  {
    NSView *root = newRootView();
    {
      MacScenePlatformController controller(root, metrics[i]);
      int native = 0;
      MutableState<resource::Image> imageState(resource::Image::FromNative(&native, 640, 10, 0, 0));
      ImageViewProps props;
      props.image(&imageState).attr(ImageViewAttr().sizePolicy(IMAGE_VIEW_SIZE_INTRINSIC));
      ImageViewNode image(props);
      short width = kUntouched;
      LOKA_VERIFY(controller.queryNaturalWidth(&image, width));
      // ResolveImageLayoutWidth is private to its translation unit; use its public conversion.
      LOKA_VERIFY(width == controller.projection().intrinsicPixelsToLu(640));
      const int overflowPixels = static_cast<int>(SHRT_MAX) * 4;
      LOKA_VERIFY(controller.projection().intrinsicPixelsToLu(overflowPixels) > SHRT_MAX);
      for (unsigned refusal = 0; refusal < 7; ++refusal)
      {
        image.props = props;
        imageState.set(resource::Image::FromNative(&native, 640, 10, 0, 0));
        switch (refusal)
        {
        case 0:
          image.props.width_ = 100;
          break;
        case 1:
          image.props.attr_.sizePolicyValue_ = IMAGE_VIEW_SIZE_AUTO;
          break;
        case 2:
          image.props.attr_.sizePolicyValue_ = IMAGE_VIEW_SIZE_FILL_PARENT;
          break;
        case 3:
          imageState.set(resource::Image::Empty());
          break;
        case 4:
          imageState.set(resource::Image::FromNative(&native, 0, 10, 0, 0));
          break;
        case 5:
          imageState.set(resource::Image::FromNative(&native, overflowPixels, 10, 0, 0));
          break;
        case 6:
          image.props.image_ = 0;
          break;
        }
        width = kUntouched;
        LOKA_VERIFY(!controller.queryNaturalWidth(&image, width));
        LOKA_VERIFY(width == kUntouched);
      }
    }
    [root release];
  }
  [pool drain];
}

void testMacRibbonNaturalWidthFrames()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  verifyRibbonFrames(RailMetrics());
  verifyRibbonFrames(loka::macos::DefaultRailMetrics());
  [pool drain];
}

void testMacDirectRowNaturalWidthFrames()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = newRootView();
  {
    MacScenePlatformController controller(root, RailMetrics());
    DirectRow row;
    row.addChild(new ButtonNode(ButtonProps().text(loka::core::String::Literal("New"))));
    row.addChild(new ButtonNode(ButtonProps().text(loka::core::String::Literal("Save As..."))));
    controller.onChange(&row, NODE_DIRTY_NONE, false);
    controller.relayout(kClientWidth, kClientHeight);
    LOKA_VERIFY([[root subviews] count] == 2);
    NSButton *first = buttonAt(root, 0);
    NSButton *second = buttonAt(root, 1);
    LOKA_VERIFY([first frame].size.width == std::ceil([[first cell] cellSize].width));
    LOKA_VERIFY([second frame].size.width == std::ceil([[second cell] cellSize].width));
    LOKA_VERIFY([second frame].origin.x == NSMaxX([first frame]) + MacSceneLayout::kGap);
    controller.onChange(0, NODE_DIRTY_NONE, false);
  }
  {
    MacScenePlatformController controller(root, RailMetrics());
    DirectRow row;
    ButtonNode *first = new ButtonNode(ButtonProps().text("< Back"));
    TextProps titleProps("A deliberately long toolbar title that must shrink before the last button");
    titleProps.blockStyle_ = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
    TextNode *title = new TextNode(titleProps);
    ButtonNode *last = new ButtonNode(ButtonProps().text("Main"));
    row.addChild(first);
    row.addChild(title);
    row.addChild(last);
    controller.onChange(&row, NODE_DIRTY_NONE, false);
    short firstWidth = 0, titleWidth = 0, lastWidth = 0;
    LOKA_VERIFY(controller.queryNaturalWidth(first, firstWidth));
    LOKA_VERIFY(controller.queryNaturalWidth(title, titleWidth));
    LOKA_VERIFY(controller.queryNaturalWidth(last, lastWidth));
    const int offer = firstWidth + lastWidth + 2 * MacSceneLayout::kGap + titleWidth / 2;
    controller.relayout(offer + 2 * MacSceneLayout::kRootInset, kClientHeight);
    LOKA_VERIFY([[root subviews] count] == 3);
    const NSRect firstFrame = [buttonAt(root, 0) frame];
    const NSRect titleFrame = [[[root subviews] objectAtIndex:1] frame];
    const NSRect lastFrame = [buttonAt(root, 2) frame];
    LOKA_VERIFY(firstFrame.size.width == firstWidth);
    LOKA_VERIFY(lastFrame.size.width == lastWidth);
    LOKA_VERIFY(titleFrame.size.width > 0 && titleFrame.size.width < titleWidth);
    LOKA_VERIFY(NSMaxX(lastFrame) <= MacSceneLayout::kRootInset + offer);
    controller.onChange(0, NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
}

void testMacOrdinaryRowStillSharesWidth()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = newRootView();
  {
    MacScenePlatformController controller(root, RailMetrics());
    NodeDefinitionBase *definition = (Row() << Button("New") << Button("Save As...")).clone();
    LOKA_VERIFY(definition != 0);
    Scene scene(definition);
    LOKA_VERIFY(scene.mount(&controller));
    SceneTestAccess::updateAttached(scene, true);
    controller.relayout(kClientWidth, kClientHeight);
    LOKA_VERIFY([[root subviews] count] == 2);
    const NSRect first = [buttonAt(root, 0) frame];
    const NSRect second = [buttonAt(root, 1) frame];
    const int gap = MacSceneLayout::kGap;
    LOKA_VERIFY(first.size.width == (kClientWidth - 2 * MacSceneLayout::kRootInset - gap) / 2);
    LOKA_VERIFY(second.size.width == first.size.width);
    LOKA_VERIFY(second.origin.x == NSMaxX(first) + gap);
    SceneTestAccess::unmount(scene);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
}

void testMacButtonNativeHeightColumn()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *root = newRootView();
  {
    MacScenePlatformController controller(root, loka::macos::DefaultRailMetrics());
    NodeDefinitionBase *definition = (Column() << Button("First") << Button("Second")).clone();
    LOKA_VERIFY(definition != 0);
    Scene scene(definition);
    LOKA_VERIFY(scene.mount(&controller));
    SceneTestAccess::updateAttached(scene, true);
    controller.relayout(kClientWidth, kClientHeight);
    LOKA_VERIFY([[root subviews] count] == 2);
    NSButton *first = buttonAt(root, 0);
    const NSRect frame = [first frame];
    const CGFloat nativeHeight = [[first cell] cellSize].height;
    const loka::macos::MacProjection &projection = controller.projection();
    const Ratio &scale = projection.railMetrics().spaceScale;
    const double pointsPerLu = static_cast<double>(scale.num) / scale.den;
    LOKA_VERIFY(frame.size.height >= nativeHeight);
    LOKA_VERIFY(frame.size.height < nativeHeight + pointsPerLu);
    const int height = projection.measurementToLu(nativeHeight);
    LOKA_VERIFY(controller.rowLayoutMetrics().buttonHeight == height);
    const int firstY = MacSceneLayout::kRootInset;
    LOKA_VERIFY(frame.origin.y == projection.projectEdge(firstY).pt);
    LOKA_VERIFY([buttonAt(root, 1) frame].origin.y
                == projection.projectEdge(firstY + height + MacSceneLayout::kGap).pt);
    SceneTestAccess::unmount(scene);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
}

void testMacButtonNativeHeightCenteredRow()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  for (int direct = 0; direct < 2; ++direct)
  {
    NSView *root = newRootView();
    {
      MacScenePlatformController controller(root, loka::macos::DefaultRailMetrics());
      // Exercise both the registered Stack handler and the controller fallback.
      StackNode *row = direct ? new DirectRow() : new StackNode(StackProps(STACK_AXIS_ROW));
      row->props.alignVertical(VERTICAL_ALIGNMENT_CENTER);
      row->addChild(new ButtonNode(ButtonProps().text(loka::core::String::Literal("Button"))));
      row->addChild(new EditTextNode(EditTextProps()));
      controller.onChange(row, NODE_DIRTY_NONE, false);
      controller.relayout(kClientWidth, kClientHeight);
      LOKA_VERIFY([[root subviews] count] == 2);
      const NSRect button = [buttonAt(root, 0) frame];
      NSView *field = [[root subviews] objectAtIndex:1];
      LOKA_VERIFY([field isKindOfClass:[NSTextField class]]);
      const Ratio &scale = controller.projection().railMetrics().spaceScale;
      const double pointsPerLu = static_cast<double>(scale.num) / scale.den;
      LOKA_VERIFY(std::fabs(NSMidY(button) - NSMidY([field frame])) <= pointsPerLu);
      controller.onChange(0, NODE_DIRTY_NONE, false);
      delete row;
    }
    LOKA_VERIFY([[root subviews] count] == 0);
    [root release];
  }
  [pool drain];
}

namespace
{
  NSSize MacDeclinedCellSize(id, SEL)
  {
    return NSZeroSize;
  }
} // namespace

void testMacButtonDeclinedHeightKeepsRowAligned()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  for (int direct = 0; direct < 2; ++direct)
  {
    NSView *root = newRootView();
    MacScenePlatformController *controller = 0;
    {
      // Only the construction-time measurement declines. Every later push cell
      // answers natively, so a reader that measured again would disagree.
      MacNativeRefusal refusal(@selector(cellSize), reinterpret_cast<IMP>(&MacDeclinedCellSize), [NSButtonCell class]);
      controller = new MacScenePlatformController(root, loka::macos::DefaultRailMetrics());
    }
    LOKA_VERIFY(controller->rowLayoutMetrics().buttonHeight == layout::FallbackControlMetrics::kButtonHeight);
    LOKA_VERIFY(MacSceneLayout::spacing().horizontalGap() == MacSceneLayout::kGap);
    StackNode *row = direct ? new DirectRow() : new StackNode(StackProps(STACK_AXIS_ROW));
    row->props.alignVertical(VERTICAL_ALIGNMENT_CENTER);
    row->addChild(new ButtonNode(ButtonProps().text(loka::core::String::Literal("Button"))));
    row->addChild(new EditTextNode(EditTextProps()));
    controller->onChange(row, NODE_DIRTY_NONE, false);
    controller->relayout(kClientWidth, kClientHeight);
    LOKA_VERIFY([[root subviews] count] == 2);
    const loka::macos::MacProjection &projection = controller->projection();
    const int top = MacSceneLayout::kRootInset;
    const NSRect button = [buttonAt(root, 0) frame];
    LOKA_VERIFY(NSMinY(button) == projection.projectEdge(top).pt);
    LOKA_VERIFY(NSMaxY(button) == projection.projectEdge(top + layout::FallbackControlMetrics::kButtonHeight).pt);
    const Ratio &scale = projection.railMetrics().spaceScale;
    const double pointsPerLu = static_cast<double>(scale.num) / scale.den;
    LOKA_VERIFY(std::fabs(NSMidY(button) - NSMidY([[[root subviews] objectAtIndex:1] frame])) <= pointsPerLu);
    controller->onChange(0, NODE_DIRTY_NONE, false);
    delete row;
    delete controller;
    LOKA_VERIFY([[root subviews] count] == 0);
    [root release];
  }
  [pool drain];
}

// Like MacObjCCompat's NSWindow category, only supplies the legacy SDK
// return type; respondsToSelector: remains the runtime availability check.
@interface NSScreen (LokaToolbarBackingScaleCompat)
- (CGFloat)backingScaleFactor;
@end

// Deliberate twin of MacClipBoxTests' flipped capture root. Opaque ground
// makes every cached pixel defined, including the space between the seats.
@interface LokaToolbarPixelRoot : NSView
@end
@implementation LokaToolbarPixelRoot
- (BOOL)isFlipped
{
  return YES;
}
- (void)drawRect:(NSRect)dirty
{
  (void)dirty;
  [[NSColor whiteColor] set];
  NSRectFill([self bounds]);
}
@end

namespace
{
  NSBitmapImageRep *toolbarCapture(NSView *root)
  {
    [root setNeedsDisplay:YES];
    [root displayIfNeeded];
    NSBitmapImageRep *rep = [root bitmapImageRepForCachingDisplayInRect:[root bounds]];
    LOKA_VERIFY(rep != nil);
    [root cacheDisplayInRect:[root bounds] toBitmapImageRep:rep];
    return rep;
  }

  void toolbarPixels(NSScreen *screen, int backing, const RailMetrics &metrics)
  {
    NSView *root = [[LokaToolbarPixelRoot alloc] initWithFrame:NSMakeRect(0, 0, 1000, 100)];
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, 1000, 100)
                                                   styleMask:LOKA_MAC_WINDOW_STYLE_TITLED
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO
                                                      screen:screen];
    LOKA_VERIFY(root && window);
    [window setReleasedWhenClosed:NO];
    [window setContentView:root];
    [window orderFront:nil];
    {
      MacScenePlatformController controller(root, metrics);
      // DirectRow keeps the seats directly under root, as testMacDirectRowNaturalWidthFrames does.
      DirectRow row;
      ButtonNode *first = new ButtonNode(ButtonProps().text("< Back"));
      TextProps titleProps("A toolbar title wide enough to need truncation");
      titleProps.blockStyle_ = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
      TextNode *title = new TextNode(titleProps);
      ButtonNode *last = new ButtonNode(ButtonProps().text("Main"));
      row.addChild(first);
      row.addChild(title);
      row.addChild(last);
      controller.onChange(&row, NODE_DIRTY_NONE, false);
      short firstWidth = 0, titleWidth = 0, lastWidth = 0;
      LOKA_VERIFY(controller.queryNaturalWidth(first, firstWidth));
      LOKA_VERIFY(controller.queryNaturalWidth(title, titleWidth));
      LOKA_VERIFY(controller.queryNaturalWidth(last, lastWidth));
      LOKA_VERIFY([[root subviews] count] == 3);
      NSTextField *field = (NSTextField *)[[root subviews] objectAtIndex:1];
      LOKA_VERIFY([field isKindOfClass:[NSTextField class]]);
      LOKA_VERIFY([[field cell] lineBreakMode] == NSLineBreakByTruncatingTail);
      NSString *text = [[field stringValue] copy];
      const CGFloat dots = [@"\u2026" sizeWithAttributes:[NSDictionary dictionaryWithObject:[field font]
                                                                                     forKey:NSFontAttributeName]]
                               .width;
      const int narrow = controller.projection().measurementToLu(dots) + 4;
      LOKA_VERIFY(titleWidth > narrow);
      // Full title is the positive control; then every logical width around
      // and below the ellipsis, including the lower bound of one.
      for (int seat = narrow + 1; seat >= 1; --seat)
      {
        NSAutoreleasePool *capturePool = [[NSAutoreleasePool alloc] init];
        const int width = seat == narrow + 1 ? titleWidth : seat;
        const int offer = firstWidth + lastWidth + 2 * MacSceneLayout::kGap + width;
        [field setStringValue:text];
        controller.relayout(offer + 2 * MacSceneLayout::kRootInset, 80);
        const NSRect frame = [field frame];
        const NSRect firstFrame = [buttonAt(root, 0) frame];
        const NSRect lastFrame = [buttonAt(root, 2) frame];
        const int x = MacSceneLayout::kRootInset + firstWidth + MacSceneLayout::kGap;
        LOKA_VERIFY(frame.size.width == controller.projection().projectLength(x, x + width).pt);
        NSBitmapImageRep *withTitle = toolbarCapture(root);
        // Change only native ink: an empty logical title declines natural
        // measurement and would change the seat. Do not reconsult the Row.
        [field setStringValue:@""];
        LOKA_VERIFY(NSEqualRects(frame, [field frame]));
        LOKA_VERIFY(NSEqualRects(firstFrame, [buttonAt(root, 0) frame]));
        LOKA_VERIFY(NSEqualRects(lastFrame, [buttonAt(root, 2) frame]));
        NSBitmapImageRep *empty = toolbarCapture(root);
        LOKA_VERIFY([withTitle pixelsWide] == [empty pixelsWide]);
        LOKA_VERIFY([withTitle pixelsHigh] == [empty pixelsHigh]);
        LOKA_VERIFY([withTitle samplesPerPixel] == [empty samplesPerPixel]);
        const CGFloat sx = [withTitle pixelsWide] / [root bounds].size.width;
        const CGFloat sy = [withTitle pixelsHigh] / [root bounds].size.height;
        LOKA_VERIFY(sx == backing && sy == backing);
        LOKA_VERIFY([withTitle samplesPerPixel] <= 5);
        unsigned inside = 0, outside = 0;
        for (NSInteger py = 0; py < [withTitle pixelsHigh]; ++py)
          for (NSInteger px = 0; px < [withTitle pixelsWide]; ++px)
          {
            NSUInteger a[5] = {0}, b[5] = {0};
            [withTitle getPixel:a atX:px y:py];
            [empty getPixel:b atX:px y:py];
            bool different = false;
            for (NSInteger c = 0; c < [withTitle samplesPerPixel]; ++c)
              different = different || a[c] != b[c];
            if (different)
            {
              if (NSPointInRect(NSMakePoint((px + 0.5) / sx, (py + 0.5) / sy), frame))
                ++inside;
              else
                ++outside;
            }
          }
        std::printf("[pin] toolbar backing=%d space=%d/%d seat=%d frame=(%.2f,%.2f,%.2f,%.2f) inside=%u outside=%u\n",
                    backing,
                    metrics.spaceScale.num,
                    metrics.spaceScale.den,
                    width,
                    static_cast<double>(frame.origin.x),
                    static_cast<double>(frame.origin.y),
                    static_cast<double>(frame.size.width),
                    static_cast<double>(frame.size.height),
                    inside,
                    outside);
        std::fflush(stdout);
        LOKA_VERIFY(outside == 0);
        if (width == titleWidth)
          LOKA_VERIFY(inside > 0);
        if (width == 1)
          LOKA_VERIFY(inside == 0);
        [capturePool drain];
      }
      [text release];
      controller.onChange(0, NODE_DIRTY_NONE, false);
    }
    LOKA_VERIFY([[root subviews] count] == 0);
    [window orderOut:nil];
    [window close];
    [window release];
    [root release];
  }
} // namespace

void testMacToolbarEllipsisNarrowPixels()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  for (int backing = 1; backing <= 2; ++backing)
  {
    NSScreen *screen = nil;
    NSArray *screens = [NSScreen screens];
    for (NSUInteger i = 0; i < [screens count]; ++i)
    {
      NSScreen *candidate = [screens objectAtIndex:i];
      const CGFloat scale =
          [candidate respondsToSelector:@selector(backingScaleFactor)] ? [candidate backingScaleFactor] : 1;
      if (scale == backing)
      {
        screen = candidate;
        break;
      }
    }
    if (!screen || loka::testing::MacConsoleScreenIsLocked())
    {
      std::printf("[skip] toolbar backing=%d: no matching screen or console locked\n", backing);
      continue;
    }
    toolbarPixels(screen, backing, RailMetrics());
    toolbarPixels(screen, backing, loka::macos::DefaultRailMetrics());
  }
  [pool drain];
}
