#define LOKA_STYLE_VOCAB_DEFINE_NAMES
#include "MacTextColorTests.hpp"
#include "MacGround.hpp"
#include "MacScenePlatformController.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/AttributedText.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "support/TestVerify.hpp"
#include <objc/runtime.h>
#include <cmath>
#include <cstdio>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using loka::dsl::testing::SceneTestAccess;

  NSTextField *freshLabel()
  {
    NSTextField *label = [[[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 160, 20)] autorelease];
    [label setEditable:NO];
    [label setSelectable:NO];
    [label setBezeled:NO];
    [label setDrawsBackground:NO];
    return label;
  }

  NSTextField *field(NSView *parent)
  {
    LOKA_VERIFY([[parent subviews] count] == 1);
    NSTextField *label = (NSTextField *)[[parent subviews] objectAtIndex:0];
    LOKA_VERIFY([label isKindOfClass:[NSTextField class]]);
    return label;
  }

  // Newer system colors are sent by selector, so these pins run on hosts that
  // predate them; the fallback order restates the ruling's priority.
  NSColor *hostColor(SEL selector, NSColor *fallback)
  {
    if ([NSColor respondsToSelector:selector])
      return (NSColor *)[NSColor performSelector:selector];
    return fallback;
  }

  // A separate expectation table makes every generated vocabulary row require
  // an explicit pin when the vocabulary grows. No RGB conversion is permitted.
  NSColor *expected(TextColor color)
  {
    switch (color)
    {
    case TEXT_COLOR_UNSET:
      return nil;
    case TEXT_COLOR_PRIMARY:
      return loka::macos::MacTextRoleColor();
    case TEXT_COLOR_SECONDARY:
      return hostColor(@selector(secondaryLabelColor), [NSColor disabledControlTextColor]);
    case TEXT_COLOR_BLACK:
      return [NSColor blackColor];
    case TEXT_COLOR_WHITE:
      return [NSColor whiteColor];
    case TEXT_COLOR_RED:
      return hostColor(@selector(systemRedColor), [NSColor redColor]);
    case TEXT_COLOR_GREEN:
      return hostColor(@selector(systemGreenColor), [NSColor greenColor]);
    case TEXT_COLOR_BLUE:
      return hostColor(@selector(systemBlueColor), [NSColor blueColor]);
    case TEXT_COLOR_CYAN:
      return hostColor(@selector(systemCyanColor), hostColor(@selector(systemTealColor), [NSColor cyanColor]));
    case TEXT_COLOR_MAGENTA:
      return hostColor(@selector(systemPurpleColor), [NSColor magentaColor]);
    case TEXT_COLOR_YELLOW:
      return hostColor(@selector(systemYellowColor), [NSColor yellowColor]);
    }
    return nil;
  }

  // Scoped selector refusal exercises the older-runtime branches on this host.
  // The inherited class method may be shared: only NSColor is intercepted.
  IMP originalResponds = 0;
  bool cyanOnly = false;
  BOOL respondsWithoutNewColors(id receiver, SEL command, SEL selector)
  {
    if (receiver == [NSColor class])
    {
      if (selector == @selector(systemCyanColor))
        return NO;
      if (!cyanOnly
          && (selector == @selector(systemTealColor) || selector == @selector(systemRedColor)
              || selector == @selector(systemGreenColor) || selector == @selector(systemBlueColor)
              || selector == @selector(systemYellowColor) || selector == @selector(systemPurpleColor)
              || selector == @selector(secondaryLabelColor) || selector == @selector(labelColor)))
        return NO;
    }
    return ((BOOL(*)(id, SEL, SEL))originalResponds)(receiver, command, selector);
  }

  class LegacyColors
  {
  public:
    explicit LegacyColors(bool onlyCyan)
        : method_(class_getClassMethod([NSColor class], @selector(respondsToSelector:)))
    {
      cyanOnly = onlyCyan;
      originalResponds = method_setImplementation(this->method_, (IMP)respondsWithoutNewColors);
    }
    ~LegacyColors()
    {
      method_setImplementation(this->method_, originalResponds);
    }

  private:
    Method method_;
    LegacyColors(const LegacyColors &);
    LegacyColors &operator=(const LegacyColors &);
  };

  void verifyAnswer(TextColor ink, NSColor *wanted)
  {
    NSColor *out = nil;
    LOKA_VERIFY(loka::macos::QueryMacTextColor(ink, out));
    LOKA_VERIFY(out == wanted || [out isEqual:wanted]);
  }

  struct RGB
  {
    double red, green, blue, alpha;
  };

  // NSAppearance arrived in the 10.9 SDK; an older SDK builds the installed
  // color checks and skips only the light/dark resolution.
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 1090
  RGB resolve(NSColor *color, NSAppearance *appearance, bool legacy)
  {
    __block RGB result;
    void (^sample)(void) = ^{
      NSColor *rgb = [color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
      LOKA_VERIFY(rgb != nil);
      result.red = [rgb redComponent];
      result.green = [rgb greenComponent];
      result.blue = [rgb blueComponent];
      result.alpha = [rgb alphaComponent];
    };
    if (!legacy && [appearance respondsToSelector:@selector(performAsCurrentDrawingAppearance:)])
      [appearance performAsCurrentDrawingAppearance:sample];
    else
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      NSAppearance *previous = [[NSAppearance currentAppearance] retain];
      [NSAppearance setCurrentAppearance:appearance];
      sample();
      [NSAppearance setCurrentAppearance:previous];
      [previous release];
#pragma clang diagnostic pop
    }
    return result;
  }
#endif

  void dynamicInstalledColors(bool legacy)
  {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
    NSView *parent = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 160, 100)];
    {
      MacScenePlatformController controller(parent, RailMetrics());
      TextNode node((Text("dynamic") + Red).props);
      node.setPropsTypeId(TextProps::staticTypeId());
      controller.onChange(&node, NODE_DIRTY_NONE, false);
      controller.relayout(160, 100);
      NSTextField *label = field(parent);
      const TextStyle styles[] = {Red, Secondary};
      const char *names[] = {"Red", "Secondary"};
      for (int i = 0; i < 2; ++i)
      {
        LOKA_VERIFY((Text("dynamic") + styles[i]).applyPropsToNode(&node));
        NSColor *installed = [label textColor];
        LOKA_VERIFY(installed == expected(styles[i].color_) || [installed isEqual:expected(styles[i].color_)]);
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 1090
        // DarkAqua arrived in 10.14 (AppKit 1671); the name is spelled out so
        // older hosts reach the skip instead of a missing symbol. Presence is
        // read from the host AppKit version: Mac OS X 10.9 answers
        // appearanceNamed: with a generic appearance for any name, never nil.
        NSAppearance *darkAqua =
            NSAppKitVersionNumber >= 1671 ? [NSAppearance appearanceNamed:@"NSAppearanceNameDarkAqua"] : nil;
        if (!darkAqua)
        {
          std::printf("[skip] %s light/dark resolution: the host has no dark appearance (10.14+)\n", names[i]);
          continue;
        }
        RGB light = resolve(installed, [NSAppearance appearanceNamed:@"NSAppearanceNameAqua"], legacy);
        RGB dark = resolve(installed, darkAqua, legacy);
        std::printf("[dynamic] %s %s light=(%.6f %.6f %.6f %.6f) dark=(%.6f %.6f %.6f %.6f)\n",
                    names[i],
                    legacy ? "legacy" : "drawing",
                    light.red,
                    light.green,
                    light.blue,
                    light.alpha,
                    dark.red,
                    dark.green,
                    dark.blue,
                    dark.alpha);
        LOKA_VERIFY(std::fabs(light.red - dark.red) + std::fabs(light.green - dark.green)
                        + std::fabs(light.blue - dark.blue)
                    > 0.01);
        LOKA_VERIFY([label textColor] == installed);
#else
        (void)legacy;
        std::printf("[skip] %s light/dark resolution: the SDK has no NSAppearance (10.9+)\n", names[i]);
#endif
      }
      controller.onChange(0, NODE_DIRTY_NONE, false);
    }
    [parent release];
    [pool drain];
  }

  AttributedString runs(const TextStyle &first)
  {
    return Styled("R", first) + Styled("U", TextStyle()) + Styled("B", Blue);
  }

  void verifyRuns(NSTextField *label, TextColor first)
  {
    NSAttributedString *value = [label attributedStringValue];
    LOKA_VERIFY([[value string] isEqualToString:@"RUB"]);
    LOKA_VERIFY([[value attribute:NSForegroundColorAttributeName atIndex:0 effectiveRange:0] isEqual:expected(first)]);
    LOKA_VERIFY([[value attribute:NSForegroundColorAttributeName atIndex:1
                   effectiveRange:0] isEqual:loka::macos::MacLabelTextColor()]);
    LOKA_VERIFY([[value attribute:NSForegroundColorAttributeName atIndex:2
                   effectiveRange:0] isEqual:expected(TEXT_COLOR_BLUE)]);
  }
} // namespace

void testMacTextFamilyMonospace()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *parent = [[NSView alloc] initWithFrame:NSZeroRect];
  {
    MacScenePlatformController controller(parent, RailMetrics());
    NSFont *mono = (NSFont *)controller.textFont(Monospace);
    NSFont *expectedMono = [NSFont userFixedPitchFontOfSize:[NSFont systemFontSize]];
    LOKA_VERIFY(mono && [mono isFixedPitch]);
    LOKA_VERIFY([[mono fontName] isEqualToString:[expectedMono fontName]]);
    NSFont *plain = (NSFont *)controller.textFont(TextStyle());
    NSFont *body = (NSFont *)controller.textFont(Body);
    LOKA_VERIFY(plain && ![plain isFixedPitch]);
    LOKA_VERIFY(body && ![body isFixedPitch]);
    LOKA_VERIFY([[plain fontName] isEqualToString:[[NSFont systemFontOfSize:[NSFont systemFontSize]] fontName]]);
    LOKA_VERIFY([[body fontName] isEqualToString:[[NSFont systemFontOfSize:12] fontName]]);
    LOKA_VERIFY(controller.textFont(TextStyle().family(TEXT_FAMILY_SYSTEM)) == plain);
    NSFont *bold = (NSFont *)controller.textFont(Monospace + Bold);
    LOKA_VERIFY(bold && [bold isFixedPitch]);
    LOKA_VERIFY([[NSFontManager sharedFontManager] traitsOfFont:bold] & NSBoldFontMask);
    NSFont *sized = (NSFont *)controller.textFont(Monospace + FontSize<12>());
    LOKA_VERIFY(sized && [sized isFixedPitch] && std::fabs([sized pointSize] - 12) < 0.01);
  }
  [parent release];
  [pool drain];
}

void testMacTextFamilyGeometry()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *parent = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 100)];
  {
    MacScenePlatformController controller(parent, RailMetrics());
    TextNode plain(Text("iiiiiiiiiiiiiiii").props);
    TextNode mono((Text("iiiiiiiiiiiiiiii") + Monospace).props);
    short plainWidth = 0, monoWidth = 0;
    LOKA_VERIFY(controller.queryNaturalWidth(&plain, plainWidth));
    LOKA_VERIFY(controller.queryNaturalWidth(&mono, monoWidth));
    NSFont *fixed = [NSFont userFixedPitchFontOfSize:[NSFont systemFontSize]];
    NSFont *nativeDefault = [freshLabel() font];
    NSAttributedString *fixedText = [[[NSAttributedString alloc]
        initWithString:@"iiiiiiiiiiiiiiii"
            attributes:[NSDictionary dictionaryWithObject:fixed forKey:NSFontAttributeName]] autorelease];
    NSAttributedString *defaultText = [[[NSAttributedString alloc]
        initWithString:@"iiiiiiiiiiiiiiii"
            attributes:[NSDictionary dictionaryWithObject:nativeDefault forKey:NSFontAttributeName]] autorelease];
    // Subtract the two advances to cancel the native cell's horizontal padding.
    // Identity RailMetrics leaves only the two measurements' integer rounding.
    const double advanceDifference = [fixedText size].width - [defaultText size].width;
    LOKA_VERIFY(std::fabs((monoWidth - plainWidth) - advanceDifference) < 2.0);
    if (![nativeDefault isFixedPitch])
      LOKA_VERIFY(monoWidth != plainWidth);
    else
      std::printf("[skip] family width contrast: the native default font is fixed pitch\n");
    controller.onChange(&mono, NODE_DIRTY_NONE, false);
    controller.relayout(400, 100);
    LOKA_VERIFY([[field(parent) font] isFixedPitch]);
    LOKA_VERIFY([[[field(parent) font] fontName] isEqualToString:[fixed fontName]]);
    controller.onChange(0, NODE_DIRTY_NONE, false);
  }
  [parent release];
  [pool drain];
}

void testMacTextColorTable()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSColor *sentinel = [NSColor orangeColor];
  NSColor *out = sentinel;
  LOKA_VERIFY(!loka::macos::QueryMacTextColor(TEXT_COLOR_UNSET, out));
  LOKA_VERIFY(out == sentinel);
  for (std::size_t i = 0;
       i < sizeof(loka::app::detail::StyleVocabularyColors) / sizeof(loka::app::detail::StyleVocabularyColors[0]);
       ++i)
    verifyAnswer(loka::app::detail::StyleVocabularyColors[i].value,
                 expected(loka::app::detail::StyleVocabularyColors[i].value));
  [pool drain];
}

void testMacTextColorFallbacks()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  // The cyan-only refusal needs both newer selectors present on the host.
  if ([NSColor respondsToSelector:@selector(systemCyanColor)] &&
      [NSColor respondsToSelector:@selector(systemTealColor)])
  {
    NSColor *teal = (NSColor *)[NSColor performSelector:@selector(systemTealColor)];
    LegacyColors legacy(true);
    LOKA_VERIFY(![NSColor respondsToSelector:@selector(systemCyanColor)]);
    verifyAnswer(TEXT_COLOR_CYAN, teal);
  }
  else
    std::printf("[skip] cyan-only refusal: the host lacks systemCyanColor (macOS 12) or systemTealColor (10.12)\n");
  {
    LegacyColors legacy(false);
    LOKA_VERIFY(![NSColor respondsToSelector:@selector(systemTealColor)]);
    verifyAnswer(TEXT_COLOR_PRIMARY, [NSColor textColor]);
    verifyAnswer(TEXT_COLOR_SECONDARY, [NSColor disabledControlTextColor]);
    verifyAnswer(TEXT_COLOR_RED, [NSColor redColor]);
    verifyAnswer(TEXT_COLOR_GREEN, [NSColor greenColor]);
    verifyAnswer(TEXT_COLOR_BLUE, [NSColor blueColor]);
    verifyAnswer(TEXT_COLOR_YELLOW, [NSColor yellowColor]);
    verifyAnswer(TEXT_COLOR_CYAN, [NSColor cyanColor]);
    verifyAnswer(TEXT_COLOR_MAGENTA, [NSColor magentaColor]);
  }
  verifyAnswer(TEXT_COLOR_CYAN, expected(TEXT_COLOR_CYAN));
  [pool drain];
}

void testMacTextColorDynamic()
{
  dynamicInstalledColors(false);
}
void testMacTextColorLegacyDrawing()
{
  dynamicInstalledColors(true);
}

void testMacTextColorStaticTransition()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSColor *nativeDefault = [freshLabel() textColor];
  LOKA_VERIFY([nativeDefault isEqual:loka::macos::MacLabelTextColor()]);
  std::printf("[default] fresh label=%s\n", [[nativeDefault description] UTF8String]);
  NSView *parent = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 160, 100)];
  NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 160, 100)
                                                 styleMask:0
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
  [window setContentView:parent];
  {
    MacScenePlatformController controller(parent, RailMetrics());
    TextNode node(Text("static").props);
    node.setPropsTypeId(TextProps::staticTypeId());
    controller.onChange(&node, NODE_DIRTY_NONE, false);
    controller.relayout(160, 100);
    NSTextField *label = field(parent);
    LOKA_VERIFY([[label textColor] isEqual:nativeDefault]);
    NSRect frame = [label frame];
    [label setNeedsDisplay:NO];
    LOKA_VERIFY(!controller.hasPendingRelayout());
    LOKA_VERIFY((Text("static") + Red).applyPropsToNode(&node));
    LOKA_VERIFY([[label textColor] isEqual:expected(TEXT_COLOR_RED)]);
    LOKA_VERIFY([label needsDisplay]);
    LOKA_VERIFY(!controller.hasPendingRelayout());
    LOKA_VERIFY(NSEqualRects([label frame], frame));
    [label setNeedsDisplay:NO];
    LOKA_VERIFY(Text("static").applyPropsToNode(&node));
    LOKA_VERIFY([[label textColor] isEqual:nativeDefault]);
    LOKA_VERIFY(![[label textColor] isEqual:expected(TEXT_COLOR_RED)]);
    LOKA_VERIFY([label needsDisplay]);
    LOKA_VERIFY(!controller.hasPendingRelayout());
    controller.relayout(160, 100);
    LOKA_VERIFY(NSEqualRects([label frame], frame));
    controller.onChange(0, NODE_DIRTY_NONE, false);
  }
  [window release];
  [parent release];
  [pool drain];
}

void testMacTextColorStateTransition()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSColor *nativeDefault = [freshLabel() textColor];
  NSView *parent = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 160, 100)];
  {
    MacScenePlatformController controller(parent, RailMetrics());
    MutableState<TextStyle> style(Red);
    NodeDefinitionBase *definition = (Text("live") + &style).clone();
    LOKA_VERIFY(definition != 0);
    Scene scene(definition);
    LOKA_VERIFY(scene.mount(&controller));
    SceneTestAccess::updateAttached(scene, true);
    controller.relayout(160, 100);
    NSTextField *label = field(parent);
    LOKA_VERIFY([[label textColor] isEqual:expected(TEXT_COLOR_RED)]);
    NSRect frame = [label frame];
    {
      StateTrackerGuard guard(SceneTestAccess::rootBoundary(scene)->tracker());
      style.set(TextStyle());
    }
    if (scene.hasPendingInvalidation())
      LOKA_VERIFY(SceneTestAccess::flushInvalidation(scene));
    LOKA_VERIFY(!scene.hasPendingInvalidation());
    MacScenePlatformController::flushPendingRelayouts();
    LOKA_VERIFY([[label textColor] isEqual:nativeDefault]);
    LOKA_VERIFY(![[label textColor] isEqual:expected(TEXT_COLOR_RED)]);
    LOKA_VERIFY(NSEqualRects([label frame], frame));
    SceneTestAccess::unmount(scene);
  }
  [parent release];
  [pool drain];
}

void testMacAttributedTextColors()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  NSView *parent = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 160, 100)];
  {
    MacScenePlatformController controller(parent, RailMetrics());
    MutableState<AttributedString> content(runs(Red));
    NodeDefinitionBase *definition = AttributedText(&content).clone();
    LOKA_VERIFY(definition != 0);
    Scene scene(definition);
    LOKA_VERIFY(scene.mount(&controller));
    SceneTestAccess::updateAttached(scene, true);
    controller.relayout(160, 100);
    NSTextField *label = field(parent);
    verifyRuns(label, TEXT_COLOR_RED);
    NSRect frame = [label frame];
    {
      StateTrackerGuard guard(SceneTestAccess::rootBoundary(scene)->tracker());
      content.set(runs(Green));
    }
    if (scene.hasPendingInvalidation())
      LOKA_VERIFY(SceneTestAccess::flushInvalidation(scene));
    LOKA_VERIFY(!scene.hasPendingInvalidation());
    MacScenePlatformController::flushPendingRelayouts();
    verifyRuns(label, TEXT_COLOR_GREEN);
    LOKA_VERIFY(NSEqualRects([label frame], frame));
    SceneTestAccess::unmount(scene);
  }
  {
    MacScenePlatformController controller(parent, RailMetrics());
    AttributedTextNode node((AttributedText(runs(Red))).props);
    node.setPropsTypeId(AttributedTextProps::staticTypeId());
    controller.onChange(&node, NODE_DIRTY_NONE, false);
    controller.relayout(160, 100);
    NSTextField *label = field(parent);
    verifyRuns(label, TEXT_COLOR_RED);
    LOKA_VERIFY(AttributedText(runs(Green)).applyPropsToNode(&node));
    MacScenePlatformController::flushPendingRelayouts();
    verifyRuns(label, TEXT_COLOR_GREEN);
    controller.onChange(0, NODE_DIRTY_NONE, false);
  }
  [parent release];
  [pool drain];
}
