#include "MacTextVersionTests.hpp"
#include "MacScenePlatformController.hpp"
#include "MacGround.hpp"
#include "context/MacTextContext.hpp"
#include "app/nodes/Text.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/TestVerify.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/MacNativeRefusal.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/StringUTF8.hpp"
#include <AppKit/AppKit.h>
#include <cmath>
#include <cstring>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;

  struct TextVersionNativeScope
  {
    NSAutoreleasePool *pool;
    NSView *root;
    TextVersionNativeScope() : pool([[NSAutoreleasePool alloc] init]),
        root([[NSView alloc] initWithFrame:NSMakeRect(0, 0, 500, 500)])
    { [NSApplication sharedApplication]; }
    ~TextVersionNativeScope() { [root release]; [pool release]; }
  };

  class TextVersionFixture : public TextVersionNativeScope
  {
  public:
    MacScenePlatformController controller;
    TextNode node;
    LayoutState state;
    explicit TextVersionFixture(const TextProps &props, const RailMetrics &metrics = RailMetrics())
        : controller(root, metrics), node(props)
    {
      [NSApplication sharedApplication];
      node.setPropsTypeId(TextProps::staticTypeId());
      state.width = 100;
      NotifySubtreeNodeAttached(&node);
      LOKA_VERIFY(controller.prepareProjectedLayout(&node, state));
    }
    ~TextVersionFixture()
    {
      controller.onChange(0, NODE_DIRTY_NONE, false);
      LifecycleFactTestAccess::MarkSubtreeRetired(&node);
      LifecycleFactTestAccess::DeliverFacts(&node);
      node.setContext(0);
    }
    NSTextField *label() const { return (NSTextField *)[[root subviews] objectAtIndex:0]; }
    MacTextContext &context() const { return *static_cast<MacTextContext *>(node.getContext()); }
    void layout() { node.layout(&controller, state); }
    void installRoot()
    {
      controller.onChange(&node, NODE_DIRTY_NONE, false);
      controller.relayout(120, 500);
    }
  private:
    TextVersionFixture(const TextVersionFixture &);
    TextVersionFixture &operator=(const TextVersionFixture &);
  };

  NSTextField *PlainControl(const TextProps &props, MacScenePlatformController &controller, NSRect frame)
  {
    NSTextField *label = [[[NSTextField alloc] initWithFrame:frame] autorelease];
    [label setEditable:NO]; [label setSelectable:NO];
    [label setBezeled:NO]; [label setDrawsBackground:NO];
    NSTextFieldCell *cell = [label cell];
    const TextStyle style = props.resolvedTextStyle();
    NSColor *colour = nil;
    if (loka::macos::QueryMacTextColor(style.color_, colour)) [label setTextColor:colour];
    if (style.declaresFont()) [cell setFont:(NSFont *)controller.textFont(style)];
    const BlockStyle &block = props.blockStyle_;
    const bool wraps = block.hasWrap_ && block.wrap_ != TEXT_WRAP_NONE;
    if (props.hasDeclaredLayoutStyle())
    {
      if ([cell respondsToSelector:@selector(setUsesSingleLineMode:)]) [cell setUsesSingleLineMode:!wraps];
      [cell setWraps:wraps]; [cell setScrollable:!wraps];
      [cell setLineBreakMode:wraps ? (block.wrap_ == TEXT_WRAP_CHAR ? NSLineBreakByCharWrapping : NSLineBreakByWordWrapping)
          : (block.hasTruncation_ && block.truncation_ == TEXT_TRUNCATION_ELLIPSIS ? NSLineBreakByTruncatingTail : NSLineBreakByClipping)];
    }
    NSTextAlignment align = LOKA_MAC_TEXT_ALIGNMENT_NATURAL;
    if (props.hasDeclaredLayoutStyle())
    {
      switch (block.hasAlign_ ? block.align_ : TEXT_ALIGN_LEFT)
      {
      case TEXT_ALIGN_LEFT: align = LOKA_MAC_TEXT_ALIGNMENT_LEFT; break;
      case TEXT_ALIGN_CENTER: align = LOKA_MAC_TEXT_ALIGNMENT_CENTER; break;
      case TEXT_ALIGN_RIGHT: align = LOKA_MAC_TEXT_ALIGNMENT_RIGHT; break;
      }
    }
    [label setAlignment:align];
    std::string utf8;
    LOKA_VERIFY(loka::platform::CollectUtf8(props.text_->get(), utf8));
    [label setStringValue:[NSString stringWithUTF8String:utf8.c_str()]];
    return label;
  }

  void VerifyPixels(NSTextField *actual, NSTextField *control)
  {
    // A window gives both views the same rendering environment. Height 400
    // preserves every wrapped line in the migration matrix.
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 500, 420)
        styleMask:0 backing:NSBackingStoreBuffered defer:NO];
    NSView *oldParent = [[actual superview] retain];
    [actual retain];
    [[window contentView] addSubview:actual];
    [[window contentView] addSubview:control];
    NSBitmapImageRep *a = [actual bitmapImageRepForCachingDisplayInRect:[actual bounds]];
    NSBitmapImageRep *b = [control bitmapImageRepForCachingDisplayInRect:[control bounds]];
    LOKA_VERIFY(a && b);
    [actual cacheDisplayInRect:[actual bounds] toBitmapImageRep:a];
    [control cacheDisplayInRect:[control bounds] toBitmapImageRep:b];
    LOKA_VERIFY([a pixelsHigh] == [b pixelsHigh] && [a bytesPerRow] == [b bytesPerRow]);
    for (NSInteger y = 0; y < [a pixelsHigh]; ++y)
      LOKA_VERIFY(std::memcmp([a bitmapData] + y * [a bytesPerRow], [b bitmapData] + y * [b bytesPerRow],
                             static_cast<size_t>([a bytesPerRow])) == 0);
    [control removeFromSuperview];
    [oldParent addSubview:actual];
    [actual release]; [oldParent release]; [window release];
  }
}

void testMacTextVersionMigration()
{
  const char *strings[] = {"Underlined words near a wrapping threshold", "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d then latin words", "line one\nline two longer", ""};
  const int widths[] = {60, 104, 108, 160, 400};
  for (int policy = 0; policy < 6; ++policy)
    for (int s = 0; s < 4; ++s)
      for (int font = 0; font < (policy ? 2 : 1); ++font)
        for (int w = 0; w < 5; ++w)
        {
          TextProps props = Text(strings[s]).props;
          if (policy) props.blockStyle_.wrap(policy >= 4 ? (policy == 5 ? TEXT_WRAP_CHAR : TEXT_WRAP_WORD) : TEXT_WRAP_NONE);
          if (policy == 2 || policy == 3) props.blockStyle_.truncation(TEXT_TRUNCATION_ELLIPSIS);
          if (policy == 3) props.blockStyle_.align(TEXT_ALIGN_CENTER);
          if (policy == 5) props.blockStyle_.align(TEXT_ALIGN_RIGHT);
          if (font) props.textStyle_ = TextStyle(TextSizeValue::points(15)) + Bold + TextStyle().color(TEXT_COLOR_RED);
          TextVersionFixture f(props);
          for (int retained = 0; retained < 2; ++retained)
          {
            if (retained)
            {
              LOKA_VERIFY((Text("previous\nvalue") + FontSize<24>() + BlockStyle().wrap(TEXT_WRAP_WORD)).applyPropsToNode(&f.node));
              f.node.props = props;
              f.context().onPropsApplied();
            }
            const NSRect frame = NSMakeRect(0, 0, widths[w], 400);
            [f.label() setFrame:frame];
            NSTextField *control = PlainControl(props, f.controller, frame);
            const NSRect bounds = NSMakeRect(0, 0, widths[w], 1000);
            LOKA_VERIFY(NSEqualSizes([[f.label() cell] cellSizeForBounds:bounds], [[control cell] cellSizeForBounds:bounds]));
            VerifyPixels(f.label(), control);
          }
        }
}

void testMacTextVersionD1()
{
  TextVersionFixture f((Text("a long label that must recover native defaults") + BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS)).props);
  LOKA_VERIFY(Text("a long label that must recover native defaults").applyPropsToNode(&f.node));
  NSTextField *control = PlainControl(f.node.props, f.controller, NSMakeRect(0, 0, 100, 400));
  LOKA_VERIFY([[f.label() cell] wraps] == [[control cell] wraps]);
  LOKA_VERIFY([[f.label() cell] isScrollable] == [[control cell] isScrollable]);
  LOKA_VERIFY([[f.label() cell] lineBreakMode] == [[control cell] lineBreakMode]);
  [f.label() setFrame:[control frame]];
  VerifyPixels(f.label(), control);
}

void testMacTextVersionD2()
{
  // D2 is red on main on Mavericks (12pt display / 13pt measure). Tahoe
  // supplies the same fresh and system font, so both revisions are green there.
  TextVersionFixture f((Text("words near a wrapping threshold repeated words near a wrapping threshold") + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
  f.state.width = 60;
  f.layout();
  NSTextField *fresh = PlainControl(f.node.props, f.controller, [f.label() frame]);
  LOKA_VERIFY([[[f.label() cell] font] isEqual:[[fresh cell] font]]);
  const NSSize size = [[f.label() cell] cellSizeForBounds:NSMakeRect(0, 0, 60, 10000000)];
  const int measured = f.controller.projection().measurementToLu(size.height) + 2;
  LOKA_VERIFY(f.state.height == (measured > 20 ? measured : 20));
  LOKA_VERIFY((Text("aligned") + BlockStyle().align(TEXT_ALIGN_RIGHT)).applyPropsToNode(&f.node));
  LOKA_VERIFY([[[f.label() cell] font] isEqual:[[fresh cell] font]]);
}

void testMacTextVersionD3()
{
  MutableState<String> a(String::Literal("A")), b(String::Literal("B"));
  TextVersionFixture f(Text(&a).props);
  LOKA_VERIFY(Text(&b).applyPropsToNode(&f.node));
  a.set(String::Literal("old A"));
  LOKA_VERIFY([[f.label() stringValue] isEqualToString:@"B"]);
  b.set(String::Literal("new B"));
  LOKA_VERIFY([[f.label() stringValue] isEqualToString:@"new B"]);
  LOKA_VERIFY(Text("literal").applyPropsToNode(&f.node));
  b.set(String::Literal("ignored"));
  LOKA_VERIFY([[f.label() stringValue] isEqualToString:@"literal"]);
  NotifySubtreeNodeDetached(&f.node); LifecycleFactTestAccess::DeliverFacts(&f.node);
  LOKA_VERIFY(Text(&a).applyPropsToNode(&f.node));
  a.set(String::Literal("hidden A"));
  LOKA_VERIFY([f.label() isHidden]);
  LOKA_VERIFY([[f.label() stringValue] isEqualToString:@"hidden A"]);
  NotifySubtreeNodeAttached(&f.node); LifecycleFactTestAccess::DeliverFacts(&f.node);
  LOKA_VERIFY(![f.label() isHidden]);
}
