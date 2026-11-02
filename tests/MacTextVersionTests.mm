#include "MacTextVersionTests.hpp"
#include "MacScenePlatformController.hpp"
#include "MacGround.hpp"
#include "context/MacTextContext.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/AttributedText.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/TestVerify.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/MacNativeRefusal.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/String.hpp"
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
    TextVersionNativeScope()
        : pool([[NSAutoreleasePool alloc] init]),
          root([[NSView alloc] initWithFrame:NSMakeRect(0, 0, 500, 500)])
    {
      [NSApplication sharedApplication];
    }
    ~TextVersionNativeScope()
    {
      [root release];
      [pool release];
    }
  };

  class TextVersionFixture : public TextVersionNativeScope
  {
  public:
    MacScenePlatformController controller;
    TextNode node;
    LayoutState state;
    explicit TextVersionFixture(const TextProps &props, const RailMetrics &metrics = RailMetrics())
        : controller(root, metrics),
          node(props)
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
    NSTextField *label() const
    {
      return (NSTextField *)[[root subviews] objectAtIndex:0];
    }
    MacTextContext &context() const
    {
      return *static_cast<MacTextContext *>(node.getContext());
    }
    void layout()
    {
      node.layout(&controller, state);
    }
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
    [label setEditable:NO];
    [label setSelectable:NO];
    [label setBezeled:NO];
    [label setDrawsBackground:NO];
    NSTextFieldCell *cell = [label cell];
    const TextStyle style = props.resolvedTextStyle();
    NSColor *colour = nil;
    if (loka::macos::QueryMacTextColor(style.color_, colour))
      [label setTextColor:colour];
    if (style.declaresFont())
      [cell setFont:(NSFont *)controller.textFont(style)];
    const BlockStyle &block = props.blockStyle_;
    const bool wraps = block.hasWrap_ && block.wrap_ != TEXT_WRAP_NONE;
    if (props.hasDeclaredLayoutStyle())
    {
      if ([cell respondsToSelector:@selector(setUsesSingleLineMode:)])
        [cell setUsesSingleLineMode:!wraps];
      [cell setWraps:wraps];
      [cell setScrollable:!wraps];
      [cell setLineBreakMode:wraps ? (block.wrap_ == TEXT_WRAP_CHAR ? NSLineBreakByCharWrapping
                                                                    : NSLineBreakByWordWrapping)
                                   : (block.hasTruncation_ && block.truncation_ == TEXT_TRUNCATION_ELLIPSIS
                                          ? NSLineBreakByTruncatingTail
                                          : NSLineBreakByClipping)];
    }
    NSTextAlignment align = LOKA_MAC_TEXT_ALIGNMENT_NATURAL;
    if (props.hasDeclaredLayoutStyle())
    {
      switch (block.hasAlign_ ? block.align_ : TEXT_ALIGN_LEFT)
      {
      case TEXT_ALIGN_LEFT:
        align = LOKA_MAC_TEXT_ALIGNMENT_LEFT;
        break;
      case TEXT_ALIGN_CENTER:
        align = LOKA_MAC_TEXT_ALIGNMENT_CENTER;
        break;
      case TEXT_ALIGN_RIGHT:
        align = LOKA_MAC_TEXT_ALIGNMENT_RIGHT;
        break;
      }
    }
    [label setAlignment:align];
    std::string utf8;
    LOKA_VERIFY(loka::platform::CollectUtf8(props.text_->get(), utf8));
    [label setStringValue:[NSString stringWithUTF8String:utf8.c_str()]];
    return label;
  }

  const char *const migrationStrings[] = {"Underlined words near a wrapping threshold",
                                          "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d then latin words",
                                          "line one\nline two longer",
                                          ""};
  const int migrationWidths[] = {60, 104, 108, 160, 400};

  TextProps MigrationProps(const char *text, int policy, int font)
  {
    TextProps props = Text(text).props;
    if (policy)
      props.blockStyle_.wrap(policy >= 4 ? (policy == 5 ? TEXT_WRAP_CHAR : TEXT_WRAP_WORD) : TEXT_WRAP_NONE);
    if (policy == 2 || policy == 3)
      props.blockStyle_.truncation(TEXT_TRUNCATION_ELLIPSIS);
    if (policy == 3)
      props.blockStyle_.align(TEXT_ALIGN_CENTER);
    if (policy == 5)
      props.blockStyle_.align(TEXT_ALIGN_RIGHT);
    if (font)
      props.textStyle_ = TextStyle(TextSizeValue::points(15)) + Bold + TextStyle().color(TEXT_COLOR_RED);
    return props;
  }

  NSBitmapImageRep *CaptureTextPixels(NSTextField *field)
  {
    NSBitmapImageRep *image = [field bitmapImageRepForCachingDisplayInRect:[field bounds]];
    LOKA_VERIFY(image);
    [field cacheDisplayInRect:[field bounds] toBitmapImageRep:image];
    return image;
  }

  void VerifyPixels(NSTextField *actual, NSTextField *control)
  {
    // A window gives both views the same rendering environment. Height 400
    // preserves every wrapped line in the migration matrix.
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 500, 420)
                                                   styleMask:0
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    NSView *oldParent = [[actual superview] retain];
    [actual retain];
    [[window contentView] addSubview:actual];
    [[window contentView] addSubview:control];
    NSBitmapImageRep *a = CaptureTextPixels(actual);
    NSBitmapImageRep *b = CaptureTextPixels(control);
    LOKA_VERIFY(a && b);
    LOKA_VERIFY([a pixelsHigh] == [b pixelsHigh] && [a bytesPerRow] == [b bytesPerRow]);
    for (NSInteger y = 0; y < [a pixelsHigh]; ++y)
      LOKA_VERIFY(std::memcmp([a bitmapData] + y * [a bytesPerRow],
                              [b bitmapData] + y * [b bytesPerRow],
                              static_cast<size_t>([a bytesPerRow]))
                  == 0);
    [control removeFromSuperview];
    [oldParent addSubview:actual];
    [actual release];
    [oldParent release];
    [window release];
  }
} // namespace

void testMacTextVersionMigration()
{
  for (int policy = 0; policy < 6; ++policy)
    for (int s = 0; s < 4; ++s)
      for (int font = 0; font < (policy ? 2 : 1); ++font)
        for (int w = 0; w < 5; ++w)
        {
          TextProps props = MigrationProps(migrationStrings[s], policy, font);
          TextVersionFixture f(props);
          for (int retained = 0; retained < 2; ++retained)
          {
            if (retained)
            {
              LOKA_VERIFY((Text("previous\nvalue") + FontSize<24>() + BlockStyle().wrap(TEXT_WRAP_WORD))
                              .applyPropsToNode(&f.node));
              f.node.props = props;
              f.context().onPropsApplied();
            }
            const NSRect frame = NSMakeRect(0, 0, migrationWidths[w], 400);
            [f.label() setFrame:frame];
            NSTextField *control = PlainControl(props, f.controller, frame);
            const NSRect bounds = NSMakeRect(0, 0, migrationWidths[w], 1000);
            LOKA_VERIFY(
                NSEqualSizes([[f.label() cell] cellSizeForBounds:bounds], [[control cell] cellSizeForBounds:bounds]));
            VerifyPixels(f.label(), control);
          }
        }
}

void testMacTextVersionD1()
{
  TextVersionFixture f(
      (Text("a long label that must recover native defaults") + BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS))
          .props);
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
  TextVersionFixture f((Text("words near a wrapping threshold repeated words near a wrapping threshold")
                        + BlockStyle().wrap(TEXT_WRAP_WORD))
                           .props);
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
  NotifySubtreeNodeDetached(&f.node);
  LifecycleFactTestAccess::DeliverFacts(&f.node);
  LOKA_VERIFY(Text(&a).applyPropsToNode(&f.node));
  a.set(String::Literal("hidden A"));
  LOKA_VERIFY([f.label() isHidden]);
  LOKA_VERIFY([[f.label() stringValue] isEqualToString:@"hidden A"]);
  NotifySubtreeNodeAttached(&f.node);
  LifecycleFactTestAccess::DeliverFacts(&f.node);
  LOKA_VERIFY(![f.label() isHidden]);
}

#include "testing/MacTextVersionTestAccess.hpp"
#include "testing/scene/NodeObservedUsesTestAccess.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "support/MacRetirementProbe.hpp"
#include "MacNodeHandlerEnsureTests.hpp"
#include <cstdio>

namespace
{
  typedef loka::dsl::testing::MacTextVersionTestAccess VersionAccess;
  typedef loka::app::testing::NodeObservedUsesTestAccess UsesAccess;
  unsigned versionBuilds = 0, versionMeasures = 0;
  int draftDeallocations = 0;
  IMP fieldInit = 0, cellMeasure = 0;
  id CountTextField(id self, SEL selector, NSRect frame)
  {
    ++versionBuilds;
    id field = ((id(*)(id, SEL, NSRect))fieldInit)(self, selector, frame);
    if (field)
      observeMacNativeDeallocation((void *)field, draftDeallocations);
    return field;
  }
  NSSize CountTextMeasure(id self, SEL selector, NSRect bounds)
  {
    ++versionMeasures;
    return ((NSSize(*)(id, SEL, NSRect))cellMeasure)(self, selector, bounds);
  }
  struct VersionCounterPool
  {
    NSAutoreleasePool *pool;
    VersionCounterPool()
        : pool([[NSAutoreleasePool alloc] init])
    {
    }
    ~VersionCounterPool()
    {
      [pool release];
    }
  };

  class VersionCounters : private VersionCounterPool
  {
  public:
    VersionCounters()
        : fields_(prepareField(), (IMP)CountTextField, [NSTextField class]),
          measures_(prepareMeasure(), (IMP)CountTextMeasure)
    {
      versionBuilds = versionMeasures = 0;
      draftDeallocations = 0;
    }

  private:
    static SEL prepareField()
    {
      [NSApplication sharedApplication];
      fieldInit = method_getImplementation(class_getInstanceMethod([NSTextField class], @selector(initWithFrame:)));
      return @selector(initWithFrame:);
    }
    static SEL prepareMeasure()
    {
      cellMeasure =
          method_getImplementation(class_getInstanceMethod([NSTextFieldCell class], @selector(cellSizeForBounds:)));
      return @selector(cellSizeForBounds:);
    }
    MacNativeRefusal fields_, measures_;
    VersionCounters(const VersionCounters &);
    VersionCounters &operator=(const VersionCounters &);
  };

  const MacTextVersion &Version(const TextVersionFixture &f)
  {
    return VersionAccess::version(f.context());
  }
  bool Measured(const TextVersionFixture &f)
  {
    CGFloat constraint = 0;
    return Version(f).measure().queryConstraint(constraint);
  }
  void VerifyVersion(const TextVersionFixture &f)
  {
    const MacTextVersion &v = Version(f);
    NSTextFieldCell *cell = [f.label() cell];
    LOKA_VERIFY(v.value());
    LOKA_VERIFY([[f.label() attributedStringValue] isEqualToAttributedString:(NSAttributedString *)v.value()]);
    LOKA_VERIFY([cell wraps] == v.cell().wraps && [cell isScrollable] == v.cell().scrollable);
    LOKA_VERIFY([cell lineBreakMode] == static_cast<NSLineBreakMode>(v.cell().lineBreakMode) &&
                [cell alignment] == static_cast<NSTextAlignment>(v.cell().alignment));
    if ([cell respondsToSelector:@selector(usesSingleLineMode)])
      LOKA_VERIFY(((BOOL)(long)[cell performSelector:@selector(usesSingleLineMode)] != NO) == v.cell().singleLine);
    LOKA_VERIFY([cell font] == (NSFont *)v.cell().font);
    LOKA_VERIFY([[cell textColor] isEqual:(NSColor *)v.cell().colour]);
  }

  class VersionOwner : public BoundaryNodeFor<VersionOwner>
  {
  public:
    explicit VersionOwner(const BoundaryPropsFor<VersionOwner> &props = BoundaryPropsFor<VersionOwner>())
        : BoundaryNodeFor<VersionOwner>(props)
    {
    }
    virtual void composeNode(NodeComposition &) {}
  };
  /** A real Boundary projects its owned input to retained literal props. The
      binding is reopened on attach and never adds a Boundary recompose door. */
  class VersionLiteralOwner : public BoundaryNodeFor<VersionLiteralOwner>
  {
  public:
    explicit VersionLiteralOwner(
        const BoundaryPropsFor<VersionLiteralOwner> &props = BoundaryPropsFor<VersionLiteralOwner>())
        : BoundaryNodeFor<VersionLiteralOwner>(props)
    {
      this->state(this->literal_, String::Literal("short"));
    }
    virtual void declareBindings(BindingToken &token)
    {
      token.watch(*this->literal_.state(), this, &VersionLiteralOwner::applyLiteral, true);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Text(this->literal_.get()) + BlockStyle().wrap(TEXT_WRAP_WORD));
    }
    void change(const String &text)
    {
      StateTrackerGuard guard(this->tracker());
      this->literal_.set(text);
    }

  private:
    void applyLiteral()
    {
      if (!this->childrenHead())
        return;
      TextNode *text = this->childrenHead()->asTextNode();
      LOKA_VERIFY(text);
      LOKA_VERIFY((Text(this->literal_.get()) + BlockStyle().wrap(TEXT_WRAP_WORD)).applyPropsToNode(text));
    }
    NodeState<String> literal_;
  };

  struct LayoutFirst
  {
    TextVersionFixture &f;
    explicit LayoutFirst(TextVersionFixture &fixture)
        : f(fixture)
    {
    }
    static void changed(void *data)
    {
      static_cast<LayoutFirst *>(data)->f.layout();
    }
  };
  struct RetireFirst
  {
    TextVersionFixture &f;
    explicit RetireFirst(TextVersionFixture &fixture)
        : f(fixture)
    {
    }
    static void changed(void *data)
    {
      RetireFirst &p = *static_cast<RetireFirst *>(data);
      LifecycleFactTestAccess::MarkSubtreeRetired(&p.f.node);
      LifecycleFactTestAccess::DeliverFacts(&p.f.node);
    }
  };
  class VersionRetryString : public loka::platform::String
  {
  public:
    bool available;
    VersionRetryString()
        : available(false)
    {
    }
    virtual bool appendUtf8(std::string &out) const
    {
      out.append("partial words repeated near wrapping threshold");
      return available;
    }
  };
  class VersionInvalidUtf8 : public loka::platform::String
  {
  public:
    virtual bool appendUtf8(std::string &out) const
    {
      out.append(1, static_cast<char>(0xff));
      return true;
    }
  };
  id NoNativeObject(id, SEL)
  {
    return nil;
  }
  NSColor *versionTestColour = nil;
  id VersionColour(id, SEL)
  {
    return versionTestColour;
  }
} // namespace

void testMacTextVersionOneVersion()
{
  VersionCounters counters;
  MutableState<String> text(String::Literal("initial wrapped words"));
  TextVersionFixture f((Text(&text) + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
  VerifyVersion(f);
  LOKA_VERIFY(!Measured(f));
  LOKA_VERIFY(versionBuilds == 2 && versionMeasures == 0); // Label + one draft.
  f.layout();
  VerifyVersion(f);
  LOKA_VERIFY(Measured(f) && versionBuilds == 3 && versionMeasures == 1);
  f.layout();
  VerifyVersion(f);
  LOKA_VERIFY(versionBuilds == 3 && versionMeasures == 1);
  text.set(String::Literal("changed wrapped words"));
  VerifyVersion(f);
  LOKA_VERIFY(!Measured(f));
  LOKA_VERIFY(versionBuilds == 4 && versionMeasures == 1);
  LOKA_VERIFY((Text("applied wrapped words") + BlockStyle().wrap(TEXT_WRAP_WORD)).applyPropsToNode(&f.node));
  VerifyVersion(f);
  LOKA_VERIFY(!Measured(f));
  LOKA_VERIFY(versionBuilds == 5 && versionMeasures == 1);
}

void testMacTextVersionOrdering()
{
  for (int wrapped = 0; wrapped < 2; ++wrapped)
    for (int first = 0; first < 2; ++first)
    {
      VersionCounters counters;
      MutableState<String> text(String::Literal("short"));
      PushStateTracker tracker;
      if (!first)
        tracker.addState(&text);
      VersionOwner owner;
      TextVersionFixture f((Text(&text) + BlockStyle().wrap(wrapped ? TEXT_WRAP_WORD : TEXT_WRAP_NONE)).props);
      BoundaryNode::declareBoundaryDirtySources(&f.node, &owner);
      f.installRoot();
      f.layout();
      LOKA_VERIFY(Measured(f));
      LOKA_VERIFY(UsesAccess::mark(f.node) == NODE_DIRTY_NONE);
      const unsigned builds = versionBuilds, measures = versionMeasures;
      LayoutFirst before(f);
      if (first)
        text.bind(&LayoutFirst::changed, &before, false);
      {
        StateTrackerGuard guard(first ? 0 : &tracker);
        text.set(String::Literal("longer text with several words that require several lines to present"));
        LOKA_VERIFY(Version(f).value());
        LOKA_VERIFY(Measured(f) == !wrapped);
        LOKA_VERIFY(versionMeasures == measures + (first && wrapped ? 1 : 0));
      }
      VerifyVersion(f);
      LOKA_VERIFY(versionBuilds == builds + (first ? 2 : 1));
      LOKA_VERIFY(f.controller.hasPendingRelayout() == (!first && wrapped));
      if (first)
        LOKA_VERIFY(UsesAccess::mark(f.node) == NODE_DIRTY_NONE);
      else
        LOKA_VERIFY(UsesAccess::mark(f.node) != NODE_DIRTY_NONE);
      const unsigned beforeLayout = versionMeasures;
      f.layout();
      LOKA_VERIFY(versionMeasures == beforeLayout + (wrapped ? 1 : 0));
      LOKA_VERIFY(UsesAccess::mark(f.node) == NODE_DIRTY_NONE);
      const unsigned hitBuilds = versionBuilds, hitMeasures = versionMeasures;
      f.layout();
      LOKA_VERIFY(versionBuilds == hitBuilds && versionMeasures == hitMeasures);
      if (first)
        text.unbind(&LayoutFirst::changed, &before);
    }
}

void testMacTextVersionNoStaleMeasure()
{
  for (int props = 0; props < 2; ++props)
  {
    VersionCounters counters;
    MutableState<String> text(String::Literal("short"));
    TextVersionFixture f((Text(&text) + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
    f.layout();
    const int oldHeight = f.state.height;
    const unsigned measured = versionMeasures;
    if (props)
      LOKA_VERIFY((Text("much longer words repeated many times much longer words repeated many times much longer words")
                   + BlockStyle().wrap(TEXT_WRAP_WORD))
                      .applyPropsToNode(&f.node));
    else
      text.set(String::Literal(
          "much longer words repeated many times much longer words repeated many times much longer words"));
    LOKA_VERIFY(!Measured(f) && versionMeasures == measured);
    // Deliberately consume marks: only absence of this version's measure can
    // force the next visit to miss. A carried old measurement fails this pin.
    f.node.takeLayoutInputs();
    f.layout();
    LOKA_VERIFY(versionMeasures == measured + 1 && f.state.height > oldHeight);
  }
}

void testMacTextVersionRelayout()
{
  for (int wraps = 0; wraps < 2; ++wraps)
  {
    VersionCounters counters;
    MutableState<String> text(String::Literal("same words"));
    const BlockStyle block = BlockStyle().wrap(wraps ? TEXT_WRAP_WORD : TEXT_WRAP_NONE);
    TextVersionFixture f((Text(&text) + block).props);
    f.installRoot();
    const unsigned measured = versionMeasures;
    LOKA_VERIFY((Text(&text) + block + TextStyle().color(TEXT_COLOR_RED)).applyPropsToNode(&f.node));
    LOKA_VERIFY(!f.controller.hasPendingRelayout() && versionMeasures == measured);
    LOKA_VERIFY((Text(&text) + block + TextStyle().color(TEXT_COLOR_RED)).applyPropsToNode(&f.node));
    LOKA_VERIFY(!f.controller.hasPendingRelayout() && versionMeasures == measured);
    text.set(text.get(), true);
    LOKA_VERIFY(!f.controller.hasPendingRelayout() && versionMeasures == measured);
    text.set(String::Literal("changed words"));
    LOKA_VERIFY(f.controller.hasPendingRelayout() == (wraps != 0));
    LOKA_VERIFY(versionMeasures == measured);
    f.controller.relayout(120, 500);
    LOKA_VERIFY((Text(&text) + block + TextStyle(TextSizeValue::points(36))).applyPropsToNode(&f.node));
    LOKA_VERIFY(f.controller.hasPendingRelayout());
  }
}

void testMacTextVersionPropsSchedule()
{
  TextVersionFixture f((Text("short") + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
  f.installRoot();
  const CGFloat before = [f.label() frame].size.height;
  LOKA_VERIFY((Text("a long literal with many words to wrap into many lines a long literal with many words")
               + BlockStyle().wrap(TEXT_WRAP_WORD))
                  .applyPropsToNode(&f.node));
  LOKA_VERIFY(f.controller.hasPendingRelayout());
  MacScenePlatformController::flushPendingRelayouts();
  LOKA_VERIFY([f.label() frame].size.height > before);

  // The owner binding projects literal props. Its tracker/Scene cycle and
  // pending native flush must reach the new height without a manual layout.
  typedef loka::dsl::testing::SceneTestAccess SceneAccess;
  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 120, 500)];
  {
    MacScenePlatformController controller(root, RailMetrics());
    Scene scene((Boundary<VersionLiteralOwner>()));
    LOKA_VERIFY(scene.mount(&controller));
    SceneAccess::updateAttached(scene, true);
    controller.relayout(120, 500);
    VersionLiteralOwner *owner = static_cast<VersionLiteralOwner *>(SceneAccess::rootBoundary(scene));
    TextNode *node = owner->childrenHead()->asTextNode();
    LOKA_VERIFY(node);
    for (int detached = 0; detached < 2; ++detached)
    {
      // Retained detach of the Text itself (a Scene-level detach discards the
      // composition and with it the owner this loop holds).
      if (detached)
      {
        NotifySubtreeNodeDetached(node);
        LifecycleFactTestAccess::DeliverFacts(node);
      }
      owner->change(String::Literal(
          detached ? "short" : "long words repeated long words repeated long words repeated long words repeated"));
      if (detached)
      {
        NotifySubtreeNodeAttached(node);
        LifecycleFactTestAccess::DeliverFacts(node);
      }
      if (scene.hasPendingInvalidation())
        SceneAccess::flushInvalidation(scene);
      MacScenePlatformController::flushPendingRelayouts();
      LOKA_VERIFY(owner->childrenHead() == node);
      NSTextField *label = (NSTextField *)[[root subviews] objectAtIndex:0];
      const int height = VersionAccess::version(*static_cast<MacTextContext *>(node->getContext())).measure().extent();
      LOKA_VERIFY([label frame].size.height == height);
      LOKA_VERIFY(detached ? height == 20 : height > 20);
    }
    SceneAccess::unmount(scene);
  }
  [root release];
}

void testMacTextVersionNonWrappingUpdates()
{
  for (int declared = 0; declared < 2; ++declared)
  {
    MutableState<String> text(String::Literal("one"));
    TextProps props = Text(&text).props;
    if (declared)
      props.blockStyle_.wrap(TEXT_WRAP_NONE);
    TextVersionFixture f(props);
    f.installRoot();
    if (!declared)
      LOKA_VERIFY([[f.label() cell] wraps]);
    for (int i = 0; i < 5; ++i)
    {
      text.set(String::Literal(i % 2 ? "one" : "a longer counter value"));
      LOKA_VERIFY(!f.controller.hasPendingRelayout());
      LOKA_VERIFY(!Version(f).wraps() && Version(f).measure().extent() == 20);
      LOKA_VERIFY([f.label() frame].size.height == 20);
      LOKA_VERIFY([[f.label() stringValue] isEqualToString:i % 2 ? @"one" : @"a longer counter value"]);
    }
  }
}

void testMacTextVersionLayoutFirstNoRelayout()
{
  // The State has no tracker: the Scene's ordinary dirty-source observer
  // flushes synchronously, before the context's deferred text observer.
  VersionCounters counters;
  TextVersionNativeScope native;
  MutableState<String> text(String::Literal("one"));
  MacScenePlatformController controller(native.root, RailMetrics());
  typedef loka::dsl::testing::SceneTestAccess SceneAccess;
  Scene scene((Text(&text) + BlockStyle().wrap(TEXT_WRAP_WORD)).clone());
  LOKA_VERIFY(scene.mount(&controller));
  SceneAccess::updateAttached(scene, true);
  controller.relayout(120, 500);
  TextNode *node = SceneAccess::rootBoundary(scene)->childrenHead()->asTextNode();
  LOKA_VERIFY(node);
  const unsigned builds = versionBuilds, measures = versionMeasures;
  text.set(String::Literal("a long value which the Scene lays out before the deferred text observer"));
  LOKA_VERIFY(!scene.hasPendingInvalidation());
  LOKA_VERIFY(!controller.hasPendingRelayout());
  const MacTextVersion &v = VersionAccess::version(*static_cast<MacTextContext *>(node->getContext()));
  CGFloat constraint = 0;
  LOKA_VERIFY(!v.measure().queryConstraint(constraint));
  LOKA_VERIFY(UsesAccess::mark(*node) == NODE_DIRTY_NONE);
  LOKA_VERIFY(versionBuilds >= builds + 2 && versionMeasures >= measures + 1);
  // An unmarked visit still measures the callback's new version once.
  const unsigned before = versionMeasures;
  LayoutState state;
  state.width = 100;
  node->layout(&controller, state);
  LOKA_VERIFY(versionMeasures == before + 1);
  SceneAccess::unmount(scene);
}

void testMacTextVersionShapeChanges()
{
  TextVersionFixture f(Text("short").props);
  f.installRoot();
  CGFloat constraint = 0;
  LOKA_VERIFY(Version(f).measure().queryConstraint(constraint));
  LOKA_VERIFY((Text("short") + TextStyle(TextSizeValue::points(48))).applyPropsToNode(&f.node));
  LOKA_VERIFY(f.controller.hasPendingRelayout());
  const int large = Version(f).measure().extent();
  LOKA_VERIFY(large > 20 && Version(f).measure().reusable(constraint));
  LOKA_VERIFY((Text("short") + FontSize<10>()).applyPropsToNode(&f.node));
  LOKA_VERIFY(Version(f).measure().extent() == 20);
  LOKA_VERIFY(
      (Text("many words to wrap many words to wrap") + BlockStyle().wrap(TEXT_WRAP_WORD)).applyPropsToNode(&f.node));
  LOKA_VERIFY(Version(f).wraps() && !Measured(f));
  f.layout();
  LOKA_VERIFY(
      (Text("many words to wrap many words to wrap") + BlockStyle().wrap(TEXT_WRAP_NONE)).applyPropsToNode(&f.node));
  LOKA_VERIFY(!Version(f).wraps() && Version(f).measure().extent() == 20);
}

void testMacTextVersionRefusalRecovery()
{
  VersionCounters counters;
  VersionRetryString *source = new VersionRetryString();
  const String refused(Managed<loka::platform::String>::Wrap(source));
  MutableState<String> text(refused);
  TextVersionFixture f((Text(&text) + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
  LOKA_VERIFY(!Version(f).value() && [[f.label() stringValue] length] == 0);
  resource::Image bitmap;
  LOKA_VERIFY(!f.context().captureBitmap(bitmap));
  LOKA_VERIFY(!f.controller.hasPendingRelayout());
  f.installRoot();
  LOKA_VERIFY([f.label() frame].size.height == 0);
  source->available = true;
  f.layout();
  VerifyVersion(f); // Same input, no publication: layout retries.
  LOKA_VERIFY(f.state.height > 0);
  for (int props = 0; props < 2; ++props)
  {
    source->available = false;
    if (props)
      f.context().onPropsApplied();
    else
      text.set(text.get(), true);
    LOKA_VERIFY(!Version(f).value() && [[f.label() stringValue] length] == 0);
    LOKA_VERIFY(f.controller.hasPendingRelayout());
    MacScenePlatformController::flushPendingRelayouts();
    LOKA_VERIFY([f.label() frame].size.height == 0);
    // A paint-only publication clears the retry's pending bit without a
    // successful layout. Recovery itself must schedule the missing placement.
    f.controller.onChange(&f.node, NODE_DIRTY_NONE, false);
    LOKA_VERIFY(!f.controller.hasPendingRelayout());
    source->available = true;
    const unsigned measures = versionMeasures;
    if (props)
      f.context().onPropsApplied();
    else
      text.set(text.get(), true);
    LOKA_VERIFY(Version(f).value() && versionMeasures == measures);
    LOKA_VERIFY(f.controller.hasPendingRelayout());
    MacScenePlatformController::flushPendingRelayouts();
    LOKA_VERIFY([f.label() frame].size.height > 0);
  }
}

void testMacTextVersionRetirePendingCallback()
{
  VersionCounters counters;
  MutableState<String> text(String::Literal("before"));
  TextVersionFixture f(Text(&text).props);
  RetireFirst retire(f);
  text.bind(&RetireFirst::changed, &retire, false);
  const unsigned builds = versionBuilds;
  text.set(String::Literal("after"));
  LOKA_VERIFY(!Version(f).value() && versionBuilds == builds);
  text.unbind(&RetireFirst::changed, &retire);
  text.set(String::Literal("still unbound"));
  LOKA_VERIFY(versionBuilds == builds);
}

void testMacTextVersionPoolLifetime()
{
  VersionCounters counters;
  for (int empty = 0; empty < 2; ++empty)
  {
    TextVersionFixture f(Text(empty ? "" : "survives the build pool").props);
    VerifyVersion(f);
    LOKA_VERIFY([(NSFont *)Version(f).cell().font pointSize] > 0);
    LOKA_VERIFY([(NSColor *)Version(f).cell().colour description]);
    LOKA_VERIFY(draftDeallocations > 0);
    // Reachable refusals after a field exists, after its settings exist, and
    // after the plain string exists. Every draft is released synchronously.
    const SEL selectors[] = { @selector(font), @selector(attributedStringValue) };
    for (int failure = 0; failure < 2; ++failure)
    {
      const int deaths = draftDeallocations;
      {
        MacNativeRefusal refuse(selectors[failure], (IMP)NoNativeObject);
        f.context().onPropsApplied();
      }
      LOKA_VERIFY(!Version(f).value() && draftDeallocations == deaths + 1);
      f.context().onPropsApplied();
      VerifyVersion(f);
    }
    const int deaths = draftDeallocations;
    const String refused(Managed<loka::platform::String>::Wrap(new VersionRetryString()));
    LOKA_VERIFY(Text(refused).applyPropsToNode(&f.node));
    LOKA_VERIFY(!Version(f).value() && draftDeallocations == deaths + 1);
    const String invalid(Managed<loka::platform::String>::Wrap(new VersionInvalidUtf8()));
    LOKA_VERIFY(Text(invalid).applyPropsToNode(&f.node));
    LOKA_VERIFY(!Version(f).value() && draftDeallocations == deaths + 2);
    LOKA_VERIFY(Text(empty ? "" : "recovered").applyPropsToNode(&f.node));
    VerifyVersion(f);
  }
}

void testMacTextVersionEmptyColourLifetime()
{
  for (int retire = 0; retire < 2; ++retire)
  {
    int deallocated = 0;
    TextVersionFixture f(Text("").props);
    NSAutoreleasePool *owners = [[NSAutoreleasePool alloc] init];
    versionTestColour = [[NSColor colorWithCalibratedRed:0.123 green:0.234 blue:0.345 alpha:1] retain];
    observeMacNativeDeallocation((void *)versionTestColour, deallocated);
    {
      MacNativeRefusal colour(@selector(controlTextColor), (IMP)VersionColour, object_getClass([NSColor class]));
      f.context().onPropsApplied();
      LOKA_VERIFY(Version(f).cell().colour == (void *)versionTestColour);
      LOKA_VERIFY([(NSAttributedString *)Version(f).value() length] == 0);
    }
    // Empty values have no runs retaining the colour. Releasing the label's
    // owner leaves only the version's explicit cell retain.
    [f.label() setTextColor:[NSColor blackColor]];
    [versionTestColour release];
    versionTestColour = nil;
    [owners release];
    LOKA_VERIFY(deallocated == 0);
    if (retire)
    {
      LifecycleFactTestAccess::MarkSubtreeRetired(&f.node);
      LifecycleFactTestAccess::DeliverFacts(&f.node);
    }
    else
      f.context().onPropsApplied();
    LOKA_VERIFY(deallocated == 1);
  }
}

void testMacTextVersionReconciliation()
{
  VersionCounters counters;
  TextVersionFixture f(
      (Text("two lines of wrapped words with explicit style") + FontSize<12>() + BlockStyle().wrap(TEXT_WRAP_WORD))
          .props);
  f.layout();
  const unsigned builds = versionBuilds, measures = versionMeasures;
  NSAttributedString *value = [(NSAttributedString *)Version(f).value() retain];
  [[f.label() cell] setFont:[NSFont systemFontOfSize:30]];
  [f.label() setAlignment:LOKA_MAC_TEXT_ALIGNMENT_RIGHT];
  [[f.label() cell] setWraps:NO];
  [[f.label() cell] setScrollable:YES];
  [[f.label() cell] setLineBreakMode:NSLineBreakByClipping];
  f.layout();
  VerifyVersion(f);
  LOKA_VERIFY([[f.label() attributedStringValue] isEqualToAttributedString:value]);
  LOKA_VERIFY(versionBuilds == builds && versionMeasures == measures);
  [value release];
}

void testMacTextVersionSizeEdges()
{
  TextVersionFixture f((Text("") + BlockStyle().wrap(TEXT_WRAP_WORD)).props, loka::macos::DefaultRailMetrics());
  f.layout();
  LOKA_VERIFY(f.state.height == 20);
  LOKA_VERIFY((Text("some words") + TextStyle(TextSizeValue::points(48)) + BlockStyle().wrap(TEXT_WRAP_WORD))
                  .applyPropsToNode(&f.node));
  f.state.width = 0;
  f.layout();
  LOKA_VERIFY(f.state.height > 20);
  f.state.width = 101;
  f.state.x = 1;
  f.layout();
  LOKA_VERIFY(f.controller.projection().projectLength(0, 101).pt == 126);
  LOKA_VERIFY([f.label() frame].size.width == 127);
  LOKA_VERIFY(Version(f).measure().reusable(126));
}

void testMacTextVersionDynamicColour()
{
  TextVersionFixture f((Text("dynamic colour") + TextStyle().color(TEXT_COLOR_PRIMARY)).props);
  NSColor *colour = nil;
  LOKA_VERIFY(loka::macos::QueryMacTextColor(TEXT_COLOR_PRIMARY, colour));
  NSColor *attribute = [(NSAttributedString *)Version(f).value() attribute:NSForegroundColorAttributeName
                                                                   atIndex:0
                                                            effectiveRange:0];
  LOKA_VERIFY(attribute == colour);
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 1090
  if (NSAppKitVersionNumber < 1671)
  {
    std::printf("[skip] Text version pixels: no dark appearance\n");
    return;
  }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  NSAppearance *old = [[NSAppearance currentAppearance] retain];
  NSBitmapImageRep *images[2];
  [f.label() setFrame:NSMakeRect(0, 0, 160, 40)];
  NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 200, 100)
                                                 styleMask:0
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
  [f.label() retain];
  NSTextField *label = f.label();
  [[window contentView] addSubview:label];
  for (int dark = 0; dark < 2; ++dark)
  {
    NSAppearance *appearance =
        [NSAppearance appearanceNamed:dark ? @"NSAppearanceNameDarkAqua" : @"NSAppearanceNameAqua"];
    [NSAppearance setCurrentAppearance:appearance];
    [label setAppearance:appearance];
    images[dark] = [[label bitmapImageRepForCachingDisplayInRect:[label bounds]] retain];
    [label cacheDisplayInRect:[label bounds] toBitmapImageRep:images[dark]];
  }
  LOKA_VERIFY([images[0] bytesPerRow] == [images[1] bytesPerRow]);
  bool differs = false;
  for (NSInteger y = 0; y < [images[0] pixelsHigh]; ++y)
    if (std::memcmp([images[0] bitmapData] + y * [images[0] bytesPerRow],
                    [images[1] bitmapData] + y * [images[1] bytesPerRow],
                    static_cast<size_t>([images[0] bytesPerRow]))
        != 0)
      differs = true;
  LOKA_VERIFY(differs);
  [images[0] release];
  [images[1] release];
  [NSAppearance setCurrentAppearance:old];
  [old release];
  [f.root addSubview:label];
  [label release];
  [window release];
#pragma clang diagnostic pop
#else
  std::printf("[skip] Text version pixels: no dark appearance in SDK\n");
#endif
}

void testMacTextVersionNaturalWidth()
{
  const char *values[] = {"natural width", "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d Latin"};
  for (int i = 0; i < 2; ++i)
    for (int declared = 0; declared < 2; ++declared)
    {
      TextProps props = Text(values[i]).props;
      if (declared)
        props.textStyle_ = FontSize<24>();
      TextVersionFixture f(props);
      short width = -1;
      LOKA_VERIFY(MacTextContext::queryNaturalWidth(&f.node, &f.controller, width));
      NSTextField *control = [[[NSTextField alloc] initWithFrame:NSZeroRect] autorelease];
      [control setEditable:NO];
      [control setSelectable:NO];
      [control setBezeled:NO];
      [control setDrawsBackground:NO];
      NSTextFieldCell *cell = [control cell];
      [cell setStringValue:[NSString stringWithUTF8String:values[i]]];
      [cell setFont:declared ? (NSFont *)f.controller.textFont(props.resolvedTextStyle()) : [cell font]];
      [cell setWraps:NO];
      [cell setScrollable:YES];
      [cell setLineBreakMode:NSLineBreakByClipping];
      const NSSize size = [cell cellSizeForBounds:NSMakeRect(0, 0, CGFLOAT_MAX, CGFLOAT_MAX)];
      LOKA_VERIFY(width == f.controller.projection().measurementToLu(size.width));
    }
}

namespace
{
  void VerifyUnderline(NSAttributedString *value, bool on)
  {
    LOKA_VERIFY(value);
    for (NSUInteger i = 0; i < [value length]; ++i)
    {
      NSRange range;
      NSNumber *attribute = [value attribute:NSUnderlineStyleAttributeName atIndex:i effectiveRange:&range];
      if (on)
      {
        LOKA_VERIFY([attribute intValue] == NSUnderlineStyleSingle);
        LOKA_VERIFY(NSEqualRanges(range, NSMakeRange(0, [value length])));
      }
      else
        LOKA_VERIFY(attribute == nil);
    }
  }

  class UnderlineRunFixture : public TextVersionNativeScope
  {
  public:
    MacScenePlatformController controller;
    AttributedTextNode node;
    LayoutState state;
    explicit UnderlineRunFixture(const AttributedTextProps &props)
        : controller(root, RailMetrics()),
          node(props)
    {
      node.setPropsTypeId(AttributedTextProps::staticTypeId());
      state.width = 160;
      NotifySubtreeNodeAttached(&node);
      LOKA_VERIFY(controller.prepareProjectedLayout(&node, state));
      node.layout(&controller, state);
    }
    ~UnderlineRunFixture()
    {
      LifecycleFactTestAccess::MarkSubtreeRetired(&node);
      LifecycleFactTestAccess::DeliverFacts(&node);
      node.setContext(0);
    }
    NSTextField *label() const
    {
      return (NSTextField *)[[root subviews] objectAtIndex:0];
    }

  private:
    UnderlineRunFixture(const UnderlineRunFixture &);
    UnderlineRunFixture &operator=(const UnderlineRunFixture &);
  };

  /** Host the fixture root without changing ownership of the label. */
  class UnderlineWindow
  {
  public:
    explicit UnderlineWindow(NSView *root)
        : window_([[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 500, 500)
                                              styleMask:0
                                                backing:NSBackingStoreBuffered
                                                  defer:NO])
    {
      [[this->window_ contentView] addSubview:root];
    }
    ~UnderlineWindow()
    {
      [this->window_ release];
    }

  private:
    NSWindow *window_;
    UnderlineWindow(const UnderlineWindow &);
    UnderlineWindow &operator=(const UnderlineWindow &);
  };
} // namespace

void testMacTextUnderlineRuns()
{
  UnderlineRunFixture f(AttributedTextProps(Styled("AB", Underline) + Styled("CD", TextStyle().underline(false))
                                            + Styled("EF", TextStyle()) + Styled("GH", Underline)));
  NSAttributedString *value = [f.label() attributedStringValue];
  LOKA_VERIFY([[value string] isEqualToString:@"ABCDEFGH"]);
  for (NSUInteger i = 0; i < 8; ++i)
  {
    NSRange range;
    NSNumber *attribute = [value attribute:NSUnderlineStyleAttributeName atIndex:i effectiveRange:&range];
    if (i < 2 || i >= 6)
    {
      LOKA_VERIFY([attribute intValue] == NSUnderlineStyleSingle);
      LOKA_VERIFY(NSEqualRanges(range, NSMakeRange(i < 2 ? 0 : 6, 2)));
    }
    else
      LOKA_VERIFY(attribute == nil);
  }
}

void testMacTextUnderlineTransitions()
{
  const TextStyle styles[] = {Underline, TextStyle().underline(false), TextStyle()};
  const char *strings[] = {"initial", "changed", "unset"};
  for (int live = 0; live < 2; ++live)
    for (int changing = 0; changing < 2; ++changing)
    {
      MutableState<String> text(String::Literal(strings[0]));
      TextVersionFixture f(Text(&text).props);
      for (int step = 0; step < 3; ++step)
      {
        const char *expected = strings[changing ? step : 0];
        if (live)
        {
          LOKA_VERIFY((Text(&text) + styles[step]).applyPropsToNode(&f.node));
          VerifyUnderline([f.label() attributedStringValue], step == 0);
          text.set(String::Literal(expected), true);
        }
        else
          LOKA_VERIFY((Text(expected) + styles[step]).applyPropsToNode(&f.node));
        LOKA_VERIFY([[f.label() stringValue] isEqualToString:[NSString stringWithUTF8String:expected]]);
        VerifyUnderline([f.label() attributedStringValue], step == 0);
      }
    }
}

void testMacTextUnderlineSizes()
{
  for (int policy = 0; policy < 6; ++policy)
    for (int s = 0; s < 4; ++s)
      for (int font = 0; font < (policy ? 2 : 1); ++font)
        for (int w = 0; w < 5; ++w)
        {
          const TextProps plain = MigrationProps(migrationStrings[s], policy, font);
          TextProps underlined = plain;
          underlined.textStyle_ = underlined.textStyle_ + Underline;
          TextVersionFixture off(plain), on(underlined);
          off.state.width = on.state.width = migrationWidths[w];
          off.layout();
          on.layout();
          LOKA_VERIFY(off.state.height == on.state.height && off.state.width == on.state.width);
          LOKA_VERIFY(Version(off).measure().extent() == Version(on).measure().extent());
          VerifyUnderline([on.label() attributedStringValue], true);
          const NSRect bounds = NSMakeRect(0, 0, migrationWidths[w], 1000);
          [off.label() setFrame:NSMakeRect(0, 0, migrationWidths[w], 400)];
          [on.label() setFrame:[off.label() frame]];
          LOKA_VERIFY(NSEqualSizes([[off.label() cell] cellSizeForBounds:bounds],
                                   [[on.label() cell] cellSizeForBounds:bounds]));
          LOKA_VERIFY(NSEqualRects([[off.label() cell] titleRectForBounds:bounds],
                                   [[on.label() cell] titleRectForBounds:bounds]));
          short a = -1, b = -1;
          const bool answers = MacTextContext::queryNaturalWidth(&off.node, &off.controller, a);
          LOKA_VERIFY(answers == MacTextContext::queryNaturalWidth(&on.node, &on.controller, b));
          if (answers)
            LOKA_VERIFY(a == b);

          // Keep the same logical segment boundaries in both values (O3).
          UnderlineRunFixture runOff(
              (AttributedText(Styled(migrationStrings[s], plain.textStyle_)) + plain.blockStyle_).props);
          UnderlineRunFixture runOn(
              (AttributedText(Styled(migrationStrings[s], underlined.textStyle_)) + plain.blockStyle_).props);
          runOff.state.width = runOn.state.width = migrationWidths[w];
          runOff.node.layout(&runOff.controller, runOff.state);
          runOn.node.layout(&runOn.controller, runOn.state);
          VerifyUnderline([runOn.label() attributedStringValue], true);
          LOKA_VERIFY(NSEqualSizes([[runOff.label() cell] cellSizeForBounds:bounds],
                                   [[runOn.label() cell] cellSizeForBounds:bounds]));
          LOKA_VERIFY(NSEqualRects([[runOff.label() cell] titleRectForBounds:bounds],
                                   [[runOn.label() cell] titleRectForBounds:bounds]));
        }
}

void testMacTextUnderlinePixels()
{
  const char *strings[] = {"Underline", "line one\nline two", ""};
  const int lines[] = {1, 2, 0};
  for (int s = 0; s < 3; ++s)
  {
    TextVersionFixture f((Text(strings[s]) + FontSize<12>() + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
    UnderlineWindow window(f.root);
    [f.label() setFrame:NSMakeRect(0, 0, 160, 400)];
    NSBitmapImageRep *off = [CaptureTextPixels(f.label()) retain];
    LOKA_VERIFY(
        (Text(strings[s]) + FontSize<12>() + BlockStyle().wrap(TEXT_WRAP_WORD) + Underline).applyPropsToNode(&f.node));
    NSBitmapImageRep *on = CaptureTextPixels(f.label());
    LOKA_VERIFY([off pixelsHigh] == [on pixelsHigh] && [off bytesPerRow] == [on bytesPerRow]);
    const int limit = [on pixelsHigh] > 400 ? 4 : 2;
    int bands = 0, bandStart = -1;
    for (NSInteger y = 0; y <= [on pixelsHigh]; ++y)
    {
      const bool differs = y < [on pixelsHigh]
                           && std::memcmp([off bitmapData] + y * [off bytesPerRow],
                                          [on bitmapData] + y * [on bytesPerRow],
                                          static_cast<size_t>([on bytesPerRow]))
                                  != 0;
      if (differs && bandStart < 0)
        bandStart = static_cast<int>(y);
      if (!differs && bandStart >= 0)
      {
        const int thickness = static_cast<int>(y) - bandStart;
        std::printf("underline pixels: string %d rows %d..%ld thickness %d (limit %d)\n",
                    s,
                    bandStart,
                    static_cast<long>(y) - 1,
                    thickness,
                    limit);
        LOKA_VERIFY(thickness <= limit);
        ++bands;
        bandStart = -1;
      }
    }
    if (!lines[s])
      std::printf("[skip] underline pixels: empty string draws nothing\n");
    LOKA_VERIFY(bands == lines[s]);
    [off release];
  }
}

void testMacTextUnderlineNoRelayoutAndEmpty()
{
  TextVersionFixture f((Text("same wrapped words") + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
  UnderlineWindow window(f.root);
  f.installRoot();
  [f.label() setNeedsDisplay:NO];
  LOKA_VERIFY((Text("same wrapped words") + BlockStyle().wrap(TEXT_WRAP_WORD) + Underline).applyPropsToNode(&f.node));
  LOKA_VERIFY(!f.controller.hasPendingRelayout());
  VerifyUnderline([f.label() attributedStringValue], true);
  LOKA_VERIFY([f.label() needsDisplay]);
  TextVersionFixture empty(Text("").props), emptyOn((Text("") + Underline).props);
  LOKA_VERIFY([[emptyOn.label() attributedStringValue] length] == 0);
  LOKA_VERIFY(
      [[emptyOn.label() attributedStringValue] isEqualToAttributedString:[empty.label() attributedStringValue]]);
}

void testMacTextUnderlineReconciliation()
{
  VersionCounters counters;
  TextVersionFixture f((Text("two lines of wrapped words with explicit style") + FontSize<12>()
                        + BlockStyle().wrap(TEXT_WRAP_WORD) + Underline)
                           .props);
  f.layout();
  LOKA_VERIFY(Measured(f));
  VerifyUnderline([f.label() attributedStringValue], true);
  const unsigned builds = versionBuilds, measures = versionMeasures;
  [[f.label() cell] setFont:[NSFont systemFontOfSize:30]];
  [f.label() setAlignment:LOKA_MAC_TEXT_ALIGNMENT_RIGHT];
  // Force the precise layout-hit leg, independently of logical dirty sources.
  f.state.inputs = NODE_DIRTY_NONE;
  f.context().layout(&f.controller, f.state);
  LOKA_VERIFY(f.state.inputs == NODE_DIRTY_NONE);
  LOKA_VERIFY(versionBuilds == builds && versionMeasures == measures);
  VerifyVersion(f);
  VerifyUnderline([f.label() attributedStringValue], true);
}

void testMacTextUnderlineColourAppearanceAttach()
{
  TextVersionFixture f((Text("dynamic underline") + Underline + TextStyle().color(TEXT_COLOR_RED)).props);
  LOKA_VERIFY(
      (Text("dynamic underline") + Underline + TextStyle().color(TEXT_COLOR_PRIMARY)).applyPropsToNode(&f.node));
  VerifyUnderline([f.label() attributedStringValue], true);
  NSColor *colour = nil;
  LOKA_VERIFY(loka::macos::QueryMacTextColor(TEXT_COLOR_PRIMARY, colour));
  LOKA_VERIFY([[f.label() attributedStringValue] attribute:NSForegroundColorAttributeName atIndex:0 effectiveRange:0]
              == colour);
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 1090
  if (NSAppKitVersionNumber >= 1671)
  {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    UnderlineWindow window(f.root);
    [f.label() setFrame:NSMakeRect(0, 0, 160, 40)];
    NSAppearance *old = [[NSAppearance currentAppearance] retain];
    NSBitmapImageRep *images[2];
    for (int dark = 0; dark < 2; ++dark)
    {
      NSAppearance *appearance =
          [NSAppearance appearanceNamed:dark ? @"NSAppearanceNameDarkAqua" : @"NSAppearanceNameAqua"];
      [NSAppearance setCurrentAppearance:appearance];
      [f.label() setAppearance:appearance];
      f.layout();
      VerifyUnderline([f.label() attributedStringValue], true);
      images[dark] = [CaptureTextPixels(f.label()) retain];
    }
    LOKA_VERIFY([images[0] bytesPerRow] == [images[1] bytesPerRow]);
    bool differs = false;
    for (NSInteger y = 0; y < [images[0] pixelsHigh]; ++y)
      if (std::memcmp([images[0] bitmapData] + y * [images[0] bytesPerRow],
                      [images[1] bitmapData] + y * [images[1] bytesPerRow],
                      static_cast<size_t>([images[0] bytesPerRow]))
          != 0)
        differs = true;
    LOKA_VERIFY(differs);
    [images[0] release];
    [images[1] release];
    [NSAppearance setCurrentAppearance:old];
    [old release];
#pragma clang diagnostic pop
  }
  else
    std::printf("[skip] underline appearance: no dark appearance\n");
#else
  std::printf("[skip] underline appearance: no dark appearance in SDK\n");
#endif
  NotifySubtreeNodeDetached(&f.node);
  LifecycleFactTestAccess::DeliverFacts(&f.node);
  LOKA_VERIFY([f.label() isHidden]);
  NotifySubtreeNodeAttached(&f.node);
  LifecycleFactTestAccess::DeliverFacts(&f.node);
  LOKA_VERIFY(![f.label() isHidden]);
  VerifyUnderline([f.label() attributedStringValue], true);
}
