#include "MacNodeHandlerEnsureTests.hpp"
#include "MacObjCCompat.hpp"
#include "MacScenePlatformController.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/TestVerify.hpp"
#include <cmath>
#include <string>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::dsl;
  using namespace loka::dsl::testing;

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
  CGFloat projectedCellWidth(MacScenePlatformController &controller, NSView *root, ButtonNode &node)
  {
    controller.onChange(&node, NODE_DIRTY_NONE, false);
    controller.relayout(640, 160);
    const CGFloat width = [[buttonAt(root, 0) cell] cellSize].width;
    controller.onChange(0, NODE_DIRTY_NONE, false);
    LOKA_VERIFY([[root subviews] count] == 0);
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
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, 240)];
    LOKA_VERIFY(root != nil);
    {
      MacScenePlatformController controller(root, metrics);
      NodeDefinitionBase *definition =
          (RibbonControl() << RibbonItem("New") << RibbonItem("Save As...")
                           << RibbonItem("Fixed").width(120)).clone();
      LOKA_VERIFY(definition != 0);
      Scene scene(definition);
      LOKA_VERIFY(scene.mount(&controller));
      SceneTestAccess::updateAttached(scene, true);
      controller.relayout(640, 160);
      LOKA_VERIFY([[root subviews] count] == 3);
      const loka::macos::MacProjection &projection = controller.projection();
      int x = 20;
      for (NSUInteger i = 0; i < 3; ++i)
      {
        NSButton *button = buttonAt(root, i);
        NSString *title = i == 0 ? @"New" : i == 1 ? @"Save As..." : @"Fixed";
        LOKA_VERIFY([[button title] isEqualToString:title]);
        // The projected button's own cell, with AppKit's default font and
        // every implicit setting, is the width the measurement must answer.
        const CGFloat points = [[button cell] cellSize].width;
        const int width = i == 2 ? 120 : projection.measurementToLu(points);
        const NSRect frame = [button frame];
        LOKA_VERIFY(frame.origin.x == projection.projectEdge(x).pt);
        LOKA_VERIFY(frame.size.width == projection.projectLength(x, x + width).pt);
        if (metrics.spaceScale.isUnit())
          LOKA_VERIFY(frame.size.width == (i == 2 ? 120 : std::ceil(points)));
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
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 640, 240)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller(root, RailMetrics());
    ButtonNode newButton(ButtonProps().text(loka::core::String::Literal("New")));
    ButtonNode saveButton(ButtonProps().text(loka::core::String::Literal("Save As...")));
    short newWidth = 0, saveWidth = 0, repeated = 0;
    LOKA_VERIFY(controller.queryNaturalWidth(&newButton, newWidth));
    LOKA_VERIFY(controller.queryNaturalWidth(&saveButton, saveWidth));
    LOKA_VERIFY(newWidth < saveWidth);
    LOKA_VERIFY(newWidth == std::ceil(projectedCellWidth(controller, root, newButton)));
    LOKA_VERIFY(saveWidth == std::ceil(projectedCellWidth(controller, root, saveButton)));
    LOKA_VERIFY(controller.queryNaturalWidth(&newButton, repeated));
    LOKA_VERIFY(repeated == newWidth);
    StackNode other((StackProps()));
    repeated = 17;
    LOKA_VERIFY(!controller.queryNaturalWidth(&other, repeated));
    LOKA_VERIFY(!controller.queryNaturalWidth(0, repeated));
    LOKA_VERIFY(repeated == 17);
    ButtonNode oversized(ButtonProps().text(loka::core::String(std::string(32768, 'W'))));
    LOKA_VERIFY(!controller.queryNaturalWidth(&oversized, repeated));
    LOKA_VERIFY(repeated == 17);

    ButtonNode untitled((ButtonProps()));
    controller.onChange(&untitled, NODE_DIRTY_NONE, false);
    controller.relayout(640, 160);
    LOKA_VERIFY([[buttonAt(root, 0) title] isEqualToString:@"Button"]);
    controller.onChange(0, NODE_DIRTY_NONE, false);
    LOKA_VERIFY(controller.queryNaturalWidth(&untitled, repeated));
    LOKA_VERIFY(repeated == std::ceil(projectedCellWidth(controller, root, untitled)));
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
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 640, 240)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller(root, RailMetrics());
    DirectRow row;
    row.addChild(new ButtonNode(ButtonProps().text(loka::core::String::Literal("New"))));
    row.addChild(new ButtonNode(ButtonProps().text(loka::core::String::Literal("Save As..."))));
    controller.onChange(&row, NODE_DIRTY_NONE, false);
    controller.relayout(640, 160);
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
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 640, 240)];
  LOKA_VERIFY(root != nil);
  {
    MacScenePlatformController controller(root, RailMetrics());
    NodeDefinitionBase *definition = (Row() << Button("New") << Button("Save As...")).clone();
    LOKA_VERIFY(definition != 0);
    Scene scene(definition);
    LOKA_VERIFY(scene.mount(&controller));
    SceneTestAccess::updateAttached(scene, true);
    controller.relayout(640, 160);
    LOKA_VERIFY([[root subviews] count] == 2);
    const NSRect first = [buttonAt(root, 0) frame];
    const NSRect second = [buttonAt(root, 1) frame];
    const int gap = layout::FallbackControlMetrics::kHorizontalSpacing;
    LOKA_VERIFY(first.size.width == (640 - 40 - gap) / 2);
    LOKA_VERIFY(second.size.width == first.size.width);
    LOKA_VERIFY(second.origin.x == NSMaxX(first) + gap);
    SceneTestAccess::unmount(scene);
  }
  LOKA_VERIFY([[root subviews] count] == 0);
  [root release];
  [pool drain];
}
