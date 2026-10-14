#include "MacNodeHandlerEnsureTests.hpp"
#include "MacObjCCompat.hpp"
#include "MacScenePlatformController.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/layout/SceneRootLayout.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/TestVerify.hpp"
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
  // starts its root SceneRootLayout::kInset in from each edge.
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
      int x = layout::SceneRootLayout::kInset;
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
        x += width + layout::FallbackControlMetrics::kHorizontalSpacing;
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
    LOKA_VERIFY([second frame].origin.x == NSMaxX([first frame])
                + layout::FallbackControlMetrics::kHorizontalSpacing);
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
    const int gap = layout::FallbackControlMetrics::kHorizontalSpacing;
    LOKA_VERIFY(first.size.width == (kClientWidth - 2 * layout::SceneRootLayout::kInset - gap) / 2);
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
    const int firstY = layout::SceneRootLayout::kInset;
    LOKA_VERIFY(frame.origin.y == projection.projectEdge(firstY).pt);
    LOKA_VERIFY([buttonAt(root, 1) frame].origin.y
                == projection.projectEdge(firstY + height + layout::FallbackControlMetrics::kVerticalSpacing).pt);
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
